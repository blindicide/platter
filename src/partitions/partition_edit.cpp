// SPDX-License-Identifier: TBD
#include "platter/partitions/partition_edit.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>

#include "platter/core/aligned_buffer.hpp"
#include "platter/core/bytes.hpp"
#include "platter/core/chacha20.hpp"

namespace platter::partitions {

namespace {

constexpr std::uint32_t kEntrySize = 128;
constexpr std::uint32_t kDefaultEntries = 128;
constexpr const char* kLinuxFs = "0FC63DAF-8483-4772-8E79-3D69D8477DE4";

std::uint64_t ceil_div(std::uint64_t a, std::uint64_t b) { return (a + b - 1) / b; }

std::vector<std::uint16_t> utf8_to_utf16(const std::string& s) {
    std::vector<std::uint16_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<std::uint8_t>(s[i]);
        std::uint32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<std::uint8_t>(s[i + 1]) & 0x3Fu);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<std::uint8_t>(s[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<std::uint8_t>(s[i + 2]) & 0x3Fu);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<std::uint8_t>(s[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<std::uint8_t>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<std::uint8_t>(s[i + 3]) & 0x3Fu);
            len = 4;
        }
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<std::uint16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<std::uint16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<std::uint16_t>(cp));
        }
    }
    return out;
}

std::array<std::uint8_t, 3> chs_of(std::uint64_t lba) {
    constexpr std::uint64_t kHeads = 255, kSectors = 63;
    if (lba >= 1024 * kHeads * kSectors) return {0xFE, 0xFF, 0xFF};
    const auto c = lba / (kHeads * kSectors);
    const auto t = lba % (kHeads * kSectors);
    const auto h = t / kSectors;
    const auto s = t % kSectors + 1;
    return {static_cast<std::uint8_t>(h), static_cast<std::uint8_t>((s & 0x3F) | ((c >> 2) & 0xC0)),
            static_cast<std::uint8_t>(c & 0xFF)};
}

bool is_ext_type(std::uint8_t t) { return t == 0x05 || t == 0x0F || t == 0x85; }

std::string mib(std::uint64_t bytes) {
    char b[48];
    if (bytes >= (1ull << 30)) std::snprintf(b, sizeof b, "%.2f GiB", static_cast<double>(bytes) / (1ull << 30));
    else std::snprintf(b, sizeof b, "%.2f MiB", static_cast<double>(bytes) / (1ull << 20));
    return b;
}

std::string describe_partition(const Partition& p, TableType t, std::uint32_t ss) {
    std::ostringstream o;
    o << "#" << p.number << " LBA " << p.first_lba << "-" << p.last_lba << " (" << mib(p.lba_count() * ss) << ") ";
    if (t == TableType::gpt) {
        o << gpt_type_name(p.type_guid);
        if (!p.name.empty()) o << " \"" << p.name << "\"";
    } else {
        o << mbr_type_name(p.mbr_type) << (p.logical ? " logical" : "") << (p.bootable ? " bootable" : "");
    }
    return o.str();
}

}  // namespace

// ------------------------------------------------------------------ aliases

