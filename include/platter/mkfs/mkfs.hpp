// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Native "quick" formatting of FAT32 and exFAT volumes (spec §10).
//
// A format writes only file system metadata (boot sectors, FATs, allocation
// bitmap, root directory) inside the confirmed range. It does NOT erase the
// data area: files that were there stay recoverable until overwritten. Use
// the erase tab first if that matters.
//
// ext4, NTFS and the like are not implemented natively; use the platform's
// own tools for them.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "platter/core/operation.hpp"
#include "platter/core/safety.hpp"

namespace platter::mkfs {

enum class FsType : std::uint8_t { fat32, exfat };
std::string_view fs_name(FsType t) noexcept;

struct FormatConfig {
    FsType type = FsType::fat32;
    std::string label;                 // FAT32: up to 11 ASCII chars (uppercased); exFAT: up to 11 characters
    std::uint32_t cluster_bytes = 0;   // 0 = automatic
    std::uint32_t volume_id = 0;       // 0 = random
};

// Where everything goes, in sectors relative to the start of the volume.
struct Layout {
    FsType type = FsType::fat32;
    std::uint32_t sector_size = 512;
    std::uint64_t total_sectors = 0;
    std::uint32_t sectors_per_cluster = 0;
    std::uint64_t cluster_count = 0;
    std::uint32_t reserved_sectors = 0;   // FAT32: 32; exFAT: unused
    std::uint64_t fat_offset = 0;         // exFAT: sectors; FAT32: == reserved_sectors
    std::uint64_t fat_sectors = 0;        // per FAT
    std::uint32_t fat_count = 2;
    std::uint64_t data_offset = 0;        // first sector of cluster 2
    // exFAT
    std::uint32_t bitmap_clusters = 0, upcase_clusters = 0, root_clusters = 1;
    std::uint32_t bitmap_first = 2, upcase_first = 0, root_first = 0;
    std::uint64_t bitmap_bytes = 0;
    std::uint32_t upcase_bytes = 0;
};

// Pure geometry: refuses volumes too small or too large for the file system.
Result<Layout> compute_layout(FsType type, std::uint64_t total_sectors, std::uint32_t sector_size,
                              std::uint32_t cluster_bytes);

struct FormatResult {
    FormatConfig config;
    Layout layout;
    std::string device_model;
    std::uint64_t first_lba = 0;
    std::uint64_t sectors = 0;
    std::uint32_t volume_id = 0;
    OperationState state = OperationState::created;
    bool verified = false;       // metadata read back and compared
    std::string failure;
    std::vector<std::string> warnings;
};

std::string format_to_text(const FormatResult& r);

class Format final : public Operation {
public:
    // The authorization must be kind filesystem_create with exactly one range,
    // which is the volume.
    Format(safety::AuthorizedTarget target, FormatConfig config);
    ~Format() override;

    FormatResult result() const;

protected:
    Status do_validate() override;
    Status do_run(OperationControl& control) override;

private:
    Status write_region(OperationControl& control, std::uint64_t sector, const std::uint8_t* data, std::size_t bytes);
    Status zero_region(OperationControl& control, std::uint64_t sector, std::uint64_t sectors);
    Status build_fat32(OperationControl& control);
    Status build_exfat(OperationControl& control);
    Status verify(const std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>>& regions);

    safety::AuthorizedTarget target_;
    FormatConfig config_;
    Layout layout_;
    std::uint64_t first_lba_ = 0;
    std::uint32_t volume_id_ = 0;
    mutable std::mutex m_;
    FormatResult r_;
    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> to_verify_;
};

}  // namespace platter::mkfs
