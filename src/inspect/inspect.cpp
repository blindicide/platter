// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/inspect/inspect.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "platter/core/bytes.hpp"
#include "platter/partitions/partition_table.hpp"

namespace platter::inspect {

namespace {

using Bytes = std::span<const std::uint8_t>;

std::string hexs(std::uint64_t v, int width = 0) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%0*llX", width, static_cast<unsigned long long>(v));
    return buf;
}
std::string decs(std::uint64_t v) { return std::to_string(v); }
std::string sdecs(std::int64_t v) { return std::to_string(v); }

std::string hex_bytes(const std::uint8_t* p, std::size_t n) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

std::string printable(const std::uint8_t* p, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        if (p[i] == 0) break;
        s += (p[i] >= 0x20 && p[i] < 0x7F) ? static_cast<char>(p[i]) : '.';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string quoted(const std::uint8_t* p, std::size_t n) { return "\"" + printable(p, n) + "\""; }

std::string bytes_text(std::uint64_t b) {
    static const char* u[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double v = static_cast<double>(b);
    int i = 0;
    while (v >= 1024.0 && i < 5) {
        v /= 1024.0;
        ++i;
    }
    char buf[48];
    if (i == 0)
        std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(b));
    else
        std::snprintf(buf, sizeof buf, "%.2f %s", v, u[i]);
    return buf;
}

// UTF-16LE (BMP + surrogate pairs) -> UTF-8.
std::string utf16le_to_utf8(const std::uint8_t* p, std::size_t units) {
    std::string out;
    for (std::size_t i = 0; i < units; ++i) {
        std::uint32_t c = le16(p + 2 * i);
        if (c == 0) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < units) {
            const std::uint32_t lo = le16(p + 2 * (i + 1));
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

bool all_zero(const std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if (p[i]) return false;
    return true;
}

// Collects fields while keeping each one inside the supplied bytes.
class Builder {
public:
    Builder(Structure& s, Bytes data) : s_(s), d_(data) {}

    bool has(std::size_t off, std::size_t size) const { return off + size <= d_.size(); }
    const std::uint8_t* at(std::size_t off) const { return d_.data() + off; }

    void add(std::size_t off, std::size_t size, std::string name, std::string value, std::string note = {}) {
        s_.fields.push_back({static_cast<std::uint32_t>(off), static_cast<std::uint32_t>(size), std::move(name),
                             std::move(value), std::move(note)});
    }
    std::optional<std::uint64_t> u8(std::size_t off, const char* name, std::string note = {}) {
        if (!has(off, 1)) return std::nullopt;
        const auto v = *at(off);
        add(off, 1, name, decs(v) + "  (" + hexs(v, 2) + ")", std::move(note));
        return v;
    }
    std::optional<std::uint64_t> u16(std::size_t off, const char* name, std::string note = {}) {
        if (!has(off, 2)) return std::nullopt;
        const auto v = le16(at(off));
        add(off, 2, name, decs(v) + "  (" + hexs(v, 4) + ")", std::move(note));
        return v;
    }
    std::optional<std::uint64_t> u32(std::size_t off, const char* name, std::string note = {}) {
        if (!has(off, 4)) return std::nullopt;
        const auto v = le32(at(off));
        add(off, 4, name, decs(v) + "  (" + hexs(v, 8) + ")", std::move(note));
        return v;
    }
    std::optional<std::uint64_t> u64(std::size_t off, const char* name, std::string note = {}) {
        if (!has(off, 8)) return std::nullopt;
        const auto v = le64(at(off));
        add(off, 8, name, decs(v) + "  (" + hexs(v, 16) + ")", std::move(note));
        return v;
    }
    void text(std::size_t off, std::size_t size, const char* name, std::string note = {}) {
        if (!has(off, size)) return;
        add(off, size, name, quoted(at(off), size), std::move(note));
    }
    void raw(std::size_t off, std::size_t size, const char* name, std::string note = {}) {
        if (!has(off, size)) return;
        add(off, size, name, hex_bytes(at(off), size), std::move(note));
    }
    void note(std::string n) { s_.notes.push_back(std::move(n)); }

private:
    Structure& s_;
    Bytes d_;
};

// ------------------------------------------------------------ boot record ID

bool pow2(std::uint64_t v) { return v && (v & (v - 1)) == 0; }

bool valid_bps(std::uint64_t v) { return v >= 512 && v <= 4096 && pow2(v); }

bool looks_ntfs(Bytes d) { return d.size() >= 512 && std::memcmp(d.data() + 3, "NTFS    ", 8) == 0; }
bool looks_exfat(Bytes d) { return d.size() >= 512 && std::memcmp(d.data() + 3, "EXFAT   ", 8) == 0; }
bool looks_fat(Bytes d) {
    if (d.size() < 512) return false;
    if (d[0] != 0xEB && d[0] != 0xE9) return false;
    if (!valid_bps(le16(d.data() + 11))) return false;
    if (!pow2(d[13])) return false;
    return std::memcmp(d.data() + 82, "FAT32   ", 8) == 0 || std::memcmp(d.data() + 54, "FAT12   ", 8) == 0 ||
           std::memcmp(d.data() + 54, "FAT16   ", 8) == 0 || std::memcmp(d.data() + 54, "FAT     ", 8) == 0;
}

bool looks_ext(Bytes d, std::size_t off) {
    if (d.size() < off + 128) return false;
    const auto* p = d.data() + off;
    if (le16(p + 56) != 0xEF53) return false;
    return le32(p) != 0 && le32(p + 24) <= 6;
}

bool looks_gpt_entries(Bytes d) {
    if (d.size() < 128) return false;
    const auto* p = d.data();
    if (all_zero(p, 16) || all_zero(p + 16, 16)) return false;
    const auto first = le64(p + 32), last = le64(p + 40);
    if (first > last || first < 2 || last >= (1ull << 48)) return false;
    // The name is zero-padded UTF-16 text: printable units, then zeros.
    bool ended = false;
    for (int i = 0; i < 36; ++i) {
        const auto u = le16(p + 56 + 2 * i);
        if (u == 0) {
            ended = true;
        } else if (ended || u < 0x20) {
            return false;
        }
    }
    return true;
}

bool looks_mbr_entries(Bytes d) {
    if (d.size() < 512 || d[510] != 0x55 || d[511] != 0xAA) return false;
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        const auto* e = d.data() + 446 + 16 * i;
        if (e[0] != 0x00 && e[0] != 0x80) return false;
        if (e[4] != 0) any = true;
    }
    return any;
}

std::string chs_text(const std::uint8_t* p) {
    const unsigned head = p[0];
    const unsigned sector = p[1] & 0x3Fu;
    const unsigned cyl = ((p[1] & 0xC0u) << 2) | p[2];
    char buf[48];
    std::snprintf(buf, sizeof buf, "C %u / H %u / S %u", cyl, head, sector);
    return buf;
}

// ---------------------------------------------------------------- civil time

std::string civil_utc(std::uint64_t secs) {
    const std::int64_t days = static_cast<std::int64_t>(secs / 86400);
    const std::int64_t rem = static_cast<std::int64_t>(secs % 86400);
    // Howard Hinnant's days -> civil algorithm.
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t dd = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t yy = y + (m <= 2 ? 1 : 0);
    char buf[128];
    std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld %02lld:%02lld:%02lld UTC", static_cast<long long>(yy),
                  static_cast<long long>(m), static_cast<long long>(dd), static_cast<long long>(rem / 3600),
                  static_cast<long long>((rem % 3600) / 60), static_cast<long long>(rem % 60));
    return buf;
}

std::string time_field(std::uint64_t secs) { return secs == 0 ? "(never)" : civil_utc(secs); }

// ----------------------------------------------------------------------- MBR

Structure interpret_mbr(Bytes d) {
    Structure s;
    s.kind = StructKind::mbr;
    s.title = "Master / Extended Boot Record";
    s.length = static_cast<std::uint32_t>(std::min<std::size_t>(512, d.size()));
    Builder b(s, d);
    if (b.has(0, 440)) {
        std::size_t nonzero = 0;
        for (std::size_t i = 0; i < 440; ++i) nonzero += d[i] != 0;
        b.add(0, 440, "Bootstrap code", decs(nonzero) + " of 440 bytes non-zero", nonzero ? "" : "empty (no boot code)");
    }
    b.u32(440, "Disk signature");
    b.u16(444, "Reserved", d.size() >= 446 && le16(d.data() + 444) == 0x5A5A ? "0x5A5A: copy-protected" : "");
    std::uint64_t prev_end = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t base = 446 + 16 * static_cast<std::size_t>(i);
        if (!b.has(base, 16)) break;
        const auto* e = d.data() + base;
        const std::string nm = "Entry " + std::to_string(i + 1);
        if (all_zero(e, 16)) {
            b.add(base, 16, nm, "(unused)");
            continue;
        }
        const auto status = e[0];
        b.add(base, 1, nm + " status", hexs(status, 2), status == 0x80 ? "bootable" : (status == 0 ? "" : "INVALID (must be 0x00 or 0x80)"));
        b.add(base + 1, 3, nm + " first CHS", chs_text(e + 1));
        b.add(base + 4, 1, nm + " type", hexs(e[4], 2), partitions::mbr_type_name(e[4]));
        b.add(base + 5, 3, nm + " last CHS", chs_text(e + 5));
        const auto first = le32(e + 8), count = le32(e + 12);
        b.add(base + 8, 4, nm + " first LBA", decs(first));
        b.add(base + 12, 4, nm + " sectors", decs(count), bytes_text(std::uint64_t{count} * 512));
        if (status != 0 && status != 0x80) b.note("entry " + std::to_string(i + 1) + " has an invalid status byte " + hexs(status, 2));
        if (e[4] != 0 && count == 0) b.note("entry " + std::to_string(i + 1) + " has a type but no sectors");
        if (count && first < prev_end && e[4] != 0x05 && e[4] != 0x0F && e[4] != 0x85)
            b.note("entry " + std::to_string(i + 1) + " starts before the previous entry ends (overlap or out of order)");
        prev_end = std::max<std::uint64_t>(prev_end, std::uint64_t{first} + count);
        if (e[4] == 0xEE) b.note("type 0xEE: protective MBR, the real table is GPT");
    }
    if (b.has(510, 2)) {
        const auto sig = le16(d.data() + 510);
        b.add(510, 2, "Boot signature", hexs(sig, 4), sig == 0xAA55 ? "ok (55 AA)" : "INVALID (expected 55 AA)");
        if (sig != 0xAA55) b.note("boot signature is not 55 AA");
    }
    return s;
}

// ----------------------------------------------------------------------- GPT

Structure interpret_gpt_header(Bytes d) {
    Structure s;
    s.kind = StructKind::gpt_header;
    s.title = "GPT header";
    Builder b(s, d);
    b.text(0, 8, "Signature", d.size() >= 8 && std::memcmp(d.data(), "EFI PART", 8) == 0 ? "ok" : "INVALID");
    b.u32(8, "Revision", d.size() >= 12 && le32(d.data() + 8) == 0x00010000 ? "1.0" : "");
    const auto hsize = b.u32(12, "Header size");
    s.length = static_cast<std::uint32_t>(hsize && *hsize >= 92 && *hsize <= d.size() ? *hsize : std::min<std::size_t>(92, d.size()));
    if (b.has(16, 4)) {
        const auto stored = le32(d.data() + 16);
        std::string note = "not checked";
        if (hsize && *hsize >= 92 && *hsize <= d.size()) {
            std::vector<std::uint8_t> copy(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(*hsize));
            put_le32(copy.data() + 16, 0);
            const auto calc = crc32(copy);
            note = calc == stored ? "ok" : "MISMATCH (computed " + hexs(calc, 8) + ")";
            if (calc != stored) b.note("header CRC32 does not match");
        } else {
            b.note("header size is outside 92..sector size; CRC not checked");
        }
        b.add(16, 4, "Header CRC32", hexs(stored, 8), note);
    }
    b.u32(20, "Reserved");
    b.u64(24, "This header LBA");
    b.u64(32, "Backup header LBA");
    b.u64(40, "First usable LBA");
    b.u64(48, "Last usable LBA");
    if (b.has(56, 16)) b.add(56, 16, "Disk GUID", partitions::Guid::from_disk(d.data() + 56).to_string());
    b.u64(72, "Entries start LBA");
    const auto count = b.u32(80, "Entry count");
    const auto esize = b.u32(84, "Entry size", d.size() >= 88 && le32(d.data() + 84) == 128 ? "standard" : "");
    b.u32(88, "Entries CRC32", "covers the whole entry array; verify it with 'inspect gpt entries'");
    if (count && esize && *esize < 128) b.note("entry size below 128 is invalid");
    if (count && esize && (*esize & (*esize - 1)) != 0) b.note("entry size is not a power of two");
    if (count && *count > 4096) b.note("unusually large entry count");
    return s;
}

Structure interpret_gpt_entries(Bytes d) {
    Structure s;
    s.kind = StructKind::gpt_entries;
    s.title = "GPT partition entries";
    const std::size_t n = d.size() / 128;
    s.length = static_cast<std::uint32_t>(n * 128);
    Builder b(s, d);
    std::size_t used = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t base = i * 128;
        const auto* e = d.data() + base;
        if (all_zero(e, 16)) continue;
        ++used;
        const std::string nm = "Entry " + std::to_string(i + 1);
        const auto type = partitions::Guid::from_disk(e);
        b.add(base, 16, nm + " type", type.to_string(), partitions::gpt_type_name(type));
        b.add(base + 16, 16, nm + " unique GUID", partitions::Guid::from_disk(e + 16).to_string());
        const auto first = le64(e + 32), last = le64(e + 40);
        b.add(base + 32, 8, nm + " first LBA", decs(first));
        b.add(base + 40, 8, nm + " last LBA", decs(last), last >= first ? bytes_text((last - first + 1) * 512) + " at 512 B/sector" : "INVALID (last < first)");
        if (last < first) b.note(nm + ": last LBA is before first LBA");
        const auto attrs = le64(e + 48);
        std::string an;
        if (attrs & 1) an += "required ";
        if (attrs & 2) an += "no-block-IO ";
        if (attrs & 4) an += "legacy-BIOS-bootable ";
        if (!an.empty()) an.pop_back();
        b.add(base + 48, 8, nm + " attributes", hexs(attrs, 16), an);
        b.add(base + 56, 72, nm + " name", "\"" + utf16le_to_utf8(e + 56, 36) + "\"");
    }
    if (used == 0) b.note("all " + std::to_string(n) + " entries in view are empty");
    else b.note(std::to_string(used) + " of " + std::to_string(n) + " entries in view are in use (empty entries are not listed)");
    return s;
}

