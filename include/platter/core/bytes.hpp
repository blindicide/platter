// SPDX-License-Identifier: TBD
// Little-endian helpers and CRC32. On-disk structures (MBR, GPT, FAT, NTFS,
// ext4) are little-endian regardless of host byte order.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace platter {

inline std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
inline std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
inline std::uint64_t le64(const std::uint8_t* p) noexcept {
    return static_cast<std::uint64_t>(le32(p)) | (static_cast<std::uint64_t>(le32(p + 4)) << 32);
}
inline void put_le16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
inline void put_le32(std::uint8_t* p, std::uint32_t v) noexcept {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline void put_le64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}

// CRC-32/ISO-HDLC (the one GPT, zip and zlib use). Pass a previous result as
// `crc` to continue a running checksum.
std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0) noexcept;

}  // namespace platter
