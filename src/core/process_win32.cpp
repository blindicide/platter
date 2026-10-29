// SPDX-License-Identifier: LicenseRef-Proprietary
#include <windows.h>

#include <thread>

#include "platter/core/process.hpp"
#include "../platform/windows/win32_util.hpp"

namespace platter {
namespace {

// Quotes one argument per the MSVC runtime's CommandLineToArgvW rules.
std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    for (auto it = a.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != a.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == a.end()) {
            q.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            q.append(backslashes * 2 + 1, L'\\');
            q.push_back(L'"');
        } else {
            q.append(backslashes, L'\\');
            q.push_back(*it);
        }
    }
    q.push_back(L'"');
    return q;
}

void drain(HANDLE h, std::string& sink, std::size_t max) {
    char buf[8192];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n > 0)
        if (sink.size() < max) sink.append(buf, std::min<std::size_t>(n, max - sink.size()));
}

}  // namespace

std::string find_in_path(const std::string& name) {
    wchar_t out[MAX_PATH];
    const DWORD n = SearchPathW(nullptr, win32::widen(name).c_str(), L".exe", MAX_PATH, out, nullptr);
    if (n == 0 || n >= MAX_PATH) return {};
    return win32::narrow(std::wstring(out, n));
}

Result<ProcessResult> run_process(const std::vector<std::string>& argv, std::chrono::milliseconds timeout,
                                  std::size_t max_output) {
    if (argv.empty()) return Error{Errc::invalid_argument, "empty argv"};
    std::string exe = argv[0];
    if (exe.find_first_of("\\/") == std::string::npos) {
        exe = find_in_path(argv[0]);
        if (exe.empty()) return Error{Errc::not_found, "cannot find " + argv[0] + " in PATH"};
    }

    std::wstring cmdline;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i) cmdline += L' ';
        cmdline += quote_arg(win32::widen(i == 0 ? exe : argv[i]));
    }

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0))
        return win32::last_error("CreatePipe");
    win32::Handle hout_r(out_r), hout_w(out_w), herr_r(err_r), herr_w(err_w);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError = err_w;
    si.hStdInput = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(win32::widen(exe).c_str(), cmdline.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
        return win32::last_error("CreateProcess " + exe);
    win32::Handle proc(pi.hProcess), thread(pi.hThread);
    hout_w.reset();
    herr_w.reset();

    ProcessResult res;
    std::thread t_out([&] { drain(out_r, res.out, max_output); });
    std::thread t_err([&] { drain(err_r, res.err, max_output); });
    const DWORD wait = WaitForSingleObject(proc.get(), static_cast<DWORD>(timeout.count()));
    if (wait == WAIT_TIMEOUT) TerminateProcess(proc.get(), 1);
    t_out.join();
    t_err.join();
    if (wait == WAIT_TIMEOUT)
        return Error{Errc::timeout, argv[0] + " did not finish within " + std::to_string(timeout.count()) + " ms"};
    DWORD code = 0;
    GetExitCodeProcess(proc.get(), &code);
    res.exit_code = static_cast<int>(code);
    return res;
}

}  // namespace platter
