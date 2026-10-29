// SPDX-License-Identifier: TBD
//
// POSIX implementation of IBlockDevice. Used for image files on every POSIX
// system and for block devices on Linux.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

#include "platter/core/range.hpp"
#include "../file_device.hpp"

namespace platter::detail {
namespace {

Errc errno_to_errc(int e) {
    switch (e) {
        case EACCES:
        case EPERM: return Errc::permission_denied;
        case EBUSY: return Errc::device_busy;
        case ENOENT:
        case ENXIO: return Errc::not_found;
        case ENODEV:
        case ENOMEDIUM: return Errc::device_removed;
        case EINVAL: return Errc::invalid_argument;
        case EROFS: return Errc::read_only;
        case ETIMEDOUT: return Errc::timeout;
        default: return Errc::io_error;
    }
}

Error os_error(const std::string& what, int e) {
    return Error{errno_to_errc(e), what + ": " + std::strerror(e), e};
}

class PosixFileDevice final : public IBlockDevice {
public:
    PosixFileDevice(int fd, DeviceInfo info, OpenMode mode, std::uint32_t alignment)
        : fd_(fd), info_(std::move(info)), mode_(mode), alignment_(alignment) {}

    ~PosixFileDevice() override {
        if (fd_ >= 0) ::close(fd_);
    }

    const DeviceInfo& info() const noexcept override { return info_; }
    OpenMode mode() const noexcept override { return mode_; }
    std::uint32_t required_alignment() const noexcept override { return alignment_; }

    Result<IOResult> read(std::uint64_t offset, void* buffer, std::size_t length) override {
        if (auto s = validate_io(offset, length, length, info_.capacity_bytes, alignment_); !s)
            return s.error();
        return transfer(offset, static_cast<char*>(buffer), length, /*writing=*/false);
    }

    Result<IOResult> write(std::uint64_t offset, const void* buffer, std::size_t length) override {
        if (mode_ != OpenMode::read_write)
            return Error{Errc::read_only, "device was opened read-only"};
        if (auto s = validate_io(offset, length, length, info_.capacity_bytes, alignment_); !s)
            return s.error();
        return transfer(offset, const_cast<char*>(static_cast<const char*>(buffer)), length,
                        /*writing=*/true);
    }

    Status flush() override {
        if (mode_ != OpenMode::read_write) return {};
        if (::fsync(fd_) != 0) return os_error("fsync", errno);
        return {};
    }

    Status discard(std::uint64_t offset, std::uint64_t length) override {
        if (mode_ != OpenMode::read_write) return Error{Errc::read_only, "device was opened read-only"};
        const auto end = checked_add(offset, length);
        if (!end || *end > info_.capacity_bytes)
            return Error{Errc::out_of_range, "discard range lies outside the device"};
#ifdef __linux__
        if (info_.bus == BusType::image) {
            // Hole punching returns the range to zeros on filesystems that support it.
            if (::fallocate(fd_, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, static_cast<off_t>(offset),
                            static_cast<off_t>(length)) != 0)
                return os_error("fallocate(PUNCH_HOLE)", errno);
            return {};
        }
        std::uint64_t range[2] = {offset, length};
        if (::ioctl(fd_, BLKDISCARD, &range) != 0) return os_error("ioctl(BLKDISCARD)", errno);
        return {};
#else
        return Error{Errc::unsupported, "discard is only implemented on Linux"};
#endif
    }

private:
    Result<IOResult> transfer(std::uint64_t offset, char* buf, std::size_t length, bool writing) {
        IOResult res;
        const auto t0 = std::chrono::steady_clock::now();
        while (res.bytes < length) {
            const auto off = static_cast<off_t>(offset + res.bytes);
            const auto want = length - res.bytes;
            ssize_t n = writing ? ::pwrite(fd_, buf + res.bytes, want, off)
                                : ::pread(fd_, buf + res.bytes, want, off);
            if (n < 0) {
                if (errno == EINTR) continue;
                const int e = errno;
                auto err = os_error(writing ? "pwrite" : "pread", e);
                err.message += " at byte offset " + std::to_string(offset + res.bytes);
                return err;
            }
            if (n == 0) {
                return Error{Errc::short_transfer,
                             "unexpected end of device after " + std::to_string(res.bytes) +
                                 " of " + std::to_string(length) + " bytes"};
            }
            res.bytes += static_cast<std::size_t>(n);
        }
        res.duration_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0)
                .count());
        return res;
    }

    int fd_;
    DeviceInfo info_;
    OpenMode mode_;
    std::uint32_t alignment_;
};

}  // namespace

Result<BlockDevicePtr> open_file_device(const std::string& path, const OpenOptions& opts,
                                        DeviceInfo info, bool is_image) {
    int flags = O_CLOEXEC;
    flags |= (opts.mode == OpenMode::read_write) ? O_RDWR : O_RDONLY;
#ifdef O_DIRECT
    if (opts.direct_io) flags |= O_DIRECT;
#endif
#ifdef __linux__
    // On Linux, O_EXCL on a block device fails with EBUSY if it is mounted or
    // held by another exclusive opener: exactly the guarantee we want.
    if (opts.exclusive && !is_image) flags |= O_EXCL;
#endif

    int fd = ::open(path.c_str(), flags);
    if (fd < 0) return os_error("open " + path, errno);

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        const int e = errno;
        ::close(fd);
        return os_error("fstat " + path, e);
    }

    if (is_image) {
        if (!S_ISREG(st.st_mode)) {
            ::close(fd);
            return Error{Errc::invalid_argument, path + " is not a regular file"};
        }
        info.capacity_bytes = static_cast<std::uint64_t>(st.st_size);
    } else {
#ifdef __linux__
        if (!S_ISBLK(st.st_mode)) {
            ::close(fd);
            return Error{Errc::invalid_argument, path + " is not a block device"};
        }
        std::uint64_t size = 0;
        if (::ioctl(fd, BLKGETSIZE64, &size) == 0) info.capacity_bytes = size;
        int lss = 0;
        if (::ioctl(fd, BLKSSZGET, &lss) == 0 && lss > 0)
            info.logical_sector_size = static_cast<std::uint32_t>(lss);
        unsigned int pss = 0;
        if (::ioctl(fd, BLKPBSZGET, &pss) == 0 && pss > 0) info.physical_sector_size = pss;
        int ro = 0;
        if (::ioctl(fd, BLKROGET, &ro) == 0) info.read_only = ro ? Tri::yes : Tri::no;
#else
        ::close(fd);
        return Error{Errc::unsupported, "raw block devices are only supported on Linux and Windows"};
#endif
    }

    const std::uint32_t alignment = opts.direct_io ? info.logical_sector_size : 1;
    return BlockDevicePtr(new PosixFileDevice(fd, std::move(info), opts.mode, alignment));
}

}  // namespace platter::detail
