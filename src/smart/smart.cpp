// SPDX-License-Identifier: TBD
#include "platter/smart/smart.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <nlohmann/json.hpp>

#include "platter/core/process.hpp"

using nlohmann::json;

namespace platter::smart {

std::string_view protocol_name(Protocol p) noexcept {
    switch (p) {
        case Protocol::unknown: return "Unknown";
        case Protocol::ata: return "ATA";
        case Protocol::nvme: return "NVMe";
        case Protocol::scsi: return "SCSI";
    }
    return "Unknown";
}

std::string_view health_name(Health h) noexcept {
    switch (h) {
        case Health::good: return "GOOD";
        case Health::warning: return "WARNING";
        case Health::critical: return "CRITICAL";
        case Health::unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const Attribute* Report::attribute(int id) const {
    for (const auto& a : attributes)
        if (a.id == id) return &a;
    return nullptr;
}

std::string format_raw(std::uint64_t raw) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%" PRIu64 " (0x%012" PRIX64 ")", raw, raw);
    return buf;
}

// ------------------------------------------------------------------ parsing

namespace {

template <typename T>
std::optional<T> opt(const json& j, const char* key) {
    if (!j.is_object()) return std::nullopt;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::nullopt;
    try {
        return it->get<T>();
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

template <typename T>
std::optional<T> opt_path(const json& j, std::initializer_list<const char*> path) {
    const json* cur = &j;
    for (auto key : path) {
        if (!cur->is_object()) return std::nullopt;
        auto it = cur->find(key);
        if (it == cur->end()) return std::nullopt;
        cur = &*it;
    }
    if (cur->is_null()) return std::nullopt;
    try {
        return cur->get<T>();
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

Tri tri_of(const std::optional<bool>& b) { return b ? (*b ? Tri::yes : Tri::no) : Tri::unknown; }

}  // namespace

Result<Report> parse_smartctl_json(const std::string& text) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        return Error{Errc::corrupt_data, std::string("smartctl output is not valid JSON: ") + e.what()};
    }
    if (!j.is_object()) return Error{Errc::corrupt_data, "smartctl JSON root is not an object"};

    Report r;
    if (auto sc = j.find("smartctl"); sc != j.end() && sc->is_object()) {
        r.smartctl_exit_status = opt<int>(*sc, "exit_status").value_or(0);
        if (auto v = sc->find("version"); v != sc->end() && v->is_array() && v->size() >= 2)
            r.smartctl_version = std::to_string((*v)[0].get<int>()) + "." + std::to_string((*v)[1].get<int>());
        if (auto msgs = sc->find("messages"); msgs != sc->end() && msgs->is_array())
            for (const auto& m : *msgs)
                if (auto s = opt<std::string>(m, "string")) r.messages.push_back(*s);
    }

    if (auto dev = j.find("device"); dev != j.end()) {
        r.device_name = opt<std::string>(*dev, "name").value_or("");
        r.device_type = opt<std::string>(*dev, "type").value_or("");
        const auto proto = opt<std::string>(*dev, "protocol").value_or("");
        if (proto == "ATA") r.protocol = Protocol::ata;
        else if (proto == "NVMe") r.protocol = Protocol::nvme;
        else if (proto == "SCSI") r.protocol = Protocol::scsi;
    }

    r.model = opt<std::string>(j, "model_name").value_or("");
    r.serial = opt<std::string>(j, "serial_number").value_or("");
    r.firmware = opt<std::string>(j, "firmware_version").value_or("");
    r.capacity_bytes = opt_path<std::uint64_t>(j, {"user_capacity", "bytes"});
    r.rotation_rate = opt<int>(j, "rotation_rate");
    r.smart_available = tri_of(opt_path<bool>(j, {"smart_support", "available"}));
    r.smart_enabled = tri_of(opt_path<bool>(j, {"smart_support", "enabled"}));
    r.overall_passed = opt_path<bool>(j, {"smart_status", "passed"});
    r.temperature_c = opt_path<int>(j, {"temperature", "current"});
    r.power_on_hours = opt_path<std::uint64_t>(j, {"power_on_time", "hours"});
    r.power_cycles = opt<std::uint64_t>(j, "power_cycle_count");

    if (auto table = j.find("ata_smart_attributes"); table != j.end()) {
        if (auto rows = table->find("table"); rows != table->end() && rows->is_array()) {
            for (const auto& row : *rows) {
                Attribute a;
                a.id = opt<int>(row, "id").value_or(0);
                a.name = opt<std::string>(row, "name").value_or("Unknown_Attribute");
                a.value = opt<int>(row, "value");
                a.worst = opt<int>(row, "worst");
                a.threshold = opt<int>(row, "thresh");
                a.when_failed = opt<std::string>(row, "when_failed").value_or("");
                a.prefailure = opt_path<bool>(row, {"flags", "prefailure"}).value_or(false);
                a.raw_value = opt_path<std::uint64_t>(row, {"raw", "value"}).value_or(0);
                a.raw_string = opt_path<std::string>(row, {"raw", "string"}).value_or(std::to_string(a.raw_value));
                if (a.id > 0) r.attributes.push_back(std::move(a));
            }
        }
    }

    if (auto nv = j.find("nvme_smart_health_information_log"); nv != j.end() && nv->is_object()) {
        NvmeHealth h;
        h.critical_warning = opt<std::uint32_t>(*nv, "critical_warning").value_or(0);
        h.temperature_c = opt<int>(*nv, "temperature");
        h.available_spare = opt<int>(*nv, "available_spare");
        h.available_spare_threshold = opt<int>(*nv, "available_spare_threshold");
        h.percentage_used = opt<int>(*nv, "percentage_used");
        h.data_units_read = opt<std::uint64_t>(*nv, "data_units_read");
        h.data_units_written = opt<std::uint64_t>(*nv, "data_units_written");
        h.power_cycles = opt<std::uint64_t>(*nv, "power_cycles");
        h.power_on_hours = opt<std::uint64_t>(*nv, "power_on_hours");
        h.unsafe_shutdowns = opt<std::uint64_t>(*nv, "unsafe_shutdowns");
        h.media_errors = opt<std::uint64_t>(*nv, "media_errors");
        h.error_log_entries = opt<std::uint64_t>(*nv, "num_err_log_entries");
        if (!r.temperature_c) r.temperature_c = h.temperature_c;
        if (!r.power_on_hours) r.power_on_hours = h.power_on_hours;
        if (!r.power_cycles) r.power_cycles = h.power_cycles;
        r.nvme = h;
    }

    auto add_tests = [&](const json& table) {
        if (!table.is_array()) return;
        for (const auto& t : table) {
            SelfTestEntry e;
            e.type = opt_path<std::string>(t, {"type", "string"}).value_or("");
            e.status = opt_path<std::string>(t, {"status", "string"}).value_or("");
            e.passed = opt_path<bool>(t, {"status", "passed"});
            e.lifetime_hours = opt<std::uint64_t>(t, "lifetime_hours");
            if (!e.lifetime_hours) e.lifetime_hours = opt<std::uint64_t>(t, "power_on_hours");
            r.self_tests.push_back(std::move(e));
        }
    };
    if (auto st = j.find("ata_smart_self_test_log"); st != j.end())
        if (auto std_log = st->find("standard"); std_log != st->end())
            if (auto tbl = std_log->find("table"); tbl != std_log->end()) add_tests(*tbl);
    if (auto st = j.find("nvme_self_test_log"); st != j.end())
        if (auto tbl = st->find("table"); tbl != st->end()) {
            // NVMe entries use self_test_code / self_test_result objects.
            for (const auto& t : *tbl) {
                SelfTestEntry e;
                e.type = opt_path<std::string>(t, {"self_test_code", "string"}).value_or("");
                e.status = opt_path<std::string>(t, {"self_test_result", "string"}).value_or("");
                if (auto v = opt_path<int>(t, {"self_test_result", "value"})) e.passed = (*v == 0);
                e.lifetime_hours = opt<std::uint64_t>(t, "power_on_hours");
                r.self_tests.push_back(std::move(e));
            }
        }

    r.self_test_supported = opt_path<bool>(j, {"ata_smart_data", "capabilities", "self_tests_supported"});
    r.ata_error_count = opt_path<std::uint64_t>(j, {"ata_smart_error_log", "summary", "count"});
    if (!r.ata_error_count) r.ata_error_count = opt_path<std::uint64_t>(j, {"ata_smart_error_log", "extended", "count"});
    return r;
}

// ------------------------------------------------------------------ assessment

namespace {

// Most counters live in the low 32 bits of the 48-bit raw field; vendors pack
// extra data above that (e.g. Seagate). Using the low dword avoids alarming
// on packed bits while still catching any real count.
std::uint64_t counter(const Attribute& a) { return a.raw_value & 0xFFFFFFFFull; }

int rank(Health h) {
    switch (h) {
        case Health::good: return 0;
        case Health::unknown: return 1;
        case Health::warning: return 2;
        case Health::critical: return 3;
    }
    return 1;
}

// Records an observation and escalates the overall status if needed.
void raise(Assessment& as, Health sev, std::string text) {
    if (rank(sev) > rank(as.status)) as.status = sev;
    as.findings.push_back({sev, std::move(text)});
}

}  // namespace

Assessment assess(const Report& r) {
    Assessment as;
    as.status = Health::good;  // provisional; downgraded to unknown below if we saw nothing

    if (r.smart_available == Tri::no) {
        as.status = Health::unknown;
        as.findings.push_back({Health::unknown, "SMART is not available through this connection"});
        for (const auto& m : r.messages) as.findings.push_back({Health::unknown, "smartctl: " + m});
        return as;
    }
    if (r.smart_enabled == Tri::no) {
        as.status = Health::unknown;
        as.findings.push_back({Health::unknown, "SMART is supported but disabled on the device"});
        return as;
    }
    if (r.smartctl_exit_status & 0x3) {
        as.status = Health::unknown;
        as.findings.push_back({Health::unknown, "smartctl could not open or query the device (exit status " +
                                                    std::to_string(r.smartctl_exit_status) + ")"});
        for (const auto& m : r.messages) as.findings.push_back({Health::unknown, "smartctl: " + m});
        return as;
    }

    if (r.overall_passed && !*r.overall_passed)
        raise(as, Health::critical, "Device reports overall SMART health: FAILED");

    // ---- ATA attributes
    for (const auto& a : r.attributes) {
        if (a.when_failed == "now")
            raise(as, Health::critical,
                  "Attribute " + std::to_string(a.id) + " " + a.name + " is at or below its threshold now");
        else if (a.when_failed == "past")
            raise(as, Health::warning,
                  "Attribute " + std::to_string(a.id) + " " + a.name + " has been below its threshold in the past");
    }
    struct Rule { int id; const char* what; Health sev; };
    static const Rule rules[] = {
        {5, "reallocated sectors", Health::warning},
        {196, "reallocation events", Health::warning},
        {197, "sectors pending reallocation", Health::warning},
        {198, "offline uncorrectable sectors", Health::warning},
        {187, "reported uncorrectable errors", Health::warning},
        {10, "spin-up retries", Health::warning},
    };
    for (const auto& rule : rules) {
        if (const auto* a = r.attribute(rule.id); a && counter(*a) > 0)
            raise(as, rule.sev, std::to_string(counter(*a)) + " " + rule.what + " (attribute " +
                                    std::to_string(rule.id) + ")");
    }
    if (const auto* crc = r.attribute(199); crc && counter(*crc) > 0)
        raise(as, Health::warning, std::to_string(counter(*crc)) +
                                       " interface CRC errors (attribute 199): usually cabling or the "
                                       "controller/bridge, not the media");

    // ---- NVMe health log
    if (r.nvme) {
        const auto& h = *r.nvme;
        if (h.critical_warning != 0) {
            std::string bits;
            if (h.critical_warning & 0x01) bits += " spare-below-threshold";
            if (h.critical_warning & 0x02) bits += " temperature";
            if (h.critical_warning & 0x04) bits += " reliability-degraded";
            if (h.critical_warning & 0x08) bits += " read-only";
            if (h.critical_warning & 0x10) bits += " volatile-backup-failed";
            raise(as, Health::critical, "NVMe critical warning flags set:" + bits);
        }
        if (h.available_spare && h.available_spare_threshold && *h.available_spare < *h.available_spare_threshold)
            raise(as, Health::critical, "Available spare " + std::to_string(*h.available_spare) +
                                            "% is below the threshold of " +
                                            std::to_string(*h.available_spare_threshold) + "%");
        if (h.percentage_used) {
            if (*h.percentage_used >= 100)
                raise(as, Health::warning, "Rated endurance exceeded (percentage used " +
                                               std::to_string(*h.percentage_used) + "%)");
            else if (*h.percentage_used >= 90)
                raise(as, Health::warning, "Approaching rated endurance (percentage used " +
                                               std::to_string(*h.percentage_used) + "%)");
        }
        if (h.media_errors && *h.media_errors > 0)
            raise(as, Health::warning, std::to_string(*h.media_errors) + " media and data integrity errors");
        if (h.unsafe_shutdowns && *h.unsafe_shutdowns > 0)
            as.findings.push_back({Health::good, std::to_string(*h.unsafe_shutdowns) +
                                                     " unsafe shutdowns recorded (informational)"});
    }

    // ---- logs
    if (!r.self_tests.empty() && r.self_tests.front().passed && !*r.self_tests.front().passed)
        raise(as, Health::critical, "Most recent self-test failed: " + r.self_tests.front().status);
    if (r.ata_error_count && *r.ata_error_count > 0)
        raise(as, Health::warning, "ATA error log contains " + std::to_string(*r.ata_error_count) + " entries");

    // ---- temperature
    if (r.temperature_c) {
        const int limit = r.protocol == Protocol::nvme ? 75 : 60;
        if (*r.temperature_c >= limit)
            raise(as, Health::warning, "Temperature " + std::to_string(*r.temperature_c) + " °C is at or above " +
                                           std::to_string(limit) + " °C");
    }

    // ---- conclusion
    if (as.status == Health::good) {
        if (!r.has_health_data()) {
            as.status = Health::unknown;
            as.findings.push_back({Health::unknown, r.overall_passed
                                                        ? "SMART reports PASSED, but no attribute or health-log "
                                                          "data was available to confirm it"
                                                        : "No SMART health data was available"});
        } else {
            as.findings.push_back({Health::good,
                                   "No adverse observations in the available SMART data. This describes the "
                                   "current state only and does not predict future reliability."});
        }
    }
    return as;
}

// ------------------------------------------------------------------ running

std::optional<std::string> find_smartctl() {
    if (const char* env = std::getenv("PLATTER_SMARTCTL"); env && *env) return std::string(env);
    if (auto p = find_in_path("smartctl"); !p.empty()) return p;
#ifdef _WIN32
    for (const char* candidate : {"C:\\Program Files\\smartmontools\\bin\\smartctl.exe",
                                  "C:\\Program Files (x86)\\smartmontools\\bin\\smartctl.exe"}) {
        if (auto p = find_in_path(candidate); !p.empty()) return p;
    }
#else
    for (const char* candidate : {"/usr/sbin/smartctl", "/sbin/smartctl", "/usr/local/sbin/smartctl"})
        if (auto p = find_in_path(candidate); !p.empty()) return p;
#endif
    return std::nullopt;
}

std::string smartctl_device_arg(const std::string& device_path) {
#ifdef _WIN32
    static const std::string pre = "\\\\.\\PhysicalDrive";
    if (device_path.size() > pre.size() && _strnicmp(device_path.c_str(), pre.c_str(), pre.size()) == 0)
        return "/dev/pd" + device_path.substr(pre.size());
#endif
    return device_path;
}

Result<Report> query(const std::string& device_path, const RunOptions& opts) {
    std::string exe = opts.smartctl_path;
    if (exe.empty()) {
        auto found = find_smartctl();
        if (!found)
            return Error{Errc::not_found,
                         "smartctl not found; install smartmontools or set PLATTER_SMARTCTL"};
        exe = *found;
    }
    // -a is a read-only information query. Never add options that start
    // tests, change settings or write logs here.
    std::vector<std::string> argv = {exe, "--json=c", "-a"};
    if (!opts.device_type.empty()) {
        argv.push_back("-d");
        argv.push_back(opts.device_type);
    }
    argv.push_back(smartctl_device_arg(device_path));

    auto pr = run_process(argv, std::chrono::duration_cast<std::chrono::milliseconds>(opts.timeout));
    if (!pr) return pr.error();
    if (pr->out.empty())
        return Error{Errc::external_tool_failed, "smartctl produced no output (exit " + std::to_string(pr->exit_code) +
                                                     "): " + pr->err};
    auto rep = parse_smartctl_json(pr->out);
    if (!rep) return rep.error();
    rep->smartctl_exit_status = pr->exit_code;
    return rep;
}

void apply_capabilities(const Report& r, DeviceInfo& info) {
    const bool usable = r.smart_available == Tri::yes && (r.smartctl_exit_status & 0x3) == 0;
    info.capabilities.set(Capability::smart, usable && r.has_health_data());
    // Self-test support: the ATA capability bit when present, otherwise only
    // claim it if smartctl returned a self-test log (proof it is reachable).
    info.capabilities.set(Capability::self_test,
                          usable && r.self_test_supported.value_or(!r.self_tests.empty()));
}

}  // namespace platter::smart
