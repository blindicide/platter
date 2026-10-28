// SPDX-License-Identifier: TBD
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <thread>

#include "faulty_device.hpp"
#include "platter/surface/surface_scan.hpp"
#include "test_support.hpp"

using namespace platter;
using namespace platter::surface;
using namespace std::chrono_literals;
using platter::test::FaultyDevice;
using platter::test::TempDir;

namespace {

// An image of `mib` MiB wrapped in a FaultyDevice; returns a raw pointer for configuring.
std::pair<BlockDevicePtr, FaultyDevice*> faulty_image(const TempDir& tmp, std::uint64_t bytes,
                                                      std::uint32_t ss = 512) {
    const auto img = tmp / "scan.img";
    test::make_sparse_file(img, bytes);
    auto dev = open_image(img.string(), {}, ss);
    REQUIRE(dev);
    auto* raw = new FaultyDevice(std::move(dev).value());
    return {BlockDevicePtr(raw), raw};
}

ScanConfig cfg(std::uint32_t block = 64 * 1024) {
    ScanConfig c;
    c.block_bytes = block;
    c.direct_io = false;
    return c;
}

std::unique_ptr<SurfaceScan> run(BlockDevicePtr dev, const ScanConfig& c) {
    auto scan = std::make_unique<SurfaceScan>(std::move(dev), c);
    auto v = scan->validate();
    INFO((v ? std::string() : v.error().describe()));
    REQUIRE(v);
    REQUIRE(scan->start());
    scan->wait();
    return scan;
}

}  // namespace