// ----------------------------------------------------------------------- FAT

Structure interpret_fat(Bytes d) {
    Structure s;
    s.kind = StructKind::fat_boot;
    s.title = "FAT boot sector";
    s.length = static_cast<std::uint32_t>(std::min<std::size_t>(512, d.size()));
    Builder b(s, d);
    b.raw(0, 3, "Jump instruction");
    b.text(3, 8, "OEM name");
    const auto bps = b.u16(11, "Bytes per sector");
    const auto spc = b.u8(13, "Sectors per cluster");
    const auto reserved = b.u16(14, "Reserved sectors");
    const auto nfats = b.u8(16, "Number of FATs");
    const auto root = b.u16(17, "Root directory entries");
    const auto ts16 = b.u16(19, "Total sectors (16-bit)");
    b.u8(21, "Media descriptor");
    const auto spf16 = b.u16(22, "Sectors per FAT (16-bit)");
    b.u16(24, "Sectors per track");
    b.u16(26, "Heads");
    b.u32(28, "Hidden sectors");
    const auto ts32 = b.u32(32, "Total sectors (32-bit)");
    const bool fat32 = b.has(82, 8) && std::memcmp(d.data() + 82, "FAT32   ", 8) == 0;
    std::optional<std::uint64_t> spf32;
    if (fat32) {
        spf32 = b.u32(36, "Sectors per FAT (32-bit)");
        b.u16(40, "Extended flags");
        b.u16(42, "Version");
        b.u32(44, "Root directory cluster");
        b.u16(48, "FSInfo sector");
        b.u16(50, "Backup boot sector");
        b.u8(64, "Drive number");
        b.u8(66, "Extended boot signature", d.size() > 66 && d[66] == 0x29 ? "ok" : "");
        b.u32(67, "Volume ID");
        b.text(71, 11, "Volume label");
        b.text(82, 8, "File system type");
    } else {
        b.u8(36, "Drive number");
        b.u8(38, "Extended boot signature", d.size() > 38 && d[38] == 0x29 ? "ok" : "");
        b.u32(39, "Volume ID");
        b.text(43, 11, "Volume label");
        b.text(54, 8, "File system type");
    }
    if (b.has(510, 2)) {
        const auto sig = le16(d.data() + 510);
        b.add(510, 2, "Boot signature", hexs(sig, 4), sig == 0xAA55 ? "ok (55 AA)" : "INVALID (expected 55 AA)");
        if (sig != 0xAA55) b.note("boot signature is not 55 AA");
    }

    if (bps && spc && reserved && nfats) {
        if (!valid_bps(*bps)) b.note("bytes per sector " + decs(*bps) + " is not a power of two between 512 and 4096");
        if (!pow2(*spc)) b.note("sectors per cluster is not a power of two");
        const std::uint64_t total = (ts16 && *ts16) ? *ts16 : (ts32 ? *ts32 : 0);
        const std::uint64_t spf = (spf16 && *spf16) ? *spf16 : (spf32 ? *spf32 : 0);
        if (total && spf && valid_bps(*bps) && *spc) {
            const std::uint64_t root_sectors = root ? (*root * 32 + *bps - 1) / *bps : 0;
            const std::uint64_t meta = *reserved + *nfats * spf + root_sectors;
            if (meta >= total) {
                b.note("the reserved area and FATs are larger than the volume");
            } else {
                const std::uint64_t clusters = (total - meta) / *spc;
                const char* type = clusters < 4085 ? "FAT12" : (clusters < 65525 ? "FAT16" : "FAT32");
                b.note("derived: " + decs(clusters) + " data clusters of " + bytes_text(*spc * *bps) + " => " + type +
                       " by the Microsoft cluster-count rule, volume " + bytes_text(total * *bps));
                b.note("derived: first FAT at sector " + decs(*reserved) + ", data area at sector " + decs(meta));
                if (fat32 && clusters < 65525) b.note("labelled FAT32 but the cluster count is below 65525");
                if (!fat32 && clusters >= 65525) b.note("labelled FAT12/16 but the cluster count is 65525 or more");
            }
        }
    }
    return s;
}

