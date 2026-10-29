// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/reports/json.hpp"

#include <chrono>
#include <ctime>

#include "platter/version.hpp"

using nlohmann::json;

namespace platter::reports {
namespace {

template <typename T>
json opt(const std::optional<T>& v) {
    return v ? json(*v) : json(nullptr);
}

json tri(Tri t) {
    switch (t) {
        case Tri::yes: return true;
        case Tri::no: return false;
        default: return nullptr;
    }
}

}  // namespace

std::string mask_serial(const std::string& serial) {
    if (serial.empty()) return {};
    const std::size_t keep = serial.size() > 8 ? 4 : 0;
    return serial.substr(0, keep) + std::string(serial.size() - keep, '*');
}

json envelope(const char* kind) {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return {{"tool", kProjectName}, {"version", kVersion}, {"schema", 1}, {"kind", kind}, {"generated_utc", ts}};
}

// by-id style stable ids ("ata-MODEL_SERIAL", "usb-..._SERIAL-0:0") can embed
// the serial number; mask it there too unless serials were requested.
std::string redact_stable_id(const DeviceInfo& d, const Privacy& p) {
    if (p.include_serials || d.serial.empty()) return d.stable_id;
    std::string id = d.stable_id;
    const std::string masked = mask_serial(d.serial);
    for (std::size_t pos = id.find(d.serial); pos != std::string::npos; pos = id.find(d.serial, pos + masked.size()))
        id.replace(pos, d.serial.size(), masked);
    return id;
}

json to_json(const DeviceInfo& d, const Privacy& p) {
    json caps = json::array();
    for (auto n : d.capabilities.names()) caps.push_back(std::string(n));
    json j = {
        {"path", d.path},
        {"name", d.name},
        {"vendor", d.vendor},
        {"model", d.model},
        {"firmware", d.firmware},
        {"stable_id", redact_stable_id(d, p)},
        {"capacity_bytes", d.capacity_bytes},
        {"logical_sector_size", d.logical_sector_size},
        {"physical_sector_size", d.physical_sector_size},
        {"total_lbas", d.total_lbas()},
        {"bus", std::string(bus_type_name(d.bus))},
        {"media", std::string(media_kind_name(d.media))},
        {"removable", tri(d.removable)},
        {"read_only", tri(d.read_only)},
        {"trim_supported", tri(d.trim_supported)},
        {"mountpoints", d.mountpoints},
        {"holders", d.holders},
        {"system_disk", d.is_system_disk},
        {"in_use", d.in_use()},
        {"topology", d.topology},
        {"capabilities", caps},
    };
    if (!d.partition_table.empty()) j["partition_table"] = d.partition_table;
    if (p.include_serials) j["serial"] = d.serial;
    return j;
}

json to_json(const partitions::PartitionTable& t) {
    using namespace partitions;
    json parts = json::array();
    for (const auto& p : t.partitions) {
        json e = {
            {"number", p.number},
            {"first_lba", p.first_lba},
            {"last_lba", p.last_lba},
            {"size_bytes", p.size_bytes},
            {"type", p.type_name},
            {"aligned_1mib", p.aligned_1mib},
        };
        if (t.type == TableType::gpt) {
            e["type_guid"] = p.type_guid.to_string();
            e["unique_guid"] = p.unique_guid.to_string();
            e["name"] = p.name;
            e["attributes"] = p.attributes;
        } else {
            e["mbr_type"] = p.mbr_type;
            e["bootable"] = p.bootable;
            e["extended"] = p.extended_container;
            e["logical"] = p.logical;
        }
        parts.push_back(std::move(e));
    }
    json j = {
        {"type", std::string(table_type_name(t.type))},
        {"sector_size", t.sector_size},
        {"total_lbas", t.total_lbas},
        {"partitions", parts},
        {"warnings", t.warnings},
    };
    if (t.type == TableType::mbr) j["disk_signature"] = t.mbr_disk_signature;
    if (t.type == TableType::gpt) {
        j["protective_mbr"] = t.protective_mbr;
        j["used_backup_gpt"] = t.used_backup_gpt;
        auto hdr = [](const std::optional<GptHeader>& h) -> json {
            if (!h) return nullptr;
            return {{"lba", h->header_lba},
                    {"header_crc_ok", h->header_crc_ok},
                    {"entries_crc_ok", h->entries_crc_ok},
                    {"disk_guid", h->disk_guid.to_string()},
                    {"first_usable_lba", h->first_usable_lba},
                    {"last_usable_lba", h->last_usable_lba},
                    {"entry_count", h->entry_count},
                    {"entry_size", h->entry_size}};
        };
        j["gpt_primary"] = hdr(t.gpt_primary);
        j["gpt_backup"] = hdr(t.gpt_backup);
    }
    return j;
}

json to_json(const smart::Assessment& a) {
    json f = json::array();
    for (const auto& x : a.findings)
        f.push_back({{"severity", std::string(smart::health_name(x.severity))}, {"text", x.text}});
    return {{"status", std::string(smart::health_name(a.status))}, {"findings", f}};
}

json to_json(const smart::Report& r, const Privacy& p) {
    json attrs = json::array();
    for (const auto& a : r.attributes)
        attrs.push_back({{"id", a.id},
                         {"name", a.name},
                         {"value", opt(a.value)},
                         {"worst", opt(a.worst)},
                         {"threshold", opt(a.threshold)},
                         {"raw", a.raw_value},
                         {"raw_string", a.raw_string},
                         {"prefailure", a.prefailure},
                         {"when_failed", a.when_failed}});
    json tests = json::array();
    for (const auto& t : r.self_tests)
        tests.push_back({{"type", t.type}, {"status", t.status}, {"passed", opt(t.passed)},
                         {"lifetime_hours", opt(t.lifetime_hours)}});
    json j = {
        {"smartctl_version", r.smartctl_version},
        {"smartctl_exit_status", r.smartctl_exit_status},
        {"messages", r.messages},
        {"device", r.device_name},
        {"device_type", r.device_type},
        {"protocol", std::string(smart::protocol_name(r.protocol))},
        {"model", r.model},
        {"firmware", r.firmware},
        {"capacity_bytes", opt(r.capacity_bytes)},
        {"rotation_rate", opt(r.rotation_rate)},
        {"smart_available", tri(r.smart_available)},
        {"smart_enabled", tri(r.smart_enabled)},
        {"overall_passed", opt(r.overall_passed)},
        {"temperature_c", opt(r.temperature_c)},
        {"power_on_hours", opt(r.power_on_hours)},
        {"power_cycles", opt(r.power_cycles)},
        {"attributes", attrs},
        {"self_tests", tests},
        {"ata_error_count", opt(r.ata_error_count)},
    };
    if (r.nvme) {
        const auto& h = *r.nvme;
        j["nvme"] = {{"critical_warning", h.critical_warning},
                     {"temperature_c", opt(h.temperature_c)},
                     {"available_spare", opt(h.available_spare)},
                     {"available_spare_threshold", opt(h.available_spare_threshold)},
                     {"percentage_used", opt(h.percentage_used)},
                     {"data_units_read", opt(h.data_units_read)},
                     {"data_units_written", opt(h.data_units_written)},
                     {"power_cycles", opt(h.power_cycles)},
                     {"power_on_hours", opt(h.power_on_hours)},
                     {"unsafe_shutdowns", opt(h.unsafe_shutdowns)},
                     {"media_errors", opt(h.media_errors)},
                     {"error_log_entries", opt(h.error_log_entries)}};
    }
    if (p.include_serials) j["serial"] = r.serial;
    return j;
}

}  // namespace platter::reports
