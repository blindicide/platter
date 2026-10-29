// SPDX-License-Identifier: TBD
//
// Privilege separation (docs/privilege-separation.md). POSIX only.
//
//   unprivileged GUI / CLI --(socketpair)--> platter-helper (root)
//
// The helper is started by the user (pkexec / sudo / already-root shell) with
// its stdin/stdout connected to the client, so nobody else can talk to it and
// there is no listening daemon. It opens block devices on request and
// enforces, in the privileged process, the same rules the client's safety
// pipeline enforces: whole-disk block devices only, never a read-write open
// without confirmed ranges, never a write outside them, never a read-write
// open of a device that is in use.
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "platter/core/broker.hpp"

namespace platter::helper {

struct ServerPolicy {
    // Tests only: also serve regular files (images) from any path.
    bool allow_regular_files = false;
    // Refuse every read-write open (a read-only helper).
    bool read_only = false;
};

// Serves one client on `fd` (a connected stream socket) until it quits or
// disconnects. Returns a process exit code.
int serve(int fd, const ServerPolicy& policy);

struct Connection;  // shared state of one client <-> helper link

class HelperBroker final : public IDeviceBroker {
public:
    // Starts `argv` (e.g. {"pkexec", "/usr/bin/platter-helper", "--stdio"}) with
    // stdin/stdout on a socketpair and performs the handshake.
    static Result<std::shared_ptr<HelperBroker>> launch(const std::vector<std::string>& argv);
    // Adopts an already-connected socket (tests). `pid` may be -1.
    static Result<std::shared_ptr<HelperBroker>> adopt(int fd, int pid = -1);

    ~HelperBroker() override;

    Result<BlockDevicePtr> open(const std::string& path, const OpenOptions& opts) override;
    std::string describe() const override;

    bool alive() const;
    std::uint32_t helper_euid() const;
    std::uint32_t helper_pid() const;

private:
    explicit HelperBroker(std::shared_ptr<Connection> c) : conn_(std::move(c)) {}
    std::shared_ptr<Connection> conn_;
};

// A broker that starts the helper on the first open_device() call (so a
// command that never touches a device never asks for a password). If the
// launch fails, every open reports that error.
std::shared_ptr<IDeviceBroker> make_lazy_broker(std::vector<std::string> argv);

// Interprets a user-facing helper setting and installs the broker:
//   ""/"none"  nothing (devices are opened directly)
//   "auto"     pkexec/sudo + the platter-helper found next to this program;
//              nothing at all when already running as root
//   anything else: a command, e.g. "sudo /opt/platter/platter-helper"
// Sets `note` to a one-line explanation of what happened.
Status configure_helper(const std::string& spec, const std::string& argv0_dir, std::string& note);

// Directory of the running executable ("" if unknown).
std::string executable_dir();

// Where to look for the helper binary: next to the running executable, then
// the usual install locations. Empty if not found.
std::string find_helper_binary(const std::string& argv0_dir);

// The command line that starts the helper with the right elevation for this
// session: the helper directly when already root, else pkexec, else sudo.
// Empty if no helper binary can be found.
std::vector<std::string> auto_helper_command(const std::string& argv0_dir);

// Splits a user-supplied command ("sudo /opt/platter/platter-helper") on
// whitespace and appends --stdio.
std::vector<std::string> split_helper_command(const std::string& command);

}  // namespace platter::helper
