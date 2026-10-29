// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include "platter/surface/surface_scan.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::surface;
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

ScanConfig wcfg(ScanMode m) {
    ScanConfig c;
    c.mode = m;
    c.block_bytes = 64 * 1024;
    c.direct_io = false;
    return c;
}

std::unique_ptr<SurfaceScan> run(safety::AuthorizedTarget t, const ScanConfig& c) {
    auto s = std::make_unique<SurfaceScan>(std::move(t), c);
    auto v = s->validate();
    INFO((v ? std::string() : v.error().describe()));
    REQUIRE(v);
    REQUIRE(s->start());
    s->wait();
    return s;
}

}  // namespace

TEST_CASE("write scan writes the default pattern across the confirmed range only", "[surface][write]") {
    TempDir tmp;
    const auto img = tmp / "s.img";
    test::make_sparse_file(img, 1 << 20);  // 2048 sectors
    auto s = run(TestAccess::make(open_rw(img), Kind::write_test, {LbaRange{256, 512}}),
                 [] { auto c = wcfg(ScanMode::write); c.start_lba = 256; c.end_lba = 767; return c; }());
    CHECK(s->state() == OperationState::completed);
    CHECK(s->stats().failed == 0);
    const auto in = test::read_at(img, 256 * 512, 4);
    CHECK(in == std::vector<std::uint8_t>{0xA5, 0x5A, 0xA5, 0x5A});
    CHECK(test::read_at(img, 0, 512) == std::vector<std::uint8_t>(512, 0));
    CHECK(test::read_at(img, 768 * 512, 512) == std::vector<std::uint8_t>(512, 0));
}

TEST_CASE("write scan refuses ranges outside the authorization and read-only handles", "[surface][write]") {
    TempDir tmp;
    const auto img = tmp / "r.img";
    test::make_sparse_file(img, 1 << 20);
    {
        auto c = wcfg(ScanMode::write);  // whole device, but only 100 sectors confirmed
        SurfaceScan s(TestAccess::make(open_rw(img), Kind::write_test, {LbaRange{0, 100}}), c);
        auto v = s.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    {
        // A write mode on the read-only constructor is refused.
        auto dev = open_image(img.string(), {}, 512);
        REQUIRE(dev);
        SurfaceScan s(std::move(dev).value(), wcfg(ScanMode::write));
        auto v = s.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    {
        // A READ scan through an authorization is refused too.
        SurfaceScan s(TestAccess::make(open_rw(img), Kind::write_test), wcfg(ScanMode::read));
        CHECK_FALSE(s.validate());
    }
    {
        SurfaceScan s(TestAccess::make(open_rw(img), Kind::write_test), wcfg(ScanMode::custom_pattern));
        CHECK_FALSE(s.validate());  // pattern required
    }
    {
        SurfaceScan s(TestAccess::make(open_rw(img), Kind::erase), wcfg(ScanMode::write));
        auto v = s.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    CHECK(test::read_at(img, 0, 512) == std::vector<std::uint8_t>(512, 0));
}

TEST_CASE("write+readback finds blocks that silently drop writes", "[surface][write]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 1 << 20);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->dropped_lbas.insert(300);  // inside block 2 (LBAs 256..383)
    auto c = wcfg(ScanMode::write_readback);
    c.pattern = {0x12, 0x34, 0x56};
    auto s = run(TestAccess::make(std::move(dev), Kind::write_test), c);
    CHECK(s->state() == OperationState::completed);
    CHECK(s->stats().failed == 1);
    CHECK(s->block(2).cls == LatencyClass::error);
    CHECK(s->block(2).errc == static_cast<std::uint16_t>(Errc::corrupt_data));
    CHECK(s->block(1).cls != LatencyClass::error);
    CHECK(test::read_at(img, 0, 3) == std::vector<std::uint8_t>{0x12, 0x34, 0x56});
}

TEST_CASE("write scan records write errors per block and keeps going", "[surface][write]") {
    TempDir tmp;
    const auto img = tmp / "w.img";
    test::make_sparse_file(img, 1 << 20);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->bad_write_lbas.insert(0);
    auto s = run(TestAccess::make(std::move(dev), Kind::write_test), wcfg(ScanMode::write));
    CHECK(s->stats().failed == 1);
    CHECK(s->stats().ok == s->layout().block_count - 1);
    CHECK(s->block(0).cls == LatencyClass::error);
}
