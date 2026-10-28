// SPDX-License-Identifier: TBD
//
// platter-cli: command-line front end over the same core as the GUI.
// Every command in v0.1 is read-only. See docs/cli-reference.md.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "platter/core/block_device.hpp"
#include "platter/partitions/partition_table.hpp"
#include "platter/platform/enumerate.hpp"
#include "platter/reports/json.hpp"
#include "platter/smart/smart.hpp"
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
    kHealthWarning = 10,
    kHealthCritical = 11,
    kHealthUnknown = 12,
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
    std::vector<std::string> positional;
};

constexpr const char* kUsageText = R"(usage: platter-cli [options] <command> [arguments]

Commands (all read-only):
  devices list                  List storage devices
  devices info <device>         Show identification and capabilities
  smart show <device>           SMART / NVMe health via smartctl
  partitions list <device>      Show the MBR/GPT partition table
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

Exit codes: 0 ok, 1 failure, 2 usage, 3 not found, 4 permission denied,
5 busy, 6 I/O error, 7 unsupported, 8 smartctl missing/failed,
10/11/12 health WARNING/CRITICAL/UNKNOWN (smart show).
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

    int flush(int code) {
        std::string payload = opts_.json ? json_.dump(2) + "\n" : buf_.str();
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
    row("Stable ID", d.stable_id);
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

    switch (a.status) {
        case smart::Health::good: return out.flush(kOk);
        case smart::Health::warning: return out.flush(kHealthWarning);
        case smart::Health::critical: return out.flush(kHealthCritical);
        default: return out.flush(kHealthUnknown);
    }
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
    if (cmd == "smart") {
        if (!arg(1) || *arg(1) != "show") return usage_error("usage: smart show <device>");
        if (!arg(2)) return usage_error("smart show needs a <device>");
        return cmd_smart_show(*opts, *arg(2));
    }
    return usage_error("unknown command '" + cmd + "'");
}
