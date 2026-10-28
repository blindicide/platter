// SPDX-License-Identifier: TBD
//
// platter-cli: command-line front end over the same core as the GUI.
// Every command is read-only. See docs/cli-reference.md.
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <map>
#include <set>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "platter/core/block_device.hpp"
#include "platter/partitions/partition_table.hpp"
#include "platter/platform/enumerate.hpp"
#include "platter/benchmark/benchmark.hpp"
#include "platter/reports/bench_report.hpp"
#include "platter/reports/json.hpp"
#include "platter/reports/scan_report.hpp"
#include "platter/storage/history.hpp"
#include "platter/smart/smart.hpp"
#include "platter/surface/surface_scan.hpp"
#include "platter/version.hpp"
#include "table.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace platter;
using platter::cli::TextTable;

namespace {

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
};

int exit_for(const Error& e) {
    switch (e.code) {
        case Errc::not_found: return kNotFound;
        case Errc::permission_denied: return kPermission;
        case Errc::device_busy: return kBusy;
        case Errc::io_error:
        case Errc::short_transfer:
        case Errc::timeout:
        case Errc::device_removed: return kIoError;
        case Errc::unsupported: return kUnsupported;
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

constexpr const char* kUsageText = R"(usage: platter-cli [options] <command> [arguments]

Commands (all read-only):
  devices list                  List storage devices
  devices info <device>         Show identification and capabilities
  smart show <device>           SMART / NVMe health via smartctl
  partitions list <device>      Show the MBR/GPT partition table
  surface scan <device>         Read-only surface scan (latency map)
  benchmark read <device>       Read benchmark (see --test)
  benchmark history [<device>]  List saved benchmark sessions
  benchmark show <id>           Show a saved benchmark session
  version                       Print version

<device> is a device path (/dev/sda, \\.\PhysicalDrive1), a short name
(sda, nvme0n1, pd1), or a raw image file (*.img).

Options:
  --json                Machine-readable JSON output
  -o, --output FILE     Write output to FILE instead of stdout
  -v, --verbose         More detail
  -q, --quiet           Only errors; rely on the exit code
  --show-serial         Include serial numbers (hidden by default)
  --sector-size N       Logical sector size for image files (512|4096)
  -d, --device-type T   smartctl device type (sat, nvme, usbjmicron, ...)
  -h, --help            This help
  --version             Print version

Surface scan options:
  --start LBA           First LBA (default 0)
  --end LBA             Last LBA, inclusive (default: end of device)
  --block-size SIZE     Bytes per request, e.g. 64K, 1M (default 1M)
  --order O             forward | reverse | random | sampled (default forward)
  --sample PERCENT      Share of blocks to test with --order sampled (default 10)
  --seed N              Seed for random / sampled order
  --retries N           Extra attempts for a failed block (default 0)
  --stop-after N        Stop after N unreadable blocks (default: never)
  --thresholds A,B,C,D  Latency class bounds in ms (default 5,20,50,200)
  --buffered            Use the OS cache instead of direct I/O
  --csv FILE            Also write per-block CSV
  --report FILE         Also write a human-readable text report
  --map                 Print an ASCII latency map at the end
  (with --json / -o, the canonical JSON scan file is written)

Benchmark options:
  --test T              seq | random | access | curve | sizes | qd (default seq)
  --block-size SIZE     Request size (defaults: seq/curve 1M, random/qd 4K,
                        access 1 sector)
  --qd N                Queue depth 1-64 (threads, each with its own handle)
  --duration SEC        Time limit per phase
  --size SIZE           Byte limit per phase (seq default 1G, sizes 256M)
  --points N            Speed-curve sample points (default 100)
  --point-size SIZE     Bytes read at each curve point (default 16M)
  --sizes LIST          Block sizes for --test sizes, e.g. 4K,64K,1M
  --qds LIST            Queue depths for --test qd, e.g. 1,4,16,32
  --start/--end LBA     Test region (default whole device)
  --buffered            Use the OS cache (results may not reflect the device)
  --chart               Append an ASCII chart
  --report FILE         Also write a text report

History (SQLite; off unless asked):
  --save                Record this benchmark / scan / SMART query in history
  --history-db FILE     History database (default: per-user data directory)
  --limit N             Rows for 'benchmark history' (default 30)

Exit codes: 0 ok, 1 failure, 2 usage, 3 not found, 4 permission denied,
5 busy, 6 I/O error, 7 unsupported, 8 smartctl missing/failed,
9 scan / benchmark found unreadable blocks, 10/11/12 health WARNING/CRITICAL/UNKNOWN
(smart show), 13 cancelled (Ctrl+C).
)";

std::optional<Options> parse_args(int argc, char** argv, int& exit_code) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* name) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                std::cerr << "platter-cli: " << name << " needs a value\n";
                return std::nullopt;
            }
            return std::string(argv[++i]);
        };
        if (a == "-h" || a == "--help") {
            std::cout << kUsageText;
            exit_code = kOk;
            return std::nullopt;
        } else if (a == "--version") {
            std::cout << kProjectName << " " << kVersion << "\n";
            exit_code = kOk;
            return std::nullopt;
        } else if (a == "--json") {
            o.json = true;
        } else if (a == "-v" || a == "--verbose") {
            o.verbose = true;
        } else if (a == "-q" || a == "--quiet") {
            o.quiet = true;
        } else if (a == "--show-serial") {
            o.show_serial = true;
        } else if (a == "-o" || a == "--output") {
            auto v = need("--output");
            if (!v) { exit_code = kUsage; return std::nullopt; }
            o.output = *v;
        } else if (a == "-d" || a == "--device-type") {
            auto v = need("--device-type");
            if (!v) { exit_code = kUsage; return std::nullopt; }
            o.smart_type = *v;
        } else if (a == "--sector-size") {
            auto v = need("--sector-size");
            if (!v || (*v != "512" && *v != "4096")) {
                std::cerr << "platter-cli: --sector-size must be 512 or 4096\n";
                exit_code = kUsage;
                return std::nullopt;
            }
            o.sector_size = static_cast<std::uint32_t>(std::stoul(*v));
        } else if (a == "--start" || a == "--end" || a == "--block-size" || a == "--order" || a == "--sample" ||
                   a == "--seed" || a == "--retries" || a == "--stop-after" || a == "--thresholds" ||
                   a == "--csv" || a == "--report" || a == "--test" || a == "--qd" || a == "--duration" ||
                   a == "--size" || a == "--points" || a == "--point-size" || a == "--sizes" || a == "--qds" ||
                   a == "--history-db" || a == "--limit") {
            auto v = need(a.c_str());
            if (!v) { exit_code = kUsage; return std::nullopt; }
            o.values[a.substr(2)] = *v;
        } else if (a == "--buffered" || a == "--map" || a == "--save" || a == "--chart") {
            o.flags.insert(a.substr(2));
        } else if (a.size() > 1 && a[0] == '-') {
            std::cerr << "platter-cli: unknown option '" << a << "'\n";
            exit_code = kUsage;
            return std::nullopt;
        } else {
            o.positional.push_back(a);
        }
    }
    return o;
}

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

