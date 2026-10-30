// SPDX-License-Identifier: LicenseRef-Proprietary
// The portable package keeps Qt and the application binaries together in app/.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {
#ifdef PLATTER_LAUNCH_CLI
BOOL WINAPI console_signal(DWORD signal) {
    // The child shares this console and handles cancellation itself.
    return signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT;
}
#endif

int fail(const wchar_t* message, DWORD error) {
    const std::wstring text = std::wstring(message) + L"\nWindows error: " + std::to_wstring(error);
#ifdef PLATTER_LAUNCH_CLI
    std::fwprintf(stderr, L"platter: %ls\n", text.c_str());
#else
    MessageBoxW(nullptr, text.c_str(), L"platter", MB_OK | MB_ICONERROR);
#endif
    return 1;
}

int launch() {
#ifdef PLATTER_LAUNCH_CLI
    SetConsoleCtrlHandler(console_signal, TRUE);
#endif
    std::vector<wchar_t> module(32768);
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size()) return fail(L"Cannot locate the launcher.", GetLastError());
    std::wstring executable(module.data(), length);
    executable.resize(executable.find_last_of(L"\\/") + 1);
#ifdef PLATTER_LAUNCH_CLI
    executable += L"app\\platter-cli.exe";
#else
    executable += L"app\\platter-gui.exe";
#endif
    // Preserve the original argument string, including Unicode and quoting.
    // Windows parses the executable token separately from the remaining arguments.
    const wchar_t* arguments = GetCommandLineW();
    if (*arguments == L'"') {
        ++arguments;
        while (*arguments && *arguments != L'"') ++arguments;
        if (*arguments) ++arguments;
    } else {
        while (*arguments && *arguments != L' ' && *arguments != L'\t') ++arguments;
    }
    std::wstring command = L"\"" + executable + L"\"" + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // Inherit the caller's working directory so relative image paths still work.
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &startup, &process))
        return fail(L"Cannot start platter. Extract the complete package, including the app folder.", GetLastError());
    CloseHandle(process.hThread);
    const DWORD waited = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    if (waited != WAIT_OBJECT_0 || !GetExitCodeProcess(process.hProcess, &code)) {
        const auto error = GetLastError();
        CloseHandle(process.hProcess);
        return fail(L"Cannot retrieve platter's exit status.", error);
    }
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
} // namespace

#ifdef PLATTER_LAUNCH_CLI
int main() { return launch(); }
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return launch(); }
#endif
