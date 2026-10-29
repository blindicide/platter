// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Unprivileged side of the privilege separation.
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>
#include <thread>

#include "platter/helper/helper.hpp"
#include "platter/platform/privileges.hpp"
#include "wire.hpp"

namespace platter::helper {

using wire::Op;
using wire::Reader;
using wire::Writer;

struct Connection {
    int fd = -1;
    int pid = -1;
    std::uint32_t helper_euid = 0;
    std::uint32_t helper_pid = 0;
    std::mutex mutex;
    bool dead = false;

    ~Connection() {
        if (fd >= 0) {
            {
                // Best effort: ask the helper to leave, then hang up.
                std::lock_guard lock(mutex);
                if (!dead) {
                    Writer w;
                    w.u8(static_cast<std::uint8_t>(Op::quit));
                    static_cast<void>(wire::write_frame(fd, w.data()));
                }
            }
            ::close(fd);
        }
        if (pid > 0) {
            for (int i = 0; i < 40; ++i) {
                int st = 0;
                if (::waitpid(pid, &st, WNOHANG) != 0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    // One request, one reply. Returns the reply body (after the status byte).
    Result<std::vector<std::uint8_t>> call(const Writer& req) {
        std::lock_guard lock(mutex);
        if (dead) return Error{Errc::io_error, "the privileged helper is no longer running"};
        if (auto s = wire::write_frame(fd, req.data_view()); !s) {
            dead = true;
            return s.error();
        }
        auto frame = wire::read_frame(fd);
        if (!frame) {
            dead = true;
            return Error{Errc::io_error, "the privileged helper stopped responding: " + frame.error().describe()};
        }
        if (frame->empty()) {
            dead = true;
            return Error{Errc::corrupt_data, "empty reply from the privileged helper"};
        }
        Reader r(frame->data(), frame->size());
        const auto status = r.u8();
        if (status != 0) {
            auto e = wire::get_error(r);
            if (!r.ok()) return Error{Errc::corrupt_data, "malformed error reply from the privileged helper"};
            return e;
        }
        frame->erase(frame->begin());
        return std::move(frame).value();
    }
};

namespace {

class RemoteDevice final : public IBlockDevice {
public:
    RemoteDevice(std::shared_ptr<Connection> c, std::uint32_t handle, DeviceInfo info, OpenMode mode, std::uint32_t alignment)
        : conn_(std::move(c)), handle_(handle), info_(std::move(info)), mode_(mode), alignment_(alignment) {}

    ~RemoteDevice() override {
        Writer w;
        w.u8(static_cast<std::uint8_t>(Op::close));
        w.u32(handle_);
        static_cast<void>(conn_->call(w));
    }

    const DeviceInfo& info() const noexcept override { return info_; }
    OpenMode mode() const noexcept override { return mode_; }
    std::uint32_t required_alignment() const noexcept override { return alignment_; }

    Result<IOResult> read(std::uint64_t offset, void* buffer, std::size_t length) override {
        IOResult total;
        auto* out = static_cast<std::uint8_t*>(buffer);
        std::size_t done = 0;
        do {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(length - done, wire::kMaxIo));
            Writer w;
            w.u8(static_cast<std::uint8_t>(Op::read));
            w.u32(handle_);
            w.u64(offset + done);
            w.u32(n);
            auto body = conn_->call(w);
            if (!body) return body.error();
            Reader r(body->data(), body->size());
            const auto got = r.u32();
            const auto ns = r.u64();
            const auto* data = r.bytes(got);
            if (!r.ok() || got > n) return Error{Errc::corrupt_data, "malformed read reply from the privileged helper"};
            if (got) std::memcpy(out + done, data, got);
            total.bytes += got;
            total.duration_ns += ns;
            done += got;
            if (got < n) return Error{Errc::short_transfer, "short read through the privileged helper"};
        } while (done < length);
        return total;
    }

    Result<IOResult> write(std::uint64_t offset, const void* buffer, std::size_t length) override {
        if (mode_ != OpenMode::read_write) return Error{Errc::read_only, "device was opened read-only"};
        IOResult total;
        const auto* in = static_cast<const std::uint8_t*>(buffer);
        std::size_t done = 0;
        do {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(length - done, wire::kMaxIo));
            Writer w;
            w.u8(static_cast<std::uint8_t>(Op::write));
            w.u32(handle_);
            w.u64(offset + done);
            w.u32(n);
            w.bytes(in + done, n);
            auto body = conn_->call(w);
            if (!body) return body.error();
            Reader r(body->data(), body->size());
            const auto put = r.u32();
            const auto ns = r.u64();
            if (!r.ok() || put != n) return Error{Errc::short_transfer, "short write through the privileged helper"};
            total.bytes += put;
            total.duration_ns += ns;
            done += n;
        } while (done < length);
        return total;
    }

    Status flush() override { return simple(Op::flush); }
    Status reread_partitions() override { return simple(Op::reread); }

    Status discard(std::uint64_t offset, std::uint64_t length) override {
        Writer w;
        w.u8(static_cast<std::uint8_t>(Op::discard));
        w.u32(handle_);
        w.u64(offset);
        w.u64(length);
        auto body = conn_->call(w);
        if (!body) return body.error();
        return {};
    }

private:
    Status simple(Op op) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(op));
        w.u32(handle_);
        auto body = conn_->call(w);
        if (!body) return body.error();
        return {};
    }

    std::shared_ptr<Connection> conn_;
    std::uint32_t handle_;
    DeviceInfo info_;
    OpenMode mode_;
    std::uint32_t alignment_;
};

}  // namespace

Result<std::shared_ptr<HelperBroker>> HelperBroker::adopt(int fd, int pid) {
    auto c = std::make_shared<Connection>();
    c->fd = fd;
    c->pid = pid;
    Writer w;
    w.u8(static_cast<std::uint8_t>(Op::hello));
    w.u32(wire::kProtocol);
    auto body = c->call(w);
    if (!body) return body.error();
    Reader r(body->data(), body->size());
    const auto proto = r.u32();
    c->helper_euid = r.u32();
    c->helper_pid = r.u32();
    if (!r.ok() || proto != wire::kProtocol) return Error{Errc::unsupported, "the helper speaks a different protocol version"};
    return std::shared_ptr<HelperBroker>(new HelperBroker(std::move(c)));
}

Result<std::shared_ptr<HelperBroker>> HelperBroker::launch(const std::vector<std::string>& argv) {
    if (argv.empty()) return Error{Errc::invalid_argument, "no helper command"};
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0)
        return Error{Errc::io_error, std::string("socketpair: ") + std::strerror(errno), errno};
    int errpipe[2];
    if (::pipe2(errpipe, O_CLOEXEC) != 0) {
        ::close(sv[0]);
        ::close(sv[1]);
        return Error{Errc::io_error, std::string("pipe: ") + std::strerror(errno), errno};
    }
    std::vector<char*> cargv;
    std::vector<std::string> copy = argv;
    for (auto& a : copy) cargv.push_back(a.data());
    cargv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int e = errno;
        ::close(sv[0]); ::close(sv[1]); ::close(errpipe[0]); ::close(errpipe[1]);
        return Error{Errc::io_error, std::string("fork: ") + std::strerror(e), e};
    }
    if (pid == 0) {
        // Child: async-signal-safe calls only.
        ::dup2(sv[1], 0);
        ::dup2(sv[1], 1);
        ::execvp(cargv[0], cargv.data());
        const int e = errno;
        const ssize_t ignored = ::write(errpipe[1], &e, sizeof e);
        static_cast<void>(ignored);
        ::_exit(127);
    }
    ::close(sv[1]);
    ::close(errpipe[1]);
    int child_errno = 0;
    ssize_t n;
    do { n = ::read(errpipe[0], &child_errno, sizeof child_errno); } while (n < 0 && errno == EINTR);
    ::close(errpipe[0]);
    if (n == static_cast<ssize_t>(sizeof child_errno)) {
        int st = 0;
        ::waitpid(pid, &st, 0);
        ::close(sv[0]);
        return Error{child_errno == ENOENT ? Errc::not_found : Errc::io_error,
                     "cannot start '" + argv[0] + "': " + std::strerror(child_errno), child_errno};
    }
    auto b = adopt(sv[0], pid);
    if (!b) {
        // adopt() already hung up and reaped the child.
        return Error{b.error().code, "the privileged helper did not start (authentication cancelled or failed?): " + b.error().describe()};
    }
    return b;
}