int fail(const Error& e, const std::string& context = {}) {
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

std::string expand_device(const std::string& arg) {
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

bool is_image_path(const std::string& arg) {
    std::error_code ec;
    return fs::is_regular_file(arg, ec);
}

Result<DeviceInfo> describe_target(const std::string& arg, const Options& o) {
    if (is_image_path(arg)) {
        auto dev = open_image(arg, {}, o.sector_size);
        if (!dev) return dev.error();
        return (*dev)->info();
    }
    return describe_device(expand_device(arg));
}

Result<BlockDevicePtr> open_target_read_only(const std::string& arg, const Options& o) {
    OpenOptions oo;
    oo.mode = OpenMode::read_only;  // v0.1: never anything else
    if (is_image_path(arg)) return open_image(arg, oo, o.sector_size);
    return open_device(expand_device(arg), oo);
}

std::string yes_no(Tri t) { return std::string(tri_name(t)); }

std::string short_capacity(std::uint64_t bytes) {
    auto s = format_capacity(bytes);
    auto p = s.find(" (");
    return p == std::string::npos ? s : s.substr(0, p);
}

// ------------------------------------------------------------- commands

int cmd_devices_list(const Options& o) {
    Out out(o);
    auto devs = enumerate_devices();
    if (!devs) return fail(devs.error(), "enumerating devices");

    reports::Privacy priv{o.show_serial};
    json arr = json::array();
    TextTable t({"Device", "Model", "Capacity", "Bus", "Media", "Sector", "Usage"},
                {TextTable::Align::left, TextTable::Align::left, TextTable::Align::right, TextTable::Align::left,
                 TextTable::Align::left, TextTable::Align::right, TextTable::Align::left});
    for (const auto& d : *devs) {
        arr.push_back(reports::to_json(d, priv));
        std::string usage;
        if (d.is_system_disk) usage = "SYSTEM ";
        if (d.is_mounted()) {
            usage += "mounted:";
            for (std::size_t i = 0; i < d.mountpoints.size(); ++i) usage += (i ? "," : " ") + d.mountpoints[i];
        } else if (!d.holders.empty()) {
            usage += "held by " + d.holders.front();
        }
        if (usage.empty()) usage = "-";
        const auto model = d.vendor.empty() || d.vendor == "ATA" ? d.model : d.vendor + " " + d.model;
        t.add({d.path, model.empty() ? "-" : model, short_capacity(d.capacity_bytes),
               std::string(bus_type_name(d.bus)), std::string(media_kind_name(d.media)),
               std::to_string(d.logical_sector_size) + "/" + std::to_string(d.physical_sector_size), usage});
    }
    auto env = reports::envelope("devices");
    env["devices"] = arr;
    out.set_json(env);
    if (devs->empty()) out.text() << "No block devices visible to this user.\n";
    else t.print(out.text());
    return out.flush(kOk);
}

int cmd_devices_info(const Options& o, const std::string& target) {
    Out out(o);
    auto info = describe_target(target, o);
    if (!info) return fail(info.error(), target);
    const auto& d = *info;

    auto& s = out.text();
    auto row = [&](const char* k, const std::string& v) {
        char key[24];
        std::snprintf(key, sizeof key, "%-20s", k);
        s << key << (v.empty() ? "-" : v) << "\n";
    };
    row("Device", d.path);
    row("Vendor", d.vendor);
    row("Model", d.model);
    row("Serial", o.show_serial ? d.serial : reports::mask_serial(d.serial));
    row("Firmware", d.firmware);
    row("Stable ID", reports::redact_stable_id(d, {o.show_serial}));
    row("Capacity", format_capacity(d.capacity_bytes));
    row("Total LBAs", std::to_string(d.total_lbas()));
    row("Sector size", std::to_string(d.logical_sector_size) + " logical / " +
                           std::to_string(d.physical_sector_size) + " physical");
    row("Bus", std::string(bus_type_name(d.bus)));
    row("Media", std::string(media_kind_name(d.media)));
    row("Removable", yes_no(d.removable));
    row("Read-only", yes_no(d.read_only));
    row("TRIM", yes_no(d.trim_supported));
    std::string mps;
    for (const auto& m : d.mountpoints) mps += (mps.empty() ? "" : ", ") + m;
    row("Mounted at", mps);
    std::string hs;
    for (const auto& h : d.holders) hs += (hs.empty() ? "" : ", ") + h;
    row("Held by", hs);
    row("System disk", d.is_system_disk ? "YES" : "No");
    std::string caps;
    for (auto c : d.capabilities.names()) caps += (caps.empty() ? "" : " ") + std::string(c);
    row("Capabilities", caps);
    if (o.verbose) row("Topology", d.topology);
    s << "\nNote: SMART-derived capabilities appear after 'platter-cli smart show'.\n";

    auto env = reports::envelope("device");
    env["device"] = reports::to_json(d, {o.show_serial});
    out.set_json(env);
    return out.flush(kOk);
}

int cmd_partitions_list(const Options& o, const std::string& target) {
    Out out(o);
    auto dev = open_target_read_only(target, o);
    if (!dev) return fail(dev.error(), target);
    auto tbl = partitions::read_partition_table(**dev);
    if (!tbl) return fail(tbl.error(), "reading partition table");
    const auto& t = *tbl;

    auto& s = out.text();
    s << "Device:  " << (*dev)->info().path << "\n";
    s << "Table:   " << partitions::table_type_name(t.type);
    if (t.type == partitions::TableType::gpt && t.gpt_primary)
        s << "  (disk GUID " << t.gpt_primary->disk_guid.to_string() << ")";
    if (t.type == partitions::TableType::mbr) {
        char sig[16];
        std::snprintf(sig, sizeof sig, "%08X", t.mbr_disk_signature);
        s << "  (disk signature " << sig << ")";
    }
    s << "\nSectors: " << t.total_lbas << " x " << t.sector_size << " bytes\n\n";

    if (t.partitions.empty()) {
        s << "No partitions.\n";
    } else {
        const bool gpt = t.type == partitions::TableType::gpt;
        TextTable tt({"#", "Start LBA", "End LBA", "Size", "Type", gpt ? "Name" : "Flags", "1MiB"},
                     {TextTable::Align::right, TextTable::Align::right, TextTable::Align::right,
                      TextTable::Align::right});
        for (const auto& p : t.partitions) {
            std::string extra;
            if (gpt) extra = p.name;
            else {
                if (p.bootable) extra += "boot ";
                if (p.extended_container) extra += "extended ";
                if (p.logical) extra += "logical";
            }
            tt.add({std::to_string(p.number), std::to_string(p.first_lba), std::to_string(p.last_lba),
                    short_capacity(p.size_bytes), p.type_name, extra, p.aligned_1mib ? "yes" : "NO"});
        }
        tt.print(s);
    }
    if (!t.warnings.empty()) {
        s << "\nWarnings:\n";
        for (const auto& w : t.warnings) s << "  ! " << w << "\n";
    }
    auto env = reports::envelope("partitions");
    env["device"] = (*dev)->info().path;
    env["table"] = reports::to_json(t);
    out.set_json(env);
    return out.flush(kOk);
}

// ------------------------------------------------------------- history

std::string history_path(const Options& o) {
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

int cmd_smart_show(const Options& o, const std::string& target) {
    Out out(o);
    if (is_image_path(target)) return fail(Error{Errc::unsupported, "image files have no SMART data"}, target);
    const auto path = expand_device(target);
    smart::RunOptions ro;
    ro.device_type = o.smart_type;
    auto rep = smart::query(path, ro);
    if (!rep) {
        if (rep.error().code == Errc::not_found) {
            std::cerr << "platter-cli: " << rep.error().message << "\n";
            return kToolMissing;
        }
        return fail(rep.error(), "smartctl");
    }
    const auto& r = *rep;
    const auto a = smart::assess(r);

    auto& s = out.text();
    s << "Device:        " << path << "  [" << smart::protocol_name(r.protocol);
    if (!r.device_type.empty()) s << ", -d " << r.device_type;
    s << "]\n";
    s << "Model:         " << (r.model.empty() ? "-" : r.model) << "\n";
    s << "Serial:        " << (o.show_serial ? r.serial : reports::mask_serial(r.serial)) << "\n";
    s << "Firmware:      " << (r.firmware.empty() ? "-" : r.firmware) << "\n";
    s << "Temperature:   " << (r.temperature_c ? std::to_string(*r.temperature_c) + " C" : "-") << "\n";
    s << "Power-on:      " << (r.power_on_hours ? std::to_string(*r.power_on_hours) + " h" : "-") << "\n";
    s << "Power cycles:  " << (r.power_cycles ? std::to_string(*r.power_cycles) : "-") << "\n";
    s << "SMART status:  " << (r.overall_passed ? (*r.overall_passed ? "PASSED" : "FAILED") : "-") << "\n\n";

    if (!r.attributes.empty()) {
        TextTable t({"ID", "Attribute", "Current", "Worst", "Threshold", "Raw Value", "Status"},
                    {TextTable::Align::right, TextTable::Align::left, TextTable::Align::right,
                     TextTable::Align::right, TextTable::Align::right, TextTable::Align::right});
        auto n = [](const std::optional<int>& v) { return v ? std::to_string(*v) : std::string("-"); };
        for (const auto& at : r.attributes) {
            std::string st = at.when_failed == "now" ? "FAILING" : at.when_failed == "past" ? "FAILED IN PAST" : "OK";
            t.add({std::to_string(at.id), at.name, n(at.value), n(at.worst), n(at.threshold),
                   o.verbose ? smart::format_raw(at.raw_value) : at.raw_string, st});
        }
        t.print(s);
        s << "\n";
    }
    if (r.nvme) {
        const auto& h = *r.nvme;
        auto v = [](const auto& x) { return x ? std::to_string(*x) : std::string("-"); };
        char cw[8];
        std::snprintf(cw, sizeof cw, "0x%02X", h.critical_warning);
        s << "NVMe critical warning:  " << cw << "\n"
          << "Available spare:        " << v(h.available_spare) << "% (threshold " << v(h.available_spare_threshold) << "%)\n"
          << "Percentage used:        " << v(h.percentage_used) << "%\n"
          << "Data units written:     " << v(h.data_units_written) << "\n"
          << "Media errors:           " << v(h.media_errors) << "\n"
          << "Unsafe shutdowns:       " << v(h.unsafe_shutdowns) << "\n"
          << "Error log entries:      " << v(h.error_log_entries) << "\n\n";
    }

    s << "Assessment:    " << smart::health_name(a.status) << "\n";
    for (const auto& f : a.findings) s << "  [" << smart::health_name(f.severity) << "] " << f.text << "\n";

    auto env = reports::envelope("smart");
    env["smart"] = reports::to_json(r, {o.show_serial});
    env["assessment"] = reports::to_json(a);
    out.set_json(env);
    if (o.flags.count("save")) {
        auto info = describe_device(path);
        DeviceInfo d = info ? *info : DeviceInfo{};
        if (!info) {
            d.path = path;
            d.model = r.model;
            d.serial = r.serial;
        }
        save_to_history(o, [&](storage::HistoryStore& h) { return h.record_smart(d, r, a); });
    }

    switch (a.status) {
        case smart::Health::good: return out.flush(kOk);
        case smart::Health::warning: return out.flush(kHealthWarning);
        case smart::Health::critical: return out.flush(kHealthCritical);
        default: return out.flush(kHealthUnknown);
    }
}

// ------------------------------------------------------------- surface scan

std::atomic<bool> g_interrupted{false};
extern "C" void on_sigint(int) { g_interrupted.store(true); }

std::optional<std::uint64_t> parse_u64(const std::string& s) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos || s.size() > 20) return std::nullopt;
    try {
        return std::stoull(s);
    } catch (...) {
        return std::nullopt;
    }
}

// "4096", "64K", "1M", "16MiB", "4G" -> bytes (binary multiples)
std::optional<std::uint64_t> parse_size(std::string s) {
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

std::string hms(std::chrono::seconds s) {
    char b[32];
    const auto t = s.count();
    std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld", static_cast<long long>(t / 3600),
                  static_cast<long long>(t / 60 % 60), static_cast<long long>(t % 60));
    return b;
}

void print_ascii_map(std::ostream& os, const surface::ScanSnapshot& snap) {
    using surface::LatencyClass;
    static const char glyph[] = {' ', '.', ':', '+', '*', '#', 'X'};
    const std::size_t n = snap.blocks.size();
    if (!n) return;
    const std::size_t cols = 64, max_rows = 16;
    const std::size_t cells = std::min<std::size_t>(n, cols * max_rows);
    const std::size_t per = (n + cells - 1) / cells;
    os << "\nLatency map (" << per << " block(s) per character; worst class shown)\n"
       << "  ' ' untested  '.' fast  ':' good  '+' medium  '*' slow  '#' very slow  'X' error\n";
    for (std::size_t c = 0; c * per < n; ++c) {
        if (c % cols == 0) os << (c ? "|\n  |" : "  |");
        int worst = 0;
        for (std::size_t i = c * per; i < std::min(n, (c + 1) * per); ++i)
            worst = std::max(worst, static_cast<int>(snap.blocks[i].cls));
        os << glyph[worst];
    }
    os << "|\n";
}

int cmd_surface_scan(const Options& o, const std::string& target) {
    using namespace surface;
    ScanConfig c;
    auto get = [&](const char* k) -> const std::string* {
        auto it = o.values.find(k);
        return it == o.values.end() ? nullptr : &it->second;
    };
    auto bad = [](const std::string& m) {
        std::cerr << "platter-cli: " << m << "\n";
        return kUsage;
    };
    if (auto v = get("start")) {
        auto n = parse_u64(*v);
        if (!n) return bad("--start must be an LBA number");
        c.start_lba = *n;
    }
    if (auto v = get("end")) {
        auto n = parse_u64(*v);
        if (!n) return bad("--end must be an LBA number");
        c.end_lba = *n;
    }
    if (auto v = get("block-size")) {
        auto n = parse_size(*v);
        if (!n || *n > UINT32_MAX) return bad("--block-size must be a size such as 64K or 1M");
        c.block_bytes = static_cast<std::uint32_t>(*n);
    }
    if (auto v = get("order")) {
        if (*v == "forward") c.order = ScanOrder::forward;
        else if (*v == "reverse") c.order = ScanOrder::reverse;
        else if (*v == "random") c.order = ScanOrder::random;
        else if (*v == "sampled") c.order = ScanOrder::sampled;
        else return bad("--order must be forward, reverse, random or sampled");
    }
    if (auto v = get("sample")) {
        try {
            c.sample_percent = std::stod(*v);
        } catch (...) {
            return bad("--sample must be a percentage");
        }
    }
    if (auto v = get("seed")) {
        auto n = parse_u64(*v);
        if (!n) return bad("--seed must be a number");
        c.seed = *n;
    }
    if (auto v = get("retries")) {
        auto n = parse_u64(*v);
        if (!n) return bad("--retries must be a number");
        c.retries = static_cast<std::uint32_t>(std::min<std::uint64_t>(*n, 1000));
    }
    if (auto v = get("stop-after")) {
        auto n = parse_u64(*v);
        if (!n || *n == 0) return bad("--stop-after must be at least 1");
        c.error_policy = ErrorPolicy::stop_after_n;
        c.max_errors = *n;
    }
    if (auto v = get("thresholds")) {
        std::istringstream in(*v);
        std::string part;
        std::size_t i = 0;
        while (std::getline(in, part, ',')) {
            if (i >= 4) return bad("--thresholds takes exactly four values");
            try {
                const double msv = std::stod(part);
                if (msv <= 0 || msv > 3'600'000) return bad("--thresholds values must be positive milliseconds");
                c.thresholds.us[i++] = static_cast<std::uint32_t>(msv * 1000.0);
            } catch (...) {
                return bad("--thresholds must look like 5,20,50,200");
            }
        }
        if (i != 4) return bad("--thresholds takes exactly four values");
    }
    c.direct_io = !o.flags.count("buffered");

    const bool image = is_image_path(target);
    const std::string path = image ? target : expand_device(target);
    auto created = create_scan(path, image, c, o.sector_size);
    if (!created) return fail(created.error(), path);
    auto& scan = *created;
    if (auto v = scan->validate(); !v) return fail(v.error(), "invalid scan configuration");

    const auto& L = scan->layout();
    if (!o.quiet) {
        std::cerr << "Scanning " << path << " (read-only): LBA " << L.start_lba << " - " << L.end_lba << ", "
                  << L.block_count << " blocks of " << scan->config().block_bytes << " bytes, "
                  << scan_order_name(scan->config().order) << ", "
                  << (scan->config().direct_io ? "direct I/O" : "buffered I/O") << ". Ctrl+C to stop.\n";
        for (const auto& w : scan->warnings()) std::cerr << "warning: " << w << "\n";
    }

    g_interrupted = false;
    auto previous = std::signal(SIGINT, on_sigint);
    if (auto s = scan->start(); !s) return fail(s.error(), "starting scan");

    const bool tty =
#ifdef _WIN32
        true;
#else
        isatty(2) != 0;
#endif
    auto last_print = std::chrono::steady_clock::now() - std::chrono::hours(1);
    bool cancel_sent = false;
    while (!is_terminal(scan->state())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (g_interrupted && !cancel_sent) {
            scan->cancel();
            cancel_sent = true;
            if (!o.quiet) std::cerr << "\nStopping...\n";
        }
        const auto now = std::chrono::steady_clock::now();
        if (o.quiet || now - last_print < (tty ? std::chrono::milliseconds(500) : std::chrono::seconds(10))) continue;
        last_print = now;
        const auto p = scan->progress();
        const auto st = scan->stats();
        char line[200];
        std::snprintf(line, sizeof line, "[%5.1f%%] LBA %-12llu %8.2f MB/s  ETA %s  errors %llu  slow %llu",
                      p.percent(), static_cast<unsigned long long>(p.current_lba), p.current_bytes_per_sec / 1e6,
                      p.eta ? hms(*p.eta).c_str() : "--:--:--", static_cast<unsigned long long>(st.failed),
                      static_cast<unsigned long long>(st.slow));
        std::cerr << (tty ? "\r" : "") << line << (tty ? "" : "\n") << std::flush;
    }
    scan->wait();
    std::signal(SIGINT, previous);
    if (!o.quiet && tty) std::cerr << "\n";

    const auto snap = scan->snapshot();
    reports::Privacy priv{o.show_serial};
    auto write_file = [&](const std::string* file, const std::string& content) {
        if (!file) return true;
        std::ofstream f(*file, std::ios::binary | std::ios::trunc);
        if (!f || !(f << content)) {
            std::cerr << "platter-cli: cannot write " << *file << "\n";
            return false;
        }
        return true;
    };
    if (o.flags.count("save")) save_to_history(o, [&](storage::HistoryStore& h) { return h.record_scan(snap); });
    bool files_ok = write_file(get("csv"), reports::scan_to_csv(snap));
    files_ok = write_file(get("report"), reports::scan_to_text(snap, priv)) && files_ok;

    Out out(o);
    out.text() << reports::scan_to_text(snap, priv);
    if (o.flags.count("map")) print_ascii_map(out.text(), snap);
    out.set_raw_json(reports::scan_to_json(snap, priv));
    int code = kOk;
    switch (snap.progress.state) {
        case OperationState::completed: code = snap.stats.failed ? kScanErrors : kOk; break;
        case OperationState::cancelled: code = kCancelled; break;
        default: code = snap.stats.failed ? kScanErrors : kIoError; break;
    }
    if (snap.progress.state == OperationState::failed && !o.quiet)
        std::cerr << "platter-cli: scan stopped: " << snap.progress.message << "\n";
    code = out.flush(code);
    return files_ok ? code : kFailure;
}

int usage_error(const std::string& msg);

// ------------------------------------------------------------- benchmark

std::optional<std::vector<std::uint32_t>> parse_list(const std::string& text, bool sizes) {
    std::vector<std::uint32_t> out;
    std::istringstream in(text);
    std::string part;
    while (std::getline(in, part, ',')) {
        auto v = sizes ? parse_size(part) : parse_u64(part);
        if (!v || *v == 0 || *v > UINT32_MAX) return std::nullopt;
        out.push_back(static_cast<std::uint32_t>(*v));
    }
    if (out.empty()) return std::nullopt;
    return out;
}

void print_bar(std::ostream& os, const std::string& label, double value, double max, const std::string& unit) {
    const int width = 46;
    const int n = max > 0 ? static_cast<int>(value / max * width + 0.5) : 0;
    char b[64];
    std::snprintf(b, sizeof b, "  %-10s |", label.substr(0, 10).c_str());
    os << b << std::string(static_cast<std::size_t>(std::max(0, n)), '#') << std::string(static_cast<std::size_t>(width - std::max(0, n)), ' ');
    std::snprintf(b, sizeof b, "| %10.1f %s\n", value, unit.c_str());
    os << b;
}

void print_bench_chart(std::ostream& os, const bench::BenchResult& r) {
    using bench::BenchKind;
    os << "\n";
    if (r.config.kind == BenchKind::speed_curve && !r.positions.empty()) {
        // Throughput vs position, 64 columns x 12 rows.
        const int cols = 64, rows = 12;
        double hi = 0;
        for (const auto& p : r.positions) hi = std::max(hi, p.bytes_per_sec / 1e6);
        // Each column shows the best point among those mapping to it, or the
        // nearest point when there are fewer points than columns.
        const std::size_t n = r.positions.size();
        std::vector<double> col(static_cast<std::size_t>(cols), 0);
        for (std::size_t c = 0; c < static_cast<std::size_t>(cols); ++c) {
            const std::size_t lo = c * n / static_cast<std::size_t>(cols);
            const std::size_t hi_i = std::max(lo + 1, (c + 1) * n / static_cast<std::size_t>(cols));
            for (std::size_t i = lo; i < std::min(hi_i, n); ++i) col[c] = std::max(col[c], r.positions[i].bytes_per_sec / 1e6);
        }
        os << "Read speed vs position (MB/s)\n";
        for (int row = rows; row >= 1; --row) {
            const double level = hi * row / rows;
            char lab[16];
            std::snprintf(lab, sizeof lab, "%8.0f |", level);
            os << lab;
            for (int c = 0; c < cols; ++c) os << (col[static_cast<std::size_t>(c)] >= level - hi / (2.0 * rows) ? '*' : ' ');
            os << "\n";
        }
        os << "         +" << std::string(cols, '-') << "\n          start of region" << std::string(cols - 26, ' ')
           << "end\n";
        return;
    }
    if (r.phases.size() > 1) {
        const bool iops = r.config.kind == BenchKind::queue_depths;
        double hi = 0;
        for (const auto& p : r.phases) hi = std::max(hi, iops ? p.metrics.iops : p.metrics.mb_per_s);
        os << (iops ? "IOPS by queue depth\n" : "Throughput by block size\n");
        for (const auto& p : r.phases)
            print_bar(os, p.label, iops ? p.metrics.iops : p.metrics.mb_per_s, hi, iops ? "IOPS" : "MB/s");
        return;
    }
    if (!r.timeline.empty()) {
        double hi = 0;
        for (const auto& t : r.timeline) hi = std::max(hi, t.bytes_per_sec / 1e6);
        os << "Throughput over time (250 ms samples)\n";
        const std::size_t step = std::max<std::size_t>(1, r.timeline.size() / 20);
        for (std::size_t i = 0; i < r.timeline.size(); i += step) {
            char lab[16];
            std::snprintf(lab, sizeof lab, "%.2f s", r.timeline[i].t_s);
            print_bar(os, lab, r.timeline[i].bytes_per_sec / 1e6, hi, "MB/s");
        }
    }
}

int cmd_benchmark_read(const Options& o, const std::string& target) {
    using namespace bench;
    BenchConfig c;
    auto get = [&](const char* k) -> const std::string* {
        auto it = o.values.find(k);
        return it == o.values.end() ? nullptr : &it->second;
    };
    auto bad = [](const std::string& m) {
        std::cerr << "platter-cli: " << m << "\n";
        return kUsage;
    };
    if (auto v = get("test")) {
        static const std::pair<const char*, BenchKind> kinds[] = {
            {"seq", BenchKind::sequential},     {"sequential", BenchKind::sequential}, {"random", BenchKind::random},
            {"access", BenchKind::access_time}, {"curve", BenchKind::speed_curve},     {"sizes", BenchKind::block_sizes},
            {"qd", BenchKind::queue_depths}};
        bool found = false;
        for (const auto& [n, k] : kinds)
            if (*v == n) {
                c.kind = k;
                found = true;
            }
        if (!found) return bad("--test must be seq, random, access, curve, sizes or qd");
    }
    if (auto v = get("block-size")) {
        auto n = parse_size(*v);
        if (!n || *n > UINT32_MAX) return bad("--block-size must be a size such as 4K or 1M");
        c.block_bytes = static_cast<std::uint32_t>(*n);
    }
    if (auto v = get("qd")) {
        auto n = parse_u64(*v);
        if (!n || *n < 1 || *n > 64) return bad("--qd must be between 1 and 64");
        c.queue_depth = static_cast<std::uint32_t>(*n);
    }
    if (auto v = get("duration")) {
        try {
            c.max_seconds = std::stod(*v);
        } catch (...) {
            return bad("--duration must be seconds");
        }
        if (c.max_seconds <= 0 || c.max_seconds > 86400) return bad("--duration must be between 0 and 86400 seconds");
    }
    if (auto v = get("size")) {
        auto n = parse_size(*v);
        if (!n || *n == 0) return bad("--size must be a size such as 256M or 4G");
        c.max_bytes = *n;
    }
    if (auto v = get("points")) {
        auto n = parse_u64(*v);
        if (!n || *n < 2 || *n > 10000) return bad("--points must be between 2 and 10000");
        c.curve_points = static_cast<std::uint32_t>(*n);
    }
    if (auto v = get("point-size")) {
        auto n = parse_size(*v);
        if (!n || *n == 0) return bad("--point-size must be a size such as 16M");
        c.curve_bytes_per_point = *n;
    }
    if (auto v = get("sizes")) {
        auto l = parse_list(*v, true);
        if (!l) return bad("--sizes must look like 4K,64K,1M");
        c.sweep_block_sizes = *l;
    }
    if (auto v = get("qds")) {
        auto l = parse_list(*v, false);
        if (!l) return bad("--qds must look like 1,4,16,32");
        c.sweep_queue_depths = *l;
    }
    if (auto v = get("start")) {
        auto n = parse_u64(*v);
        if (!n) return bad("--start must be an LBA number");
        c.start_lba = *n;
    }
    if (auto v = get("end")) {
        auto n = parse_u64(*v);
        if (!n) return bad("--end must be an LBA number");
        c.end_lba = *n;
    }
    if (auto v = get("seed")) {
        auto n = parse_u64(*v);
        if (!n) return bad("--seed must be a number");
        c.seed = *n;
    }
    c.direct_io = !o.flags.count("buffered");

    const bool image = is_image_path(target);
    const std::string path = image ? target : expand_device(target);
    auto created = create_benchmark(path, image, c, o.sector_size);
    if (!created) return fail(created.error(), path);
    auto& b = *created;
    if (auto v = b->validate(); !v) return fail(v.error(), "invalid benchmark configuration");
    const auto& ec = b->config();
    if (!o.quiet)
        std::cerr << bench_kind_title(ec.kind) << " on " << path << " (read-only), block " << ec.block_bytes
                  << " bytes, QD " << ec.queue_depth << ", " << (ec.direct_io ? "direct" : "buffered")
                  << " I/O. Ctrl+C to stop.\n";

    g_interrupted = false;
    auto previous = std::signal(SIGINT, on_sigint);
    if (auto st = b->start(); !st) return fail(st.error(), "starting benchmark");
    const bool tty =
#ifdef _WIN32
        true;
#else
        isatty(2) != 0;
#endif
    auto last = std::chrono::steady_clock::now() - std::chrono::hours(1);
    bool cancel_sent = false;
    while (!is_terminal(b->state())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (g_interrupted && !cancel_sent) {
            b->cancel();
            cancel_sent = true;
            if (!o.quiet) std::cerr << "\nStopping...\n";
        }
        const auto now = std::chrono::steady_clock::now();
        if (o.quiet || now - last < (tty ? std::chrono::milliseconds(500) : std::chrono::seconds(10))) continue;
        last = now;
        const auto p = b->progress();
        char line[160];
        std::snprintf(line, sizeof line, "[%5.1f%%] %8.2f MB/s  LBA %-12llu errors %llu", b->fraction() * 100.0,
                      p.current_bytes_per_sec / 1e6, static_cast<unsigned long long>(p.current_lba),
                      static_cast<unsigned long long>(p.errors));
        std::cerr << (tty ? "\r" : "") << line << (tty ? "" : "\n") << std::flush;
    }
    b->wait();
    std::signal(SIGINT, previous);
    if (!o.quiet && tty) std::cerr << "\n";

    const auto r = b->result();
    reports::Privacy priv{o.show_serial};
    Out out(o);
    out.text() << reports::bench_to_text(r, priv);
    if (o.flags.count("chart")) print_bench_chart(out.text(), r);
    out.set_raw_json(reports::bench_to_json(r, priv));
    if (o.flags.count("save") && r.state != OperationState::failed)
        save_to_history(o, [&](storage::HistoryStore& h) { return h.record_benchmark(r); });
    bool files_ok = true;
    if (auto file = get("report")) {
        std::ofstream f(*file, std::ios::binary | std::ios::trunc);
        files_ok = f && (f << reports::bench_to_text(r, priv));
        if (!files_ok) std::cerr << "platter-cli: cannot write " << *file << "\n";
    }
    std::uint64_t errors = 0;
    for (const auto& ph : r.phases) errors += ph.metrics.errors;
    int code = kOk;
    switch (r.state) {
        case OperationState::completed: code = errors ? kScanErrors : kOk; break;
        case OperationState::cancelled: code = kCancelled; break;
        default:
            if (!o.quiet) std::cerr << "platter-cli: benchmark stopped: " << r.message << "\n";
            code = errors ? kScanErrors : kIoError;
            break;
    }
    code = out.flush(code);
    return files_ok ? code : kFailure;
}

int cmd_benchmark_history(const Options& o, const std::string* target) {
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    std::string key;
    if (target) {
        auto info = describe_target(*target, o);
        if (!info) return fail(info.error(), *target);
        key = storage::HistoryStore::device_key(*info);
    }
    int limit = 30;
    if (auto it = o.values.find("limit"); it != o.values.end()) {
        auto n = parse_u64(it->second);
        if (!n || *n == 0 || *n > 100000) return usage_error("--limit must be a positive number");
        limit = static_cast<int>(*n);
    }
    auto rows = (*store)->list(storage::SessionKind::benchmark, key, limit);
    if (!rows) return fail(rows.error(), "history");
    Out out(o);
    json arr = json::array();
    TextTable t({"#", "Started (UTC)", "Test", "Device", "State", "Result"},
                {TextTable::Align::right});
    for (const auto& r : *rows) {
        arr.push_back({{"id", r.id}, {"started_utc", r.started_utc}, {"test", r.subtype}, {"device_key", r.device_key},
                       {"device_model", r.device_model}, {"state", r.state}, {"headline", r.headline},
                       {"value", r.primary_value}});
        t.add({std::to_string(r.id), r.started_utc, r.subtype, r.device_model.empty() ? r.device_key : r.device_model,
               r.state, r.headline});
    }
    auto env = reports::envelope("benchmark-history");
    env["database"] = (*store)->path();
    env["sessions"] = arr;
    out.set_json(env);
    if (rows->empty()) out.text() << "No saved benchmarks in " << (*store)->path() << "\n";
    else t.print(out.text());
    return out.flush(kOk);
}

int cmd_benchmark_show(const Options& o, const std::string& id_text) {
    auto id = parse_u64(id_text);
    if (!id) return usage_error("benchmark show needs a numeric session id");
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    auto r = (*store)->load_benchmark(static_cast<std::int64_t>(*id));
    if (!r) return fail(r.error(), "history");
    Out out(o);
    out.text() << reports::bench_to_text(*r);
    if (o.flags.count("chart")) print_bench_chart(out.text(), *r);
    out.set_raw_json(reports::bench_to_json(*r));
    return out.flush(kOk);
}

int usage_error(const std::string& msg) {
    std::cerr << "platter-cli: " << msg << "\nTry 'platter-cli --help'.\n";
    return kUsage;
}

}  // namespace

