// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_test_macros.hpp>

#include <numeric>

#include "platter/core/block_device.hpp"
#include "test_support.hpp"

using namespace platter;
using platter::test::TempDir;

TEST_CASE("image device reports geometry and reads data", "[image]") {
    TempDir tmp;
    const auto img = tmp / "disk.img";
    test::make_sparse_file(img, 1 << 20);
    std::vector<std::uint8_t> pattern(512);
    std::iota(pattern.begin(), pattern.end(), std::uint8_t{0});
    test::write_at(img, 4096, pattern);

    auto dev = open_image(img.string());
    REQUIRE(dev);
    auto& d = **dev;
    CHECK(d.capacity() == (1u << 20));
    CHECK(d.sector_size() == 512);
    CHECK(d.info().total_lbas() == 2048);
    CHECK(d.info().bus == BusType::image);
    CHECK(d.mode() == OpenMode::read_only);
    CHECK(d.info().capabilities.has(Capability::read));
    CHECK_FALSE(d.info().capabilities.has(Capability::write));

    std::vector<std::uint8_t> buf(512);
    REQUIRE(read_sectors(d, 8, 1, buf.data(), buf.size()));
    CHECK(buf == pattern);

    // Sparse regions read as zero.
    REQUIRE(read_sectors(d, 0, 1, buf.data(), buf.size()));
    CHECK(std::all_of(buf.begin(), buf.end(), [](auto b) { return b == 0; }));
}

TEST_CASE("image device refuses out-of-range and write-on-read-only", "[image][safety]") {
    TempDir tmp;
    const auto img = tmp / "disk.img";
    test::make_sparse_file(img, 64 * 512);
    auto dev = open_image(img.string());
    REQUIRE(dev);
    std::vector<std::uint8_t> buf(1024);

    auto r = (*dev)->read(63 * 512, buf.data(), 1024);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == Errc::out_of_range);

    auto w = (*dev)->write(0, buf.data(), 512);
    REQUIRE_FALSE(w);
    CHECK(w.error().code == Errc::read_only);

    // The image is untouched.
    CHECK(test::read_at(img, 0, 512) == std::vector<std::uint8_t>(512, 0));
}

TEST_CASE("read-write image round trip", "[image]") {
    TempDir tmp;
    const auto img = tmp / "rw.img";
    test::make_sparse_file(img, 16 * 4096);
    OpenOptions o;
    o.mode = OpenMode::read_write;
    auto dev = open_image(img.string(), o, 4096);
    REQUIRE(dev);
    CHECK((*dev)->info().total_lbas() == 16);
    CHECK((*dev)->info().capabilities.has(Capability::write));

    std::vector<std::uint8_t> data(4096, 0xA5);
    REQUIRE((*dev)->write(4096 * 3, data.data(), data.size()));
    REQUIRE((*dev)->flush());
    std::vector<std::uint8_t> back(4096);
    auto r = (*dev)->read(4096 * 3, back.data(), back.size());
    REQUIRE(r);
    CHECK(r->bytes == 4096);
    CHECK(back == data);
}

TEST_CASE("image open errors", "[image]") {
    TempDir tmp;
    CHECK(open_image((tmp / "missing.img").string()).error().code == Errc::not_found);
    CHECK(open_image(tmp.path().string()).error().code != Errc::ok);  // a directory
    test::make_sparse_file(tmp / "x.img", 4096);
    CHECK(open_image((tmp / "x.img").string(), {}, 1000).error().code == Errc::invalid_argument);
}

TEST_CASE("a trailing partial sector is not addressable", "[image][safety]") {
    TempDir tmp;
    const auto img = tmp / "odd.img";
    test::make_sparse_file(img, 512 * 4 + 100);
    auto dev = open_image(img.string());
    REQUIRE(dev);
    CHECK((*dev)->info().total_lbas() == 4);
    std::vector<std::uint8_t> buf(512);
    CHECK(read_sectors(**dev, 3, 1, buf.data(), buf.size()));
}