// -------------------------------------------------------------------- exFAT

Structure interpret_exfat(Bytes d) {
    Structure s;
    s.kind = StructKind::exfat_boot;
    s.title = "exFAT boot sector";
    s.length = static_cast<std::uint32_t>(std::min<std::size_t>(512, d.size()));
    Builder b(s, d);
    b.raw(0, 3, "Jump instruction");
    b.text(3, 8, "File system name");
    if (b.has(11, 53)) b.add(11, 53, "Must be zero", all_zero(d.data() + 11, 53) ? "all zero" : "NOT ZERO", all_zero(d.data() + 11, 53) ? "ok" : "invalid");
    if (b.has(11, 53) && !all_zero(d.data() + 11, 53)) b.note("the 53 bytes after the name must be zero on exFAT");
    b.u64(64, "Partition offset (sectors)");
    const auto vlen = b.u64(72, "Volume length (sectors)");
    b.u32(80, "FAT offset (sectors)");
    b.u32(84, "FAT length (sectors)");
    b.u32(88, "Cluster heap offset (sectors)");
    const auto ccount = b.u32(92, "Cluster count");
    b.u32(96, "Root directory cluster");
    b.u32(100, "Volume serial number");
    b.u16(106, "Volume flags");
    const auto bsh = b.u8(108, "Bytes per sector shift");
    const auto ssh = b.u8(109, "Sectors per cluster shift");
    b.u8(110, "Number of FATs");
    b.u8(111, "Drive select");
    b.u8(112, "Percent in use", d.size() > 112 && d[112] == 0xFF ? "not available" : "");
    if (b.has(104, 2)) b.add(104, 2, "File system revision", std::to_string(d[105]) + "." + std::to_string(d[104]));
    if (b.has(510, 2)) {
        const auto sig = le16(d.data() + 510);
        b.add(510, 2, "Boot signature", hexs(sig, 4), sig == 0xAA55 ? "ok (55 AA)" : "INVALID (expected 55 AA)");
        if (sig != 0xAA55) b.note("boot signature is not 55 AA");
    }
    if (bsh && ssh) {
        if (*bsh < 9 || *bsh > 12) b.note("bytes-per-sector shift " + decs(*bsh) + " is outside 9..12");
        else if (*bsh + *ssh > 25) b.note("cluster size would exceed 32 MiB");
        else if (ccount && vlen)
            b.note("derived: sector " + bytes_text(1ull << *bsh) + ", cluster " + bytes_text(1ull << (*bsh + *ssh)) + ", volume " +
                   bytes_text(*vlen << *bsh));
    }
    return s;
}

