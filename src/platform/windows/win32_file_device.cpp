// SPDX-License-Identifier: TBD
//
// Win32 implementation of IBlockDevice for image files and \\.\PhysicalDriveN.
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <vector>

#include "platter/core/range.hpp"
#include "../file_device.hpp"
#include "win32_util.hpp"

namespace platter::win32 {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w, int len) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    if (len < 0 && !s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::string narrow(const std::wstring& w) { return narrow(w.data(), static_cast<int>(w.size())); }

Errc win_error_to_errc(DWORD code) noexcept {
    switch (code) {
        case ERROR_ACCESS_DENIED:
        case ERROR_PRIVILEGE_NOT_HELD: return Errc::permission_denied;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_DRIVE: return Errc::not_found;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_BUSY: return Errc::device_busy;
        case ERROR_DEVICE_NOT_CONNECTED:
        case ERROR_NOT_READY:
        case ERROR_NO_SUCH_DEVICE:
        case ERROR_DEV_NOT_EXIST:
        case ERROR_MEDIA_CHANGED: return Errc::device_removed;
        case ERROR_SEM_TIMEOUT:
        case ERROR_TIMEOUT: return Errc::timeout;
        case ERROR_WRITE_PROTECT: return Errc::read_only;
        case ERROR_INVALID_PARAMETER: return Errc::invalid_argument;
        case ERROR_NOT_SUPPORTED:
        case ERROR_INVALID_FUNCTION: return Errc::unsupported;
        default: return Errc::io_error;
    }
}

std::string win_error_message(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string msg = n ? narrow(buf, static_cast<int>(n)) : "error " + std::to_string(code);
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ' || msg.back() == '.'))
        msg.pop_back();
    return msg;
}

Error last_error(const std::string& what) {
    const DWORD code = GetLastError();
    return Error{win_error_to_errc(code), what + ": " + win_error_message(code), static_cast<std::int64_t>(code)};
}

}  // namespace platter::win32

namespace platter::detail {
namespace {

using win32::Handle;

#ifndef FSCTL_ALLOW_EXTENDED_DASD_IO
#define FSCTL_ALLOW_EXTENDED_DASD_IO CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 32, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

// "\\.\PhysicalDrive3" -> 3
std::optional<DWORD> physical_drive_number(const std::string& path) {
    static const std::string kPrefix = "\\\\.\\PhysicalDrive";
    if (path.size() <= kPrefix.size() || path.compare(0, kPrefix.size(), kPrefix) != 0) return std::nullopt;
    DWORD n = 0;
    for (std::size_t i = kPrefix.size(); i < path.size(); ++i) {
        if (path[i] < '0' || path[i] > '9' || n > 100000) return std::nullopt;
        n = n * 10 + static_cast<DWORD>(path[i] - '0');
    }
    return n;
}

// Locks and dismounts every volume that has an extent on `disk`, returning the
// handles: the locks last exactly as long as the handles stay open. Any volume
// that cannot be locked (in use) aborts the whole open: never write underneath
// a mounted filesystem.
Result<std::vector<Handle>> lock_volumes_of_disk(DWORD disk) {
    std::vector<Handle> locked;
    wchar_t name[MAX_PATH + 1] = {};
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE) return locked;  // no volumes at all
    struct FindGuard {
        HANDLE h;
        ~FindGuard() { FindVolumeClose(h); }
    } guard{find};
    do {
        std::wstring vol = name;
        while (!vol.empty() && vol.back() == L'\\') vol.pop_back();  // CreateFile wants no trailing backslash
        Handle h(CreateFileW(vol.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr));
        if (!h.valid()) continue;  // CD-ROM, network etc.: not ours
        alignas(VOLUME_DISK_EXTENTS) char buf[1024];
        DWORD ret = 0;
        if (!DeviceIoControl(h.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf, sizeof buf, &ret, nullptr))
            continue;  // not a disk volume
        const auto* ext = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buf);
        bool ours = false;
        for (DWORD i = 0; i < ext->NumberOfDiskExtents; ++i)
            if (ext->Extents[i].DiskNumber == disk) ours = true;
        if (!ours) continue;
        if (!DeviceIoControl(h.get(), FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &ret, nullptr)) {
            auto e = win32::last_error("cannot lock volume " + win32::narrow(vol) + " (in use by another program)");
            e.code = Errc::device_busy;
            return e;
        }
        if (!DeviceIoControl(h.get(), FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &ret, nullptr))
            return win32::last_error("cannot dismount volume " + win32::narrow(vol));
        locked.push_back(std::move(h));
    } while (FindNextVolumeW(find, name, MAX_PATH));
    return locked;
}

class Win32FileDevice final : public IBlockDevice {
public:
    Win32FileDevice(Handle h, DeviceInfo info, OpenMode mode, std::uint32_t alignment, std::vector<Handle> volume_locks = {})
        : h_(std::move(h)),
          volume_locks_(std::move(volume_locks)),
          info_(std::move(info)),
          mode_(mode),
          alignment_(alignment) {}

    const DeviceInfo& info() const noexcept override { return info_; }
    OpenMode mode() const noexcept override { return mode_; }
    std::uint32_t required_alignment() const noexcept override { return alignment_; }

    Result<IOResult> read(std::uint64_t offset, void* buffer, std::size_t length) override {
        if (auto s = validate_io(offset, length, length, info_.capacity_bytes, alignment_); !s) return s.error();
        return transfer(offset, static_cast<char*>(buffer), length, false);
    }

