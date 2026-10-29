// SPDX-License-Identifier: TBD
#pragma once

#include <chrono>
#include <string>

namespace platter {

// "2026-10-29T09:11:00Z"
std::string utc_timestamp(std::chrono::system_clock::time_point tp);
inline std::string utc_now() { return utc_timestamp(std::chrono::system_clock::now()); }

}  // namespace platter