// --------------------------------------------------------------------- NTFS

Structure interpret_ntfs_boot(Bytes d) {
    Structure s;
    s.kind = StructKind::ntfs_boot;
    s.title = "NTFS boot sector";
    s.length = static_cast<std::uint32_t>(std::min<std::size_t>(512, d.size()));
    Builder b(s, d);
    b.raw(0, 3, "Jump instruction");
    b.text(3, 8, "OEM ID");
    const auto bps = b.u16(11, "Bytes per sector");
    const auto spc = b.u8(13, "Sectors per cluster");
    b.u8(21, "Media descriptor");
    b.u16(24, "Sectors per track");
    b.u16(26, "Heads");
    b.u32(28, "Hidden sectors");
    const auto total = b.u64(40, "Total sectors");
    const auto mft = b.u64(48, "$MFT start cluster");
    b.u64(56, "$MFTMirr start cluster");
    std::optional<std::int64_t> rec;
    if (b.has(64, 1)) {
        const auto v = static_cast<std::int8_t>(d[64]);
        rec = v;
        b.add(64, 1, "Clusters per MFT record", sdecs(v), v < 0 ? "= 2^" + decs(static_cast<std::uint64_t>(-v)) + " bytes" : "clusters");
    }
    b.u8(68, "Clusters per index buffer");
    b.u64(72, "Volume serial number");
    b.u32(80, "Checksum");
    if (b.has(510, 2)) {
        const auto sig = le16(d.data() + 510);
        b.add(510, 2, "Boot signature", hexs(sig, 4), sig == 0xAA55 ? "ok (55 AA)" : "INVALID (expected 55 AA)");
        if (sig != 0xAA55) b.note("boot signature is not 55 AA");
    }
    if (bps && spc) {
        if (!valid_bps(*bps)) b.note("bytes per sector " + decs(*bps) + " is not a power of two between 512 and 4096");
        else if (!pow2(*spc)) b.note("sectors per cluster is not a power of two");
        else {
            const std::uint64_t cluster = *bps * *spc;
            if (total) b.note("derived: cluster " + bytes_text(cluster) + ", volume " + bytes_text(*total * *bps));
            if (mft) b.note("derived: $MFT begins at byte " + decs(*mft * cluster) + " (sector " + decs(*mft * *spc) + ") of the volume");
            if (rec) {
                const std::uint64_t rb = *rec < 0 && *rec > -32 ? (1ull << static_cast<unsigned>(-*rec)) : (*rec > 0 ? static_cast<std::uint64_t>(*rec) * cluster : 0);
                if (rb) b.note("derived: MFT record size " + bytes_text(rb));
                else b.note("MFT record size field is out of range");
            }
        }
    }
    return s;
}

