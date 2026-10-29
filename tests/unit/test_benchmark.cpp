// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <mutex>
#include <set>
#include <thread>

#include "faulty_device.hpp"
#include "platter/benchmark/benchmark.hpp"
#include "test_support.hpp"

using namespace platter;
using namespace platter::bench;
using namespace std::chrono_literals;
using platter::test::FaultyDevice;
using platter::test::TempDir;

namespace {

// Opener that wraps a fresh read-only image handle in a FaultyDevice per call
// (one per worker thread, like the real thing), with shared fault settings.
struct Rig {
    TempDir tmp;
    std::filesystem::path img;
    std::uint32_t ss;
    std::set<std::uint64_t> bad;
    std::uint64_t remove_after = 0;
    std::chrono::microseconds delay{0};
    std::mutex m;
    std::vector<std::pair<std::uint64_t, std::size_t>> reads;
    std::atomic<int> opens{0};

    explicit Rig(std::uint64_t bytes, std::uint32_t sector = 512) : img(tmp / "b.img"), ss(sector) {
        test::make_sparse_file(img, bytes);
    }
    DeviceOpener opener() {
        return [this]() -> Result<BlockDevicePtr> {
            ++opens;
            auto d = open_image(img.string(), {}, ss);
            if (!d) return d.error();
            auto* f = new FaultyDevice(std::move(d).value());
            f->bad_lbas = bad;
            f->remove_after_reads = remove_after;
            f->real_delay = delay;
            f->on_read = [this](std::uint64_t off, std::size_t len) {
                std::lock_guard lk(m);
                reads.emplace_back(off, len);
            };
            return BlockDevicePtr(f);
        };
    }
};

std::unique_ptr<Benchmark> run(Rig& rig, BenchConfig c) {
    c.direct_io = false;
    auto b = std::make_unique<Benchmark>(rig.opener(), c, rig.img.string(), /*is_image=*/true);
    auto v = b->validate();
    INFO((v ? std::string() : v.error().describe()));
    REQUIRE(v);
    REQUIRE(b->start());
    b->wait();
    return b;
}

}  // namespace

