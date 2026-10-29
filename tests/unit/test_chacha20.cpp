// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include "platter/core/chacha20.hpp"

using namespace platter;

TEST_CASE("ChaCha20 matches RFC 8439 section 2.3.2", "[chacha20]") {
    ChaCha20::Key key{};
    for (std::size_t i = 0; i < key.size(); ++i) key[i] = static_cast<std::uint8_t>(i);
    ChaCha20::Nonce nonce = {0, 0, 0, 9, 0, 0, 0, 0x4a, 0, 0, 0, 0};
    ChaCha20 c(key, nonce);

    // Block counter 1 == stream offset 64.
    std::array<std::uint8_t, 64> out{};
    c.generate(64, out.data(), out.size());
    const std::array<std::uint8_t, 64> expected = {
        0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4,
        0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03, 0x04, 0x22, 0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e,
        0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xaa, 0x09, 0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2,
        0xb5, 0x12, 0x9c, 0xd1, 0xde, 0x16, 0x4e, 0xb9, 0xcb, 0xd0, 0x83, 0xe8, 0xa2, 0x50, 0x3c, 0x4e};
    CHECK(out == expected);
}

TEST_CASE("ChaCha20 is position addressable", "[chacha20]") {
    ChaCha20 c(ChaCha20::random_key(), ChaCha20::random_nonce());
    std::vector<std::uint8_t> whole(5000);
    c.generate(0, whole.data(), whole.size());

    // Arbitrary, unaligned slices must equal the same bytes of the whole stream.
    for (auto [off, len] : {std::pair<std::size_t, std::size_t>{0, 1}, {1, 63}, {63, 2}, {64, 64}, {100, 1000}, {4096, 904}}) {
        std::vector<std::uint8_t> part(len);
        c.generate(off, part.data(), len);
        CHECK(std::equal(part.begin(), part.end(), whole.begin() + static_cast<std::ptrdiff_t>(off)));
    }
}

TEST_CASE("ChaCha20 keeps working beyond 2^32 blocks", "[chacha20]") {
    ChaCha20 c(ChaCha20::random_key(), ChaCha20::random_nonce());
    const std::uint64_t far = (std::uint64_t{1} << 32) * 64 + 128;  // block 2^32 + 2
    std::vector<std::uint8_t> a(64), b(64), low(64);
    c.generate(far, a.data(), a.size());
    c.generate(far, b.data(), b.size());
    c.generate(128, low.data(), low.size());
    CHECK(a == b);
    CHECK(a != low);  // not a wrapped copy of the low counter range
}

TEST_CASE("different keys give different streams", "[chacha20]") {
    ChaCha20 a(ChaCha20::random_key(), ChaCha20::random_nonce());
    ChaCha20 b(ChaCha20::random_key(), ChaCha20::random_nonce());
    std::vector<std::uint8_t> x(256), y(256);
    a.generate(0, x.data(), x.size());
    b.generate(0, y.data(), y.size());
    CHECK(x != y);
}