HelperBroker::~HelperBroker() = default;

bool HelperBroker::alive() const {
    std::lock_guard lock(conn_->mutex);
    return !conn_->dead;
}

std::uint32_t HelperBroker::helper_euid() const { return conn_->helper_euid; }
std::uint32_t HelperBroker::helper_pid() const { return conn_->helper_pid; }

std::string HelperBroker::describe() const {
    return "platter-helper pid " + std::to_string(conn_->helper_pid) + (conn_->helper_euid == 0 ? " (root)" : " (uid " + std::to_string(conn_->helper_euid) + ")");
}

Result<BlockDevicePtr> HelperBroker::open(const std::string& path, const OpenOptions& opts) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(Op::open));
    w.str(path);
    w.u8(opts.mode == OpenMode::read_write ? 1 : 0);
    w.u8(opts.direct_io ? 1 : 0);
    w.u8(opts.exclusive ? 1 : 0);
    w.u32(static_cast<std::uint32_t>(opts.write_ranges.size()));
    for (const auto& s : opts.write_ranges) {
        w.u64(s.offset);
        w.u64(s.length);
    }
    auto body = conn_->call(w);
    if (!body) return body.error();
    Reader r(body->data(), body->size());
    const auto handle = r.u32();
    const auto rw = r.u8();
    const auto align = r.u32();
    auto info = wire::get_info(r);
    if (!r.ok()) return Error{Errc::corrupt_data, "malformed open reply from the privileged helper"};
    return BlockDevicePtr(new RemoteDevice(conn_, handle, std::move(info), rw ? OpenMode::read_write : OpenMode::read_only, align));
}

