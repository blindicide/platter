// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include "platter/core/bytes.hpp"
#include "platter/mkfs/mkfs.hpp"
#include "platter/partitions/partition_table.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::mkfs;
using platter::safety::Kind;
using platter::safety::TestAccess;
using platter::test::FaultyWriteDevice;
using platter::test::TempDir;

namespace {

BlockDevicePtr open_rw(const std::filesystem::path& p, std::uint32_t ss = 512) {
    OpenOptions oo;
    oo.mode = OpenMode::read_write;
    auto d = open_image(p.string(), oo, ss);
    REQUIRE(d);
    return std::move(d).value();
}

std::unique_ptr<Format> run(safety::AuthorizedTarget t, const FormatConfig& c, bool expect_valid = true) {
    auto f = std::make_unique<Format>(std::move(t), c);
    if (!expect_valid) return f;  // the caller inspects validate() itself
    auto v = f->validate();
    INFO((v ? std::string() : v.error().describe()));
    REQUIRE(v);
    REQUIRE(f->start());
    f->wait();
    return f;
}

std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t o) { return le32(b.data() + o); }
std::uint16_t u16(const std::vector<std::uint8_t>& b, std::size_t o) { return le16(b.data() + o); }

}  // namespace

TEST_CASE("layout: FAT32 follows the cluster size table and the cluster minimum", "[mkfs][layout]") {
    auto l = compute_layout(FsType::fat32, (512ull << 20) / 512, 512, 0);
    REQUIRE(l);
    CHECK(l->sectors_per_cluster == 8);  // 4 KiB clusters for 512 MiB
    CHECK(l->reserved_sectors == 32);
    CHECK(l->fat_count == 2);
    CHECK(l->cluster_count >= 65525);
    CHECK(l->data_offset == l->reserved_sectors + 2ull * l->fat_sectors);

    auto big = compute_layout(FsType::fat32, (64ull << 30) / 512, 512, 0);
    REQUIRE(big);
    CHECK(big->sectors_per_cluster * 512u == 32768);

    auto tiny = compute_layout(FsType::fat32, (8ull << 20) / 512, 512, 0);
    REQUIRE_FALSE(tiny);
    CHECK(tiny.error().code == Errc::invalid_argument);

    auto bad = compute_layout(FsType::fat32, (512ull << 20) / 512, 512, 3000);
    REQUIRE_FALSE(bad);
    auto bad_ss = compute_layout(FsType::fat32, 1 << 20, 1024, 0);
    REQUIRE_FALSE(bad_ss);
}

TEST_CASE("layout: exFAT places bitmap, upcase table and root directory in order", "[mkfs][layout]") {
    auto l = compute_layout(FsType::exfat, (256ull << 20) / 512, 512, 0);
    REQUIRE(l);
    CHECK(l->fat_offset == 24);
    CHECK(l->bitmap_first == 2);
    CHECK(l->upcase_first == l->bitmap_first + l->bitmap_clusters);
    CHECK(l->root_first == l->upcase_first + l->upcase_clusters);
    CHECK(l->bitmap_bytes == (l->cluster_count + 7) / 8);
    CHECK(l->data_offset + l->cluster_count * l->sectors_per_cluster <= (256ull << 20) / 512);

    auto tiny = compute_layout(FsType::exfat, 1024, 512, 0);  // 512 KiB
    REQUIRE_FALSE(tiny);
}

TEST_CASE("FAT32 format writes a consistent boot sector, FSInfo, backups and FATs", "[mkfs][fat32]") {
    for (std::uint32_t ss : {512u, 4096u}) {
        INFO("sector size " << ss);
        TempDir tmp;
        const auto img = tmp / "f.img";
        const std::uint64_t bytes = ss == 512 ? 128ull << 20 : 1ull << 30;  // sparse; 4 KiB sectors need more space
        test::make_sparse_file(img, bytes);
        const std::uint64_t total = bytes / ss;
        FormatConfig c;
        c.type = FsType::fat32;
        c.label = "my disk";
        c.volume_id = 0xCAFEF00D;
        auto f = run(TestAccess::make(open_rw(img, ss), Kind::filesystem_create, {LbaRange{0, total}}), c);
        REQUIRE(f->state() == OperationState::completed);
        const auto r = f->result();
        CHECK(r.verified);

        const auto bs = test::read_at(img, 0, ss);
        CHECK(bs[510] == 0x55);
        CHECK(bs[511] == 0xAA);
        CHECK(u16(bs, 11) == ss);
        CHECK(bs[13] == r.layout.sectors_per_cluster);
        CHECK(u16(bs, 14) == 32);
        CHECK(bs[16] == 2);
        CHECK(u32(bs, 32) == total);
        CHECK(u32(bs, 36) == r.layout.fat_sectors);
        CHECK(u32(bs, 44) == 2);
        CHECK(u16(bs, 48) == 1);
        CHECK(u16(bs, 50) == 6);
        CHECK(u32(bs, 67) == 0xCAFEF00D);
        CHECK(std::string(bs.begin() + 71, bs.begin() + 82) == "MY DISK    ");
        CHECK(std::string(bs.begin() + 82, bs.begin() + 90) == "FAT32   ");

        const auto fsinfo = test::read_at(img, std::uint64_t{ss}, ss);
        CHECK(u32(fsinfo, 0) == 0x41615252);
        CHECK(u32(fsinfo, 484) == 0x61417272);
        CHECK(u32(fsinfo, 508) == 0xAA550000);
        CHECK(test::read_at(img, std::uint64_t{ss} * 6, ss) == bs);  // backup boot sector

        const auto fat = test::read_at(img, std::uint64_t{ss} * 32, 12);
        CHECK(u32(fat, 0) == 0x0FFFFFF8);
        CHECK(u32(fat, 4) == 0x0FFFFFFF);
        CHECK(u32(fat, 8) == 0x0FFFFFFF);  // root directory, one cluster
        const auto fat2 = test::read_at(img, (32 + r.layout.fat_sectors) * ss, 12);
        CHECK(fat2 == fat);
    }
}

