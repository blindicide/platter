// SPDX-License-Identifier: TBD
#include "platter/core/range.hpp"

#include <string>

namespace platter {

Result<LbaRange> lba_range_inclusive(std::uint64_t first, std::uint64_t last) {
    if (last < first)
        return Error{Errc::invalid_argument, "end LBA " + std::to_string(last) +
                                                 " is before start LBA " + std::to_string(first)};
    if (first == 0 && last == UINT64_MAX)
        return Error{Errc::overflow, "range covers the entire 64-bit LBA space"};
    return LbaRange{first, last - first + 1};
}

Result<ByteSpan> to_bytes(const LbaRange& r, std::uint32_t sector_size) {
    if (sector_size == 0) return Error{Errc::invalid_argument, "sector size is zero"};
    auto off = checked_mul(r.first, sector_size);
    auto len = checked_mul(r.count, sector_size);
    if (!off || !len) return Error{Errc::overflow, "LBA range overflows byte offsets"};
    if (!checked_add(*off, *len)) return Error{Errc::overflow, "LBA range end overflows"};
    return ByteSpan{*off, *len};
}

Status validate_io(std::uint64_t offset, std::size_t length, std::size_t buffer_size,
                   std::uint64_t capacity_bytes, std::uint32_t alignment) {
    if (length > buffer_size)
        return Error{Errc::invalid_argument, "request of " + std::to_string(length) +
                                                 " bytes exceeds buffer of " +
                                                 std::to_string(buffer_size) + " bytes"};
    auto end = checked_add(offset, static_cast<std::uint64_t>(length));
    if (!end) return Error{Errc::overflow, "offset + length overflows 64 bits"};
    if (*end > capacity_bytes)
        return Error{Errc::out_of_range, "request [" + std::to_string(offset) + ", " +
                                             std::to_string(*end) + ") exceeds capacity " +
                                             std::to_string(capacity_bytes)};
    if (alignment > 1 && (!is_aligned(offset, alignment) || !is_aligned(length, alignment)))
        return Error{Errc::misaligned, "offset/length must be multiples of " +
                                           std::to_string(alignment) + " bytes"};
    return {};
}

Status validate_lba_range(const LbaRange& r, std::uint64_t total_lbas) {
    if (r.empty()) return Error{Errc::invalid_argument, "empty LBA range"};
    auto end = checked_add(r.first, r.count);
    if (!end) return Error{Errc::overflow, "LBA range end overflows"};
    if (*end > total_lbas)
        return Error{Errc::out_of_range, "LBA range ends at " + std::to_string(*end - 1) +
                                             " but device has " + std::to_string(total_lbas) +
                                             " LBAs"};
    return {};
}

}  // namespace platter
