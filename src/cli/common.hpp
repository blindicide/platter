// SPDX-License-Identifier: TBD
//
// Helpers shared by every platter-cli command file.
#pragma once

#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "platter/core/block_device.hpp"
#include "platter/platform/enumerate.hpp"
#include "platter/core/operation.hpp"
#include "platter/core/safety.hpp"
#include "platter/storage/history.hpp"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <functional>
#include <thread>

namespace platter::cli {

namespace fs = std::filesystem;
using nlohmann::json;
using namespace platter;

// ------------------------------------------------------------- exit codes
// Keep in sync with docs/cli-reference.md.
enum Exit : int {
    kOk = 0,
    kFailure = 1,
    kUsage = 2,
    kNotFound = 3,
    kPermission = 4,
    kBusy = 5,
    kIoError = 6,
    kUnsupported = 7,
    kToolMissing = 8,
    kScanErrors = 9,
    kHealthWarning = 10,
    kHealthCritical = 11,
    kHealthUnknown = 12,
    kCancelled = 13,
    kRefused = 14,          // a safety check refused a destructive request
    kCompletedWithErrors = 15,  // destructive operation finished but some sectors failed
    kVerifyFailed = 16,         // a checksum or read-back comparison did not match
};

inline int exit_for(const Error& e) {
    switch (e.code) {
        case Errc::not_found: return kNotFound;
        case Errc::permission_denied: return kPermission;
        case Errc::device_busy: return kBusy;
        case Errc::io_error:
        case Errc::short_transfer:
        case Errc::timeout:
        case Errc::device_removed: return kIoError;
        case Errc::unsupported: return kUnsupported;
        case Errc::refused: return kRefused;
        case Errc::external_tool_failed: return kToolMissing;
        case Errc::invalid_argument:
        case Errc::out_of_range:
        case Errc::misaligned: return kUsage;
        default: return kFailure;
    }
}

// ------------------------------------------------------------- options

struct Options {
    bool json = false;
    bool verbose = false;
    bool quiet = false;
    bool show_serial = false;
    std::string output;
    std::uint32_t sector_size = 512;
    std::string smart_type;  // -d for smartctl
    std::map<std::string, std::string> values;  // command-specific --key VALUE options
    std::set<std::string> flags;                // command-specific --flag options
    std::vector<std::string> positional;
};

// ------------------------------------------------------------- output

class Out {
public:
    explicit Out(const Options& o) : opts_(o) {}

    std::ostream& text() { return buf_; }
    void set_json(json j) { json_ = std::move(j); }
    // Pre-serialized JSON written verbatim (canonical files such as scan exports).
    void set_raw_json(std::string s) { raw_json_ = std::move(s); }

    int flush(int code) {
        std::string payload = opts_.json ? (raw_json_.empty() ? json_.dump(2) : raw_json_) + "\n" : buf_.str();
        if (opts_.quiet && !opts_.json && opts_.output.empty()) return code;
        if (!opts_.output.empty()) {
            std::ofstream f(opts_.output, std::ios::binary | std::ios::trunc);
            if (!f) {
                std::cerr << "platter-cli: cannot write " << opts_.output << "\n";
                return kFailure;
            }
            f << payload;
        } else {
            std::cout << payload;
        }
        return code;
    }

private:
    const Options& opts_;
    std::ostringstream buf_;
    json json_;
    std::string raw_json_;
};

inline int fail(const Error& e, const std::string& context = {}) {
    std::cerr << "platter-cli: " << (context.empty() ? "" : context + ": ") << e.describe() << "\n";
    if (e.code == Errc::permission_denied) {
#ifdef _WIN32
        std::cerr << "hint: raw device access needs an elevated (Administrator) prompt\n";
#else
        std::cerr << "hint: raw device access usually needs root (sudo) or membership in the 'disk' group\n";
#endif
    }
    return exit_for(e);
}

// ------------------------------------------------------------- targets

inline std::string expand_device(const std::string& arg) {
#ifdef _WIN32
    auto digits = [](const std::string& s) {
        return !s.empty() && s.size() <= 3 && s.find_first_not_of("0123456789") == std::string::npos;
    };
    if (digits(arg)) return "\\\\.\\PhysicalDrive" + arg;
    if (arg.size() > 2 && (arg.rfind("pd", 0) == 0 || arg.rfind("PD", 0) == 0) && digits(arg.substr(2)))
        return "\\\\.\\PhysicalDrive" + arg.substr(2);
    return arg;
#else
    if (arg.find('/') == std::string::npos) return "/dev/" + arg;
    return arg;
#endif
}

inline bool is_image_path(const std::string& arg) {
    std::error_code ec;
    return fs::is_regular_file(arg, ec);
}

inline Result<DeviceInfo> describe_target(const std::string& arg, const Options& o) {
    if (is_image_path(arg)) {
        auto dev = open_image(arg, {}, o.sector_size);
        if (!dev) return dev.error();
        return (*dev)->info();
    }
    return describe_device(expand_device(arg));
}

inline Result<BlockDevicePtr> open_target_read_only(const std::string& arg, const Options& o) {
    OpenOptions oo;
    oo.mode = OpenMode::read_only;  // v0.1: never anything else
    if (is_image_path(arg)) return open_image(arg, oo, o.sector_size);
    return open_device(expand_device(arg), oo);
}

inline std::string yes_no(Tri t) { return std::string(tri_name(t)); }

inline std::string short_capacity(std::uint64_t bytes) {
    auto s = format_capacity(bytes);
    auto p = s.find(" (");
    return p == std::string::npos ? s : s.substr(0, p);
}
// ------------------------------------------------------------- history

inline std::string history_path(const Options& o) {
    auto it = o.values.find("history-db");
    return it != o.values.end() ? it->second : storage::HistoryStore::default_path();
}

// Opens the history DB and runs `fn`; reports problems as warnings so a
// failed save never hides the measurement that was just made.
template <typename Fn>
void save_to_history(const Options& o, Fn&& fn) {
    const auto path = history_path(o);
    auto store = storage::HistoryStore::open(path);
    if (!store) {
        std::cerr << "warning: not saved to history (" << store.error().describe() << ")\n";
        return;
    }
    auto id = fn(**store);
    if (!id) std::cerr << "warning: not saved to history (" << id.error().describe() << ")\n";
    else if (!o.quiet) std::cerr << "Saved to history as #" << *id << " (" << path << ")\n";
}
// ------------------------------------------------------------- surface scan

inline std::atomic<bool> g_interrupted{false};
extern "C" inline void on_sigint(int) { g_interrupted.store(true); }

inline std::optional<std::uint64_t> parse_u64(const std::string& s) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos || s.size() > 20) return std::nullopt;
    try {
        return std::stoull(s);
    } catch (...) {
        return std::nullopt;
    }
}