const std::vector<TypeAlias>& type_aliases() {
    static const std::vector<TypeAlias> k = {
        {"linux", 0x83, kLinuxFs, "Linux filesystem"},
        {"swap", 0x82, "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", "Linux swap"},
        {"esp", 0xEF, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "EFI System Partition"},
        {"ntfs", 0x07, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "NTFS / Microsoft basic data"},
        {"exfat", 0x07, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "exFAT / Microsoft basic data"},
        {"fat32", 0x0C, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "FAT32 (LBA) / Microsoft basic data"},
        {"data", 0x07, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data"},
        {"lvm", 0x8E, "E6D6D379-F507-44C2-A23C-238F2A3DF928", "Linux LVM"},
        {"raid", 0xFD, "A19D880F-05FC-4D3B-A006-743F0F84911E", "Linux RAID"},
        {"home", 0x83, "933AC7E1-2EB4-4F13-B844-0E14E2AEF915", "Linux home"},
        {"root", 0x83, "4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709", "Linux root (x86-64)"},
        {"recovery", 0x27, "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", "Windows Recovery"},
        {"msr", 0x00, "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", "Microsoft reserved (GPT only)"},
        {"bios-boot", 0x00, "21686148-6449-6E6F-744E-656564454649", "BIOS boot (GPT only)"},
    };
    return k;
}

std::optional<TypeAlias> find_type_alias(const std::string& name) {
    std::string n;
    for (char c : name) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (const auto& a : type_aliases())
        if (n == a.name) return a;
    return std::nullopt;
}

Guid random_guid() {
    const auto k = ChaCha20::random_key();
    Guid g;
    std::copy_n(k.begin(), 16, g.bytes.begin());
    g.bytes[7] = static_cast<std::uint8_t>((g.bytes[7] & 0x0F) | 0x40);  // version 4
    g.bytes[8] = static_cast<std::uint8_t>((g.bytes[8] & 0x3F) | 0x80);  // RFC 4122 variant
    return g;
}

// ------------------------------------------------------------------ construction

Result<PartitionPlan> PartitionPlan::blank(TableType type, std::uint32_t sector_size, std::uint64_t total_lbas) {
    if (type != TableType::mbr && type != TableType::gpt) return Error{Errc::invalid_argument, "choose mbr or gpt"};
    if (sector_size != 512 && sector_size != 4096) return Error{Errc::invalid_argument, "sector size must be 512 or 4096"};
    PartitionPlan p;
    p.type_ = type;
    p.ss_ = sector_size;
    p.total_ = total_lbas;
    if (type == TableType::gpt) {
        p.disk_guid_ = random_guid();
        if (p.usable().count == 0) return Error{Errc::invalid_argument, "device is too small for a GPT"};
    } else {
        const auto k = ChaCha20::random_key();
        p.mbr_signature_ = le32(k.data());
        if (p.mbr_signature_ == 0) p.mbr_signature_ = 0xC0FFEE11u;
        if (total_lbas < 2) return Error{Errc::invalid_argument, "device is too small for an MBR"};
    }
    return p;
}

Result<PartitionPlan> PartitionPlan::from_table(const PartitionTable& t, const std::vector<std::uint8_t>& sector0) {
    if (t.type == TableType::none)
        return Error{Errc::invalid_argument, "the device has no partition table; create one first"};
    PartitionPlan p;
    p.type_ = t.type;
    p.ss_ = t.sector_size;
    p.total_ = t.total_lbas;
    p.parts_ = t.partitions;
    if (t.type == TableType::gpt) {
        const auto& h = t.used_backup_gpt ? t.gpt_backup : t.gpt_primary;
        if (!h) return Error{Errc::corrupt_data, "no usable GPT header"};
        if (h->entry_size != kEntrySize)
            return Error{Errc::unsupported, "GPT entry size " + std::to_string(h->entry_size) + " is not supported for editing"};
        if (h->entry_count < kDefaultEntries || h->entry_count > 1024)
            return Error{Errc::unsupported, "GPT with " + std::to_string(h->entry_count) + " entries is not supported for editing"};
        p.entry_count_ = h->entry_count;
        p.disk_guid_ = h->disk_guid;
    } else {
        p.mbr_signature_ = t.mbr_disk_signature;
        if (sector0.size() >= 440) p.boot_code_.assign(sector0.begin(), sector0.begin() + 440);
    }
    return p;
}

Result<PartitionPlan> PartitionPlan::from_device(IBlockDevice& dev) {
    auto t = read_partition_table(dev);
    if (!t) return t.error();
    std::vector<std::uint8_t> s0(dev.sector_size());
    if (auto s = read_sectors(dev, 0, 1, s0.data(), s0.size()); !s) return s.error();
    return from_table(*t, s0);
}

// ------------------------------------------------------------------ geometry

std::uint64_t PartitionPlan::gpt_entry_sectors() const {
    return ceil_div(std::uint64_t{entry_count_} * kEntrySize, ss_);
}

LbaRange PartitionPlan::usable() const {
    if (type_ == TableType::gpt) {
        const auto es = gpt_entry_sectors();
        const std::uint64_t first = 2 + es;
        if (total_ < first + es + 2) return {0, 0};
        const std::uint64_t last = total_ - 2 - es;
        if (last < first) return {0, 0};
        return {first, last - first + 1};
    }
    if (total_ < 2) return {0, 0};
    return {1, total_ - 1};
}

const Partition* PartitionPlan::find(std::uint32_t number) const {
    for (const auto& p : parts_)
        if (p.number == number) return &p;
    return nullptr;
}

const Partition* PartitionPlan::container() const {
    for (const auto& p : parts_)
        if (p.extended_container) return &p;
    return nullptr;
}

std::vector<LbaRange> PartitionPlan::free_spans(bool in_extended) const {
    LbaRange region = usable();
    std::vector<LbaRange> occupied;
    if (in_extended) {
        const auto* c = container();
        if (!c) return {};
        region = {c->first_lba, c->lba_count()};
        std::vector<const Partition*> logicals;
        for (const auto& p : parts_)
            if (p.logical) logicals.push_back(&p);
        std::sort(logicals.begin(), logicals.end(), [](auto* a, auto* b) { return a->first_lba < b->first_lba; });
        for (std::size_t i = 0; i < logicals.size(); ++i) {
            const std::uint64_t start = i == 0 ? c->first_lba : logicals[i]->first_lba - 1;  // EBR sector
            occupied.push_back({start, logicals[i]->last_lba - start + 1});
        }
        if (logicals.empty()) occupied.push_back({c->first_lba, 1});  // the first EBR
    } else {
        for (const auto& p : parts_)
            if (!p.logical) occupied.push_back({p.first_lba, p.lba_count()});
    }
    std::sort(occupied.begin(), occupied.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<LbaRange> out;
    if (region.empty()) return out;
    std::uint64_t cursor = region.first;
    const std::uint64_t end = region.first + region.count;  // exclusive
    for (const auto& o : occupied) {
        if (o.first >= end) break;
        if (o.first > cursor) out.push_back({cursor, o.first - cursor});
        cursor = std::max(cursor, o.first + o.count);
    }
    if (cursor < end) out.push_back({cursor, end - cursor});
    return out;
}

std::optional<std::uint64_t> PartitionPlan::first_fit_start(std::uint64_t align_bytes, bool in_extended) const {
    const std::uint64_t align_lbas = std::max<std::uint64_t>(1, align_bytes / ss_);
    for (const auto& span : free_spans(in_extended)) {
        std::uint64_t start = ceil_div(span.first, align_lbas) * align_lbas;
        if (in_extended && start == span.first) start += align_lbas;  // room for the EBR sector
        if (start < span.first + span.count) return start;
    }
    return std::nullopt;
}

// ------------------------------------------------------------------ editing

void PartitionPlan::renumber() {
    if (type_ != TableType::mbr) return;
    std::vector<Partition*> logicals;
    for (auto& p : parts_)
        if (p.logical) logicals.push_back(&p);
    std::sort(logicals.begin(), logicals.end(), [](auto* a, auto* b) { return a->first_lba < b->first_lba; });
    std::uint32_t n = 5;
    for (auto* p : logicals) p->number = n++;
    std::sort(parts_.begin(), parts_.end(), [](const Partition& a, const Partition& b) { return a.number < b.number; });
}

namespace {
std::vector<std::string> new_problems(const std::vector<std::string>& before, const std::vector<std::string>& after) {
    std::set<std::string> old(before.begin(), before.end());
    std::vector<std::string> out;
    for (const auto& a : after)
        if (!old.count(a)) out.push_back(a);
    return out;
}
}  // namespace

Result<std::uint32_t> PartitionPlan::add(const PartitionSpec& spec) {
    const auto before_problems = problems();
    const auto saved = parts_;
    Partition p;
    p.first_lba = spec.first_lba;
    p.attributes = spec.attributes;

    // Length 0 = fill the free span that starts at/contains first_lba.
    std::uint64_t count = spec.lba_count;
    if (count == 0) {
        for (const auto& span : free_spans(spec.logical)) {
            if (spec.first_lba >= span.first && spec.first_lba < span.first + span.count) {
                count = span.first + span.count - spec.first_lba;
                break;
            }
        }
        if (count == 0) return Error{Errc::invalid_argument, "no free space at LBA " + std::to_string(spec.first_lba)};
    }
    if (count > UINT64_MAX - spec.first_lba) return Error{Errc::overflow, "partition extends past 2^64 sectors"};
    p.last_lba = spec.first_lba + count - 1;
    p.size_bytes = count * ss_;
    p.aligned_1mib = (spec.first_lba * ss_) % kMiB1 == 0;

    if (type_ == TableType::gpt) {
        if (spec.logical || spec.extended) return Error{Errc::invalid_argument, "GPT has no extended or logical partitions"};
        p.type_guid = spec.type_guid.is_zero() ? *Guid::parse(kLinuxFs) : spec.type_guid;
        p.unique_guid = spec.unique_guid ? *spec.unique_guid : random_guid();
        if (utf8_to_utf16(spec.name).size() > 36) return Error{Errc::invalid_argument, "GPT partition names are limited to 36 UTF-16 units"};
        p.name = spec.name;
        p.type_name = gpt_type_name(p.type_guid);
        std::set<std::uint32_t> used;
        for (const auto& q : parts_) used.insert(q.number);
        std::uint32_t slot = 1;
        while (used.count(slot)) ++slot;
        if (slot > entry_count_) return Error{Errc::out_of_range, "the GPT entry array is full"};
        p.number = slot;
    } else if (type_ == TableType::mbr) {
        p.mbr_type = spec.extended ? std::uint8_t{0x0F} : spec.mbr_type;
        if (p.mbr_type == 0) return Error{Errc::invalid_argument, "MBR partition type 0x00 means unused"};
        if (is_ext_type(p.mbr_type) && !spec.extended) return Error{Errc::invalid_argument, "use the extended flag to create an extended partition"};
        p.bootable = spec.bootable;
        p.type_name = mbr_type_name(p.mbr_type);
        if (spec.extended) {
            if (container()) return Error{Errc::invalid_argument, "an MBR disk can have only one extended partition"};
            p.extended_container = true;
        }
        if (spec.logical) {
            if (!container()) return Error{Errc::invalid_argument, "create an extended partition first"};
            p.logical = true;
        } else {
            std::set<std::uint32_t> used;
            for (const auto& q : parts_)
                if (!q.logical) used.insert(q.number);
            std::uint32_t slot = 1;
            while (used.count(slot)) ++slot;
            if (slot > 4) return Error{Errc::out_of_range, "an MBR disk holds at most four primary partitions"};
            p.number = slot;
        }
    } else {
        return Error{Errc::invalid_argument, "no partition table in the plan"};
    }

    parts_.push_back(p);
    renumber();
    const auto fresh = new_problems(before_problems, problems());
    if (!fresh.empty()) {
        parts_ = saved;
        return Error{Errc::invalid_argument, fresh.front()};
    }
    // After renumbering, the new partition's number may have changed (logicals).
    for (const auto& q : parts_)
        if (q.first_lba == p.first_lba && q.last_lba == p.last_lba) return q.number;
    return p.number;
}

Status PartitionPlan::remove(std::uint32_t number) {
    auto it = std::find_if(parts_.begin(), parts_.end(), [&](const Partition& p) { return p.number == number; });
    if (it == parts_.end()) return Error{Errc::not_found, "no partition " + std::to_string(number)};
    if (it->extended_container) {
        parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Partition& p) { return p.logical; }), parts_.end());
        it = std::find_if(parts_.begin(), parts_.end(), [&](const Partition& p) { return p.number == number; });
    }
    parts_.erase(it);
    renumber();
    return {};
}