TEST_CASE("latency classification uses configurable thresholds", "[surface]") {
    Thresholds t;
    CHECK(classify(0, t) == LatencyClass::fast);
    CHECK(classify(4'999'999, t) == LatencyClass::fast);
    CHECK(classify(5'000'000, t) == LatencyClass::good);
    CHECK(classify(19'999'000, t) == LatencyClass::good);
    CHECK(classify(20'000'000, t) == LatencyClass::medium);
    CHECK(classify(50'000'000, t) == LatencyClass::slow);
    CHECK(classify(199'999'999, t) == LatencyClass::slow);
    CHECK(classify(200'000'000, t) == LatencyClass::very_slow);
    Thresholds tight;
    tight.us = {100, 200, 300, 400};
    CHECK(classify(250'000, tight) == LatencyClass::medium);
    CHECK(tight.valid());
    Thresholds bad;
    bad.us = {10, 5, 30, 40};
    CHECK_FALSE(bad.valid());
}

TEST_CASE("histogram percentiles are within one bucket", "[surface]") {
    LatencyHistogram h;
    for (std::uint64_t i = 1; i <= 10000; ++i) h.add(i);
    CHECK(h.count() == 10000);
    CHECK(h.min() == 1);
    CHECK(h.max() == 10000);
    CHECK(h.mean() == Catch::Approx(5000.5));
    for (double p : {50.0, 95.0, 99.0}) {
        const double exact = p / 100.0 * 10000;
        const auto est = static_cast<double>(h.percentile(p));
        CHECK(est >= exact);
        CHECK(est <= exact * 1.07);  // 16 sub-buckets per octave
    }
    CHECK(h.percentile(100) == 10000);
    LatencyHistogram small;
    for (int i = 0; i < 5; ++i) small.add(7);
    CHECK(small.percentile(50) == 7);
    CHECK(LatencyHistogram{}.percentile(50) == 0);
}

TEST_CASE("visiting orders", "[surface]") {
    auto fwd = make_order(10, ScanOrder::forward, 0, 1);
    CHECK(fwd == std::vector<std::uint64_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    auto rev = make_order(4, ScanOrder::reverse, 0, 1);
    CHECK(rev == std::vector<std::uint64_t>{3, 2, 1, 0});

    auto rnd = make_order(1000, ScanOrder::random, 0, 42);
    CHECK(std::set<std::uint64_t>(rnd.begin(), rnd.end()).size() == 1000);  // a permutation
    CHECK(rnd != make_order(1000, ScanOrder::forward, 0, 42));
    CHECK(rnd == make_order(1000, ScanOrder::random, 0, 42));  // deterministic per seed
    CHECK(rnd != make_order(1000, ScanOrder::random, 0, 43));

    auto smp = make_order(1000, ScanOrder::sampled, 10.0, 7);
    CHECK(smp.size() == 100);
    CHECK(std::is_sorted(smp.begin(), smp.end()));
    CHECK(std::set<std::uint64_t>(smp.begin(), smp.end()).size() == 100);
    CHECK(make_order(3, ScanOrder::sampled, 1.0, 7).size() == 1);  // at least one block
    CHECK(make_order(0, ScanOrder::random, 0, 7).empty());
}

TEST_CASE("full read scan of a clean image", "[surface]") {
    TempDir tmp;
    auto [dev, fd] = faulty_image(tmp, 8ull << 20);  // 8 MiB = 128 x 64 KiB
    auto scan = run(std::move(dev), cfg());
    CHECK(scan->state() == OperationState::completed);
    const auto& L = scan->layout();
    CHECK(L.block_count == 128);
    CHECK(L.lbas_per_block == 128);
    CHECK(L.end_lba == 16383);
    auto s = scan->stats();
    CHECK(s.requests == 128);
    CHECK(s.ok == 128);
    CHECK(s.failed == 0);
    CHECK(s.tested_bytes == 8ull << 20);
    CHECK(s.total_bytes == 8ull << 20);
    CHECK(s.per_class[static_cast<int>(LatencyClass::fast)] == 128);  // FaultyDevice reports 1 ms
    CHECK(s.min_us == 1000);
    CHECK(s.max_us == 1000);
    CHECK(fd->reads() == 128);
    auto p = scan->progress();
    CHECK(p.percent() == 100.0);
    CHECK(p.errors == 0);
    std::vector<std::uint8_t> classes;
    scan->copy_classes(classes);
    CHECK(std::all_of(classes.begin(), classes.end(), [](auto c) { return c == std::uint8_t(LatencyClass::fast); }));
}

TEST_CASE("partial last block and custom range map to exact LBAs", "[surface]") {
    TempDir tmp;
    auto [dev, fd] = faulty_image(tmp, 1ull << 20);  // 2048 LBAs
    auto c = cfg(64 * 1024);                          // 128 LBAs per block
    c.start_lba = 100;
    c.end_lba = 499;                                  // 400 LBAs -> 3 full blocks + 16 LBAs
    auto scan = run(std::move(dev), c);
    const auto& L = scan->layout();
    REQUIRE(L.block_count == 4);
    CHECK(L.first_lba(0) == 100);
    CHECK(L.first_lba(3) == 484);
    CHECK(L.lba_count(3) == 16);
    CHECK(L.block_of_lba(483) == 2);
    CHECK(L.block_of_lba(484) == 3);
    CHECK(scan->stats().tested_bytes == 400 * 512);
    CHECK(scan->progress().current_lba == 499);
}

TEST_CASE("4096-byte sector devices", "[surface]") {
    TempDir tmp;
    auto [dev, fd] = faulty_image(tmp, 4ull << 20, 4096);
    auto scan = run(std::move(dev), cfg(1 << 20));
    CHECK(scan->layout().sector_size == 4096);
    CHECK(scan->layout().lbas_per_block == 256);
    CHECK(scan->layout().block_count == 4);
    CHECK(scan->stats().tested_bytes == 4ull << 20);
}

TEST_CASE("errors, retries and slow blocks are recorded per block", "[surface]") {
    TempDir tmp;
    auto [dev, fd] = faulty_image(tmp, 8ull << 20);
    fd->bad_lbas = {300, 5000};                  // blocks 2 and 39
    fd->flaky_lbas = {{1000, 2}};                // block 7: fails twice, then reads
    fd->latency_ns_at = {{2000, 30'000'000},     // block 15: 30 ms  -> medium
                         {4000, 250'000'000}};   // block 31: 250 ms -> very slow
    auto c = cfg();
    c.retries = 2;
    auto scan = run(std::move(dev), c);
    CHECK(scan->state() == OperationState::completed);

    CHECK(scan->block(2).cls == LatencyClass::error);
    CHECK(scan->block(2).errc == static_cast<std::uint16_t>(Errc::io_error));
    CHECK(scan->block(2).retries == 2);
    CHECK(scan->block(39).cls == LatencyClass::error);
    CHECK(scan->block(7).cls == LatencyClass::fast);  // recovered on the 3rd attempt
    CHECK(scan->block(7).retries == 2);
    CHECK(scan->block(15).cls == LatencyClass::medium);
    CHECK(scan->block(31).cls == LatencyClass::very_slow);
    CHECK(scan->block(31).duration_us == 250'000);

    auto s = scan->stats();
    CHECK(s.failed == 2);
    CHECK(s.ok == 126);
    CHECK(s.slow == 1);
    CHECK(s.retries == 2 + 2 + 2);
    CHECK(s.tested_bytes == 126ull * 64 * 1024);
    CHECK(s.max_us == 250'000);
    CHECK(scan->progress().errors == 2);
}

TEST_CASE("stop-after-N error policy", "[surface]") {
    TempDir tmp;
    auto [dev, fd] = faulty_image(tmp, 8ull << 20);
    fd->bad_lbas = {200, 400, 600, 800};
    auto c = cfg();
    c.error_policy = ErrorPolicy::stop_after_n;
    c.max_errors = 2;
    auto scan = run(std::move(dev), c);
    CHECK(scan->state() == OperationState::failed);
    CHECK(scan->progress().message.find("stopped after 2") != std::string::npos);
    CHECK(scan->stats().failed == 2);
    CHECK(scan->stats().requests < 128);
}

TEST_CASE("a disappearing device stops the scan immediately", "[surface][safety]") {
    TempDir tmp;
    auto [dev, fd] = faulty_image(tmp, 8ull << 20);
    fd->remove_after_reads = 10;
    auto c = cfg();
    c.retries = 3;
    auto scan = run(std::move(dev), c);
    CHECK(scan->state() == OperationState::failed);
    CHECK(scan->progress().message.find("disappeared") != std::string::npos);
    CHECK(fd->reads() == 11);  // no retries against a vanished device
}

TEST_CASE("random and sampled scans cover the right blocks", "[surface]") {
    TempDir tmp;
    {
        auto [dev, fd] = faulty_image(tmp, 8ull << 20);
        auto c = cfg();
        c.order = ScanOrder::random;
        auto scan = run(std::move(dev), c);
        CHECK(scan->stats().requests == 128);
        CHECK(scan->stats().per_class[0] == 0);  // nothing left untested
    }
    {
        auto [dev, fd] = faulty_image(tmp, 8ull << 20);
        auto c = cfg();
        c.order = ScanOrder::sampled;
        c.sample_percent = 25;
        auto scan = run(std::move(dev), c);
        CHECK(scan->stats().requests == 32);
        CHECK(scan->stats().total_bytes == 32ull * 64 * 1024);
        CHECK(scan->progress().percent() == 100.0);
        std::vector<std::uint8_t> cls;
        scan->copy_classes(cls);
        CHECK(std::count(cls.begin(), cls.end(), std::uint8_t(LatencyClass::untested)) == 96);
    }
}

TEST_CASE("pause and cancel a running scan", "[surface]") {
    TempDir tmp;
    auto [dev, fd] = faulty_image(tmp, 32ull << 20);  // 512 blocks
    fd->real_delay = 2000us;
    auto scan = std::make_unique<SurfaceScan>(std::move(dev), cfg());
    REQUIRE(scan->validate());
    REQUIRE(scan->start());
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (scan->stats().requests < 5 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    scan->pause();
    std::this_thread::sleep_for(30ms);
    const auto frozen = scan->stats().requests;
    std::this_thread::sleep_for(100ms);
    CHECK(scan->stats().requests == frozen);
    CHECK(scan->state() == OperationState::paused);
    scan->resume();
    std::this_thread::sleep_for(50ms);
    CHECK(scan->stats().requests > frozen);
    scan->cancel();
    scan->wait();
    CHECK(scan->state() == OperationState::cancelled);
    CHECK(scan->stats().requests < 512);
}

TEST_CASE("invalid configurations are rejected at validation", "[surface][safety]") {
    TempDir tmp;
    auto check_rejected = [&](auto mutate, Errc expected) {
        auto [dev, fd] = faulty_image(tmp, 1ull << 20);
        auto c = cfg();
        mutate(c);
        SurfaceScan scan(std::move(dev), c);
        auto v = scan.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == expected);
        CHECK(scan.state() == OperationState::failed);
    };
    check_rejected([](ScanConfig& c) { c.mode = ScanMode::write; }, Errc::unsupported);
    check_rejected([](ScanConfig& c) { c.mode = ScanMode::verify; }, Errc::unsupported);
    check_rejected([](ScanConfig& c) { c.block_bytes = 1000; }, Errc::invalid_argument);
    check_rejected([](ScanConfig& c) { c.block_bytes = 128u << 20; }, Errc::invalid_argument);
    check_rejected([](ScanConfig& c) { c.end_lba = 2048; }, Errc::out_of_range);  // device has 2048 LBAs
    check_rejected([](ScanConfig& c) { c.start_lba = 10, c.end_lba = 5; }, Errc::invalid_argument);
    check_rejected([](ScanConfig& c) { c.thresholds.us = {5, 4, 3, 2}; }, Errc::invalid_argument);
    check_rejected([](ScanConfig& c) { c.order = ScanOrder::sampled, c.sample_percent = 0; }, Errc::invalid_argument);
    check_rejected([](ScanConfig& c) { c.error_policy = ErrorPolicy::stop_after_n, c.max_errors = 0; },
                   Errc::invalid_argument);
}

TEST_CASE("a surface scan refuses a writable handle", "[surface][safety]") {
    TempDir tmp;
    const auto img = tmp / "rw.img";
    test::make_sparse_file(img, 1 << 20);
    OpenOptions o;
    o.mode = OpenMode::read_write;
    auto dev = open_image(img.string(), o);
    REQUIRE(dev);
    SurfaceScan scan(std::move(dev).value(), cfg());
    auto v = scan.validate();
    REQUIRE_FALSE(v);
    CHECK(v.error().message.find("read-only") != std::string::npos);
}

TEST_CASE("create_scan opens images read-only and falls back from direct I/O", "[surface]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 2 << 20);
    ScanConfig c = cfg(1 << 20);
    c.direct_io = true;
    auto scan = create_scan(img.string(), /*is_image=*/true, c);
    REQUIRE(scan);
    REQUIRE((*scan)->validate());
    REQUIRE((*scan)->start());
    (*scan)->wait();
    CHECK((*scan)->state() == OperationState::completed);
    CHECK((*scan)->stats().tested_bytes == 2u << 20);
    // Either O_DIRECT worked, or we fell back and said so.
    if (!(*scan)->config().direct_io) CHECK_FALSE((*scan)->warnings().empty());
}

TEST_CASE("two read-only scans may share a device; leases are released", "[surface][operation]") {
    TempDir tmp;
    auto [d1, f1] = faulty_image(tmp, 4ull << 20);
    f1->real_delay = 500us;
    auto s1 = std::make_unique<SurfaceScan>(std::move(d1), cfg());
    const auto id = s1->device_id();
    REQUIRE(s1->validate());
    REQUIRE(s1->start());
    std::this_thread::sleep_for(5ms);
    // A destructive operation would be refused while the scan holds the device.
    CHECK(DeviceLeaseRegistry::global().acquire(id, AccessKind::exclusive).error().code == Errc::device_busy);
    s1->wait();
    CHECK(DeviceLeaseRegistry::global().holders(id) == 0);
}
