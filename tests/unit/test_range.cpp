// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include "platter/core/bytes.hpp"
#include "platter/core/range.hpp"

using namespace platter;

TEST_CASE("checked arithmetic detects overflow", "[range]") {
    CHECK(checked_add(1, 2) == 3u);
    CHECK_FALSE(checked_add(UINT64_MAX, 1));
    CHECK(checked_add(UINT64_MAX - 1, 1) == UINT64_MAX);
    CHECK(checked_mul(0, UINT64_MAX) == 0u);
    CHECK_FALSE(checked_mul(UINT64_MAX / 2 + 1, 2));
    CHECK(checked_mul(1ull << 32, 1ull << 31) == (1ull << 63));
}

TEST_CASE("LbaRange semantics", "[range]") {
    LbaRange r{100, 50};
    CHECK(r.last() == 149);
    CHECK(r.contains(100));
    CHECK(r.contains(149));
    CHECK_FALSE(r.contains(150));
    CHECK_FALSE(r.contains(99));
    CHECK(r.overlaps({149, 1}));
    CHECK_FALSE(r.overlaps({150, 10}));
    CHECK_FALSE(r.overlaps({0, 100}));
    CHECK_FALSE(r.overlaps({120, 0}));
}

TEST_CASE("lba_range_inclusive", "[range]") {
    auto r = lba_range_inclusive(10, 19);
    REQUIRE(r);
    CHECK(r->count == 10);
    CHECK(lba_range_inclusive(5, 4).error().code == Errc::invalid_argument);
    CHECK(lba_range_inclusive(0, UINT64_MAX).error().code == Errc::overflow);
    CHECK(lba_range_inclusive(7, 7)->count == 1);
}

TEST_CASE("to_bytes handles 512 and 4096 sectors and overflow", "[range]") {
    auto b = to_bytes({2, 3}, 512);
    REQUIRE(b);
    CHECK(b->offset == 1024);
    CHECK(b->length == 1536);
    auto b4 = to_bytes({1, 1}, 4096);
    REQUIRE(b4);
    CHECK(b4->offset == 4096);
    CHECK(to_bytes({UINT64_MAX / 512 + 1, 1}, 512).error().code == Errc::overflow);
    CHECK(to_bytes({0, 1}, 0).error().code == Errc::invalid_argument);
}

TEST_CASE("validate_io rejects every class of bad request", "[range][safety]") {
    constexpr std::uint64_t cap = 1 << 20;
    CHECK(validate_io(0, 512, 512, cap, 512));
    CHECK(validate_io(cap - 512, 512, 512, cap, 512));
    CHECK(validate_io(0, 0, 0, cap, 1));

    CHECK(validate_io(0, 1024, 512, cap, 1).error().code == Errc::invalid_argument);  // buffer too small
    CHECK(validate_io(cap - 511, 512, 512, cap, 1).error().code == Errc::out_of_range);
    CHECK(validate_io(cap, 1, 1, cap, 1).error().code == Errc::out_of_range);
    CHECK(validate_io(UINT64_MAX - 10, 512, 512, cap, 1).error().code == Errc::overflow);
    CHECK(validate_io(100, 512, 512, cap, 512).error().code == Errc::misaligned);
    CHECK(validate_io(512, 100, 512, cap, 512).error().code == Errc::misaligned);
}

TEST_CASE("validate_lba_range", "[range][safety]") {
    CHECK(validate_lba_range({0, 100}, 100));
    CHECK(validate_lba_range({99, 1}, 100));
    CHECK(validate_lba_range({99, 2}, 100).error().code == Errc::out_of_range);
    CHECK(validate_lba_range({0, 0}, 100).error().code == Errc::invalid_argument);
    CHECK(validate_lba_range({UINT64_MAX, 2}, 100).error().code == Errc::overflow);
}

TEST_CASE("crc32 matches the standard check value", "[bytes]") {
    const std::string s = "123456789";
    auto span = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    CHECK(crc32(span) == 0xCBF43926u);
    // Running checksum equals one-shot.
    auto a = crc32(span.subspan(0, 4));
    CHECK(crc32(span.subspan(4), a) == 0xCBF43926u);
    CHECK(crc32({}) == 0u);
}

TEST_CASE("little-endian helpers round-trip", "[bytes]") {
    std::uint8_t buf[8]{};
    put_le64(buf, 0x0102030405060708ull);
    CHECK(buf[0] == 0x08);
    CHECK(le64(buf) == 0x0102030405060708ull);
    put_le32(buf, 0xDEADBEEF);
    CHECK(le32(buf) == 0xDEADBEEF);
    put_le16(buf, 0xABCD);
    CHECK(le16(buf) == 0xABCD);
}