#define PLATTER_FIND(num)                                                                                  \
    auto it = std::find_if(parts_.begin(), parts_.end(), [&](const Partition& p) { return p.number == (num); }); \
    if (it == parts_.end()) return Error{Errc::not_found, "no partition " + std::to_string(num)};

Status PartitionPlan::set_mbr_type(std::uint32_t number, std::uint8_t type) {
    PLATTER_FIND(number)
    if (type_ != TableType::mbr) return Error{Errc::invalid_argument, "not an MBR table"};
    if (type == 0) return Error{Errc::invalid_argument, "type 0x00 means unused"};
    if (it->extended_container != is_ext_type(type))
        return Error{Errc::invalid_argument, "cannot convert between extended and ordinary partitions; delete and re-create"};
    it->mbr_type = type;
    it->type_name = mbr_type_name(type);
    return {};
}

Status PartitionPlan::set_type_guid(std::uint32_t number, const Guid& type) {
    PLATTER_FIND(number)
    if (type_ != TableType::gpt) return Error{Errc::invalid_argument, "not a GPT table"};
    if (type.is_zero()) return Error{Errc::invalid_argument, "the all-zero type means unused"};
    it->type_guid = type;
    it->type_name = gpt_type_name(type);
    return {};
}

Status PartitionPlan::set_name(std::uint32_t number, const std::string& name) {
    PLATTER_FIND(number)
    if (type_ != TableType::gpt) return Error{Errc::invalid_argument, "only GPT partitions have names"};
    if (utf8_to_utf16(name).size() > 36) return Error{Errc::invalid_argument, "GPT partition names are limited to 36 UTF-16 units"};
    it->name = name;
    return {};
}

