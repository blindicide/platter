// SPDX-License-Identifier: TBD
#include "platter/core/device.hpp"

#include <array>
#include <cstdio>

namespace platter {

std::string_view bus_type_name(BusType b) noexcept {
    switch (b) {
        case BusType::unknown: return "Unknown";
        case BusType::ata: return "ATA";
        case BusType::scsi: return "SCSI";
        case BusType::nvme: return "NVMe";
        case BusType::usb: return "USB";
        case BusType::mmc: return "MMC";
        case BusType::virtio: return "VirtIO";
        case BusType::loop: return "Loop";
        case BusType::raid: return "RAID/DM";
        case BusType::image: return "Image";
    }
    return "Unknown";
}

std::string_view media_kind_name(MediaKind m) noexcept {
    switch (m) {
        case MediaKind::unknown: return "Unknown";
        case MediaKind::rotational: return "HDD";
        case MediaKind::solid_state: return "SSD";
    }
    return "Unknown";
}

std::string_view tri_name(Tri t) noexcept {
    switch (t) {
        case Tri::unknown: return "Unknown";
        case Tri::no: return "No";
        case Tri::yes: return "Yes";
    }
    return "Unknown";
}

std::string_view capability_name(Capability c) noexcept {
    switch (c) {
        case Capability::read: return "READ";
        case Capability::write: return "WRITE";
        case Capability::smart: return "SMART";
        case Capability::self_test: return "SELF_TEST";
        case Capability::secure_erase: return "SECURE_ERASE";
        case Capability::sanitize: return "SANITIZE";
        case Capability::trim: return "TRIM";
        case Capability::power_management: return "POWER_MANAGEMENT";
        case Capability::partition_edit: return "PARTITION_EDIT";
        case Capability::benchmark: return "BENCHMARK";
        case Capability::raw_sector_access: return "RAW_SECTOR_ACCESS";
    }
    return "?";
}

const std::vector<Capability>& all_capabilities() {
    static const std::vector<Capability> all = {
        Capability::read,         Capability::write,          Capability::smart,
        Capability::self_test,    Capability::secure_erase,   Capability::sanitize,
        Capability::trim,         Capability::power_management, Capability::partition_edit,
        Capability::benchmark,    Capability::raw_sector_access,
    };
    return all;
}

std::vector<std::string_view> CapabilitySet::names() const {
    std::vector<std::string_view> out;
    for (auto c : all_capabilities())
        if (has(c)) out.push_back(capability_name(c));
    return out;
}

std::string format_capacity(std::uint64_t bytes) {
    static constexpr std::array<const char*, 7> dec = {"B", "kB", "MB", "GB", "TB", "PB", "EB"};
    static constexpr std::array<const char*, 7> bin = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    auto scale = [](std::uint64_t v, double base, const auto& units) {
        double d = static_cast<double>(v);
        std::size_t i = 0;
        while (d >= base && i + 1 < units.size()) {
            d /= base;
            ++i;
        }
        char buf[48];
        if (i == 0) std::snprintf(buf, sizeof buf, "%llu %s", static_cast<unsigned long long>(v), units[0]);
        else std::snprintf(buf, sizeof buf, "%.2f %s", d, units[i]);
        return std::string(buf);
    };
    if (bytes < 1000) return scale(bytes, 1000.0, dec);
    return scale(bytes, 1000.0, dec) + " (" + scale(bytes, 1024.0, bin) + ")";
}

}  // namespace platter
