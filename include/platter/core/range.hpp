// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Overflow-safe LBA / byte-range arithmetic. Every I/O path in platter must
// go through validate_io() before touching a device; see docs/safety.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "platter/core/error.hpp"

namespace platter {

constexpr std::optional<std::uint64_t> checked_add(std::uint64_t a, std::uint64_t b) noexcept {
    if (a > UINT64_MAX - b) return std::nullopt;
    return a + b;
}

constexpr std::optional<std::uint64_t> checked_mul(std::uint64_t a, std::uint64_t b) noexcept {
    if (a != 0 && b > UINT64_MAX / a) return std::nullopt;
    return a * b;
}

constexpr bool is_power_of_two(std::uint64_t v) noexcept { return v != 0 && (v & (v - 1)) == 0; }

constexpr bool is_aligned(std::uint64_t v, std::uint64_t alignment) noexcept {
    return alignment != 0 && v % alignment == 0;
}

// Half-open range of logical blocks: [first, first + count).
struct LbaRange {
    std::uint64_t first = 0;
    std::uint64_t count = 0;

    // Inclusive last LBA; only meaningful when count > 0.
    std::uint64_t last() const noexcept { return first + count - 1; }
    bool empty() const noexcept { return count == 0; }
    bool contains(std::uint64_t lba) const noexcept { return lba >= first && lba - first < count; }
    bool overlaps(const LbaRange& o) const noexcept {
        if (empty() || o.empty()) return false;
        return first < o.first + o.count && o.first < first + count;
    }
    bool operator==(const LbaRange&) const = default;
};

// Build an LbaRange from an inclusive [first, last] pair, rejecting last < first.
Result<LbaRange> lba_range_inclusive(std::uint64_t first, std::uint64_t last);

// Convert an LBA range to a byte offset/length, checking for overflow.
struct ByteSpan {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};
Result<ByteSpan> to_bytes(const LbaRange& r, std::uint32_t sector_size);

// Validate a raw I/O request against a device. Checks, in order:
//   * buffer is large enough for the request
//   * offset + length does not overflow
//   * the request lies entirely within capacity
//   * offset and length are multiples of `alignment` (if alignment > 1)
Status validate_io(std::uint64_t offset, std::size_t length, std::size_t buffer_size,
                   std::uint64_t capacity_bytes, std::uint32_t alignment);

// Validate an LBA range against a device of `total_lbas` logical blocks.
Status validate_lba_range(const LbaRange& r, std::uint64_t total_lbas);

}  // namespace platter