Status PartitionPlan::set_bootable(std::uint32_t number, bool on) {
    PLATTER_FIND(number)
    if (type_ != TableType::mbr) return Error{Errc::invalid_argument, "the bootable flag is an MBR concept (GPT: attribute bit 2)"};
    if (it->extended_container) return Error{Errc::invalid_argument, "an extended partition cannot be bootable"};
    if (on && !it->logical)
        for (auto& q : parts_)
            if (!q.logical) q.bootable = false;  // one active primary
    it->bootable = on;
    return {};
}

Status PartitionPlan::set_attributes(std::uint32_t number, std::uint64_t attrs) {
    PLATTER_FIND(number)
    if (type_ != TableType::gpt) return Error{Errc::invalid_argument, "attributes are a GPT concept"};
    it->attributes = attrs;
    return {};
}

Status PartitionPlan::resize(std::uint32_t number, std::uint64_t first_lba, std::uint64_t last_lba) {
    PLATTER_FIND(number)
    if (last_lba < first_lba) return Error{Errc::invalid_argument, "end is before start"};
    const auto before = problems();
    const auto saved = parts_;
    it->first_lba = first_lba;
    it->last_lba = last_lba;
    it->size_bytes = (last_lba - first_lba + 1) * ss_;
    it->aligned_1mib = (first_lba * ss_) % kMiB1 == 0;
    renumber();
    const auto fresh = new_problems(before, problems());
    if (!fresh.empty()) {
        parts_ = saved;
        return Error{Errc::invalid_argument, fresh.front()};
    }
    return {};
}
#undef PLATTER_FIND

