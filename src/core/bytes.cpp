// SPDX-License-Identifier: TBD
#include "platter/core/bytes.hpp"

#include <array>

namespace platter {
namespace {

constexpr std::array<std::uint32_t, 256> make_table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[i] = c;
    }
    return t;
}

constexpr auto kTable = make_table();

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc) noexcept {
    crc = ~crc;
    for (auto b : data) crc = kTable[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

}  // namespace platter
