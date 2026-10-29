// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/core/chacha20.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

namespace platter {
namespace {

constexpr std::uint32_t rotl(std::uint32_t v, int n) noexcept { return (v << n) | (v >> (32 - n)); }

constexpr std::uint32_t load_le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void quarter(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d) noexcept {
    a += b; d ^= a; d = rotl(d, 16);
    c += d; b ^= c; b = rotl(b, 12);
    a += b; d ^= a; d = rotl(d, 8);
    c += d; b ^= c; b = rotl(b, 7);
}

}  // namespace

ChaCha20::ChaCha20(const Key& key, const Nonce& nonce) noexcept {
    state_[0] = 0x61707865;
    state_[1] = 0x3320646e;
    state_[2] = 0x79622d32;
    state_[3] = 0x6b206574;
    for (int i = 0; i < 8; ++i) state_[4 + static_cast<std::size_t>(i)] = load_le32(key.data() + 4 * i);
    state_[12] = 0;
    for (int i = 0; i < 3; ++i) state_[13 + static_cast<std::size_t>(i)] = load_le32(nonce.data() + 4 * i);
}

void ChaCha20::block(std::uint64_t block_index, std::uint8_t out[64]) const noexcept {
    std::array<std::uint32_t, 16> x = state_;
    x[12] = static_cast<std::uint32_t>(block_index);
    // Past 2^32 blocks (256 GiB) the high bits perturb the first nonce word,
    // so one key covers any device size. Epoch 0 is plain RFC 8439.
    x[13] += static_cast<std::uint32_t>(block_index >> 32);
    const auto in = x;
    for (int i = 0; i < 10; ++i) {
        quarter(x[0], x[4], x[8], x[12]);
        quarter(x[1], x[5], x[9], x[13]);
        quarter(x[2], x[6], x[10], x[14]);
        quarter(x[3], x[7], x[11], x[15]);
        quarter(x[0], x[5], x[10], x[15]);
        quarter(x[1], x[6], x[11], x[12]);
        quarter(x[2], x[7], x[8], x[13]);
        quarter(x[3], x[4], x[9], x[14]);
    }
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint32_t v = x[i] + in[i];
        out[4 * i + 0] = static_cast<std::uint8_t>(v);
        out[4 * i + 1] = static_cast<std::uint8_t>(v >> 8);
        out[4 * i + 2] = static_cast<std::uint8_t>(v >> 16);
        out[4 * i + 3] = static_cast<std::uint8_t>(v >> 24);
    }
}

void ChaCha20::generate(std::uint64_t offset, std::uint8_t* out, std::size_t length) const noexcept {
    std::uint8_t tmp[64];
    while (length > 0) {
        const auto skip = static_cast<std::size_t>(offset % 64);
        block(offset / 64, tmp);
        const std::size_t n = std::min<std::size_t>(64 - skip, length);
        std::memcpy(out, tmp + skip, n);
        out += n;
        offset += n;
        length -= n;
    }
}

ChaCha20::Key ChaCha20::random_key() {
    std::random_device rd;
    Key k{};
    for (std::size_t i = 0; i < k.size(); i += 4) {
        const std::uint32_t v = rd();
        std::memcpy(k.data() + i, &v, 4);
    }
    // Mix in the clock so a broken random_device still gives distinct keys per run.
    const auto t = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    for (std::size_t i = 0; i < 8; ++i) k[i] ^= static_cast<std::uint8_t>(t >> (8 * i));
    return k;
}

ChaCha20::Nonce ChaCha20::random_nonce() {
    std::random_device rd;
    Nonce n{};
    for (std::size_t i = 0; i < n.size(); i += 4) {
        const std::uint32_t v = rd();
        std::memcpy(n.data() + i, &v, 4);
    }
    return n;
}

}  // namespace platter