// "4096", "64K", "1M", "16MiB", "4G" -> bytes (binary multiples)
inline std::optional<std::uint64_t> parse_size(std::string s) {
    std::uint64_t mult = 1;
    for (auto* suf : {"KiB", "MiB", "GiB", "K", "M", "G", "k", "m", "g"}) {
        const std::string sf = suf;
        if (s.size() > sf.size() && s.compare(s.size() - sf.size(), sf.size(), sf) == 0) {
            const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(sf[0])));
            mult = u == 'K' ? 1024ull : u == 'M' ? 1024ull * 1024 : 1024ull * 1024 * 1024;
            s.resize(s.size() - sf.size());
            break;
        }
    }
    auto v = parse_u64(s);
    if (!v || *v > (UINT64_MAX / mult)) return std::nullopt;
    return *v * mult;
}

inline std::string hms(std::chrono::seconds s) {
    char b[32];
    const auto t = s.count();
    std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld", static_cast<long long>(t / 3600),
                  static_cast<long long>(t / 60 % 60), static_cast<long long>(t % 60));
    return b;
}

// ----------------------------------------------------- shared command helpers

inline const std::string* opt(const Options& o, const char* key) {
    auto it = o.values.find(key);
    return it == o.values.end() ? nullptr : &it->second;
}

inline bool stdin_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

inline bool stderr_is_tty() {
#ifdef _WIN32
    return true;
#else
    return isatty(STDERR_FILENO) != 0;
#endif
}

inline bool write_text_file(const std::string& file, const std::string& content) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f || !(f << content)) {
        std::cerr << "platter-cli: cannot write " << file << "\n";
        return false;
    }
    return true;
}

// Runs `op` (already validated) to completion on its worker thread, printing a
// progress line made by `line(progress)` and turning Ctrl+C into cancel().
inline void run_with_progress(Operation& op, const Options& o,
                              const std::function<std::string(const OperationProgress&)>& line) {
    g_interrupted = false;
    auto previous = std::signal(SIGINT, on_sigint);
    const bool tty = stderr_is_tty();
    auto last_print = std::chrono::steady_clock::now() - std::chrono::hours(1);
    bool cancel_sent = false;
    while (!is_terminal(op.state())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (g_interrupted && !cancel_sent) {
            op.cancel();
            cancel_sent = true;
            if (!o.quiet) std::cerr << "\nStopping...\n";
        }
        const auto now = std::chrono::steady_clock::now();
        if (o.quiet || now - last_print < (tty ? std::chrono::milliseconds(500) : std::chrono::seconds(10))) continue;
        last_print = now;
        std::cerr << (tty ? "\r" : "") << line(op.progress()) << (tty ? "   " : "\n") << std::flush;
    }
    op.wait();
    std::signal(SIGINT, previous);
    if (!o.quiet && tty) std::cerr << "\n";
}

inline std::string standard_progress_line(const OperationProgress& p) {
    char b[240];
    std::snprintf(b, sizeof b, "[%5.1f%%] %8.2f MB/s  ETA %s  errors %llu  %s", p.percent(),
                  p.current_bytes_per_sec / 1e6, p.eta ? hms(*p.eta).c_str() : "--:--:--",
                  static_cast<unsigned long long>(p.errors), p.message.c_str());
    return b;
}

// What the user is about to destroy, shown before any confirmation.
inline void print_preview(std::ostream& os, const safety::Preview& p, bool show_serial) {
    os << safety::preview_text(p, show_serial);
}

