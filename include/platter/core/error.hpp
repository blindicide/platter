// SPDX-License-Identifier: TBD
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace platter {

// Error categories shared by every backend. Keep this list stable: the CLI
// maps these to documented exit codes (see docs/cli-reference.md).
enum class Errc : std::uint16_t {
    ok = 0,
    invalid_argument,
    out_of_range,
    misaligned,
    overflow,
    not_found,
    permission_denied,
    device_busy,
    device_removed,
    io_error,
    short_transfer,
    timeout,
    unsupported,
    read_only,
    cancelled,
    corrupt_data,
    external_tool_failed,
    internal,
    // A safety check (docs/safety.md) refused a destructive request: wrong
    // confirmation, device in use, identity changed, ...
    refused,
};

std::string_view errc_name(Errc e) noexcept;

struct Error {
    Errc code = Errc::internal;
    std::string message;
    // Raw OS error (errno on POSIX, GetLastError() on Windows); 0 if none.
    std::int64_t os_error = 0;

    Error() = default;
    Error(Errc c, std::string msg, std::int64_t os = 0)
        : code(c), message(std::move(msg)), os_error(os) {}

    std::string describe() const;
};

// Minimal expected-like result type. C++20 has no std::expected and we do not
// want to require C++23 on MSVC yet.
template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : v_(std::move(value)) {}
    Result(Error err) : v_(std::move(err)) {}

    bool ok() const noexcept { return std::holds_alternative<T>(v_); }
    explicit operator bool() const noexcept { return ok(); }

    T& value() & { return std::get<T>(v_); }
    const T& value() const& { return std::get<T>(v_); }
    T&& value() && { return std::get<T>(std::move(v_)); }

    const Error& error() const& { return std::get<Error>(v_); }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

private:
    std::variant<T, Error> v_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error err) : err_(std::move(err)), ok_(false) {}

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }
    const Error& error() const& { return err_; }

private:
    Error err_{};
    bool ok_ = true;
};

using Status = Result<void>;

}  // namespace platter
