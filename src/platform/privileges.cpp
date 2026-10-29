// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/platform/privileges.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace platter {

bool running_elevated() {
#ifdef _WIN32
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elev{};
    DWORD len = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof elev, &len);
    CloseHandle(token);
    return ok && elev.TokenIsElevated;
#else
    return ::geteuid() == 0;
#endif
}

std::string privilege_label() {
#ifdef _WIN32
    return running_elevated() ? "Administrator" : "user";
#else
    return running_elevated() ? "root" : "user";
#endif
}

}  // namespace platter
