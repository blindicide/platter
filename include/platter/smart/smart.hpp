// SPDX-License-Identifier: TBD
//
// SMART / NVMe health via smartmontools. platter does not issue SMART
// commands itself; it runs `smartctl --json` (machine-readable output, spec
// §5.1) and interprets the result.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "platter/core/device.hpp"
#include "platter/core/error.hpp"

namespace platter::smart {

enum class Protocol : std::uint8_t { unknown, ata, nvme, scsi };
std::string_view protocol_name(Protocol p) noexcept;

struct Attribute {
    int id = 0;
    std::string name;
    std::optional<int> value;      // normalized current
    std::optional<int> worst;
    std::optional<int> threshold;
    std::uint64_t raw_value = 0;   // as decoded by smartctl
    std::string raw_string;        // smartctl's human rendering of raw
    bool prefailure = false;
    std::string when_failed;       // "", "now", "past"
};

struct NvmeHealth {
    std::uint32_t critical_warning = 0;
    std::optional<int> temperature_c;
    std::optional<int> available_spare;
    std::optional<int> available_spare_threshold;
    std::optional<int> percentage_used;
    std::optional<std::uint64_t> data_units_read;     // 1000 * 512-byte units
    std::optional<std::uint64_t> data_units_written;
    std::optional<std::uint64_t> power_cycles;
    std::optional<std::uint64_t> power_on_hours;
    std::optional<std::uint64_t> unsafe_shutdowns;
    std::optional<std::uint64_t> media_errors;
    std::optional<std::uint64_t> error_log_entries;
};

struct SelfTestEntry {
    std::string type;
    std::string status;
    std::optional<bool> passed;
    std::optional<std::uint64_t> lifetime_hours;
};

struct Report {
    int smartctl_exit_status = 0;
    std::string smartctl_version;
    std::vector<std::string> messages;   // smartctl's own diagnostics

    std::string device_name;
    std::string device_type;             // smartctl -d type: sat, nvme, usbjmicron...
    Protocol protocol = Protocol::unknown;

    std::string model;
    std::string serial;
    std::string firmware;
    std::optional<std::uint64_t> capacity_bytes;
    std::optional<int> rotation_rate;    // 0 = SSD

    Tri smart_available = Tri::unknown;
    Tri smart_enabled = Tri::unknown;
    std::optional<bool> overall_passed;

    std::optional<int> temperature_c;
    std::optional<std::uint64_t> power_on_hours;
    std::optional<std::uint64_t> power_cycles;

    std::vector<Attribute> attributes;   // ATA only
    std::optional<NvmeHealth> nvme;
    std::vector<SelfTestEntry> self_tests;   // newest first
    std::optional<bool> self_test_supported; // ATA capability bit, if reported
    std::optional<std::uint64_t> ata_error_count;

    const Attribute* attribute(int id) const;
    bool has_health_data() const { return !attributes.empty() || nvme.has_value(); }
};

// Parses `smartctl --json` output. Tolerates missing sections.
Result<Report> parse_smartctl_json(const std::string& json_text);

enum class Health : std::uint8_t { good, warning, critical, unknown };
std::string_view health_name(Health h) noexcept;

struct Finding {
    Health severity = Health::unknown;  // good = informational
    std::string text;
};

struct Assessment {
    Health status = Health::unknown;
    std::vector<Finding> findings;      // every observation that contributed
};

// Conservative assessment (spec §5.3): PASSED alone never yields GOOD; no
// failure probabilities; every contributing observation is listed.
Assessment assess(const Report& r);

// Formats a raw value as "1234 (0x0000000004D2)".
std::string format_raw(std::uint64_t raw);

// ---- running smartctl -----------------------------------------------------

struct RunOptions {
    std::string smartctl_path;   // empty => find_smartctl()
    std::chrono::seconds timeout{60};
    std::string device_type;     // passed as -d <type> when non-empty
};

// Locates smartctl: $PLATTER_SMARTCTL, then next to the executable (smartmontools/ or alongside), then PATH, then the default Windows
// install directory.
std::optional<std::string> find_smartctl();

// Translates a platter device path into the form smartctl expects
// (\\.\PhysicalDrive3 -> /dev/pd3 on Windows; unchanged on Linux).
std::string smartctl_device_arg(const std::string& device_path);

// Runs `smartctl --json=c -a <device>` (read-only query) and parses it.
// smartctl's non-zero exit status is NOT treated as an error: its bits carry
// health information and are preserved in Report::smartctl_exit_status.
Result<Report> query(const std::string& device_path, const RunOptions& opts = {});

// Fills SMART-derived capabilities into a DeviceInfo.
void apply_capabilities(const Report& r, DeviceInfo& info);

}  // namespace platter::smart