int main(int argc, char** argv) {
    int code = kOk;
    auto opts = parse_args(argc, argv, code);
    if (!opts) return code;
    const auto& p = opts->positional;
    if (p.empty()) return usage_error("no command given");

    const auto& cmd = p[0];
    auto arg = [&](std::size_t i) -> const std::string* { return i < p.size() ? &p[i] : nullptr; };

    if (cmd == "version") {
        std::cout << kProjectName << " " << kVersion << "\n";
        return kOk;
    }
    if (cmd == "devices") {
        if (!arg(1) || *arg(1) == "list") return cmd_devices_list(*opts);
        if (*arg(1) == "info") {
            if (!arg(2)) return usage_error("devices info needs a <device>");
            return cmd_devices_info(*opts, *arg(2));
        }
        return usage_error("unknown devices subcommand '" + *arg(1) + "'");
    }
    if (cmd == "partitions") {
        if (!arg(1) || *arg(1) != "list") return usage_error("usage: partitions list <device>");
        if (!arg(2)) return usage_error("partitions list needs a <device>");
        return cmd_partitions_list(*opts, *arg(2));
    }
    if (cmd == "surface") {
        if (!arg(1) || *arg(1) != "scan") return usage_error("usage: surface scan <device>");
        if (!arg(2)) return usage_error("surface scan needs a <device>");
        return cmd_surface_scan(*opts, *arg(2));
    }
    if (cmd == "benchmark") {
        if (arg(1) && *arg(1) == "read") {
            if (!arg(2)) return usage_error("benchmark read needs a <device>");
            return cmd_benchmark_read(*opts, *arg(2));
        }
        if (arg(1) && *arg(1) == "history") return cmd_benchmark_history(*opts, arg(2));
        if (arg(1) && *arg(1) == "show") {
            if (!arg(2)) return usage_error("benchmark show needs a session id");
            return cmd_benchmark_show(*opts, *arg(2));
        }
        return usage_error("usage: benchmark read <device> | benchmark history [<device>] | benchmark show <id>");
    }
    if (cmd == "smart") {
        if (!arg(1) || *arg(1) != "show") return usage_error("usage: smart show <device>");
        if (!arg(2)) return usage_error("smart show needs a <device>");
        return cmd_smart_show(*opts, *arg(2));
    }
    return usage_error("unknown command '" + cmd + "'");
}