    Result<IOResult> write(std::uint64_t offset, const void* buffer, std::size_t length) override {
        if (mode_ != OpenMode::read_write) return Error{Errc::read_only, "device was opened read-only"};
        if (auto s = validate_io(offset, length, length, info_.capacity_bytes, alignment_); !s) return s.error();
        return transfer(offset, const_cast<char*>(static_cast<const char*>(buffer)), length, true);
    }

    Status flush() override {
        if (mode_ != OpenMode::read_write) return {};
        if (!FlushFileBuffers(h_.get())) return win32::last_error("FlushFileBuffers");
        return {};
    }

    Status reread_partitions() override {
        if (mode_ != OpenMode::read_write) return Error{Errc::read_only, "device was opened read-only"};
        DWORD ret = 0;
        if (!DeviceIoControl(h_.get(), IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &ret, nullptr))
            return win32::last_error("IOCTL_DISK_UPDATE_PROPERTIES");
        return {};
    }

private:
    Result<IOResult> transfer(std::uint64_t offset, char* buf, std::size_t length, bool writing) {
        constexpr std::size_t kMaxChunk = std::size_t{1} << 30;  // DWORD-safe, sector-aligned
        IOResult res;
        const auto t0 = std::chrono::steady_clock::now();
        while (res.bytes < length) {
            const std::uint64_t off = offset + res.bytes;
            const auto want = static_cast<DWORD>(std::min(length - res.bytes, kMaxChunk));
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(off & 0xFFFFFFFFu);
            ov.OffsetHigh = static_cast<DWORD>(off >> 32);
            DWORD done = 0;
            const BOOL ok = writing ? WriteFile(h_.get(), buf + res.bytes, want, &done, &ov)
                                    : ReadFile(h_.get(), buf + res.bytes, want, &done, &ov);
            if (!ok) {
                if (GetLastError() == ERROR_HANDLE_EOF) {
                    done = 0;
                } else {
                    auto err = win32::last_error(writing ? "WriteFile" : "ReadFile");
                    err.message += " at byte offset " + std::to_string(off);
                    return err;
                }
            }
            if (done == 0)
                return Error{Errc::short_transfer, "unexpected end of device after " + std::to_string(res.bytes) +
                                                       " of " + std::to_string(length) + " bytes"};
            res.bytes += done;
        }
        res.duration_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
        return res;
    }

    Handle h_;
    std::vector<Handle> volume_locks_;  // closed after h_: locks outlive all writes
    DeviceInfo info_;
    OpenMode mode_;
    std::uint32_t alignment_;
};

}  // namespace

Result<BlockDevicePtr> open_file_device(const std::string& path, const OpenOptions& opts, DeviceInfo info,
                                        bool is_image) {
    const DWORD access = GENERIC_READ | (opts.mode == OpenMode::read_write ? GENERIC_WRITE : 0);
    // Physical disks must be shared or Windows refuses the open while volumes
    // are mounted; exclusivity on disks comes from FSCTL_LOCK_VOLUME on each
    // volume (see lock_volumes_of_disk). Images can be opened exclusively.
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    // (still readable by others: tools and tests may inspect the file while it is held)
    if (opts.exclusive && is_image) share = FILE_SHARE_READ;
    // Writing to a physical disk: take every volume on it first.
    std::vector<Handle> volume_locks;
    if (opts.exclusive && opts.mode == OpenMode::read_write && !is_image) {
        const auto disk = physical_drive_number(path);
        if (!disk) return Error{Errc::invalid_argument, "exclusive write access needs a \\\\.\\PhysicalDriveN path, got " + path};
        auto locks = lock_volumes_of_disk(*disk);
        if (!locks) return locks.error();
        volume_locks = std::move(locks).value();
    }
    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    if (opts.direct_io) flags |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;

    Handle h(CreateFileW(win32::widen(path).c_str(), access, share, nullptr, OPEN_EXISTING, flags, nullptr));
    if (!h.valid()) return win32::last_error("CreateFile " + path);

    if (is_image) {
        BY_HANDLE_FILE_INFORMATION fi{};
        if (!GetFileInformationByHandle(h.get(), &fi)) return win32::last_error("GetFileInformationByHandle");
        if (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            return Error{Errc::invalid_argument, path + " is a directory"};
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(h.get(), &size)) return win32::last_error("GetFileSizeEx");
        info.capacity_bytes = static_cast<std::uint64_t>(size.QuadPart);
    } else {
        GET_LENGTH_INFORMATION len{};
        DWORD ret = 0;
        if (DeviceIoControl(h.get(), IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &len, sizeof len, &ret, nullptr))
            info.capacity_bytes = static_cast<std::uint64_t>(len.Length.QuadPart);
        else
            return win32::last_error("IOCTL_DISK_GET_LENGTH_INFO");
    }

    if (!volume_locks.empty() || (opts.exclusive && opts.mode == OpenMode::read_write && !is_image)) {
        // Volumes are locked and dismounted; let raw writes reach their extents.
        DWORD ret = 0;
        DeviceIoControl(h.get(), FSCTL_ALLOW_EXTENDED_DASD_IO, nullptr, 0, nullptr, 0, &ret, nullptr);
    }
    const std::uint32_t alignment = opts.direct_io ? info.logical_sector_size : 1;
    return BlockDevicePtr(new Win32FileDevice(std::move(h), std::move(info), opts.mode, alignment, std::move(volume_locks)));
}

}  // namespace platter::detail
