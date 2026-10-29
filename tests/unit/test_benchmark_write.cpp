// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

#include "platter/benchmark/benchmark.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::bench;
using platter::safety::Kind;
using platter::safety::TestAccess;
using platter::test::FaultyWriteDevice;
using platter::test::TempDir;

namespace {

BlockDevicePtr open_rw(const std::filesystem::path& p) {
    OpenOptions oo;
    oo.mode = OpenMode::read_write;
    auto d = open_image(p.string(), oo, 512);
    REQUIRE(d);
    return std::move(d).value();
}

BenchConfig wcfg(BenchKind k) {
    BenchConfig c;
    c.kind = k;
    c.direct_io = false;
    c.max_bytes = 1 << 20;
    c.max_seconds = 0.4;
    c.block_bytes = 64 * 1024;
    return c;
}

std::unique_ptr<Benchmark> run(safety::AuthorizedTarget t, const BenchConfig& c) {
    auto b = std::make_unique<Benchmark>(std::move(t), c);
    auto v = b->validate();
    INFO((v ? std::string() : v.error().describe()));
    REQUIRE(v);
    REQUIRE(b->start());
    b->wait();
    return b;
}

}  // namespace

TEST_CASE("kinds: write kinds are named and flagged", "[benchmark][write]") {
    CHECK(is_write_kind(BenchKind::sequential_write));
    CHECK(is_write_kind(BenchKind::random_write));
    CHECK_FALSE(is_write_kind(BenchKind::sequential));
    CHECK(bench_kind_name(BenchKind::sequential_write) == "sequential_write");
    CHECK(bench_kind_title(BenchKind::random_write) == "Random write");
    CHECK(kBenchKindCount == 8);
}

TEST_CASE("sequential write benchmark writes only the confirmed region", "[benchmark][write]") {
    TempDir tmp;
    const auto img = tmp / "w.img";
    test::make_sparse_file(img, 8 << 20);  // 16384 sectors
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    auto* raw = dev.get();

    auto c = wcfg(BenchKind::sequential_write);
    c.start_lba = 1024;
    c.end_lba = 1024 + 2048 - 1;  // 1 MiB region
    auto b = run(TestAccess::make(std::move(dev), Kind::write_test, {LbaRange{1024, 4096}}), c);
    CHECK(b->state() == OperationState::completed);

    auto r = b->result();
    REQUIRE(r.phases.size() == 1);
    CHECK(r.phases[0].metrics.bytes == (1u << 20));
    CHECK(r.phases[0].metrics.mb_per_s > 0);
    CHECK(r.phases[0].metrics.errors == 0);
    bool destructive = false;
    for (const auto& w : r.warnings) destructive |= w.find("DESTRUCTIVE") != std::string::npos;
    CHECK(destructive);
    CHECK_FALSE(r.positions.empty());

    // Data really landed, only inside the region.
    const auto inside = test::read_at(img, 1024 * 512, 1 << 20);
    CHECK(std::count(inside.begin(), inside.end(), 0) < static_cast<std::ptrdiff_t>(inside.size() / 10));
    CHECK(test::read_at(img, 0, 512) == std::vector<std::uint8_t>(512, 0));
    CHECK(test::read_at(img, (1024 + 2048) * 512, 512) == std::vector<std::uint8_t>(512, 0));
    for (const auto& w : raw->write_log()) {
        CHECK(w.offset >= 1024u * 512);
        CHECK(w.offset + w.length <= (1024u + 2048u) * 512);
    }
}

TEST_CASE("random write benchmark does IOPS inside its region", "[benchmark][write]") {
    TempDir tmp;
    const auto img = tmp / "rw.img";
    test::make_sparse_file(img, 4 << 20);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    auto* raw = dev.get();

    auto c = wcfg(BenchKind::random_write);
    c.block_bytes = 4096;
    c.start_lba = 2048;
    c.end_lba = 4095;
    auto b = run(TestAccess::make(std::move(dev), Kind::write_test, {LbaRange{2048, 2048}}), c);
    CHECK(b->state() == OperationState::completed);
    auto r = b->result();
    REQUIRE(r.phases.size() == 1);
    CHECK(r.phases[0].metrics.ops > 10);
    CHECK(r.phases[0].metrics.iops > 0);
    CHECK(r.headline().find("IOPS") != std::string::npos);
    for (const auto& w : raw->write_log()) {
        CHECK(w.offset >= 2048u * 512);
        CHECK(w.offset + w.length <= 4096u * 512);
        CHECK(w.offset % 4096 == 0);  // block aligned within the region
    }
}

TEST_CASE("write benchmark is refused without the right authorization", "[benchmark][write]") {
    TempDir tmp;
    const auto img = tmp / "no.img";
    test::make_sparse_file(img, 2 << 20);

    // Wrong kind of authorization.
    {
        Benchmark b(TestAccess::make(open_rw(img), Kind::erase), wcfg(BenchKind::sequential_write));
        auto v = b.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    // Region outside the confirmed range.
    {
        auto c = wcfg(BenchKind::sequential_write);
        c.start_lba = 0;
        c.end_lba = 999;
        Benchmark b(TestAccess::make(open_rw(img), Kind::write_test, {LbaRange{500, 500}}), c);
        auto v = b.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    // A read benchmark cannot be started through an authorization.
    {
        Benchmark b(TestAccess::make(open_rw(img), Kind::write_test), wcfg(BenchKind::sequential));
        CHECK_FALSE(b.validate());
    }
    // A write kind cannot run on the read-only constructor.
    {
        Benchmark b([&]() -> Result<BlockDevicePtr> { return open_image(img.string(), {}, 512); }, wcfg(BenchKind::random_write),
                    img.string(), true);
        auto v = b.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    CHECK(test::read_at(img, 0, 512) == std::vector<std::uint8_t>(512, 0));
}

TEST_CASE("write errors are counted and a vanishing device is fatal", "[benchmark][write]") {
    TempDir tmp;
    const auto img = tmp / "e.img";
    test::make_sparse_file(img, 2 << 20);
    {
        auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
        for (std::uint64_t l = 0; l < 4096; l += 8) dev->bad_write_lbas.insert(l);  // every 4 KiB block start
        auto c = wcfg(BenchKind::sequential_write);
        c.block_bytes = 4096;
        c.max_bytes = 512 * 1024;  // 128 blocks, all failing
        auto b = run(TestAccess::make(std::move(dev), Kind::write_test), c);
        // 64 errors is the abort threshold; this run produces more than that.
        CHECK(b->state() == OperationState::failed);
        CHECK(b->result().phases.size() == 1);
        CHECK(b->result().phases[0].metrics.errors > 0);
    }
    {
        auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
        dev->remove_after_writes = 5;
        auto c = wcfg(BenchKind::sequential_write);
        c.block_bytes = 4096;
        auto b = run(TestAccess::make(std::move(dev), Kind::write_test), c);
        CHECK(b->state() == OperationState::failed);
        INFO(b->progress().message);
        CHECK(b->progress().message.find("disappeared") != std::string::npos);
    }
}
