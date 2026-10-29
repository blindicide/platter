// SPDX-License-Identifier: LicenseRef-Proprietary
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "platter/core/process.hpp"

extern char** environ;

namespace platter {

std::string find_in_path(const std::string& name) {
    if (name.find('/') != std::string::npos) return ::access(name.c_str(), X_OK) == 0 ? name : std::string{};
    const char* path = std::getenv("PATH");
    if (!path) return {};
    std::istringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) continue;
        auto candidate = dir + "/" + name;
        struct stat st {};
        if (::stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(candidate.c_str(), X_OK) == 0)
            return candidate;
    }
    return {};
}

Result<ProcessResult> run_process(const std::vector<std::string>& argv, std::chrono::milliseconds timeout,
                                  std::size_t max_output) {
    if (argv.empty()) return Error{Errc::invalid_argument, "empty argv"};
    int out_pipe[2], err_pipe[2];
    if (::pipe2(out_pipe, O_CLOEXEC) != 0) return Error{Errc::internal, "pipe failed", errno};
    if (::pipe2(err_pipe, O_CLOEXEC) != 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return Error{Errc::internal, "pipe failed", errno};
    }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, err_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    pid_t pid = 0;
    const int rc = ::posix_spawnp(&pid, argv[0].c_str(), &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    if (rc != 0) {
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        return Error{rc == ENOENT ? Errc::not_found : Errc::external_tool_failed,
                     "cannot start " + argv[0] + ": " + std::strerror(rc), rc};
    }

    ProcessResult res;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    pollfd fds[2] = {{out_pipe[0], POLLIN, 0}, {err_pipe[0], POLLIN, 0}};
    std::string* sinks[2] = {&res.out, &res.err};
    int open_fds = 2;
    bool timed_out = false;
    char buf[8192];
    while (open_fds > 0) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            timed_out = true;
            break;
        }
        const int n = ::poll(fds, 2, static_cast<int>(left.count()));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const ssize_t r = ::read(fds[i].fd, buf, sizeof buf);
            if (r > 0) {
                if (sinks[i]->size() < max_output)
                    sinks[i]->append(buf, std::min<std::size_t>(static_cast<std::size_t>(r), max_output - sinks[i]->size()));
            } else if (r == 0 || errno != EINTR) {
                ::close(fds[i].fd);
                fds[i].fd = -1;
                --open_fds;
            }
        }
    }
    for (auto& f : fds)
        if (f.fd >= 0) ::close(f.fd);

    if (timed_out) ::kill(pid, SIGKILL);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (timed_out)
        return Error{Errc::timeout, argv[0] + " did not finish within " + std::to_string(timeout.count()) + " ms"};
    if (WIFEXITED(status)) res.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        return Error{Errc::external_tool_failed, argv[0] + " killed by signal " + std::to_string(WTERMSIG(status))};
    return res;
}

}  // namespace platter
