// SPDX-License-Identifier: TBD
#include "platter/core/error.hpp"

namespace platter {

std::string_view errc_name(Errc e) noexcept {
    switch (e) {
        case Errc::ok: return "ok";
        case Errc::invalid_argument: return "invalid_argument";
        case Errc::out_of_range: return "out_of_range";
        case Errc::misaligned: return "misaligned";
        case Errc::overflow: return "overflow";
        case Errc::not_found: return "not_found";
        case Errc::permission_denied: return "permission_denied";
        case Errc::device_busy: return "device_busy";
        case Errc::device_removed: return "device_removed";
        case Errc::io_error: return "io_error";
        case Errc::short_transfer: return "short_transfer";
        case Errc::timeout: return "timeout";
        case Errc::unsupported: return "unsupported";
        case Errc::read_only: return "read_only";
        case Errc::cancelled: return "cancelled";
        case Errc::corrupt_data: return "corrupt_data";
        case Errc::external_tool_failed: return "external_tool_failed";
        case Errc::internal: return "internal";
    }
    return "unknown";
}

std::string Error::describe() const {
    std::string s{errc_name(code)};
    if (!message.empty()) {
        s += ": ";
        s += message;
    }
    if (os_error != 0) {
        s += " (os error ";
        s += std::to_string(os_error);
        s += ")";
    }
    return s;
}

}  // namespace platter