Structure interpret_mft_record(Bytes d) {
    Structure s;
    s.kind = StructKind::ntfs_mft_record;
    s.title = "NTFS MFT record header";
    s.length = static_cast<std::uint32_t>(std::min<std::size_t>(48, d.size()));
    Builder b(s, d);
    b.text(0, 4, "Signature", d.size() >= 4 && std::memcmp(d.data(), "FILE", 4) == 0 ? "ok" : "INVALID");
    const auto fix_off = b.u16(4, "Update sequence offset");
    const auto fix_cnt = b.u16(6, "Update sequence entries");
    b.u64(8, "$LogFile sequence number");
    b.u16(16, "Sequence number");
    b.u16(18, "Hard link count");
    const auto attr_off = b.u16(20, "First attribute offset");
    if (b.has(22, 2)) {
        const auto fl = le16(d.data() + 22);
        std::string n;
        if (fl & 1) n += "in use ";
        if (fl & 2) n += "directory ";
        if (!n.empty()) n.pop_back();
        else n = "deleted file";
        b.add(22, 2, "Flags", hexs(fl, 4), n);
    }
    const auto used = b.u32(24, "Bytes in use");
    const auto alloc = b.u32(28, "Bytes allocated");
    b.u64(32, "Base record reference", d.size() >= 40 && le64(d.data() + 32) == 0 ? "0: this is a base record" : "extension of another record");
    b.u16(40, "Next attribute id");
    if (b.has(44, 4)) b.u32(44, "MFT record number");
    if (fix_off && fix_cnt && (*fix_off < 42 || *fix_off + *fix_cnt * 2 > d.size()))
        b.note("update sequence array lies outside the supplied bytes or overlaps the header");
    if (attr_off && used && alloc && *used > *alloc) b.note("bytes in use exceed bytes allocated");
    b.note("the last two bytes of each 512-byte stride hold the update sequence number on disk; "
           "the real values are in the update sequence array");
    return s;
}

// ---------------------------------------------------------------------- ext

const char* ext_os(std::uint64_t v) {
    switch (v) {
        case 0: return "Linux";
        case 1: return "Hurd";
        case 2: return "Masix";
        case 3: return "FreeBSD";
        case 4: return "Lites";
        default: return "unknown";
    }
}

