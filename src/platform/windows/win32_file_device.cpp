// SPDX-License-Identifier: TBD
//
// Win32 implementation of IBlockDevice for image files and \\.\PhysicalDriveN.
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <chrono>

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

class Win32FileDevice final : public IBlockDevice {
public:
    Win32FileDevice(Handle h, DeviceInfo info, OpenMode mode, std::uint32_t alignment)
        : h_(std::move(h)), info_(std::move(info)), mode_(mode), alignment_(alignment) {}

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
    // volume (destructive pipeline, v0.4+). Images can be opened exclusively.
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    if (opts.exclusive && is_image) share = 0;
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

    const std::uint32_t alignment = opts.direct_io ? info.logical_sector_size : 1;
    return BlockDevicePtr(new Win32FileDevice(std::move(h), std::move(info), opts.mode, alignment));
}

}  // namespace platter::detail
