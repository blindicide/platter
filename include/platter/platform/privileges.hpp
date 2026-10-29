// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <string>

namespace platter {

// True if the process runs as root (POSIX) or with an elevated admin token (Windows).
bool running_elevated();

// "root", "Administrator", or "user".
std::string privilege_label();

}  // namespace platter