Structure interpret_ext(Bytes d) {
    Structure s;
    s.kind = StructKind::ext_superblock;
    s.title = "ext2/3/4 superblock";
    s.length = static_cast<std::uint32_t>(std::min<std::size_t>(1024, d.size()));
    Builder b(s, d);
    const auto inodes = b.u32(0, "Inodes count");
    auto blocks = b.u32(4, "Blocks count (low)");
    b.u32(8, "Reserved blocks (low)");
    b.u32(12, "Free blocks (low)");
    b.u32(16, "Free inodes");
    b.u32(20, "First data block");
    const auto lbs = b.u32(24, "Log block size", d.size() >= 28 && le32(d.data() + 24) <= 6 ? "= " + bytes_text(1024ull << le32(d.data() + 24)) : "INVALID");
    b.u32(28, "Log cluster size");
    b.u32(32, "Blocks per group");
    b.u32(36, "Clusters per group");
    const auto ipg = b.u32(40, "Inodes per group");
    if (b.has(44, 4)) b.add(44, 4, "Last mount time", time_field(le32(d.data() + 44)));
    if (b.has(48, 4)) b.add(48, 4, "Last write time", time_field(le32(d.data() + 48)));
    b.u16(52, "Mount count");
    b.u16(54, "Max mount count");
    b.u16(56, "Magic", d.size() >= 58 && le16(d.data() + 56) == 0xEF53 ? "ok (EF53)" : "INVALID");
    if (b.has(58, 2)) {
        const auto st = le16(d.data() + 58);
        std::string n;
        if (st & 1) n += "cleanly unmounted ";
        if (st & 2) n += "errors detected ";
        if (st & 4) n += "orphans being recovered ";
        if (!n.empty()) n.pop_back();
        else n = "unknown";
        b.add(58, 2, "State", hexs(st, 4), n);
        if (st & 2) b.note("the file system is flagged as having errors");
        if (!(st & 1)) b.note("the file system was not cleanly unmounted");
    }
    b.u16(60, "Errors behaviour");
    b.u16(62, "Minor revision");
    if (b.has(64, 4)) b.add(64, 4, "Last check time", time_field(le32(d.data() + 64)));
    b.u32(68, "Check interval (s)");
    if (b.has(72, 4)) b.add(72, 4, "Creator OS", decs(le32(d.data() + 72)), ext_os(le32(d.data() + 72)));
    const auto rev = b.u32(76, "Revision level");
    std::uint64_t compat = 0, incompat = 0, rocompat = 0;
    if (rev && *rev >= 1) {
        b.u32(84, "First non-reserved inode");
        b.u16(88, "Inode size");
        if (b.has(92, 12)) {
            compat = le32(d.data() + 92);
            incompat = le32(d.data() + 96);
            rocompat = le32(d.data() + 100);
            b.add(92, 4, "Compatible features", hexs(compat, 8), compat & 4 ? "has_journal" : "");
            std::string inc;
            if (incompat & 0x2) inc += "filetype ";
            if (incompat & 0x4) inc += "needs_recovery ";
            if (incompat & 0x40) inc += "extents ";
            if (incompat & 0x80) inc += "64bit ";
            if (incompat & 0x200) inc += "flex_bg ";
            if (!inc.empty()) inc.pop_back();
            b.add(96, 4, "Incompatible features", hexs(incompat, 8), inc);
            b.add(100, 4, "Read-only compatible features", hexs(rocompat, 8));
            if (incompat & 0x4) b.note("needs_recovery: the journal has not been replayed (unclean shutdown)");
        }
        if (b.has(104, 16)) b.add(104, 16, "UUID", partitions::Guid::from_disk(d.data() + 104).to_string().substr(0, 36),
                                  "(shown in GUID byte order)");
        b.text(120, 16, "Volume name");
        b.text(136, 64, "Last mounted at");
    }
    if (blocks && lbs && *lbs <= 6) {
        std::uint64_t total = *blocks;
        if ((incompat & 0x80) && b.has(336, 4)) total |= std::uint64_t{le32(d.data() + 336)} << 32;
        const std::uint64_t bs = 1024ull << *lbs;
        const char* type = (incompat & 0x40 || incompat & 0x80) ? "ext4" : ((compat & 4) ? "ext3" : "ext2");
        b.note(std::string("derived: looks like ") + type + ", " + decs(total) + " blocks of " + bytes_text(bs) + " = " + bytes_text(total * bs));
        if (inodes && ipg && *ipg) b.note("derived: " + decs((*inodes + *ipg - 1) / *ipg) + " block groups");
    }
    return s;
}

}  // namespace

// ============================================================================
// Hex view
// ============================================================================

std::uint32_t HexLayout::hex_column(std::uint32_t i) const noexcept {
    return kOffsetDigits + 2 + i * 3 + (bytes_per_row > 8 ? i / 8 : 0);
}
std::uint32_t HexLayout::ascii_column(std::uint32_t i) const noexcept {
    // two spaces after the last hex pair, then '|', then the characters
    return hex_column(bytes_per_row - 1) + 5 + i;
}
std::uint32_t HexLayout::row_width() const noexcept { return ascii_column(bytes_per_row) + 1; }

std::string hex_dump(Bytes data, std::uint64_t base_offset, const HexLayout& layout) {
    static const char* digits = "0123456789ABCDEF";
    const std::uint32_t bpr = std::max<std::uint32_t>(1, layout.bytes_per_row);
    HexLayout l = layout;
    l.bytes_per_row = bpr;
    std::string out;
    out.reserve((data.size() / bpr + 1) * (l.row_width() + 1));
    for (std::size_t row = 0; row < data.size(); row += bpr) {
        std::string line(l.row_width(), ' ');
        char off[32];
        std::snprintf(off, sizeof off, "%0*llX", static_cast<int>(HexLayout::kOffsetDigits),
                      static_cast<unsigned long long>(base_offset + row));
        std::memcpy(line.data(), off, std::min<std::size_t>(std::strlen(off), HexLayout::kOffsetDigits));
        const std::size_t n = std::min<std::size_t>(bpr, data.size() - row);
        for (std::size_t i = 0; i < n; ++i) {
            const auto v = data[row + i];
            const auto c = l.hex_column(static_cast<std::uint32_t>(i));
            line[c] = digits[v >> 4];
            line[c + 1] = digits[v & 15];
            line[l.ascii_column(static_cast<std::uint32_t>(i))] = (v >= 0x20 && v < 0x7F) ? static_cast<char>(v) : '.';
        }
        line[l.ascii_column(0) - 1] = '|';
        line[l.row_width() - 1] = '|';
        out += line;
        out += '\n';
    }
    return out;
}

// ============================================================================
// Search
// ============================================================================

namespace {
int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::uint8_t fold(std::uint8_t c) { return (c >= 'A' && c <= 'Z') ? static_cast<std::uint8_t>(c + 32) : c; }
}  // namespace

