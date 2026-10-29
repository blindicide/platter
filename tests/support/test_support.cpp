// SPDX-License-Identifier: LicenseRef-Proprietary
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>

#include "platter/core/bytes.hpp"
#include "platter/partitions/partition_table.hpp"

namespace fs = std::filesystem;

namespace platter::test {

TempDir::TempDir() {
    static std::atomic<unsigned> counter{0};
    std::random_device rd;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = fs::temp_directory_path() /
            ("platter-test-" + std::to_string(rd()) + "-" + std::to_string(stamp) + "-" +
             std::to_string(counter++));
    fs::create_directories(path_);
}

TempDir::~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
}

void make_sparse_file(const fs::path& p, std::uint64_t size) {
    { std::ofstream(p, std::ios::binary | std::ios::trunc); }
    fs::resize_file(p, size);
}

void write_at(const fs::path& p, std::uint64_t offset, const std::vector<std::uint8_t>& data) {
    std::fstream f(p, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    f.seekp(static_cast<std::streamoff>(offset));
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f) throw std::runtime_error("write failed on " + p.string());
}

std::vector<std::uint8_t> read_at(const fs::path& p, std::uint64_t offset, std::size_t len) {
    std::ifstream f(p, std::ios::binary);
    f.seekg(static_cast<std::streamoff>(offset));
    std::vector<std::uint8_t> out(len);
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(len));
    return out;
}

namespace {

void put_guid(std::uint8_t* dst, const std::string& text) {
    auto g = partitions::Guid::parse(text);
    if (!g) throw std::runtime_error("bad guid " + text);
    std::copy(g->bytes.begin(), g->bytes.end(), dst);
}

std::vector<std::uint8_t> gpt_header(std::uint64_t my_lba, std::uint64_t alt_lba, std::uint64_t first_usable,
                                     std::uint64_t last_usable, const std::string& disk_guid,
                                     std::uint64_t entries_lba, std::uint32_t entries_crc, std::uint32_t ss) {
    std::vector<std::uint8_t> h(ss, 0);
    std::copy_n("EFI PART", 8, h.begin());
    put_le32(&h[8], 0x00010000);
    put_le32(&h[12], 92);
    put_le64(&h[24], my_lba);
    put_le64(&h[32], alt_lba);
    put_le64(&h[40], first_usable);
    put_le64(&h[48], last_usable);
    put_guid(&h[56], disk_guid);
    put_le64(&h[72], entries_lba);
    put_le32(&h[80], 128);
    put_le32(&h[84], 128);
    put_le32(&h[88], entries_crc);
    put_le32(&h[16], crc32(std::span<const std::uint8_t>(h.data(), 92)));
    return h;
}

}  // namespace

void write_gpt(const fs::path& image, std::uint64_t total_lbas, std::uint32_t ss, const std::vector<GptPart>& parts,
               const std::string& disk_guid) {
    constexpr std::uint32_t kEntries = 128, kEntrySize = 128;
    std::vector<std::uint8_t> entries(kEntries * kEntrySize, 0);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        auto* e = &entries[i * kEntrySize];
        put_guid(e, parts[i].type_guid);
        // Deterministic unique GUID per partition.
        put_guid(e + 16, "11111111-2222-3333-4444-5555555555" + std::string(i < 10 ? "0" : "") + std::to_string(i));
        put_le64(e + 32, parts[i].first_lba);
        put_le64(e + 40, parts[i].last_lba);
        for (std::size_t c = 0; c < parts[i].name.size() && c < 36; ++c)
            put_le16(e + 56 + 2 * c, static_cast<std::uint16_t>(static_cast<unsigned char>(parts[i].name[c])));
    }
    const std::uint32_t ecrc = crc32(entries);
    const std::uint64_t entry_sectors = (entries.size() + ss - 1) / ss;
    const std::uint64_t last = total_lbas - 1;
    const std::uint64_t first_usable = 2 + entry_sectors;
    const std::uint64_t last_usable = last - entry_sectors - 1;

    // Protective MBR
    std::vector<std::uint8_t> mbr(ss, 0);
    mbr[446 + 4] = 0xEE;
    put_le32(&mbr[446 + 8], 1);
    put_le32(&mbr[446 + 12], static_cast<std::uint32_t>(std::min<std::uint64_t>(last, 0xFFFFFFFFu)));
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    write_at(image, 0, mbr);

    std::vector<std::uint8_t> padded = entries;
    padded.resize(entry_sectors * ss, 0);
    write_at(image, 1ull * ss, gpt_header(1, last, first_usable, last_usable, disk_guid, 2, ecrc, ss));
    write_at(image, 2ull * ss, padded);
    write_at(image, (last - entry_sectors) * ss, padded);
    write_at(image, last * ss,
             gpt_header(last, 1, first_usable, last_usable, disk_guid, last - entry_sectors, ecrc, ss));
}

void write_mbr(const fs::path& image, std::uint32_t ss, const std::vector<MbrPart>& primaries,
               const std::vector<MbrPart>& logicals, std::uint32_t disk_signature) {
    auto put_entry = [](std::uint8_t* e, const MbrPart& p, std::uint32_t start) {
        e[0] = p.bootable ? 0x80 : 0x00;
        e[4] = p.type;
        put_le32(e + 8, start);
        put_le32(e + 12, p.count);
    };
    std::vector<std::uint8_t> mbr(ss, 0);
    put_le32(&mbr[440], disk_signature);
    const MbrPart* ext = nullptr;
    for (std::size_t i = 0; i < primaries.size() && i < 4; ++i) {
        put_entry(&mbr[446 + 16 * i], primaries[i], primaries[i].first_lba);
        if (!ext && (primaries[i].type == 0x05 || primaries[i].type == 0x0F)) ext = &primaries[i];
    }
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    write_at(image, 0, mbr);

    if (!ext || logicals.empty()) return;
    for (std::size_t i = 0; i < logicals.size(); ++i) {
        const std::uint32_t ebr = logicals[i].first_lba - 2048;
        std::vector<std::uint8_t> s(ss, 0);
        put_entry(&s[446], logicals[i], logicals[i].first_lba - ebr);  // relative to this EBR
        if (i + 1 < logicals.size()) {
            const std::uint32_t next_ebr = logicals[i + 1].first_lba - 2048;
            MbrPart link{0x05, 0, logicals[i + 1].count + 2048};
            put_entry(&s[462], link, next_ebr - ext->first_lba);  // relative to extended start
        }
        s[510] = 0x55;
        s[511] = 0xAA;
        write_at(image, std::uint64_t{ebr} * ss, s);
    }
}

}  // namespace platter::test