// ------------------------------------------------------------------ validation

std::vector<std::string> PartitionPlan::problems() const {
    std::vector<std::string> out;
    const auto region = usable();
    if (type_ == TableType::gpt) {
        if (region.empty()) out.push_back("the device is too small for a GPT");
        if (parts_.size() > entry_count_) out.push_back("more partitions than GPT entries");
    }
    const auto id = [](const Partition& p) { return "partition " + std::to_string(p.number); };

    std::vector<const Partition*> flat;  // everything except logicals
    std::vector<const Partition*> logicals;
    int extended = 0, primaries = 0;
    for (const auto& p : parts_) {
        if (p.last_lba < p.first_lba) out.push_back(id(p) + " ends before it starts");
        const bool inside = !region.empty() && p.first_lba >= region.first && p.last_lba < region.first + region.count;
        if (!inside) out.push_back(id(p) + " lies outside the usable area (LBA " + std::to_string(region.first) + " - " +
                                   std::to_string(region.first + region.count - 1) + ")");
        if (type_ == TableType::mbr) {
            const std::uint64_t cnt = p.last_lba - p.first_lba + 1;
            if (p.first_lba > UINT32_MAX || cnt > UINT32_MAX || p.first_lba + cnt > (std::uint64_t{1} << 32))
                out.push_back(id(p) + " is beyond what an MBR can address (2^32 sectors)");
            if (p.extended_container) ++extended;
            if (!p.logical) ++primaries;
        } else if (type_ == TableType::gpt) {
            if (p.type_guid.is_zero()) out.push_back(id(p) + " has no type");
        }
        (p.logical ? logicals : flat).push_back(&p);
    }
    if (type_ == TableType::mbr) {
        if (primaries > 4) out.push_back("more than four primary partitions");
        if (extended > 1) out.push_back("more than one extended partition");
        if (!logicals.empty() && extended == 0) out.push_back("logical partitions need an extended partition");
    }
    // Overlaps among primaries / GPT entries.
    std::vector<const Partition*> sorted = flat;
    std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->first_lba < b->first_lba; });
    for (std::size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i]->first_lba <= sorted[i - 1]->last_lba)
            out.push_back(id(*sorted[i - 1]) + " overlaps " + id(*sorted[i]));
    if (type_ == TableType::gpt) {
        std::set<std::array<std::uint8_t, 16>> seen;
        for (const auto& p : parts_)
            if (!p.unique_guid.is_zero() && !seen.insert(p.unique_guid.bytes).second)
                out.push_back(id(p) + " shares its unique GUID with another partition");
    }
    // Logicals live inside the container, with room for their EBR sectors.
    if (const auto* c = container()) {
        std::sort(logicals.begin(), logicals.end(), [](auto* a, auto* b) { return a->first_lba < b->first_lba; });
        for (std::size_t i = 0; i < logicals.size(); ++i) {
            const auto* l = logicals[i];
            if (l->first_lba <= c->first_lba || l->last_lba > c->last_lba)
                out.push_back(id(*l) + " does not fit inside the extended partition (it needs one free sector before it for its EBR)");
            if (i > 0 && l->first_lba <= logicals[i - 1]->last_lba + 1)
                out.push_back(id(*l) + " overlaps or abuts " + id(*logicals[i - 1]) + " (an EBR sector must fit between them)");
        }
    }
    return out;
}

