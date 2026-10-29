// SPDX-License-Identifier: TBD
//
// Partition table editing (spec §10): an in-memory plan that is validated,
// diffed against what is on disk, encoded to exact sector images, and finally
// written through a safety::AuthorizedTarget confirmed for exactly the
// metadata ranges the plan touches.
//
// What this does NOT do: move or resize file systems, or move partition data.
// Changing a partition's bounds only rewrites the table.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "platter/core/range.hpp"
#include "platter/core/safety.hpp"
#include "platter/partitions/partition_table.hpp"

namespace platter::partitions {

constexpr std::uint64_t kMiB1 = 1024 * 1024;

struct PartitionSpec {
    std::uint64_t first_lba = 0;
    std::uint64_t lba_count = 0;

    // MBR
    std::uint8_t mbr_type = 0x83;
    bool bootable = false;
    bool logical = false;   // lives inside the extended container
    bool extended = false;  // the extended container itself (type 0x0F)

    // GPT
    Guid type_guid;                    // zero = "Linux filesystem"
    std::optional<Guid> unique_guid;   // default: random
    std::uint64_t attributes = 0;
    std::string name;                  // up to 36 UTF-16 units
};

struct SectorWrite {
    std::uint64_t lba = 0;
    std::vector<std::uint8_t> data;  // whole sectors
};

// Well-known types by short name: "linux", "esp", "swap", "ntfs", "fat32",
// "exfat", "lvm", "raid", "msr", "recovery", "bios-boot", "home", "root".
struct TypeAlias {
    const char* name;
    std::uint8_t mbr_type;
    const char* gpt_guid;
    const char* description;
};
const std::vector<TypeAlias>& type_aliases();
std::optional<TypeAlias> find_type_alias(const std::string& name);

Guid random_guid();

class PartitionPlan {
public:
    // A fresh, empty table (destroys whatever was there when written).
    static Result<PartitionPlan> blank(TableType type, std::uint32_t sector_size, std::uint64_t total_lbas);
    // Starts from what is on the device. `sector0` is the current LBA 0 (its
    // boot code is preserved for MBR); may be empty.
    static Result<PartitionPlan> from_table(const PartitionTable& table, const std::vector<std::uint8_t>& sector0);
    static Result<PartitionPlan> from_device(IBlockDevice& dev);

    TableType type() const noexcept { return type_; }
    std::uint32_t sector_size() const noexcept { return ss_; }
    std::uint64_t total_lbas() const noexcept { return total_; }
    const std::vector<Partition>& partitions() const noexcept { return parts_; }
    const Partition* find(std::uint32_t number) const;

    // The region partitions may occupy: GPT first..last usable, MBR 1..total-1.
    LbaRange usable() const;
    // Free space between partitions. With in_extended, the free space inside
    // the MBR extended container (for logical partitions) instead.
    std::vector<LbaRange> free_spans(bool in_extended = false) const;
    // Largest aligned start inside a free span (1 MiB by default).
    std::optional<std::uint64_t> first_fit_start(std::uint64_t align_bytes = kMiB1, bool in_extended = false) const;

    Result<std::uint32_t> add(const PartitionSpec& spec);
    Status remove(std::uint32_t number);
    Status set_mbr_type(std::uint32_t number, std::uint8_t type);
    Status set_type_guid(std::uint32_t number, const Guid& type);
    Status set_name(std::uint32_t number, const std::string& name);
    Status set_bootable(std::uint32_t number, bool on);
    Status set_attributes(std::uint32_t number, std::uint64_t attrs);
    // New bounds only; data is not moved and no file system is resized.
    Status resize(std::uint32_t number, std::uint64_t first_lba, std::uint64_t last_lba);

    // Problems make the plan unwritable; warnings are shown but allowed.
    std::vector<std::string> problems() const;
    std::vector<std::string> warnings() const;

    // Exact sector images for the whole table (all copies, all EBRs).
    Result<std::vector<SectorWrite>> encode() const;
    // The LBA ranges encode() writes, merged. Use as the confirmed ranges.
    Result<std::vector<LbaRange>> metadata_ranges() const;

    // Human-readable list of differences against the table currently on disk.
    std::string diff(const PartitionTable& before) const;

private:
    PartitionPlan() = default;
    void renumber();
    std::uint64_t gpt_entry_sectors() const;
    Result<std::vector<SectorWrite>> encode_gpt() const;
    Result<std::vector<SectorWrite>> encode_mbr() const;
    const Partition* container() const;

    TableType type_ = TableType::none;
    std::uint32_t ss_ = 512;
    std::uint64_t total_ = 0;
    std::vector<Partition> parts_;

    // MBR
    std::uint32_t mbr_signature_ = 0;
    std::vector<std::uint8_t> boot_code_;  // 440 bytes (or empty = zeros)
    // GPT
    Guid disk_guid_;
    std::uint32_t entry_count_ = 128;
};

// Writes the plan: refuses unless the target is a partition_edit authorization
// matching the plan's geometry, writes every region, flushes, reads each one
// back and compares. Non-fatal conditions (the OS could not re-read the table)
// are appended to `notes`.
Status apply_plan(safety::AuthorizedTarget& target, const PartitionPlan& plan, std::vector<std::string>* notes = nullptr);

}  // namespace platter::partitions
