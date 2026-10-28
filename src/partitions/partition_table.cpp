// SPDX-License-Identifier: TBD
#include "platter/partitions/partition_table.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

#include "platter/core/bytes.hpp"
#include "platter/core/range.hpp"

namespace platter::partitions {

// ------------------------------------------------------------------ Guid

bool Guid::is_zero() const noexcept {
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0; });
}

std::string Guid::to_string() const {
    char buf[37];
    const auto* b = bytes.data();
    std::snprintf(buf, sizeof buf, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", le32(b),
                  le16(b + 4), le16(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return buf;
}

std::optional<Guid> Guid::parse(const std::string& text) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        return std::nullopt;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::array<std::uint8_t, 16> be{};  // textual (big-endian) order
    std::size_t j = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '-') { ++i; continue; }
        const int hi = hex(text[i]), lo = hex(text[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        be[j++] = static_cast<std::uint8_t>(hi * 16 + lo);
        i += 2;
    }
    Guid g;
    // First three fields are stored little-endian on disk.
    g.bytes = {be[3], be[2], be[1], be[0], be[5], be[4], be[7], be[6],
               be[8], be[9], be[10], be[11], be[12], be[13], be[14], be[15]};
    return g;
}

Guid Guid::from_disk(const std::uint8_t* p) noexcept {
    Guid g;
    std::memcpy(g.bytes.data(), p, 16);
    return g;
}

std::string_view table_type_name(TableType t) noexcept {
    switch (t) {
        case TableType::none: return "none";
        case TableType::mbr: return "mbr";
        case TableType::gpt: return "gpt";
    }
    return "?";
}

// ------------------------------------------------------------------ type names

std::string mbr_type_name(std::uint8_t type) {
    switch (type) {
        case 0x00: return "Empty";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 <32M";
        case 0x05: return "Extended (CHS)";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS/exFAT/HPFS";
        case 0x0B: return "FAT32 (CHS)";
        case 0x0C: return "FAT32 (LBA)";
        case 0x0E: return "FAT16 (LBA)";
        case 0x0F: return "Extended (LBA)";
        case 0x11: return "Hidden FAT12";
        case 0x12: return "Diagnostics/Recovery";
        case 0x17: return "Hidden NTFS";
        case 0x1B: return "Hidden FAT32";
        case 0x1C: return "Hidden FAT32 (LBA)";
        case 0x27: return "Windows Recovery";
        case 0x42: return "Windows Dynamic";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux";
        case 0x85: return "Linux extended";
        case 0x8E: return "Linux LVM";
        case 0xA5: return "FreeBSD";
        case 0xAF: return "Apple HFS/HFS+";
        case 0xEE: return "GPT protective";
        case 0xEF: return "EFI System";
        case 0xFD: return "Linux RAID";
        default: break;
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "Unknown (0x%02X)", type);
    return buf;
}

std::string gpt_type_name(const Guid& type) {
    struct Known { const char* guid; const char* name; };
    static const Known known[] = {
        {"C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "EFI System"},
        {"21686148-6449-6E6F-744E-656564454649", "BIOS boot"},
        {"E3C9E316-0B5C-4DB8-817D-F92DF00215AE", "Microsoft reserved"},
        {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data"},
        {"DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", "Windows Recovery"},
        {"5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", "Windows LDM metadata"},
        {"AF9B60A0-1431-4F62-BC68-3311714A69AD", "Windows LDM data"},
        {"E75CAF8F-F680-4CEE-AFA3-B001E56EFC2D", "Windows Storage Spaces"},
        {"0FC63DAF-8483-4772-8E79-3D69D8477DE4", "Linux filesystem"},
        {"4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709", "Linux root (x86-64)"},
        {"B921B045-1DF0-41C3-AF44-4C6F280D3FAE", "Linux root (ARM64)"},
        {"933AC7E1-2EB4-4F13-B844-0E14E2AEF915", "Linux home"},
        {"BC13C2FF-59E6-4262-A352-B275FD6F7172", "Linux extended boot"},
        {"0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", "Linux swap"},
        {"E6D6D379-F507-44C2-A23C-238F2A3DF928", "Linux LVM"},
        {"A19D880F-05FC-4D3B-A006-743F0F84911E", "Linux RAID"},
        {"CA7D7CCB-63ED-4C53-861C-1742536059CC", "Linux LUKS"},
        {"7C3457EF-0000-11AA-AA11-00306543ECAC", "Apple APFS"},
        {"48465300-0000-11AA-AA11-00306543ECAC", "Apple HFS+"},
        {"516E7CB4-6ECF-11D6-8FF8-00022D09712B", "FreeBSD data"},
    };
    if (type.is_zero()) return "Unused";
    const auto s = type.to_string();
    for (const auto& k : known)
        if (s == k.guid) return k.name;
    return "Unknown";
}

// ------------------------------------------------------------------ parsing

namespace {

constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint32_t kMaxGptEntryBytes = 4 * 1024 * 1024;  // sanity cap
constexpr int kMaxLogicalPartitions = 256;

bool is_extended(std::uint8_t t) { return t == 0x05 || t == 0x0F || t == 0x85; }

std::string utf16le_to_utf8(const std::uint8_t* p, std::size_t units) {
    std::string out;
    for (std::size_t i = 0; i < units; ++i) {
        std::uint32_t cp = le16(p + 2 * i);
        if (cp == 0) break;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units) {
            const std::uint32_t lo = le16(p + 2 * (i + 1));
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

struct Reader {
    IBlockDevice& dev;
    std::uint32_t ss;
    std::uint64_t total;

    Result<std::vector<std::uint8_t>> sectors(std::uint64_t lba, std::uint64_t count) {
        if (auto s = validate_lba_range({lba, count}, total); !s) return s.error();
        auto bytes = checked_mul(count, ss);
        if (!bytes || *bytes > 64 * kMiB) return Error{Errc::out_of_range, "metadata read too large"};
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(*bytes));
        if (auto s = read_sectors(dev, lba, count, buf.data(), buf.size()); !s) return s.error();
        return buf;
    }
};

void finish_partition(Partition& p, std::uint32_t ss) {
    p.size_bytes = checked_mul(p.lba_count(), ss).value_or(0);
    p.aligned_1mib = checked_mul(p.first_lba, ss).value_or(1) % kMiB == 0;
}

// Parses a GPT header sector. Returns nullopt if the signature is absent.
std::optional<GptHeader> parse_gpt_header(Reader& rd, std::uint64_t lba, const std::vector<std::uint8_t>& sec,
                                          std::vector<std::string>& warnings, const char* which) {
    if (sec.size() < 92 || std::memcmp(sec.data(), "EFI PART", 8) != 0) return std::nullopt;
    GptHeader h;
    h.header_lba = lba;
    h.revision = le32(&sec[8]);
    h.header_size = le32(&sec[12]);
    const std::uint32_t stored_crc = le32(&sec[16]);
    const std::uint64_t my_lba = le64(&sec[24]);
    h.alternate_lba = le64(&sec[32]);
    h.first_usable_lba = le64(&sec[40]);
    h.last_usable_lba = le64(&sec[48]);
    h.disk_guid = Guid::from_disk(&sec[56]);
    h.entries_lba = le64(&sec[72]);
    h.entry_count = le32(&sec[80]);
    h.entry_size = le32(&sec[84]);
    const std::uint32_t stored_entries_crc = le32(&sec[88]);

    const std::string tag = std::string(which) + " GPT header at LBA " + std::to_string(lba);
    if (h.header_size < 92 || h.header_size > rd.ss) {
        warnings.push_back(tag + ": invalid header size " + std::to_string(h.header_size));
        return h;
    }
    std::vector<std::uint8_t> copy(sec.begin(), sec.begin() + h.header_size);
    put_le32(&copy[16], 0);
    h.header_crc_ok = crc32(copy) == stored_crc;
    if (!h.header_crc_ok) warnings.push_back(tag + ": header CRC32 mismatch");
    if (my_lba != lba) {
        warnings.push_back(tag + ": header claims to be at LBA " + std::to_string(my_lba));
        h.header_crc_ok = false;
    }
    if (h.entry_size < 128 || h.entry_size % 8 != 0) {
        warnings.push_back(tag + ": invalid entry size " + std::to_string(h.entry_size));
        return h;
    }
    const auto table_bytes = checked_mul(h.entry_count, h.entry_size);
    if (!table_bytes || *table_bytes > kMaxGptEntryBytes || h.entry_count == 0) {
        warnings.push_back(tag + ": implausible entry array (" + std::to_string(h.entry_count) +
                           " x " + std::to_string(h.entry_size) + ")");
        return h;
    }
    const std::uint64_t entry_sectors = (*table_bytes + rd.ss - 1) / rd.ss;
    auto entries = rd.sectors(h.entries_lba, entry_sectors);
    if (!entries) {
        warnings.push_back(tag + ": cannot read entry array: " + entries.error().describe());
        return h;
    }
    h.entries_crc_ok =
        crc32(std::span<const std::uint8_t>(entries->data(), static_cast<std::size_t>(*table_bytes))) ==
        stored_entries_crc;
    if (!h.entries_crc_ok) warnings.push_back(tag + ": partition entry array CRC32 mismatch");
    return h;
}

Result<std::vector<Partition>> read_gpt_entries(Reader& rd, const GptHeader& h) {
    const std::uint64_t table_bytes = std::uint64_t{h.entry_count} * h.entry_size;
    auto entries = rd.sectors(h.entries_lba, (table_bytes + rd.ss - 1) / rd.ss);
    if (!entries) return entries.error();
    std::vector<Partition> parts;
    for (std::uint32_t i = 0; i < h.entry_count; ++i) {
        const std::uint8_t* e = entries->data() + std::size_t{i} * h.entry_size;
        Partition p;
        p.type_guid = Guid::from_disk(e);
        if (p.type_guid.is_zero()) continue;
        p.number = i + 1;
        p.unique_guid = Guid::from_disk(e + 16);
        p.first_lba = le64(e + 32);
        p.last_lba = le64(e + 40);
        p.attributes = le64(e + 48);
        p.name = utf16le_to_utf8(e + 56, 36);
        p.type_name = gpt_type_name(p.type_guid);
        if (p.last_lba < p.first_lba) p.last_lba = p.first_lba;  // flagged by validate below
        finish_partition(p, rd.ss);
        parts.push_back(std::move(p));
    }
    return parts;
}

Status read_ebr_chain(Reader& rd, const Partition& container, std::vector<Partition>& out,
                      std::vector<std::string>& warnings) {
    std::set<std::uint64_t> visited;
    std::uint64_t ebr_lba = container.first_lba;
    std::uint32_t number = 5;
    for (int guard = 0; guard < kMaxLogicalPartitions; ++guard) {
        if (!visited.insert(ebr_lba).second) {
            warnings.push_back("EBR chain loops back to LBA " + std::to_string(ebr_lba));
            return {};
        }
        if (ebr_lba < container.first_lba || ebr_lba > container.last_lba) {
            warnings.push_back("EBR at LBA " + std::to_string(ebr_lba) + " lies outside the extended partition");
            return {};
        }
        auto sec = rd.sectors(ebr_lba, 1);
        if (!sec) {
            warnings.push_back("cannot read EBR at LBA " + std::to_string(ebr_lba) + ": " + sec.error().describe());
            return {};
        }
        const auto& s = *sec;
        if (s[510] != 0x55 || s[511] != 0xAA) {
            warnings.push_back("EBR at LBA " + std::to_string(ebr_lba) + " has no 0x55AA signature");
            return {};
        }
        const std::uint8_t* e0 = &s[446];
        const std::uint8_t* e1 = &s[462];
        if (e0[4] != 0 && le32(e0 + 12) != 0) {
            Partition p;
            p.number = number++;
            p.logical = true;
            p.mbr_type = e0[4];
            p.bootable = (e0[0] & 0x80) != 0;
            p.first_lba = ebr_lba + le32(e0 + 8);
            p.last_lba = p.first_lba + le32(e0 + 12) - 1;
            p.type_name = mbr_type_name(p.mbr_type);
            finish_partition(p, rd.ss);
            out.push_back(std::move(p));
        }
        if (e1[4] == 0 || le32(e1 + 8) == 0) return {};
        ebr_lba = container.first_lba + le32(e1 + 8);
    }
    warnings.push_back("more than " + std::to_string(kMaxLogicalPartitions) + " logical partitions; stopped");
    return {};
}

void validate_layout(PartitionTable& t) {
    for (const auto& p : t.partitions) {
        const auto id = "partition " + std::to_string(p.number);
        if (p.first_lba == 0) t.warnings.push_back(id + " starts at LBA 0");
        if (p.last_lba >= t.total_lbas)
            t.warnings.push_back(id + " extends past the end of the device (last LBA " +
                                 std::to_string(p.last_lba) + ", device has " + std::to_string(t.total_lbas) + ")");
        if (t.type == TableType::gpt) {
            const auto& h = t.used_backup_gpt ? t.gpt_backup : t.gpt_primary;
            if (h && (p.first_lba < h->first_usable_lba || p.last_lba > h->last_usable_lba))
                t.warnings.push_back(id + " lies outside the GPT usable area");
        }
    }
    // Overlap check (extended containers legitimately contain their logicals).
    std::vector<const Partition*> v;
    for (const auto& p : t.partitions)
        if (!p.extended_container) v.push_back(&p);
    std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->first_lba < b->first_lba; });
    for (std::size_t i = 1; i < v.size(); ++i)
        if (v[i]->first_lba <= v[i - 1]->last_lba)
            t.warnings.push_back("partitions " + std::to_string(v[i - 1]->number) + " and " +
                                 std::to_string(v[i]->number) + " overlap");
}

}  // namespace

Result<PartitionTable> read_partition_table(IBlockDevice& dev) {
    PartitionTable t;
    t.sector_size = dev.sector_size();
    t.total_lbas = dev.info().total_lbas();
    if (t.total_lbas == 0) return t;
    Reader rd{dev, t.sector_size, t.total_lbas};

    auto mbr = rd.sectors(0, 1);
    if (!mbr) return mbr.error();
    const bool mbr_sig = (*mbr)[510] == 0x55 && (*mbr)[511] == 0xAA;

    // ---- GPT (probe even without a valid MBR signature; report it as a warning)
    if (t.total_lbas >= 3) {
        auto sec = rd.sectors(1, 1);
        if (!sec) return sec.error();
        auto primary = parse_gpt_header(rd, 1, *sec, t.warnings, "primary");
        std::optional<GptHeader> backup;
        if (primary) {
            std::uint64_t alt = primary->alternate_lba;
            if (alt == 0 || alt >= t.total_lbas) alt = t.total_lbas - 1;
            if (alt != t.total_lbas - 1)
                t.warnings.push_back("primary GPT points to backup at LBA " + std::to_string(alt) +
                                     " but the device ends at LBA " + std::to_string(t.total_lbas - 1));
            if (auto b = rd.sectors(alt, 1)) backup = parse_gpt_header(rd, alt, *b, t.warnings, "backup");
        } else if (auto b = rd.sectors(t.total_lbas - 1, 1)) {
            backup = parse_gpt_header(rd, t.total_lbas - 1, *b, t.warnings, "backup");
        }

        if (primary || backup) {
            t.type = TableType::gpt;
            t.gpt_primary = primary;
            t.gpt_backup = backup;
            if (!backup) t.warnings.push_back("backup GPT header is missing");

            auto valid = [](const std::optional<GptHeader>& h) { return h && h->header_crc_ok && h->entries_crc_ok; };
            const GptHeader* use = nullptr;
            if (valid(primary)) {
                use = &*primary;
            } else if (valid(backup)) {
                use = &*backup;
                t.used_backup_gpt = true;
                t.warnings.push_back("primary GPT is damaged; partitions were read from the backup GPT");
            } else {
                t.warnings.push_back("no valid GPT copy found; partition entries were not trusted");
            }

            const std::uint8_t* pe = mbr->data() + 446;
            t.protective_mbr = mbr_sig && pe[4] == 0xEE && le32(pe + 8) == 1;
            if (!t.protective_mbr) t.warnings.push_back("GPT disk lacks a valid protective MBR");

            if (use) {
                auto parts = read_gpt_entries(rd, *use);
                if (!parts) return parts.error();
                t.partitions = std::move(parts).value();
            }
            validate_layout(t);
            return t;
        }
    }

    // ---- MBR
    if (!mbr_sig) return t;  // unpartitioned (or unknown scheme)
    t.type = TableType::mbr;
    t.mbr_disk_signature = le32(mbr->data() + 440);
    std::vector<Partition> extended;
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint8_t* e = mbr->data() + 446 + 16 * i;
        const std::uint8_t type = e[4];
        const std::uint32_t start = le32(e + 8), count = le32(e + 12);
        if (type == 0 || count == 0) continue;
        if ((e[0] & 0x7F) != 0) t.warnings.push_back("MBR entry " + std::to_string(i + 1) + " has invalid status byte");
        if (type == 0xEE) t.warnings.push_back("MBR contains a GPT protective entry but no GPT was found");
        Partition p;
        p.number = i + 1;
        p.mbr_type = type;
        p.bootable = (e[0] & 0x80) != 0;
        p.first_lba = start;
        p.last_lba = std::uint64_t{start} + count - 1;
        p.extended_container = is_extended(type);
        p.type_name = mbr_type_name(type);
        finish_partition(p, t.sector_size);
        if (p.extended_container) extended.push_back(p);
        t.partitions.push_back(std::move(p));
    }
    if (extended.size() > 1) t.warnings.push_back("more than one extended partition in MBR");
    for (const auto& c : extended)
        if (auto s = read_ebr_chain(rd, c, t.partitions, t.warnings); !s) return s.error();
    validate_layout(t);
    return t;
}

}  // namespace platter::partitions