std::vector<std::string> PartitionPlan::warnings() const {
    std::vector<std::string> out;
    for (const auto& p : parts_) {
        if ((p.first_lba * ss_) % kMiB1 != 0 && !p.extended_container)
            out.push_back("partition " + std::to_string(p.number) + " is not aligned to 1 MiB");
    }
    if (type_ == TableType::mbr && total_ > (std::uint64_t{1} << 32))
        out.push_back("MBR can only address the first 2^32 sectors; the rest of the disk cannot be partitioned");
    return out;
}

// ------------------------------------------------------------------ encoding

Result<std::vector<SectorWrite>> PartitionPlan::encode() const {
    if (auto pr = problems(); !pr.empty()) return Error{Errc::invalid_argument, "the plan is not valid: " + pr.front()};
    return type_ == TableType::gpt ? encode_gpt() : encode_mbr();
}

Result<std::vector<SectorWrite>> PartitionPlan::encode_gpt() const {
    const auto es = gpt_entry_sectors();
    const auto region = usable();
    const std::uint64_t last_lba = total_ - 1;

    std::vector<std::uint8_t> entries(static_cast<std::size_t>(es * ss_), 0);
    for (const auto& p : parts_) {
        std::uint8_t* e = entries.data() + std::size_t{p.number - 1} * kEntrySize;
        std::memcpy(e, p.type_guid.bytes.data(), 16);
        std::memcpy(e + 16, p.unique_guid.bytes.data(), 16);
        put_le64(e + 32, p.first_lba);
        put_le64(e + 40, p.last_lba);
        put_le64(e + 48, p.attributes);
        const auto name = utf8_to_utf16(p.name);
        for (std::size_t i = 0; i < name.size() && i < 36; ++i) put_le16(e + 56 + 2 * i, name[i]);
    }
    const std::uint32_t entries_crc =
        crc32(std::span<const std::uint8_t>(entries.data(), std::size_t{entry_count_} * kEntrySize));

    auto header = [&](std::uint64_t my, std::uint64_t alt, std::uint64_t entries_lba) {
        std::vector<std::uint8_t> sec(ss_, 0);
        std::memcpy(sec.data(), "EFI PART", 8);
        put_le32(&sec[8], 0x00010000);
        put_le32(&sec[12], 92);
        put_le64(&sec[24], my);
        put_le64(&sec[32], alt);
        put_le64(&sec[40], region.first);
        put_le64(&sec[48], region.first + region.count - 1);
        std::memcpy(&sec[56], disk_guid_.bytes.data(), 16);
        put_le64(&sec[72], entries_lba);
        put_le32(&sec[80], entry_count_);
        put_le32(&sec[84], kEntrySize);
        put_le32(&sec[88], entries_crc);
        put_le32(&sec[16], crc32(std::span<const std::uint8_t>(sec.data(), 92)));
        return sec;
    };

    std::vector<std::uint8_t> pmbr(ss_, 0);
    {
        std::uint8_t* e = &pmbr[446];
        e[0] = 0x00;
        e[1] = 0x00; e[2] = 0x02; e[3] = 0x00;  // CHS of LBA 1
        e[4] = 0xEE;
        e[5] = 0xFF; e[6] = 0xFF; e[7] = 0xFF;
        put_le32(e + 8, 1);
        put_le32(e + 12, static_cast<std::uint32_t>(std::min<std::uint64_t>(total_ - 1, UINT32_MAX)));
        pmbr[510] = 0x55;
        pmbr[511] = 0xAA;
    }

    std::vector<SectorWrite> w;
    w.push_back({0, std::move(pmbr)});
    w.push_back({1, header(1, last_lba, 2)});
    w.push_back({2, entries});
    w.push_back({total_ - 1 - es, entries});
    w.push_back({last_lba, header(last_lba, 1, total_ - 1 - es)});
    return w;
}

