// SPDX-License-Identifier: TBD
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "platter/core/device.hpp"
#include "platter/core/error.hpp"

namespace platter {

enum class OpenMode : std::uint8_t {
    read_only,
    // Read-write is only ever requested by destructive operations after the
    // full safety pipeline (docs/safety.md). Nothing in v0.1 uses it on
    // physical devices.
    read_write,
};

struct OpenOptions {
    OpenMode mode = OpenMode::read_only;
    // Bypass the OS page cache (O_DIRECT / FILE_FLAG_NO_BUFFERING). Requires
    // sector-aligned offsets, lengths and buffers.
    bool direct_io = false;
    // Take an exclusive lock where the platform supports it (O_EXCL on Linux
    // block devices, FSCTL_LOCK_VOLUME on Windows volumes).
    bool exclusive = false;
};

struct IOResult {
    std::size_t bytes = 0;  // bytes actually transferred
    // Wall-clock duration of the OS call, in nanoseconds. Used by surface scans.
    std::uint64_t duration_ns = 0;
};

class IBlockDevice {
public:
    virtual ~IBlockDevice() = default;

    IBlockDevice(const IBlockDevice&) = delete;
    IBlockDevice& operator=(const IBlockDevice&) = delete;

    virtual const DeviceInfo& info() const noexcept = 0;
    virtual OpenMode mode() const noexcept = 0;

    // Reads exactly `length` bytes at `offset`. Fails with short_transfer if the
    // OS returns fewer bytes (partial data is still in `buffer` and reported in
    // IOResult via the error's message).
    virtual Result<IOResult> read(std::uint64_t offset, void* buffer, std::size_t length) = 0;
    virtual Result<IOResult> write(std::uint64_t offset, const void* buffer, std::size_t length) = 0;
    virtual Status flush() = 0;

    // Tells the device that a byte range no longer holds useful data (TRIM /
    // UNMAP / hole punching). Optional: the default reports `unsupported`.
    // Whether the range reads back as zeros afterwards is device specific, so
    // callers that care must read it back. Requires a read-write handle.
    virtual Status discard(std::uint64_t offset, std::uint64_t length) {
        static_cast<void>(offset);
        static_cast<void>(length);
        return Error{Errc::unsupported, "discard is not supported by this backend"};
    }

    // Alignment the caller must honour for offsets and lengths.
    virtual std::uint32_t required_alignment() const noexcept = 0;

    std::uint64_t capacity() const noexcept { return info().capacity_bytes; }
    std::uint32_t sector_size() const noexcept { return info().logical_sector_size; }

protected:
    IBlockDevice() = default;
};

using BlockDevicePtr = std::unique_ptr<IBlockDevice>;

// Opens a raw image file as a block device. Always available on every platform.
Result<BlockDevicePtr> open_image(const std::string& path, const OpenOptions& opts = {},
                                  std::uint32_t sector_size = 512);

// Opens a physical device by path using the platform backend.
Result<BlockDevicePtr> open_device(const std::string& path, const OpenOptions& opts = {});

// Convenience: read whole sectors starting at `lba`.
Status read_sectors(IBlockDevice& dev, std::uint64_t lba, std::uint64_t count, void* buffer,
                    std::size_t buffer_size);

}  // namespace platter
