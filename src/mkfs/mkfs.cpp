// SPDX-License-Identifier: TBD
#include "platter/mkfs/mkfs.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <sstream>

#include "platter/core/aligned_buffer.hpp"
#include "platter/core/bytes.hpp"
#include "platter/core/chacha20.hpp"
#include "platter/core/time_util.hpp"

namespace platter::mkfs {

namespace {

constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kFat32MinClusters = 65525;
constexpr std::uint64_t kFat32MaxClusters = 0x0FFFFFF5;
constexpr std::uint64_t kExfatMaxClusters = 0xFFFFFFF5;
constexpr std::size_t kChunk = 1 * 1024 * 1024;

const std::uint16_t kUpcase[] = {
#include "upcase_table.inc"
};
constexpr std::uint32_t kUpcaseBytes = sizeof(kUpcase);

std::uint64_t ceil_div(std::uint64_t a, std::uint64_t b) { return (a + b - 1) / b; }
bool is_pow2(std::uint64_t v) { return v && !(v & (v - 1)); }

std::uint32_t upcase_checksum() {
    std::uint32_t sum = 0;
    for (auto w : kUpcase)
        for (int i = 0; i < 2; ++i) {
            const auto b = static_cast<std::uint8_t>((w >> (8 * i)) & 0xFF);
            sum = ((sum & 1) ? 0x80000000u : 0u) + (sum >> 1) + b;
        }
    return sum;
}

std::vector<std::uint16_t> to_utf16(const std::string& s) {
    std::vector<std::uint16_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<std::uint8_t>(s[i]);
        std::uint32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<std::uint8_t>(s[i + 1]) & 0x3Fu); len = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) { cp = ((c & 0x0Fu) << 12) | ((static_cast<std::uint8_t>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<std::uint8_t>(s[i + 2]) & 0x3Fu); len = 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) { cp = ((c & 0x07u) << 18) | ((static_cast<std::uint8_t>(s[i + 1]) & 0x3Fu) << 12) | ((static_cast<std::uint8_t>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<std::uint8_t>(s[i + 3]) & 0x3Fu); len = 4; }
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

Result<std::string> clean_fat_label(const std::string& label) {
    std::string out;
    for (unsigned char c : label) {
        if (c < 0x20 || c >= 0x7F || std::string("\"*+,./:;<=>?[\\]|").find(static_cast<char>(c)) != std::string::npos)
            return Error{Errc::invalid_argument, "FAT32 labels may only contain printable ASCII without \"*+,./:;<=>?[\\]|"};
        out.push_back(static_cast<char>(std::toupper(c)));
    }
    if (out.size() > 11) return Error{Errc::invalid_argument, "FAT32 labels are limited to 11 characters"};
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

Result<std::vector<std::uint16_t>> clean_exfat_label(const std::string& label) {
    auto u = to_utf16(label);
    if (u.size() > 11) return Error{Errc::invalid_argument, "exFAT labels are limited to 11 characters"};
    for (auto c : u)
        if (c < 0x20 || std::string("\"*/:<>?\\|").find(static_cast<char>(c < 0x80 ? c : 'a')) != std::string::npos)
            return Error{Errc::invalid_argument, "the exFAT label contains a character that is not allowed"};
    return u;
}

// The stub mkfs.fat writes: prints a message and waits for a key.
const std::uint8_t kBootStub[] = {0x0E, 0x1F, 0xBE, 0x77, 0x7C, 0xAC, 0x22, 0xC0, 0x74, 0x0B, 0x56, 0xB4, 0x0E, 0xBB, 0x07,
                                  0x00, 0xCD, 0x10, 0x5E, 0xEB, 0xF0, 0x32, 0xE4, 0xCD, 0x16, 0xCD, 0x19, 0xEB, 0xFE};
const char kBootMessage[] = "This is not a bootable disk.  Please insert a bootable floppy and\r\npress any key to try again ... \r\n";

}  // namespace

std::string_view fs_name(FsType t) noexcept { return t == FsType::fat32 ? "FAT32" : "exFAT"; }

// ------------------------------------------------------------------ layout

Result<Layout> compute_layout(FsType type, std::uint64_t total_sectors, std::uint32_t ss, std::uint32_t cluster_bytes) {
    if (ss != 512 && ss != 4096) return Error{Errc::invalid_argument, "sector size must be 512 or 4096"};
    Layout L;
    L.type = type;
    L.sector_size = ss;
    L.total_sectors = total_sectors;
    const std::uint64_t volume_bytes = total_sectors * ss;

    if (cluster_bytes != 0 && (!is_pow2(cluster_bytes) || cluster_bytes < ss || cluster_bytes > (type == FsType::fat32 ? 65536u : 32u * 1024 * 1024)))
        return Error{Errc::invalid_argument, "the cluster size must be a power of two between the sector size and " +
                                                 std::string(type == FsType::fat32 ? "64 KiB" : "32 MiB")};

    if (type == FsType::fat32) {
        if (total_sectors > 0xFFFFFFFFull) return Error{Errc::invalid_argument, "a FAT32 volume can have at most 2^32 sectors"};
        L.reserved_sectors = 32;
        L.fat_offset = L.reserved_sectors;
        L.fat_count = 2;
        std::uint32_t cb = cluster_bytes;
        const bool automatic = cb == 0;
        if (automatic) {
            cb = volume_bytes <= 260 * kMiB ? 512 : volume_bytes <= 8192 * kMiB ? 4096 : volume_bytes <= 16384 * kMiB ? 8192
                 : volume_bytes <= 32768 * kMiB ? 16384 : 32768;
            cb = std::max<std::uint32_t>(cb, ss);
        }
        for (;;) {
            L.sectors_per_cluster = cb / ss;
            if (total_sectors <= L.reserved_sectors + 2) return Error{Errc::invalid_argument, "the volume is too small for FAT32"};
            const std::uint64_t avail = total_sectors - L.reserved_sectors;
            L.fat_sectors = ceil_div((avail / L.sectors_per_cluster + 2) * 4, ss);
            if (avail <= 2 * L.fat_sectors) return Error{Errc::invalid_argument, "the volume is too small for FAT32"};
            L.cluster_count = (avail - 2 * L.fat_sectors) / L.sectors_per_cluster;
            if (L.cluster_count > kFat32MaxClusters) {
                if (!automatic || cb >= 65536) return Error{Errc::invalid_argument, "too many clusters for FAT32; use a larger cluster size"};
                cb *= 2;
                continue;
            }
            if (L.cluster_count < kFat32MinClusters) {
                if (!automatic || cb <= ss) return Error{Errc::invalid_argument, "the volume is too small for FAT32 (it needs at least 65525 clusters; about 33 MiB with 512-byte clusters)"};
                cb /= 2;
                continue;
            }
            break;
        }
        L.data_offset = L.reserved_sectors + 2ull * L.fat_sectors;
        return L;
    }

    // exFAT
    if (volume_bytes < 1 * kMiB) return Error{Errc::invalid_argument, "the volume is too small for exFAT (minimum 1 MiB)"};
    std::uint32_t cb = cluster_bytes;
    if (cb == 0) {
        cb = volume_bytes <= 256 * kMiB ? 4096 : volume_bytes <= 32768 * kMiB ? 32768 : 131072;
        cb = std::max<std::uint32_t>(cb, ss);
    }
    L.sectors_per_cluster = cb / ss;
    L.fat_count = 1;
    L.fat_offset = 24;
    std::uint64_t clusters = total_sectors / L.sectors_per_cluster;
    for (int i = 0; i < 6; ++i) {
        L.fat_sectors = ceil_div((clusters + 2) * 4, ss);
        L.data_offset = ceil_div(L.fat_offset + L.fat_sectors, L.sectors_per_cluster) * L.sectors_per_cluster;
        if (total_sectors <= L.data_offset) return Error{Errc::invalid_argument, "the volume is too small for exFAT"};
        const auto next = (total_sectors - L.data_offset) / L.sectors_per_cluster;
        if (next == clusters) break;
        clusters = next;
    }
    L.fat_sectors = ceil_div((clusters + 2) * 4, ss);
    L.data_offset = ceil_div(L.fat_offset + L.fat_sectors, L.sectors_per_cluster) * L.sectors_per_cluster;
    if (total_sectors <= L.data_offset) return Error{Errc::invalid_argument, "the volume is too small for exFAT"};
    L.cluster_count = (total_sectors - L.data_offset) / L.sectors_per_cluster;
    if (L.cluster_count > kExfatMaxClusters) return Error{Errc::invalid_argument, "too many clusters for exFAT; use a larger cluster size"};
    L.bitmap_bytes = ceil_div(L.cluster_count, 8);
    L.bitmap_clusters = static_cast<std::uint32_t>(ceil_div(L.bitmap_bytes, cb));
    L.upcase_bytes = kUpcaseBytes;
    L.upcase_clusters = static_cast<std::uint32_t>(ceil_div(kUpcaseBytes, cb));
    L.root_clusters = 1;
    L.bitmap_first = 2;
    L.upcase_first = L.bitmap_first + L.bitmap_clusters;
    L.root_first = L.upcase_first + L.upcase_clusters;
    if (L.cluster_count < static_cast<std::uint64_t>(L.root_first) + L.root_clusters + 1)
        return Error{Errc::invalid_argument, "the volume is too small for exFAT with this cluster size"};
    return L;
}

// ------------------------------------------------------------------ operation

Format::Format(safety::AuthorizedTarget target, FormatConfig config)
    : Operation("format", target.device_id(), AccessKind::exclusive), target_(std::move(target)), config_(std::move(config)) {}

Format::~Format() = default;

FormatResult Format::result() const {
    std::lock_guard lk(m_);
    auto r = r_;
    r.state = state();
    return r;
}

Status Format::do_validate() {
    if (target_.kind() != safety::Kind::filesystem_create)
        return Error{Errc::refused, "the authorization was not granted for creating a file system"};
    if (target_.ranges().size() != 1) return Error{Errc::refused, "a format needs exactly one confirmed range (the volume)"};
    const auto& range = target_.ranges()[0];
    first_lba_ = range.first;
    auto L = compute_layout(config_.type, range.count, target_.sector_size(), config_.cluster_bytes);
    if (!L) return L.error();
    layout_ = *L;
    if (config_.type == FsType::fat32) {
        if (auto l = clean_fat_label(config_.label); !l) return l.error();
    } else {
        if (auto l = clean_exfat_label(config_.label); !l) return l.error();
    }
    if (target_.direct_io() && (layout_.sector_size % target_.required_alignment() != 0 && target_.required_alignment() % layout_.sector_size != 0))
        return Error{Errc::misaligned, "unsupported I/O alignment"};
    volume_id_ = config_.volume_id;
    if (volume_id_ == 0) {
        const auto k = ChaCha20::random_key();
        volume_id_ = le32(k.data());
        if (volume_id_ == 0) volume_id_ = 0x1BADB002u;
    }
    // Progress covers the metadata only.
    const std::uint64_t cb = std::uint64_t{layout_.sectors_per_cluster} * layout_.sector_size;
    std::uint64_t total;
    if (config_.type == FsType::fat32) total = (layout_.reserved_sectors + 2 * layout_.fat_sectors) * std::uint64_t{layout_.sector_size} + cb;
    else total = (layout_.fat_offset + layout_.fat_sectors) * std::uint64_t{layout_.sector_size} + (std::uint64_t{layout_.bitmap_clusters} + layout_.upcase_clusters + layout_.root_clusters) * cb;
    set_total_bytes(total);
    std::lock_guard lk(m_);
    r_ = FormatResult{};
    r_.config = config_;
    r_.layout = layout_;
    r_.device_model = target_.info().model;
    r_.first_lba = first_lba_;
    r_.sectors = range.count;
    r_.volume_id = volume_id_;
    r_.warnings = target_.warnings();
    return {};
}

Status Format::write_region(OperationControl& control, std::uint64_t sector, const std::uint8_t* data, std::size_t bytes) {
    const std::size_t align = std::max<std::size_t>(4096, target_.required_alignment());
    AlignedBuffer buf(std::min(bytes, kChunk), align);
    const std::uint64_t ss = layout_.sector_size;
    std::size_t done = 0;
    while (done < bytes) {
        if (!control.checkpoint()) return Error{Errc::cancelled, "cancelled"};
        const std::size_t n = std::min(bytes - done, kChunk);
        std::memcpy(buf.data(), data + done, n);
        const std::uint64_t off = (first_lba_ * ss) + sector * ss + done;
        auto r = target_.write(off, buf.data(), n);
        if (!r) return r.error();
        done += n;
        add_completed(n, first_lba_ + sector + done / ss);
    }
    return {};
}

Status Format::zero_region(OperationControl& control, std::uint64_t sector, std::uint64_t sectors) {
    const std::size_t align = std::max<std::size_t>(4096, target_.required_alignment());
    AlignedBuffer buf(kChunk, align);
    buf.fill(0);
    const std::uint64_t ss = layout_.sector_size;
    const std::uint64_t total = sectors * ss;
    std::uint64_t done = 0;
    while (done < total) {
        if (!control.checkpoint()) return Error{Errc::cancelled, "cancelled"};
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(total - done, kChunk));
        auto r = target_.write(first_lba_ * ss + sector * ss + done, buf.data(), n);
        if (!r) return r.error();
        done += n;
        add_completed(n, first_lba_ + sector + done / ss);
    }
    return {};
}

Status Format::verify(const std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>>& regions) {
    const std::size_t align = std::max<std::size_t>(4096, target_.required_alignment());
    const std::uint64_t ss = layout_.sector_size;
    for (const auto& [sector, expected] : regions) {
        AlignedBuffer buf(expected.size(), align);
        auto r = target_.read((first_lba_ + sector) * ss, buf.data(), expected.size());
        if (!r) return Error{Errc::io_error, "cannot read back sector " + std::to_string(sector) + ": " + r.error().describe()};
        if (std::memcmp(buf.data(), expected.data(), expected.size()) != 0)
            return Error{Errc::corrupt_data, "read-back of sector " + std::to_string(sector) + " does not match what was written"};
    }
    return {};
}

Status Format::do_run(OperationControl& control) {
    set_message("Writing file system metadata");
    Status s = config_.type == FsType::fat32 ? build_fat32(control) : build_exfat(control);
    if (!s) {
        std::lock_guard lk(m_);
        r_.failure = s.error().describe();
        return s;
    }
    if (auto f = target_.flush(); !f) return f;
    set_message("Verifying");
    if (auto v = verify(to_verify_); !v) {
        std::lock_guard lk(m_);
        r_.failure = v.error().describe();
        return v;
    }
    std::lock_guard lk(m_);
    r_.verified = true;
    r_.warnings.push_back("Quick format: the data area was not erased; old files may still be recoverable. Use ERASE first if that matters.");
    return {};
}

Status Format::build_fat32(OperationControl& control) {
    const auto& L = layout_;
    const std::uint32_t ss = L.sector_size;
    const std::uint64_t cluster_bytes = std::uint64_t{L.sectors_per_cluster} * ss;
    auto label = clean_fat_label(config_.label);

    // 1. Invalidate: clear the reserved area (old boot sector, FSInfo, ...).
    if (auto z = zero_region(control, 0, L.reserved_sectors); !z) return z;
    // 2. Zero both FATs.
    if (auto z = zero_region(control, L.reserved_sectors, 2ull * L.fat_sectors); !z) return z;

    // 3. First FAT sector: media descriptor, end-of-chain markers, root cluster.
    std::vector<std::uint8_t> fat0(ss, 0);
    put_le32(&fat0[0], 0x0FFFFFF8);
    put_le32(&fat0[4], 0x0FFFFFFF);
    put_le32(&fat0[8], 0x0FFFFFFF);  // cluster 2 = root directory, end of chain
    if (auto w = write_region(control, L.reserved_sectors, fat0.data(), fat0.size()); !w) return w;
    if (auto w = write_region(control, L.reserved_sectors + L.fat_sectors, fat0.data(), fat0.size()); !w) return w;
    to_verify_.push_back({L.reserved_sectors, fat0});
    to_verify_.push_back({L.reserved_sectors + L.fat_sectors, fat0});

    // 4. Root directory cluster (with the volume label entry, if any).
    std::vector<std::uint8_t> root(static_cast<std::size_t>(cluster_bytes), 0);
    if (!label->empty()) {
        std::memset(root.data(), ' ', 11);
        std::memcpy(root.data(), label->data(), label->size());
        root[11] = 0x08;  // volume label attribute
        // creation/modification time: now
        const auto t = utc_now();  // "YYYY-MM-DDTHH:MM:SSZ"
        auto num = [&](std::size_t pos, std::size_t len) { return std::stoul(t.substr(pos, len)); };
        const unsigned yr = static_cast<unsigned>(num(0, 4)), mo = static_cast<unsigned>(num(5, 2)), da = static_cast<unsigned>(num(8, 2));
        const unsigned hh = static_cast<unsigned>(num(11, 2)), mi = static_cast<unsigned>(num(14, 2)), se = static_cast<unsigned>(num(17, 2));
        const auto date = static_cast<std::uint16_t>(((yr >= 1980 ? yr - 1980 : 0) << 9) | (mo << 5) | da);
        const auto time = static_cast<std::uint16_t>((hh << 11) | (mi << 5) | (se / 2));
        put_le16(&root[22], time);
        put_le16(&root[24], date);
    }
    const std::uint64_t root_sector = L.data_offset;  // cluster 2
    if (auto w = write_region(control, root_sector, root.data(), root.size()); !w) return w;
    to_verify_.push_back({root_sector, std::vector<std::uint8_t>(root.begin(), root.begin() + std::min<std::size_t>(root.size(), 4096 > ss ? 4096 : ss))});

    // 5. FSInfo + boot sector, then their backups (written last: the volume
    // only becomes recognizable once everything else is in place).
    std::vector<std::uint8_t> fsinfo(ss, 0);
    put_le32(&fsinfo[0], 0x41615252);
    put_le32(&fsinfo[484], 0x61417272);
    put_le32(&fsinfo[488], static_cast<std::uint32_t>(L.cluster_count - 1));  // free clusters
    put_le32(&fsinfo[492], 3);                                                // next free
    put_le32(&fsinfo[508], 0xAA550000);

    std::vector<std::uint8_t> boot(ss, 0);
    boot[0] = 0xEB; boot[1] = 0x58; boot[2] = 0x90;
    std::memcpy(&boot[3], "PLATTER ", 8);
    put_le16(&boot[11], static_cast<std::uint16_t>(ss));
    boot[13] = static_cast<std::uint8_t>(L.sectors_per_cluster);
    put_le16(&boot[14], static_cast<std::uint16_t>(L.reserved_sectors));
    boot[16] = 2;
    put_le16(&boot[17], 0);
    put_le16(&boot[19], 0);
    boot[21] = 0xF8;
    put_le16(&boot[22], 0);
    put_le16(&boot[24], 63);
    put_le16(&boot[26], 255);
    put_le32(&boot[28], first_lba_ <= 0xFFFFFFFFull ? static_cast<std::uint32_t>(first_lba_) : 0);  // hidden sectors
    put_le32(&boot[32], static_cast<std::uint32_t>(L.total_sectors));
    put_le32(&boot[36], static_cast<std::uint32_t>(L.fat_sectors));
    put_le16(&boot[40], 0);
    put_le16(&boot[42], 0);
    put_le32(&boot[44], 2);   // root cluster
    put_le16(&boot[48], 1);   // FSInfo sector
    put_le16(&boot[50], 6);   // backup boot sector
    boot[64] = 0x80;
    boot[66] = 0x29;
    put_le32(&boot[67], volume_id_);
    std::memset(&boot[71], ' ', 11);
    if (label->empty()) std::memcpy(&boot[71], "NO NAME", 7);
    else std::memcpy(&boot[71], label->data(), label->size());
    std::memcpy(&boot[82], "FAT32   ", 8);
    std::memcpy(&boot[90], kBootStub, sizeof kBootStub);
    std::memcpy(&boot[0x77], kBootMessage, sizeof kBootMessage - 1);
    boot[510] = 0x55;
    boot[511] = 0xAA;
    if (ss > 512) {  // keep the signature at the end of larger sectors as well
        boot[ss - 2] = 0x55;
        boot[ss - 1] = 0xAA;
        fsinfo[ss - 2] = 0x55;
        fsinfo[ss - 1] = 0xAA;
    }
    // FSInfo and the boot sector also carry the 0x55AA signature at 510.
    fsinfo[510] = 0x55;
    fsinfo[511] = 0xAA;

    for (const std::uint64_t base : {std::uint64_t{6}, std::uint64_t{0}}) {  // backup first, primary last
        if (auto w = write_region(control, base + 1, fsinfo.data(), fsinfo.size()); !w) return w;
        if (auto w = write_region(control, base, boot.data(), boot.size()); !w) return w;
        to_verify_.push_back({base, boot});
        to_verify_.push_back({base + 1, fsinfo});
    }
    return {};
}

Status Format::build_exfat(OperationControl& control) {
    const auto& L = layout_;
    const std::uint32_t ss = L.sector_size;
    const std::uint64_t cb = std::uint64_t{L.sectors_per_cluster} * ss;
    auto label = clean_exfat_label(config_.label);

    // 1. Invalidate both boot regions, then zero the FAT.
    if (auto z = zero_region(control, 0, 24); !z) return z;
    if (auto z = zero_region(control, L.fat_offset, L.fat_sectors); !z) return z;

    // 2. FAT: media entry + chains for the three system files.
    {
        std::vector<std::uint8_t> head(static_cast<std::size_t>(std::min<std::uint64_t>(L.fat_sectors * ss, ceil_div((std::uint64_t{L.root_first} + L.root_clusters) * 4 + 8, ss) * ss)), 0);
        put_le32(&head[0], 0xFFFFFFF8);
        put_le32(&head[4], 0xFFFFFFFF);
        auto chain = [&](std::uint32_t first, std::uint32_t count) {
            for (std::uint32_t i = 0; i < count; ++i)
                put_le32(&head[4ull * (first + i)], i + 1 == count ? 0xFFFFFFFFu : first + i + 1);
        };
        chain(L.bitmap_first, L.bitmap_clusters);
        chain(L.upcase_first, L.upcase_clusters);
        chain(L.root_first, L.root_clusters);
        if (auto w = write_region(control, L.fat_offset, head.data(), head.size()); !w) return w;
        to_verify_.push_back({L.fat_offset, head});
    }

    // 3. Allocation bitmap: system files marked used.
    const std::uint32_t used_clusters = L.bitmap_clusters + L.upcase_clusters + L.root_clusters;
    {
        std::vector<std::uint8_t> bitmap(static_cast<std::size_t>(L.bitmap_clusters * cb), 0);
        for (std::uint32_t c = 0; c < used_clusters; ++c) bitmap[c / 8] = static_cast<std::uint8_t>(bitmap[c / 8] | (1u << (c % 8)));
        const std::uint64_t sector = L.data_offset + std::uint64_t{L.bitmap_first - 2} * L.sectors_per_cluster;
        if (auto w = write_region(control, sector, bitmap.data(), bitmap.size()); !w) return w;
        to_verify_.push_back({sector, std::vector<std::uint8_t>(bitmap.begin(), bitmap.begin() + std::min<std::size_t>(bitmap.size(), ss))});
    }

    // 4. Up-case table.
    const std::uint32_t upcase_sum = upcase_checksum();
    {
        std::vector<std::uint8_t> up(static_cast<std::size_t>(L.upcase_clusters * cb), 0);
        for (std::size_t i = 0; i < sizeof kUpcase / sizeof kUpcase[0]; ++i) put_le16(&up[2 * i], kUpcase[i]);
        const std::uint64_t sector = L.data_offset + std::uint64_t{L.upcase_first - 2} * L.sectors_per_cluster;
        if (auto w = write_region(control, sector, up.data(), up.size()); !w) return w;
        to_verify_.push_back({sector, std::vector<std::uint8_t>(up.begin(), up.begin() + std::min<std::size_t>(up.size(), ss))});
    }

    // 5. Root directory: label, bitmap, up-case entries.
    {
        std::vector<std::uint8_t> root(static_cast<std::size_t>(L.root_clusters * cb), 0);
        std::uint8_t* e = root.data();
        if (!label->empty()) {
            e[0] = 0x83;
            e[1] = static_cast<std::uint8_t>(label->size());
            for (std::size_t i = 0; i < label->size(); ++i) put_le16(e + 2 + 2 * i, (*label)[i]);
        } else {
            e[0] = 0x03;  // unused label entry
        }
        e += 32;
        e[0] = 0x81;
        e[1] = 0;
        put_le32(e + 20, L.bitmap_first);
        put_le64(e + 24, L.bitmap_bytes);
        e += 32;
        e[0] = 0x82;
        put_le32(e + 4, upcase_sum);
        put_le32(e + 20, L.upcase_first);
        put_le64(e + 24, L.upcase_bytes);
        const std::uint64_t sector = L.data_offset + std::uint64_t{L.root_first - 2} * L.sectors_per_cluster;
        if (auto w = write_region(control, sector, root.data(), root.size()); !w) return w;
        to_verify_.push_back({sector, std::vector<std::uint8_t>(root.begin(), root.begin() + std::min<std::size_t>(root.size(), ss))});
    }

    // 6. Boot regions (main + backup), checksum sector last.
    std::vector<std::uint8_t> region(12ull * ss, 0);
    {
        std::uint8_t* b = region.data();
        b[0] = 0xEB; b[1] = 0x76; b[2] = 0x90;
        std::memcpy(b + 3, "EXFAT   ", 8);
        put_le64(b + 64, first_lba_);
        put_le64(b + 72, L.total_sectors);
        put_le32(b + 80, static_cast<std::uint32_t>(L.fat_offset));
        put_le32(b + 84, static_cast<std::uint32_t>(L.fat_sectors));
        put_le32(b + 88, static_cast<std::uint32_t>(L.data_offset));
        put_le32(b + 92, static_cast<std::uint32_t>(L.cluster_count));
        put_le32(b + 96, L.root_first);
        put_le32(b + 100, volume_id_);
        put_le16(b + 104, 0x0100);
        put_le16(b + 106, 0);
        b[108] = static_cast<std::uint8_t>(std::countr_zero(ss));
        b[109] = static_cast<std::uint8_t>(std::countr_zero(L.sectors_per_cluster));
        b[110] = 1;
        b[111] = 0x80;
        b[112] = static_cast<std::uint8_t>(std::min<std::uint64_t>(100, (std::uint64_t{used_clusters} * 100 + L.cluster_count / 2) / L.cluster_count));
        std::memset(b + 120, 0xF4, 390);
        b[510] = 0x55;
        b[511] = 0xAA;
        for (int i = 1; i <= 8; ++i) put_le32(b + std::size_t{ss} * i + ss - 4, 0xAA550000);  // extended boot sectors
        // sector 9: OEM parameters (all zero = none); sector 10 reserved (zero).
        std::uint32_t sum = 0;
        for (std::size_t i = 0; i < 11ull * ss; ++i) {
            if (i == 106 || i == 107 || i == 112) continue;
            sum = ((sum & 1) ? 0x80000000u : 0u) + (sum >> 1) + b[i];
        }
        for (std::size_t i = 0; i < ss; i += 4) put_le32(b + 11ull * ss + i, sum);
    }
    // Backup first, main last.
    if (auto w = write_region(control, 12, region.data(), region.size()); !w) return w;
    if (auto w = write_region(control, 0, region.data(), region.size()); !w) return w;
    to_verify_.push_back({0, region});
    to_verify_.push_back({12, region});
    return {};
}

std::string format_to_text(const FormatResult& r) {
    std::ostringstream o;
    const auto& L = r.layout;
    o << "platter format report\n=====================\n";
    o << "File system   " << fs_name(L.type) << "\n";
    if (!r.device_model.empty()) o << "Device        " << r.device_model << "\n";
    o << "Volume        LBA " << r.first_lba << ", " << r.sectors << " sectors ("
      << format_capacity(r.sectors * std::uint64_t{L.sector_size}) << ")\n";
    o << "Cluster size  " << L.sectors_per_cluster * L.sector_size << " bytes, " << L.cluster_count << " clusters\n";
    if (!r.config.label.empty()) o << "Label         " << r.config.label << "\n";
    char id[16];
    std::snprintf(id, sizeof id, "%04X-%04X", (r.volume_id >> 16) & 0xFFFF, r.volume_id & 0xFFFF);
    o << "Volume ID     " << id << "\n";
    o << "State         " << operation_state_name(r.state) << "\n";
    o << "Verified      " << (r.verified ? "yes (metadata read back)" : "no") << "\n";
    if (!r.failure.empty()) o << "Failure       " << r.failure << "\n";
    for (const auto& w : r.warnings) o << "Note          " << w << "\n";
    return o.str();
}

}  // namespace platter::mkfs