TEST_CASE("metrics math", "[benchmark]") {
    LatencyHistogram h;
    for (int i = 0; i < 100; ++i) h.add(1000);  // 1 ms each
    auto m = compute_metrics(h, 100, 100ull << 20, 2, 2'000'000'000ull);  // 100 MiB in 2 s
    CHECK(m.ops == 100);
    CHECK(m.errors == 2);
    CHECK(m.mib_per_s == Catch::Approx(50.0));
    CHECK(m.mb_per_s == Catch::Approx(52.4288));
    CHECK(m.iops == Catch::Approx(50.0));
    CHECK(m.lat_mean_us == Catch::Approx(1000.0));
    CHECK(m.lat_median_us == 1000);
    CHECK(m.lat_max_us == 1000);
    auto z = compute_metrics(LatencyHistogram{}, 0, 0, 0, 0);
    CHECK(z.mb_per_s == 0);
    CHECK(z.iops == 0);
}

TEST_CASE("histogram merge and bucket export", "[benchmark]") {
    LatencyHistogram a, b;
    for (std::uint64_t i = 0; i < 10; ++i) a.add(5);
    for (std::uint64_t i = 0; i < 30; ++i) b.add(1000);
    a.merge(b);
    CHECK(a.count() == 40);
    CHECK(a.min() == 5);
    CHECK(a.max() == 1000);
    auto bk = a.buckets();
    REQUIRE(bk.size() == 2);
    CHECK(bk[0] == std::pair<std::uint64_t, std::uint64_t>{5, 10});
    CHECK(bk[1].second == 30);
    CHECK(bk[1].first >= 1000);
}

TEST_CASE("defaults depend on the kind", "[benchmark]") {
    auto s = with_defaults({.kind = BenchKind::sequential}, 512);
    CHECK(s.block_bytes == 1u << 20);
    CHECK(s.max_bytes == 1ull << 30);
    auto r = with_defaults({.kind = BenchKind::random}, 512);
    CHECK(r.block_bytes == 4096);
    CHECK(r.max_seconds == 10);
    auto a = with_defaults({.kind = BenchKind::access_time, .queue_depth = 8}, 4096);
    CHECK(a.block_bytes == 4096);  // one 4Kn sector
    CHECK(a.queue_depth == 1);     // access time is always QD 1
    auto r4k = with_defaults({.kind = BenchKind::random}, 4096);
    CHECK(r4k.block_bytes == 4096);
}

TEST_CASE("sequential read: exact byte count, positions, timeline", "[benchmark]") {
    for (std::uint32_t qd : {1u, 4u}) {
        DYNAMIC_SECTION("QD " << qd) {
            Rig rig(64ull << 20);
            auto b = run(rig, {.kind = BenchKind::sequential, .queue_depth = qd, .max_bytes = 32ull << 20});
            CHECK(b->state() == OperationState::completed);
            auto r = b->result();
            REQUIRE(r.phases.size() == 1);
            const auto& m = r.phases[0].metrics;
            CHECK(m.bytes == 32ull << 20);
            CHECK(m.ops == 32);
            CHECK(m.errors == 0);
            CHECK(m.mb_per_s > 0);
            CHECK(r.phases[0].queue_depth == qd);
            CHECK(rig.opens == static_cast<int>(qd) + 1);  // one probe + one handle per worker
            CHECK_FALSE(r.timeline.empty());
            CHECK_FALSE(r.positions.empty());
            // Every block read exactly once, contiguously from LBA 0.
            std::set<std::uint64_t> offs;
            for (auto& [o, l] : rig.reads) {
                CHECK(l == (1u << 20));
                offs.insert(o);
            }
            CHECK(offs.size() == 32);
            CHECK(*offs.begin() == 0);
            CHECK(*offs.rbegin() == 31ull << 20);
            CHECK(b->fraction() == Catch::Approx(1.0));
            CHECK_FALSE(r.started_utc.empty());
        }
    }
}

TEST_CASE("random read stays inside the region and aligned", "[benchmark]") {
    Rig rig(64ull << 20);
    BenchConfig c{.kind = BenchKind::random, .start_lba = 20480, .end_lba = 40959, .queue_depth = 3, .max_seconds = 0.3};
    auto b = run(rig, c);
    CHECK(b->state() == OperationState::completed);
    auto r = b->result();
    REQUIRE(r.phases.size() == 1);
    CHECK(r.phases[0].metrics.ops > 50);
    CHECK(r.phases[0].metrics.iops > 0);
    CHECK(r.config.block_bytes == 4096);
    std::set<std::uint64_t> distinct;
    std::size_t bad_reads = 0;
    for (auto& [o, l] : rig.reads) {
        const bool ok = l == 4096 && o % 4096 == 0 && o >= 20480ull * 512 && o + l <= 40960ull * 512;
        bad_reads += !ok;
        distinct.insert(o);
    }
    CHECK(bad_reads == 0);  // every read inside the region, 4 KiB, 4 KiB-aligned
    CHECK(distinct.size() > 20);  // actually random
    CHECK(r.headline().find("IOPS @ QD3") != std::string::npos);
}

TEST_CASE("access time uses single-sector QD1 reads", "[benchmark]") {
    Rig rig(16ull << 20);
    auto b = run(rig, {.kind = BenchKind::access_time, .queue_depth = 16, .max_seconds = 0.2});
    auto r = b->result();
    REQUIRE(r.phases.size() == 1);
    CHECK(r.phases[0].block_bytes == 512);
    CHECK(r.phases[0].queue_depth == 1);
    CHECK(rig.opens == 2);
    CHECK(r.headline().find(" ms") != std::string::npos);
    CHECK(std::all_of(rig.reads.begin(), rig.reads.end(), [](auto& rd) { return rd.second == 512; }));
}

TEST_CASE("speed curve samples evenly spaced positions", "[benchmark]") {
    Rig rig(64ull << 20);
    auto b = run(rig, {.kind = BenchKind::speed_curve, .block_bytes = 1u << 20, .curve_points = 8,
                       .curve_bytes_per_point = 2ull << 20});
    CHECK(b->state() == OperationState::completed);
    auto r = b->result();
    REQUIRE(r.positions.size() == 8);
    CHECK(r.positions.front().lba == 0);
    CHECK(r.positions.back().lba == (64ull << 20) / 512 - 4096);  // last point ends at the last LBA
    for (std::size_t i = 1; i < r.positions.size(); ++i) {
        CHECK(r.positions[i].lba > r.positions[i - 1].lba);
        CHECK(r.positions[i].lba % 2048 == 0);  // block-aligned
    }
    REQUIRE(r.phases.size() == 1);
    CHECK(r.phases[0].metrics.bytes == 8 * (2ull << 20));
    CHECK(r.phases[0].metrics.ops == 16);
    CHECK(r.headline().rfind("min ", 0) == 0);
}

TEST_CASE("block size sweep: one phase per usable size, offset steps", "[benchmark]") {
    Rig rig(64ull << 20, 4096);
    BenchConfig c{.kind = BenchKind::block_sizes, .max_bytes = 4ull << 20};
    c.sweep_block_sizes = {512, 4096, 65536, 1u << 20};  // 512 is not a 4Kn sector multiple
    auto b = run(rig, c);
    auto r = b->result();
    REQUIRE(r.phases.size() == 3);
    CHECK(r.phases[0].label == "4 KiB");
    CHECK(r.phases[1].label == "64 KiB");
    CHECK(r.phases[2].label == "1 MiB");
    for (const auto& p : r.phases) CHECK(p.metrics.bytes == 4ull << 20);
    bool skipped = false;
    for (const auto& w : r.warnings) skipped |= w.find("Skipped block size 512") != std::string::npos;
    CHECK(skipped);
}

TEST_CASE("queue depth sweep", "[benchmark]") {
    Rig rig(64ull << 20);
    BenchConfig c{.kind = BenchKind::queue_depths, .max_seconds = 0.15};
    c.sweep_queue_depths = {1, 2, 4};
    auto b = run(rig, c);
    auto r = b->result();
    REQUIRE(r.phases.size() == 3);
    CHECK(r.phases[0].label == "QD 1");
    CHECK(r.phases[2].queue_depth == 4);
    for (const auto& p : r.phases) CHECK(p.metrics.ops > 0);
    CHECK(rig.opens == 1 + 1 + 2 + 4);
}

TEST_CASE("read errors are counted; a vanished device fails the benchmark", "[benchmark][safety]") {
    {
        Rig rig(16ull << 20);
        rig.bad = {4096, 8192};  // blocks 2 and 4 of 1 MiB
        auto b = run(rig, {.kind = BenchKind::sequential, .max_bytes = 16ull << 20});
        CHECK(b->state() == OperationState::completed);
        auto r = b->result();
        CHECK(r.phases[0].metrics.errors == 2);
        CHECK(r.phases[0].metrics.ops == 14);
        CHECK(b->progress().errors == 2);
    }
    {
        Rig rig(16ull << 20);
        rig.remove_after = 3;
        auto b = run(rig, {.kind = BenchKind::sequential, .max_bytes = 16ull << 20});
        CHECK(b->state() == OperationState::failed);
        CHECK(b->result().message.find("disappeared") != std::string::npos);
    }
}

TEST_CASE("cancel stops a benchmark; pause is refused", "[benchmark]") {
    Rig rig(64ull << 20);
    rig.delay = 2000us;
    BenchConfig c{.kind = BenchKind::random, .max_seconds = 30, .direct_io = false};
    auto b = std::make_unique<Benchmark>(rig.opener(), c, rig.img.string(), true);
    REQUIRE(b->validate());
    REQUIRE(b->start());
    std::this_thread::sleep_for(100ms);
    b->pause();
    CHECK(b->state() == OperationState::running);  // pausing would distort timing
    b->cancel();
    b->wait();
    CHECK(b->state() == OperationState::cancelled);
    auto r = b->result();
    REQUIRE(r.phases.size() == 1);  // partial result kept
    CHECK(r.phases[0].metrics.ops > 0);
}

TEST_CASE("benchmark validation and safety", "[benchmark][safety]") {
    Rig rig(8ull << 20);
    auto rejected = [&](BenchConfig c) {
        Benchmark b(rig.opener(), c, rig.img.string(), true);
        auto v = b.validate();
        REQUIRE_FALSE(v);
        return v.error();
    };
    CHECK(rejected({.block_bytes = 1000}).code == Errc::invalid_argument);
    CHECK(rejected({.block_bytes = 128u << 20}).code == Errc::invalid_argument);
    CHECK(rejected({.queue_depth = 0}).code == Errc::invalid_argument);
    CHECK(rejected({.queue_depth = 65}).code == Errc::invalid_argument);
    CHECK(rejected({.end_lba = 1u << 20}).code == Errc::out_of_range);
    CHECK(rejected({.kind = BenchKind::speed_curve, .curve_points = 1}).code == Errc::invalid_argument);
    BenchConfig bad_qd{.kind = BenchKind::queue_depths};
    bad_qd.sweep_queue_depths = {1, 100};
    CHECK(rejected(bad_qd).code == Errc::invalid_argument);

    // A writable handle is refused outright.
    Benchmark rw([&]() -> Result<BlockDevicePtr> {
                     OpenOptions o;
                     o.mode = OpenMode::read_write;
                     return open_image(rig.img.string(), o);
                 },
                 {}, rig.img.string(), true);
    auto v = rw.validate();
    REQUIRE_FALSE(v);
    CHECK(v.error().message.find("read-only") != std::string::npos);
}

TEST_CASE("cache caveats are reported (spec 7.4)", "[benchmark]") {
    Rig rig(16ull << 20);
    auto b = run(rig, {.kind = BenchKind::random, .max_seconds = 0.05});
    auto w = b->result().warnings;
    auto has = [&](const char* s) {
        return std::any_of(w.begin(), w.end(), [&](const std::string& x) { return x.find(s) != std::string::npos; });
    };
    CHECK(has("Image file"));
    CHECK(has("Buffered I/O"));
    CHECK(has("under 2 GiB"));
}

TEST_CASE("create_benchmark opens images read-only", "[benchmark]") {
    TempDir tmp;
    const auto img = tmp / "c.img";
    test::make_sparse_file(img, 8ull << 20);
    auto b = create_benchmark(img.string(), true, {.kind = BenchKind::sequential, .max_bytes = 4ull << 20});
    REQUIRE(b);
    REQUIRE((*b)->validate());
    REQUIRE((*b)->start());
    (*b)->wait();
    CHECK((*b)->state() == OperationState::completed);
    CHECK((*b)->result().phases.at(0).metrics.bytes == 4ull << 20);
}
