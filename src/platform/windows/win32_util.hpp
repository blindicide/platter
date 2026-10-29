// SPDX-License-Identifier: LicenseRef-Proprietary
// Internal Win32 helpers shared by the Windows backend.
#pragma once

#ifdef _WIN32

#include <windows.h>

#include <string>

#include "platter/core/error.hpp"

namespace platter::win32 {

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);
std::string narrow(const wchar_t* wide, int len = -1);

Errc win_error_to_errc(DWORD code) noexcept;
std::string win_error_message(DWORD code);
Error last_error(const std::string& what);

// RAII HANDLE.
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE h) : h_(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : h_(o.release()) {}
    Handle& operator=(Handle&& o) noexcept {
        if (this != &o) {
            reset();
            h_ = o.release();
        }
        return *this;
    }
    HANDLE get() const noexcept { return h_; }
    bool valid() const noexcept { return h_ != INVALID_HANDLE_VALUE && h_ != nullptr; }
    HANDLE release() noexcept {
        HANDLE h = h_;
        h_ = INVALID_HANDLE_VALUE;
        return h;
    }
    void reset() noexcept {
        if (valid()) CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

}  // namespace platter::win32

#endif