std::string find_helper_binary(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    if (!dir.empty()) {
        candidates.push_back(fs::path(dir) / "platter-helper");
        candidates.push_back(fs::path(dir) / ".." / "libexec" / "platter" / "platter-helper");
    }
    for (const char* p : {"/usr/local/libexec/platter/platter-helper", "/usr/libexec/platter/platter-helper",
                          "/usr/lib/platter/platter-helper", "/usr/local/bin/platter-helper", "/usr/bin/platter-helper"})
        candidates.emplace_back(p);
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec) && ::access(c.c_str(), X_OK) == 0) return fs::weakly_canonical(c, ec).string();
    }
    return {};
}

std::vector<std::string> split_helper_command(const std::string& command) {
    std::istringstream in(command);
    std::vector<std::string> v;
    for (std::string t; in >> t;) v.push_back(t);
    if (!v.empty()) v.emplace_back("--stdio");
    return v;
}

std::vector<std::string> auto_helper_command(const std::string& argv0_dir) {
    const auto bin = find_helper_binary(argv0_dir);
    if (bin.empty()) return {};
    if (running_elevated()) return {bin, "--stdio"};
    namespace fs = std::filesystem;
    for (const char* dir : {"/usr/bin", "/bin", "/usr/local/bin"}) {
        if (fs::exists(fs::path(dir) / "pkexec")) return {(fs::path(dir) / "pkexec").string(), bin, "--stdio"};
    }
    for (const char* dir : {"/usr/bin", "/bin", "/usr/local/bin"}) {
        if (fs::exists(fs::path(dir) / "sudo")) return {(fs::path(dir) / "sudo").string(), bin, "--stdio"};
    }
    return {};
}

namespace {
class LazyBroker final : public IDeviceBroker {
public:
    explicit LazyBroker(std::vector<std::string> argv) : argv_(std::move(argv)) {}
    Result<BlockDevicePtr> open(const std::string& path, const OpenOptions& opts) override {
        std::shared_ptr<HelperBroker> b;
        {
            std::lock_guard lock(mutex_);
            if (!broker_ && !error_) {
                auto r = HelperBroker::launch(argv_);
                if (r) broker_ = *r;
                else error_ = r.error();
            }
            if (error_) return *error_;
            b = broker_;
        }
        return b->open(path, opts);
    }
    std::string describe() const override {
        std::lock_guard lock(mutex_);
        if (broker_) return broker_->describe();
        if (error_) return "platter-helper failed to start";
        return "platter-helper (starts on first device access)";
    }

private:
    std::vector<std::string> argv_;
    mutable std::mutex mutex_;
    std::shared_ptr<HelperBroker> broker_;
    std::optional<Error> error_;
};
}  // namespace

std::shared_ptr<IDeviceBroker> make_lazy_broker(std::vector<std::string> argv) {
    return std::make_shared<LazyBroker>(std::move(argv));
}

std::string executable_dir() {
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : p.parent_path().string();
}

Status configure_helper(const std::string& spec, const std::string& argv0_dir, std::string& note) {
    if (spec.empty() || spec == "none") {
        note.clear();
        return {};
    }
    if (spec == "auto") {
        if (running_elevated()) {
            note = "already running as root; opening devices directly";
            return {};
        }
        auto argv = auto_helper_command(argv0_dir);
        if (argv.empty())
            return Error{Errc::not_found, "platter-helper was not found (looked next to the program and in the usual libexec directories), or neither pkexec nor sudo is installed"};
        set_device_broker(make_lazy_broker(std::move(argv)));
        note = "devices will be opened through the privileged helper";
        return {};
    }
    auto argv = split_helper_command(spec);
    if (argv.empty()) return Error{Errc::invalid_argument, "empty helper command"};
    set_device_broker(make_lazy_broker(std::move(argv)));
    note = "devices will be opened through: " + spec;
    return {};
}

}  // namespace platter::helper