Result<Pattern> parse_hex_pattern(std::string_view text) {
    Pattern p;
    std::size_t i = 0;
    auto bad = [&](const std::string& why) { return Result<Pattern>(Error{Errc::invalid_argument, "bad hex pattern: " + why}); };
    while (i < text.size()) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++i;
            continue;
        }
        if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            i += 2;
            continue;
        }
        if (c == '?') {
            std::size_t n = 1;
            if (i + 1 < text.size() && text[i + 1] == '?') n = 2;
            i += n;
            p.bytes.push_back(0);
            p.mask.push_back(0);
            continue;
        }
        const int hi = hexval(c);
        if (hi < 0) return bad(std::string("unexpected '") + c + "'");
        if (i + 1 >= text.size()) return bad("odd number of hex digits");
        const int lo = hexval(text[i + 1]);
        if (lo < 0) return bad(std::string("unexpected '") + text[i + 1] + "'");
        p.bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
        p.mask.push_back(0xFF);
        i += 2;
    }
    if (p.bytes.empty()) return bad("nothing to look for");
    if (std::all_of(p.mask.begin(), p.mask.end(), [](std::uint8_t m) { return m == 0; })) return bad("only wildcards");
    return p;
}

Pattern ascii_pattern(std::string_view text, bool ci) {
    Pattern p;
    p.case_insensitive = ci;
    for (char c : text) {
        p.bytes.push_back(static_cast<std::uint8_t>(c));
        p.mask.push_back(0xFF);
    }
    return p;
}

Result<Pattern> utf16le_pattern(std::string_view u8, bool ci) {
    Pattern p;
    p.case_insensitive = ci;
    std::size_t i = 0;
    while (i < u8.size()) {
        const auto b0 = static_cast<std::uint8_t>(u8[i]);
        std::uint32_t cp = 0;
        std::size_t len = 1;
        if (b0 < 0x80) cp = b0;
        else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1Fu; len = 2; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0Fu; len = 3; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07u; len = 4; }
        else return Error{Errc::invalid_argument, "text is not valid UTF-8"};
        if (i + len > u8.size()) return Error{Errc::invalid_argument, "text is not valid UTF-8"};
        for (std::size_t k = 1; k < len; ++k) {
            const auto bk = static_cast<std::uint8_t>(u8[i + k]);
            if ((bk & 0xC0) != 0x80) return Error{Errc::invalid_argument, "text is not valid UTF-8"};
            cp = (cp << 6) | (bk & 0x3Fu);
        }
        i += len;
        auto put = [&](std::uint32_t unit) {
            p.bytes.push_back(static_cast<std::uint8_t>(unit & 0xFF));
            p.bytes.push_back(static_cast<std::uint8_t>(unit >> 8));
            p.mask.push_back(0xFF);
            p.mask.push_back(0xFF);
        };
        if (cp >= 0x10000) {
            cp -= 0x10000;
            put(0xD800 + (cp >> 10));
            put(0xDC00 + (cp & 0x3FF));
        } else {
            put(cp);
        }
    }
    if (p.bytes.empty()) return Error{Errc::invalid_argument, "nothing to look for"};
    return p;
}

bool matches_at(const Pattern& p, const std::uint8_t* data) noexcept {
    for (std::size_t i = 0; i < p.bytes.size(); ++i) {
        if (p.mask[i] == 0) continue;
        std::uint8_t a = data[i], b = p.bytes[i];
        if (p.case_insensitive) {
            a = fold(a);
            b = fold(b);
        }
        if ((a & p.mask[i]) != (b & p.mask[i])) return false;
    }
    return true;
}

std::optional<std::size_t> find_in_buffer(Bytes buf, const Pattern& p, std::size_t from) {
    if (p.empty() || buf.size() < p.size()) return std::nullopt;
    for (std::size_t i = from; i + p.size() <= buf.size(); ++i)
        if (matches_at(p, buf.data() + i)) return i;
    return std::nullopt;
}

Result<SearchOutcome> search_device(IBlockDevice& dev, const Pattern& p, std::uint64_t first_byte, std::uint64_t end_byte,
                                    std::size_t max_hits, const SearchProgress& progress, std::size_t block_bytes) {
    if (p.empty() || p.mask.size() != p.bytes.size()) return Error{Errc::invalid_argument, "empty search pattern"};
    const std::uint32_t ss = dev.sector_size();
    if (ss == 0) return Error{Errc::invalid_argument, "device has no sector size"};
    end_byte = std::min<std::uint64_t>(end_byte, dev.capacity());
    SearchOutcome out;
    if (first_byte >= end_byte) {
        out.complete = true;
        return out;
    }
    block_bytes = std::max<std::size_t>(ss, block_bytes / ss * ss);

    // Streaming matcher: keeps the last (n-1) bytes so a match may span two
    // reads, and forgets them when the next read is not adjacent (a hole).
    std::vector<std::uint8_t> carry, window;
    std::uint64_t carry_end = 0;
    auto feed = [&](const std::uint8_t* d, std::size_t n, std::uint64_t off) -> bool {
        if (carry_end != off) carry.clear();
        window.assign(carry.begin(), carry.end());
        window.insert(window.end(), d, d + n);
        const std::uint64_t window_start = off - carry.size();
        for (std::size_t i = 0; i + p.size() <= window.size(); ++i) {
            if (!matches_at(p, window.data() + i)) continue;
            const std::uint64_t at = window_start + i;
            if (at < first_byte || at + p.size() > end_byte) continue;
            out.hits.push_back(at);
            if (out.hits.size() >= max_hits) {
                out.truncated = true;
                return false;
            }
        }
        const std::size_t keep = std::min<std::size_t>(p.size() - 1, window.size());
        carry.assign(window.end() - static_cast<std::ptrdiff_t>(keep), window.end());
        carry_end = off + n;
        return true;
    };

    std::vector<std::uint8_t> buf(block_bytes);
    const std::uint64_t scan_start = first_byte / ss * ss;
    std::uint64_t pos = scan_start;
    while (pos < end_byte) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(block_bytes, end_byte - pos));
        const std::size_t len = (want + ss - 1) / ss * ss;
        bool ok = false;
        if (pos + len <= dev.capacity()) ok = static_cast<bool>(dev.read(pos, buf.data(), len));
        if (ok) {
            if (!feed(buf.data(), want, pos)) return out;
        } else {
            for (std::size_t off = 0; off < want; off += ss) {
                auto r = dev.read(pos + off, buf.data(), ss);
                if (r) {
                    if (!feed(buf.data(), std::min<std::size_t>(ss, want - off), pos + off)) return out;
                } else {
                    if (r.error().code == Errc::device_removed || r.error().code == Errc::permission_denied) return r.error();
                    ++out.unreadable_sectors;
                    carry.clear();
                }
            }
        }
        pos += want;
        out.scanned_bytes = pos - scan_start;
        if (progress && !progress(out.scanned_bytes)) return out;
    }
    out.complete = true;
    return out;
}

