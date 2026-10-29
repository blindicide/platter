// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Hex sector inspector (spec §13): classic offset / hex / ASCII rendering,
// byte-pattern search over a device or image, and read-only interpreters for
// the structures people most often need to look at (MBR/EBR, GPT header and
// entries, FAT, exFAT and NTFS boot sectors, NTFS MFT records and ext2/3/4
// superblocks).
//
// Nothing here writes. Interpreters never trust the data: every field is
// bounds-checked against the bytes actually supplied and inconsistencies are
// reported as notes instead of being "fixed".
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "platter/core/block_device.hpp"
#include "platter/core/error.hpp"

namespace platter::inspect {

// ------------------------------------------------------------------ hex view

struct HexLayout {
    std::uint32_t bytes_per_row = 16;
    static constexpr std::uint32_t kOffsetDigits = 12;

    // Character columns inside one rendered row (see hex_dump()).
    std::uint32_t hex_column(std::uint32_t i) const noexcept;    // first char of byte i's hex pair
    std::uint32_t ascii_column(std::uint32_t i) const noexcept;  // char of byte i in the ASCII gutter
    std::uint32_t row_width() const noexcept;
};

// "000000000000  55 AA 00 ...  |U...|", one line per row, '\n' terminated.
// `base_offset` is the byte offset of data[0] on the device.
std::string hex_dump(std::span<const std::uint8_t> data, std::uint64_t base_offset, const HexLayout& layout = {});

// ------------------------------------------------------------------- search

struct Pattern {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> mask;      // 0xFF: byte must match, 0x00: wildcard (same size as bytes)
    bool case_insensitive = false;       // ASCII letters only
    bool empty() const noexcept { return bytes.empty(); }
    std::size_t size() const noexcept { return bytes.size(); }
};

// "55 AA ?? 0F", "55aa", "0x55 0xAA" — pairs of hex digits, '??' is a wildcard.
Result<Pattern> parse_hex_pattern(std::string_view text);
Pattern ascii_pattern(std::string_view text, bool case_insensitive = false);
// UTF-8 text encoded as UTF-16LE (how NTFS and exFAT store names).
Result<Pattern> utf16le_pattern(std::string_view utf8, bool case_insensitive = false);

bool matches_at(const Pattern& p, const std::uint8_t* data) noexcept;
std::optional<std::size_t> find_in_buffer(std::span<const std::uint8_t> buf, const Pattern& p, std::size_t from = 0);

struct SearchOutcome {
    std::vector<std::uint64_t> hits;     // byte offsets, ascending
    std::uint64_t scanned_bytes = 0;
    std::uint64_t unreadable_sectors = 0;  // skipped; a match spanning one is not reported
    bool complete = false;               // false: stopped by the callback or max_hits
    bool truncated = false;              // max_hits reached
};

// Return false from the callback to stop. It is called after every block with
// the number of bytes scanned so far.
using SearchProgress = std::function<bool(std::uint64_t scanned_bytes)>;

// Searches [first_byte, end_byte) (end clamped to the capacity). Reads are
// sector-aligned; matches may span block boundaries.
Result<SearchOutcome> search_device(IBlockDevice& dev, const Pattern& p, std::uint64_t first_byte,
                                    std::uint64_t end_byte, std::size_t max_hits = 1000,
                                    const SearchProgress& progress = {}, std::size_t block_bytes = 1u << 20);

// ------------------------------------------------------------- interpreters

enum class StructKind : std::uint8_t {
    mbr,
    gpt_header,
    gpt_entries,
    fat_boot,
    exfat_boot,
    ntfs_boot,
    ntfs_mft_record,
    ext_superblock,
};
inline constexpr int kStructKindCount = 8;
std::string_view struct_kind_name(StructKind k) noexcept;
std::optional<StructKind> parse_struct_kind(std::string_view s) noexcept;

struct Field {
    std::uint32_t offset = 0;   // from the start of the structure
    std::uint32_t size = 0;
    std::string name;
    std::string value;
    std::string note;           // e.g. "ok", "mismatch", "= 512 bytes"
};

struct Structure {
    StructKind kind = StructKind::mbr;
    std::string title;
    std::uint32_t length = 0;               // bytes the structure covers within the data
    std::vector<Field> fields;
    std::vector<std::string> notes;         // inconsistencies and observations
};

struct Detected {
    StructKind kind;
    std::uint32_t offset;                   // where the structure starts within the supplied data
};

// Looks at the bytes of one or more sectors starting at `lba` and decides what
// they are, if anything we know. `data` must be at least one sector.
std::optional<Detected> detect(std::span<const std::uint8_t> data, std::uint64_t lba, std::uint32_t sector_size);

// `data` starts at the first byte of the structure. Fails only when there are
// too few bytes to say anything useful.
Result<Structure> interpret(StructKind kind, std::span<const std::uint8_t> data, std::uint32_t sector_size);

// Plain-text rendering used by the CLI: one "offset size name value" line per field.
std::string structure_to_text(const Structure& s);

// Seconds since 1970 as "2024-05-17 08:30:00 UTC".
std::string unix_time_text(std::uint64_t seconds);

}  // namespace platter::inspect