TEST_CASE("exFAT format writes matching main and backup boot regions with a valid checksum", "[mkfs][exfat]") {
    for (std::uint32_t ss : {512u, 4096u}) {
        INFO("sector size " << ss);
        TempDir tmp;
        const auto img = tmp / "x.img";
        const std::uint64_t bytes = 64ull << 20;
        test::make_sparse_file(img, bytes);
        const std::uint64_t total = bytes / ss;
        FormatConfig c;
        c.type = FsType::exfat;
        c.label = "Stick";
        c.volume_id = 0x12345678;
        auto f = run(TestAccess::make(open_rw(img, ss), Kind::filesystem_create, {LbaRange{0, total}}), c);
        REQUIRE(f->state() == OperationState::completed);
        const auto r = f->result();
        CHECK(r.verified);

        const auto main = test::read_at(img, 0, std::uint64_t{ss} * 12);
        const auto backup = test::read_at(img, std::uint64_t{ss} * 12, std::uint64_t{ss} * 12);
        CHECK(main == backup);
        CHECK(std::string(main.begin() + 3, main.begin() + 11) == "EXFAT   ");
        CHECK(std::all_of(main.begin() + 11, main.begin() + 64, [](auto b) { return b == 0; }));
        CHECK(le64(main.data() + 72) == total);
        CHECK(u32(main, 80) == 24);
        CHECK(u32(main, 96) == r.layout.root_first);
        CHECK(u32(main, 100) == 0x12345678);
        CHECK(main[108] == std::countr_zero(ss));
        CHECK(main[110] == 1);
        CHECK(main[510] == 0x55);
        CHECK(main[511] == 0xAA);
        const auto ext = test::read_at(img, std::uint64_t{ss} * 1, ss);  // extended boot sector
        CHECK(ext[ss - 2] == 0x55);
        CHECK(ext[ss - 1] == 0xAA);

        // checksum sector: the boot checksum (skipping VolumeFlags and PercentInUse) repeated
        std::uint32_t sum = 0;
        for (std::size_t i = 0; i < std::size_t{ss} * 11; ++i) {
            if (i == 106 || i == 107 || i == 112) continue;
            sum = ((sum & 1) ? 0x80000000u : 0u) + (sum >> 1) + main[i];
        }
        const auto cs = test::read_at(img, std::uint64_t{ss} * 11, ss);
        for (std::size_t i = 0; i < ss; i += 4) REQUIRE(u32(cs, i) == sum);
    }
}

