// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platter {

enum class BusType : std::uint8_t {
    unknown,
    ata,       // SATA / PATA
    scsi,      // SAS, generic SCSI
    nvme,
    usb,
    mmc,       // SD / eMMC
    virtio,
    loop,      // Linux loop device
    raid,      // md / dm / storage spaces
    image,     // a raw image file opened by platter
};

std::string_view bus_type_name(BusType b) noexcept;

enum class MediaKind : std::uint8_t { unknown, rotational, solid_state };

std::string_view media_kind_name(MediaKind m) noexcept;

// Capabilities a device actually exposes on this platform / connection.
// Must be derived from real device and platform information, never from the
// bus type alone (spec section 4.3).
enum class Capability : std::uint32_t {
    read              = 1u << 0,
    write             = 1u << 1,
    smart             = 1u << 2,
    self_test         = 1u << 3,
    secure_erase      = 1u << 4,
    sanitize          = 1u << 5,
    trim              = 1u << 6,
    power_management  = 1u << 7,
    partition_edit    = 1u << 8,
    benchmark         = 1u << 9,
    raw_sector_access = 1u << 10,
};

class CapabilitySet {
public:
    constexpr CapabilitySet() = default;
    constexpr void set(Capability c, bool on = true) noexcept {
        if (on) bits_ |= static_cast<std::uint32_t>(c);
        else bits_ &= ~static_cast<std::uint32_t>(c);
    }
    constexpr bool has(Capability c) const noexcept {
        return (bits_ & static_cast<std::uint32_t>(c)) != 0;
    }
    constexpr std::uint32_t bits() const noexcept { return bits_; }
    std::vector<std::string_view> names() const;

private:
    std::uint32_t bits_ = 0;
};

std::string_view capability_name(Capability c) noexcept;
const std::vector<Capability>& all_capabilities();

// Tri-state for facts the platform may not be able to tell us.
enum class Tri : std::uint8_t { unknown, no, yes };
std::string_view tri_name(Tri t) noexcept;

struct DeviceInfo {
    // Platform path used to open the device: /dev/sda, \\.\PhysicalDrive0, or an image path.
    std::string path;
    // Short kernel / OS name (sda, nvme0n1, PhysicalDrive0).
    std::string name;

    std::string vendor;
    std::string model;
    std::string serial;     // never exported unless explicitly requested (spec 4.2)
    std::string firmware;
    // Best available stable identifier (WWN, EUI-64, by-id link...). May be empty.
    std::string stable_id;

    std::uint64_t capacity_bytes = 0;
    std::uint32_t logical_sector_size = 512;
    std::uint32_t physical_sector_size = 512;

    BusType bus = BusType::unknown;
    MediaKind media = MediaKind::unknown;
    Tri removable = Tri::unknown;
    Tri read_only = Tri::unknown;
    Tri trim_supported = Tri::unknown;

    // Mount points of the device itself or any of its partitions.
    std::vector<std::string> mountpoints;
    // True if a mounted filesystem is "/" (Linux) or the Windows system/boot volume.
    bool is_system_disk = false;
    // Holders such as dm-crypt, LVM or md that sit on top of the device.
    std::vector<std::string> holders;

    std::string partition_table;  // "gpt", "mbr", "none", or "" if not probed
    std::string topology;         // bus path where available

    CapabilitySet capabilities;

    std::uint64_t total_lbas() const noexcept {
        return logical_sector_size ? capacity_bytes / logical_sector_size : 0;
    }
    bool is_mounted() const noexcept { return !mountpoints.empty(); }
    // Something is using this device: mounted, held by a storage stack, or system disk.
    bool in_use() const noexcept { return is_mounted() || !holders.empty() || is_system_disk; }
};

// Human-readable capacity, e.g. "1.00 TB (931.51 GiB)".
std::string format_capacity(std::uint64_t bytes);

}  // namespace platter