Result<std::vector<SectorWrite>> PartitionPlan::encode_mbr() const {
    std::vector<SectorWrite> w;
    std::vector<std::uint8_t> s0(ss_, 0);
    if (boot_code_.size() == 440) std::memcpy(s0.data(), boot_code_.data(), 440);
    put_le32(&s0[440], mbr_signature_);

    auto put_entry = [](std::uint8_t* e, bool boot, std::uint8_t type, std::uint64_t first, std::uint64_t count) {
        e[0] = boot ? 0x80 : 0x00;
        const auto cs = chs_of(first);
        e[1] = cs[0]; e[2] = cs[1]; e[3] = cs[2];
        e[4] = type;
        const auto ce = chs_of(first + count - 1);
        e[5] = ce[0]; e[6] = ce[1]; e[7] = ce[2];
        put_le32(e + 8, static_cast<std::uint32_t>(first));
        put_le32(e + 12, static_cast<std::uint32_t>(count));
    };

    std::vector<const Partition*> logicals;
    for (const auto& p : parts_) {
        if (p.logical) { logicals.push_back(&p); continue; }
        put_entry(&s0[446 + 16 * (p.number - 1)], p.bootable, p.mbr_type, p.first_lba, p.lba_count());
    }
    s0[510] = 0x55;
    s0[511] = 0xAA;
    w.push_back({0, std::move(s0)});

    if (const auto* c = container()) {
        std::sort(logicals.begin(), logicals.end(), [](auto* a, auto* b) { return a->first_lba < b->first_lba; });
        // An extended partition without logicals still needs an (empty) first EBR so tools see it as valid.
        if (logicals.empty()) {
            std::vector<std::uint8_t> ebr(ss_, 0);
            ebr[510] = 0x55;
            ebr[511] = 0xAA;
            w.push_back({c->first_lba, std::move(ebr)});
        }
        for (std::size_t i = 0; i < logicals.size(); ++i) {
            const auto* l = logicals[i];
            const std::uint64_t ebr_lba = i == 0 ? c->first_lba : l->first_lba - 1;
            std::vector<std::uint8_t> ebr(ss_, 0);
            put_entry(&ebr[446], l->bootable, l->mbr_type, l->first_lba - ebr_lba, l->lba_count());
            // put_entry wrote absolute-looking CHS from the relative start; fix CHS to the real LBA.
            const auto cs = chs_of(l->first_lba), ce = chs_of(l->last_lba);
            ebr[447] = cs[0]; ebr[448] = cs[1]; ebr[449] = cs[2];
            ebr[451] = ce[0]; ebr[452] = ce[1]; ebr[453] = ce[2];
            if (i + 1 < logicals.size()) {
                const auto* n = logicals[i + 1];
                const std::uint64_t next_ebr = n->first_lba - 1;
                std::uint8_t* e = &ebr[462];
                e[4] = 0x05;
                const auto cn = chs_of(next_ebr), cl = chs_of(n->last_lba);
                e[1] = cn[0]; e[2] = cn[1]; e[3] = cn[2];
                e[5] = cl[0]; e[6] = cl[1]; e[7] = cl[2];
                put_le32(e + 8, static_cast<std::uint32_t>(next_ebr - c->first_lba));
                put_le32(e + 12, static_cast<std::uint32_t>(n->last_lba + 1 - next_ebr));
            }
            ebr[510] = 0x55;
            ebr[511] = 0xAA;
            w.push_back({ebr_lba, std::move(ebr)});
        }
    }
    return w;
}