TEST_CASE("format refuses wrong authorization kinds and multiple ranges", "[mkfs][refusal]") {
    TempDir tmp;
    const auto img = tmp / "r.img";
    test::make_sparse_file(img, 64ull << 20);
    const std::uint64_t total = (64ull << 20) / 512;
    {
        auto f = run(TestAccess::make(open_rw(img), Kind::erase, {LbaRange{0, total}}), {}, false);
        auto v = f->validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    {
        auto f = run(TestAccess::make(open_rw(img), Kind::filesystem_create, {LbaRange{0, 100000}, LbaRange{110000, 2000}}), {}, false);
        auto v = f->validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
}

TEST_CASE("format validates labels", "[mkfs][label]") {
    TempDir tmp;
    const auto img = tmp / "l.img";
    test::make_sparse_file(img, 64ull << 20);
    const std::uint64_t total = (64ull << 20) / 512;
    for (const char* bad : {"WAY TOO LONG LABEL", "a*b", "a/b"}) {
        FormatConfig c;
        c.label = bad;
        auto f = run(TestAccess::make(open_rw(img), Kind::filesystem_create, {LbaRange{0, total}}), c, false);
        auto v = f->validate();
        INFO(bad);
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::invalid_argument);
    }
    FormatConfig x;
    x.type = FsType::exfat;
    x.label = "twelve chars";
    auto f = run(TestAccess::make(open_rw(img), Kind::filesystem_create, {LbaRange{0, total}}), x, false);
    REQUIRE_FALSE(f->validate());
}

TEST_CASE("format writes stay inside the volume and leave the data area alone", "[mkfs][range]") {
    TempDir tmp;
    const auto img = tmp / "p.img";
    const std::uint64_t bytes = 128ull << 20;
    test::make_sparse_file(img, bytes);
    const std::uint64_t first = 2048, count = 200000;  // volume inside the image
    test::write_at(img, 0, std::vector<std::uint8_t>(512, 0xEE));
    test::write_at(img, (first + count) * 512, std::vector<std::uint8_t>(512, 0xDD));
    test::write_at(img, (first + count - 1) * 512, std::vector<std::uint8_t>(512, 0xCC));

    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    auto* raw = dev.get();
    FormatConfig c;
    auto f = run(TestAccess::make(std::move(dev), Kind::filesystem_create, {LbaRange{first, count}}), c);
    REQUIRE(f->state() == OperationState::completed);
    for (const auto& w : raw->write_log()) {
        CHECK(w.offset >= first * 512);
        CHECK(w.offset + w.length <= (first + count) * 512);
    }
    CHECK(test::read_at(img, 0, 512) == std::vector<std::uint8_t>(512, 0xEE));
    CHECK(test::read_at(img, (first + count) * 512, 512) == std::vector<std::uint8_t>(512, 0xDD));
    CHECK(test::read_at(img, (first + count - 1) * 512, 512) == std::vector<std::uint8_t>(512, 0xCC));  // quick format
    CHECK(test::read_at(img, first * 512 + 510, 2) == std::vector<std::uint8_t>{0x55, 0xAA});
}

TEST_CASE("a failed write fails the format and leaves no valid boot sector", "[mkfs][fault]") {
    TempDir tmp;
    const auto img = tmp / "w.img";
    test::make_sparse_file(img, 64ull << 20);
    const std::uint64_t total = (64ull << 20) / 512;
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->bad_write_lbas = {40};  // inside the FAT area
    auto f = run(TestAccess::make(std::move(dev), Kind::filesystem_create, {LbaRange{0, total}}), {});
    CHECK(f->state() == OperationState::failed);
    CHECK_FALSE(f->result().verified);
    // The boot sector is written last, so a half-finished format is not recognizable.
    CHECK(test::read_at(img, 510, 2) != std::vector<std::uint8_t>{0x55, 0xAA});
}

TEST_CASE("a silently dropped metadata write is caught by read-back verification", "[mkfs][fault]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 64ull << 20);
    const std::uint64_t total = (64ull << 20) / 512;
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->dropped_lbas = {1};  // FSInfo
    auto f = run(TestAccess::make(std::move(dev), Kind::filesystem_create, {LbaRange{0, total}}), {});
    CHECK(f->state() == OperationState::failed);
    CHECK_FALSE(f->result().verified);
}

TEST_CASE("a formatted device reads back as unpartitioned, not as a bogus MBR", "[mkfs][partitions]") {
    for (auto type : {FsType::fat32, FsType::exfat}) {
        TempDir tmp;
        const auto img = tmp / "v.img";
        test::make_sparse_file(img, 64ull << 20);
        const std::uint64_t total = (64ull << 20) / 512;
        FormatConfig c;
        c.type = type;
        auto f = run(TestAccess::make(open_rw(img), Kind::filesystem_create, {LbaRange{0, total}}), c);
        REQUIRE(f->state() == OperationState::completed);
        auto d = open_image(img.string());
        REQUIRE(d);
        auto t = partitions::read_partition_table(**d);
        REQUIRE(t);
        CHECK(t->type == partitions::TableType::none);
        CHECK(t->partitions.empty());
        REQUIRE_FALSE(t->warnings.empty());
        CHECK(t->warnings[0].find("volume boot record") != std::string::npos);
    }
}

TEST_CASE("format can be cancelled before it starts writing", "[mkfs][cancel]") {
    TempDir tmp;
    const auto img = tmp / "c.img";
    test::make_sparse_file(img, 64ull << 20);
    const std::uint64_t total = (64ull << 20) / 512;
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->write_delay = std::chrono::milliseconds(20);
    auto* raw = dev.get();
    auto f = std::make_unique<Format>(TestAccess::make(std::move(dev), Kind::filesystem_create, {LbaRange{0, total}}), FormatConfig{});
    REQUIRE(f->validate());
    REQUIRE(f->start());
    f->cancel();
    f->wait();
    CHECK((f->state() == OperationState::cancelled || f->state() == OperationState::completed));
    if (f->state() == OperationState::cancelled) CHECK(raw->writes() < 100);
}