// The typed confirmation: --confirm TEXT, else asked on a terminal, else absent.
inline std::optional<std::string> obtain_confirmation(const Options& o, const safety::Preview& p) {
    if (auto c = opt(o, "confirm")) return *c;
    if (!stdin_is_tty()) return std::nullopt;
    std::cerr << "\n" << p.prompt << "\n> " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return std::nullopt;
    return line;
}

// Parses "--range FIRST:COUNT" (LBAs); "--start/--end" inclusive is also accepted.
inline Result<std::vector<LbaRange>> parse_target_ranges(const Options& o) {
    std::vector<LbaRange> ranges;
    if (auto r = opt(o, "range")) {
        std::istringstream in(*r);
        std::string part;
        while (std::getline(in, part, ',')) {
            const auto colon = part.find(':');
            if (colon == std::string::npos) return Error{Errc::invalid_argument, "--range must look like FIRST:COUNT[,FIRST:COUNT...]"};
            auto first = parse_u64(part.substr(0, colon));
            auto count = parse_u64(part.substr(colon + 1));
            if (!first || !count || *count == 0) return Error{Errc::invalid_argument, "--range must look like FIRST:COUNT with COUNT >= 1"};
            ranges.push_back(LbaRange{*first, *count});
        }
        return ranges;
    }
    const auto* s = opt(o, "start");
    const auto* e = opt(o, "end");
    if (s || e) {
        auto first = s ? parse_u64(*s) : std::optional<std::uint64_t>(0);
        if (!first) return Error{Errc::invalid_argument, "--start must be a number"};
        if (!e) return Error{Errc::invalid_argument, "--start needs --end for a destructive operation"};
        auto last = parse_u64(*e);
        if (!last || *last < *first) return Error{Errc::invalid_argument, "--end must be a number >= --start"};
        ranges.push_back(LbaRange{*first, *last - *first + 1});
    }
    return ranges;
}

// "DEADBEEF" -> bytes (1 to 4096).
inline Result<std::vector<std::uint8_t>> parse_hex_pattern(const std::string& v) {
    if (v.size() % 2 != 0 || v.empty() || v.size() > 8192 || v.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return Error{Errc::invalid_argument, "--pattern must be hexadecimal bytes, e.g. DEADBEEF (1 to 4096 bytes)"};
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < v.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoul(v.substr(i, 2), nullptr, 16)));
    return out;
}

inline safety::Target make_target(const std::string& arg, const Options& o) {
    safety::Target t;
    t.is_image = is_image_path(arg);
    t.path = t.is_image ? arg : expand_device(arg);
    t.image_sector_size = o.sector_size;
    return t;
}

inline int usage_error(const std::string& msg) {
    std::cerr << "platter-cli: " << msg << "\nTry 'platter-cli --help'.\n";
    return kUsage;
}

// The whole destructive-command front half: preview, refuse on blockers,
// --dry-run, typed confirmation, authorize. Returns the authorized target, or
// nullopt with `code` set to the exit code the command must return (including
// kOk for --dry-run).
inline std::optional<safety::AuthorizedTarget> authorize_from_cli(const Options& o, const std::string& target_arg,
                                                                   safety::Kind kind, const std::string& summary,
                                                                   std::vector<LbaRange> ranges, const char* what,
                                                                   int& code) {
    safety::Request req;
    req.kind = kind;
    req.target = make_target(target_arg, o);
    req.ranges = std::move(ranges);
    req.summary = summary;

    auto preview = safety::make_preview(req);
    if (!preview) {
        code = fail(preview.error(), req.target.path);
        return std::nullopt;
    }
    if (!o.quiet) print_preview(std::cerr, *preview, o.show_serial);
    if (!preview->allowed()) {
        std::cerr << "platter-cli: refused: this device cannot be used for " << what << ".\n";
        code = kRefused;
        return std::nullopt;
    }
    if (o.flags.count("dry-run")) {
        std::cerr << "Dry run: nothing was written.\n";
        code = kOk;
        return std::nullopt;
    }
    if (preview->needs_dismount_ack() && !o.flags.count("dismount-volumes")) {
        std::cerr << "platter-cli: refused: volumes are mounted on this disk and will be locked and dismounted "
                     "(open files in them are lost).\nRe-run with --dismount-volumes to agree, or unmount them first.\n";
        code = kRefused;
        return std::nullopt;
    }
    auto typed = obtain_confirmation(o, *preview);
    if (!typed) {
        std::cerr << "platter-cli: this will DESTROY data. " << preview->prompt
                  << "\nRe-run with --confirm TEXT (or on a terminal to be asked), or use --dry-run.\n";
        code = kRefused;
        return std::nullopt;
    }
    safety::AuthorizeOptions ao;
    ao.acknowledge_dismount = o.flags.count("dismount-volumes") > 0;
    auto target = safety::authorize(*preview, *typed, ao);
    if (!target) {
        code = fail(target.error(), "authorization");
        return std::nullopt;
    }
    for (const auto& w : target->warnings()) std::cerr << "warning: " << w << "\n";
    return std::move(target).value();
}

}  // namespace platter::cli
