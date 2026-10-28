// SPDX-License-Identifier: TBD
//
// Read-only MBR / GPT inspection. Writing partition tables arrives in v0.5
// and will live behind the destructive-operation safety pipeline.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "platter/core/block_device.hpp"
#include "platter/core/error.hpp"

namespace platter::partitions {

struct Guid {
    std::array<std::uint8_t, 16> bytes{};  // on-disk (mixed-endian) order

    bool is_zero() const noexcept;
    // Canonical text form, e.g. C12A7328-F81F-11D2-BA4B-00A0C93EC93B.
    std::string to_string() const;
    static std::optional<Guid> parse(const std::string& text);
    static Guid from_disk(const std::uint8_t* p) noexcept;
    bool operator==(const Guid&) const = default;
};

enum class TableType : std::uint8_t { none, mbr, gpt };
std::string_view table_type_name(TableType t) noexcept;

struct Partition {
    std::uint32_t number = 0;        // 1-based; MBR logicals start at 5
    std::uint64_t first_lba = 0;
    std::uint64_t last_lba = 0;      // inclusive
    std::uint64_t size_bytes = 0;

    // MBR fields
    std::uint8_t mbr_type = 0;
    bool bootable = false;
    bool extended_container = false; // the 0x05/0x0F entry itself
    bool logical = false;

    // GPT fields
    Guid type_guid;
    Guid unique_guid;
    std::uint64_t attributes = 0;
    std::string name;                // UTF-8, converted from UTF-16LE

    std::string type_name;           // human readable
    bool aligned_1mib = false;

    std::uint64_t lba_count() const noexcept { return last_lba - first_lba + 1; }
};

struct GptHeader {
    std::uint64_t header_lba = 0;
    std::uint32_t revision = 0;
    std::uint32_t header_size = 0;
    bool header_crc_ok = false;
    std::uint64_t alternate_lba = 0;
    std::uint64_t first_usable_lba = 0;
    std::uint64_t last_usable_lba = 0;
    Guid disk_guid;
    std::uint64_t entries_lba = 0;
    std::uint32_t entry_count = 0;
    std::uint32_t entry_size = 0;
    bool entries_crc_ok = false;
};

struct PartitionTable {
    TableType type = TableType::none;
    std::uint32_t sector_size = 512;
    std::uint64_t total_lbas = 0;

    std::uint32_t mbr_disk_signature = 0;
    bool protective_mbr = false;     // GPT disks: is LBA 0 a proper 0xEE protective MBR?

    std::optional<GptHeader> gpt_primary;
    std::optional<GptHeader> gpt_backup;
    bool used_backup_gpt = false;

    std::vector<Partition> partitions;
    // Non-fatal problems: CRC mismatches, overlaps, out-of-bounds entries, ...
    std::vector<std::string> warnings;
};

// Reads and interprets the partition table of `dev`. Never writes.
Result<PartitionTable> read_partition_table(IBlockDevice& dev);

std::string mbr_type_name(std::uint8_t type);
std::string gpt_type_name(const Guid& type);

}  // namespace platter::partitions