// ============================================================================
// Detection and dispatch
// ============================================================================

std::string_view struct_kind_name(StructKind k) noexcept {
    switch (k) {
        case StructKind::mbr: return "mbr";
        case StructKind::gpt_header: return "gpt-header";
        case StructKind::gpt_entries: return "gpt-entries";
        case StructKind::fat_boot: return "fat";
        case StructKind::exfat_boot: return "exfat";
        case StructKind::ntfs_boot: return "ntfs";
        case StructKind::ntfs_mft_record: return "mft";
        case StructKind::ext_superblock: return "ext";
    }
    return "?";
}

std::optional<StructKind> parse_struct_kind(std::string_view s) noexcept {
    for (int i = 0; i < kStructKindCount; ++i) {
        const auto k = static_cast<StructKind>(i);
        if (struct_kind_name(k) == s) return k;
    }
    return std::nullopt;
}

std::optional<Detected> detect(Bytes data, std::uint64_t lba, std::uint32_t sector_size) {
    if (data.size() < 64) return std::nullopt;
    if (data.size() >= 8 && std::memcmp(data.data(), "EFI PART", 8) == 0) return Detected{StructKind::gpt_header, 0};
    if (data.size() >= 4 && std::memcmp(data.data(), "FILE", 4) == 0 && data.size() >= 48 && le16(data.data() + 4) >= 42 &&
        le16(data.data() + 4) < 128)
        return Detected{StructKind::ntfs_mft_record, 0};
    if (looks_ntfs(data)) return Detected{StructKind::ntfs_boot, 0};
    if (looks_exfat(data)) return Detected{StructKind::exfat_boot, 0};
    if (looks_fat(data)) return Detected{StructKind::fat_boot, 0};
    if (looks_ext(data, 0)) return Detected{StructKind::ext_superblock, 0};
    if (sector_size >= 2048 && looks_ext(data, 1024)) return Detected{StructKind::ext_superblock, 1024};
    if (lba == 0 && data.size() >= 512 && data[510] == 0x55 && data[511] == 0xAA) return Detected{StructKind::mbr, 0};
    if (looks_mbr_entries(data)) return Detected{StructKind::mbr, 0};
    if (looks_gpt_entries(data)) return Detected{StructKind::gpt_entries, 0};
    return std::nullopt;
}

Result<Structure> interpret(StructKind kind, Bytes data, std::uint32_t sector_size) {
    static_cast<void>(sector_size);
    std::size_t need = 0;
    switch (kind) {
        case StructKind::mbr: need = 512; break;
        case StructKind::gpt_header: need = 92; break;
        case StructKind::gpt_entries: need = 128; break;
        case StructKind::fat_boot: need = 90; break;
        case StructKind::exfat_boot: need = 120; break;
        case StructKind::ntfs_boot: need = 84; break;
        case StructKind::ntfs_mft_record: need = 48; break;
        case StructKind::ext_superblock: need = 264; break;
    }
    if (data.size() < need)
        return Error{Errc::out_of_range, std::string(struct_kind_name(kind)) + " needs at least " + std::to_string(need) +
                                             " bytes, " + std::to_string(data.size()) + " supplied"};
    switch (kind) {
        case StructKind::mbr: return interpret_mbr(data);
        case StructKind::gpt_header: return interpret_gpt_header(data);
        case StructKind::gpt_entries: return interpret_gpt_entries(data);
        case StructKind::fat_boot: return interpret_fat(data);
        case StructKind::exfat_boot: return interpret_exfat(data);
        case StructKind::ntfs_boot: return interpret_ntfs_boot(data);
        case StructKind::ntfs_mft_record: return interpret_mft_record(data);
        case StructKind::ext_superblock: return interpret_ext(data);
    }
    return Error{Errc::invalid_argument, "unknown structure"};
}

std::string structure_to_text(const Structure& s) {
    std::string out = s.title + "\n";
    std::size_t w = 0;
    for (const auto& f : s.fields) w = std::max(w, f.name.size());
    for (const auto& f : s.fields) {
        char head[48];
        std::snprintf(head, sizeof head, "  %5u +%-3u  ", f.offset, f.size);
        out += head;
        out += f.name;
        out.append(w + 2 - f.name.size(), ' ');
        out += f.value;
        if (!f.note.empty()) out += "   <- " + f.note;
        out += '\n';
    }
    for (const auto& n : s.notes) out += "  note: " + n + "\n";
    return out;
}

std::string unix_time_text(std::uint64_t seconds) { return civil_utc(seconds); }

}  // namespace platter::inspect