Result<std::vector<LbaRange>> PartitionPlan::metadata_ranges() const {
    auto w = encode();
    if (!w) return w.error();
    std::vector<LbaRange> r;
    for (const auto& s : *w) r.push_back({s.lba, s.data.size() / ss_});
    std::sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<LbaRange> merged;
    for (const auto& x : r) {
        if (!merged.empty() && merged.back().first + merged.back().count >= x.first) {
            merged.back().count = std::max(merged.back().first + merged.back().count, x.first + x.count) - merged.back().first;
        } else {
            merged.push_back(x);
        }
    }
    return merged;
}

// ------------------------------------------------------------------ diff

std::string PartitionPlan::diff(const PartitionTable& before) const {
    std::ostringstream o;
    if (before.type != type_) {
        o << "Table: " << table_type_name(before.type) << " -> " << table_type_name(type_)
          << " (all existing partitions are removed)\n";
        for (const auto& p : before.partitions) o << "  - " << describe_partition(p, before.type, before.sector_size) << "\n";
        for (const auto& p : parts_) o << "  + " << describe_partition(p, type_, ss_) << "\n";
        return o.str();
    }
    bool any = false;
    for (const auto& p : before.partitions) {
        const Partition* match = nullptr;
        for (const auto& q : parts_)
            if (q.first_lba == p.first_lba || (type_ == TableType::gpt && q.number == p.number && q.unique_guid == p.unique_guid)) match = &q;
        if (!match) {
            o << "  - " << describe_partition(p, type_, ss_) << "\n";
            any = true;
        } else if (match->first_lba != p.first_lba || match->last_lba != p.last_lba || match->mbr_type != p.mbr_type ||
                   match->bootable != p.bootable || match->type_guid != p.type_guid || match->name != p.name ||
                   match->attributes != p.attributes || match->number != p.number) {
            o << "  ~ " << describe_partition(p, type_, ss_) << "\n    -> " << describe_partition(*match, type_, ss_) << "\n";
            any = true;
        }
    }
    for (const auto& q : parts_) {
        bool existed = false;
        for (const auto& p : before.partitions)
            if (q.first_lba == p.first_lba || (type_ == TableType::gpt && q.number == p.number && q.unique_guid == p.unique_guid)) existed = true;
        if (!existed) {
            o << "  + " << describe_partition(q, type_, ss_) << "\n";
            any = true;
        }
    }
    if (!any) o << "  (no changes to the partition list; the table will be rewritten consistently)\n";
    return o.str();
}

// ------------------------------------------------------------------ apply

Status apply_plan(safety::AuthorizedTarget& target, const PartitionPlan& plan, std::vector<std::string>* notes) {
    if (target.kind() != safety::Kind::partition_edit)
        return Error{Errc::refused, "the authorization was not granted for a partition edit"};
    if (target.sector_size() != plan.sector_size() || target.info().total_lbas() != plan.total_lbas())
        return Error{Errc::refused, "the device geometry differs from the one the plan was made for"};
    auto writes = plan.encode();
    if (!writes) return writes.error();

    const std::size_t align = std::max<std::size_t>(4096, target.required_alignment());
    for (const auto& w : *writes) {
        const std::uint64_t off = w.lba * plan.sector_size();
        if (!target.allows(off, w.data.size()))
            return Error{Errc::refused, "internal error: the plan writes outside the confirmed ranges (LBA " + std::to_string(w.lba) + ")"};
        AlignedBuffer buf(w.data.size(), align);
        std::memcpy(buf.data(), w.data.data(), w.data.size());
        auto r = target.write(off, buf.data(), w.data.size());
        if (!r) return r.error();
    }
    if (auto f = target.flush(); !f) return f;

    for (const auto& w : *writes) {
        AlignedBuffer buf(w.data.size(), align);
        auto r = target.read(w.lba * plan.sector_size(), buf.data(), w.data.size());
        if (!r) return Error{Errc::io_error, "cannot read back LBA " + std::to_string(w.lba) + ": " + r.error().describe()};
        if (std::memcmp(buf.data(), w.data.data(), w.data.size()) != 0)
            return Error{Errc::corrupt_data, "read-back of LBA " + std::to_string(w.lba) + " does not match what was written"};
    }
    if (auto s = target.reread_partition_table(); !s && notes)
        notes->push_back("The operating system did not re-read the partition table (" + s.error().describe() +
                         "); replug the device or reboot before using the new partitions.");
    return {};
}

}  // namespace platter::partitions
