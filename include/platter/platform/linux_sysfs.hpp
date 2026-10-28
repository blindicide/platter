// SPDX-License-Identifier: TBD
//
// Linux sysfs enumeration with injectable roots, so it can be unit-tested
// against a fake tree (tests/fixtures/sysfs) instead of the real /sys.
#pragma once

#ifdef __linux__

#include <string>
#include <vector>

#include "platter/core/device.hpp"
#include "platter/core/error.hpp"

namespace platter::sysfs {

struct Roots {
    std::string sys_block = "/sys/block";
    std::string dev = "/dev";
    std::string mounts = "/proc/self/mounts";
    std::string swaps = "/proc/swaps";
    std::string disk_by_id = "/dev/disk/by-id";
};

struct MountEntry {
    std::string source;      // /dev/sda1
    std::string mountpoint;  // /boot
    std::string fstype;
};

// Parses /proc/self/mounts-format text. Octal escapes (\040) are decoded.
std::vector<MountEntry> parse_mounts(const std::string& text);
// Parses /proc/swaps-format text; returns the swap device paths.
std::vector<std::string> parse_swaps(const std::string& text);

// Names in sys_block that should be skipped (ram*, zram*, empty loops, ...).
bool should_skip(const std::string& name, std::uint64_t size_bytes);

Result<std::vector<DeviceInfo>> enumerate(const Roots& roots);
Result<DeviceInfo> describe(const Roots& roots, const std::string& name);

}  // namespace platter::sysfs

#endif
