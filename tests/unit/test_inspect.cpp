// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>

#include "platter/core/bytes.hpp"
#include "platter/inspect/inspect.hpp"
#include "platter/mkfs/mkfs.hpp"
#include "platter/partitions/partition_table.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::inspect;
using platter::safety::Kind;
using platter::safety::TestAccess;
using platter::test::TempDir;

namespace {

BlockDevicePtr open_ro(const std::filesystem::path& p, std::uint32_t ss = 512) {
    auto d = open_image(p.string(), {}, ss);
    REQUIRE(d);
    return std::move(d).value();
}

const Field* find_field(const Structure& s, const std::string& name) {
    for (const auto& f : s.fields)
        if (f.name == name) return &f;
    return nullptr;
}

bool has_note(const Structure& s, const std::string& part) {
    return std::any_of(s.notes.begin(), s.notes.end(), [&](const std::string& n) { return n.find(part) != std::string::npos; });
}

void format_image(const std::filesystem::path& img, mkfs::FsType t, std::uint64_t bytes) {
    test::make_sparse_file(img, bytes);
    OpenOptions oo;
    oo.mode = OpenMode::read_write;
    auto d = open_image(img.string(), oo, 512);
    REQUIRE(d);
    mkfs::FormatConfig c;
    c.type = t;
    c.label = "INSPECT";
    c.volume_id = 0x1234ABCD;
    mkfs::Format f(TestAccess::make(std::move(d).value(), Kind::filesystem_create, {LbaRange{0, bytes / 512}}), c);
    REQUIRE(f.validate());
    REQUIRE(f.start());
    f.wait();
    REQUIRE(f.state() == OperationState::completed);
}

std::vector<std::uint8_t> sector(const std::filesystem::path& img, std::uint64_t lba, std::size_t n = 512) {
    return test::read_at(img, lba * 512, n);
}

}  // namespace

TEST_CASE("hex_dump renders offset, hex and ASCII columns", "[inspect][hex]") {
    std::vector<std::uint8_t> d(20);
    for (std::size_t i = 0; i < d.size(); ++i) d[i] = static_cast<std::uint8_t>(0x41 + i);
    d[3] = 0x00;
    const auto text = hex_dump(d, 0x100);
    // two rows
    CHECK(std::count(text.begin(), text.end(), '\n') == 2);
    CHECK(text.rfind("000000000100  41 42 43 00 45 46 47 48  49 4A 4B 4C 4D 4E 4F 50  |ABC.EFGHIJKLMNOP|\n", 0) == 0);
    CHECK(text.find("000000000110  51 52 53 54") == text.find('\n') + 1);

    HexLayout l;
    // the layout columns point at the right characters
    const auto first_line = text.substr(0, text.find('\n'));
    CHECK(first_line.substr(l.hex_column(0), 2) == "41");
    CHECK(first_line.substr(l.hex_column(8), 2) == "49");
    CHECK(first_line[l.ascii_column(1)] == 'B');
    CHECK(first_line[l.ascii_column(3)] == '.');
    CHECK(first_line.size() == l.row_width());

    HexLayout l8;
    l8.bytes_per_row = 8;
    const auto t8 = hex_dump(std::span<const std::uint8_t>(d.data(), 8), 0, l8);
    CHECK(t8 == "000000000000  41 42 43 00 45 46 47 48  |ABC.EFGH|\n");
}

TEST_CASE("patterns: hex with wildcards, ASCII, case folding, UTF-16LE", "[inspect][search]") {
    auto p = parse_hex_pattern("55 aa ?? 0x0F");
    REQUIRE(p);
    CHECK(p->bytes.size() == 4);
    CHECK(p->mask == std::vector<std::uint8_t>{0xFF, 0xFF, 0x00, 0xFF});
    const std::uint8_t yes[] = {0x55, 0xAA, 0x99, 0x0F};
    const std::uint8_t no[] = {0x55, 0xAA, 0x99, 0x0E};
    CHECK(matches_at(*p, yes));
    CHECK_FALSE(matches_at(*p, no));

    CHECK_FALSE(parse_hex_pattern(""));
    CHECK_FALSE(parse_hex_pattern("5"));
    CHECK_FALSE(parse_hex_pattern("zz"));
    CHECK_FALSE(parse_hex_pattern("?? ??"));

    const std::string hay = "Hello, World";
    const std::vector<std::uint8_t> buf(hay.begin(), hay.end());
    CHECK(find_in_buffer(buf, ascii_pattern("World")) == 7);
    CHECK_FALSE(find_in_buffer(buf, ascii_pattern("world")).has_value());
    CHECK(find_in_buffer(buf, ascii_pattern("world", true)) == 7);

    auto u = utf16le_pattern("Hi");
    REQUIRE(u);
    CHECK(u->bytes == std::vector<std::uint8_t>{'H', 0, 'i', 0});
    auto emoji = utf16le_pattern("\xF0\x9F\x98\x80");
    REQUIRE(emoji);
    CHECK(emoji->bytes == std::vector<std::uint8_t>{0x3D, 0xD8, 0x00, 0xDE});
    CHECK_FALSE(utf16le_pattern("\xC3"));
}

TEST_CASE("search_device finds matches across block boundaries and honours the range", "[inspect][search]") {
    TempDir tmp;
    const auto img = tmp / "s.img";
    test::make_sparse_file(img, 4ull << 20);
    const std::string needle = "NEEDLE-IN-A-DISK";
    auto put = [&](std::uint64_t off) { test::write_at(img, off, std::vector<std::uint8_t>(needle.begin(), needle.end())); };
    put(100);
    put(1024 * 1024 - 5);          // straddles the 1 MiB block boundary
    put(2 * 1024 * 1024 + 4000);
    auto dev = open_ro(img);

    auto r = search_device(*dev, ascii_pattern(needle), 0, UINT64_MAX);
    REQUIRE(r);
    CHECK(r->complete);
    CHECK(r->hits == std::vector<std::uint64_t>{100, 1024 * 1024 - 5, 2 * 1024 * 1024 + 4000});

    // Small blocks must give the same answer.
    auto r2 = search_device(*dev, ascii_pattern(needle), 0, UINT64_MAX, 1000, {}, 4096);
    REQUIRE(r2);
    CHECK(r2->hits == r->hits);

    // The range limits what is reported, including matches that start before it.
    auto r3 = search_device(*dev, ascii_pattern(needle), 101, 2 * 1024 * 1024);
    REQUIRE(r3);
    CHECK(r3->hits == std::vector<std::uint64_t>{1024 * 1024 - 5});

    // A match that ends past the end of the range is not reported.
    auto r4 = search_device(*dev, ascii_pattern(needle), 0, 110);
    REQUIRE(r4);
    CHECK(r4->hits.empty());

    // max_hits stops early and says so.
    auto r5 = search_device(*dev, ascii_pattern(needle), 0, UINT64_MAX, 2);
    REQUIRE(r5);
    CHECK(r5->hits.size() == 2);
    CHECK(r5->truncated);
    CHECK_FALSE(r5->complete);

    // The progress callback can cancel.
    int calls = 0;
    auto r6 = search_device(*dev, ascii_pattern(needle), 0, UINT64_MAX, 1000, [&](std::uint64_t) { return ++calls < 2; }, 4096);
    REQUIRE(r6);
    CHECK_FALSE(r6->complete);
    CHECK(calls == 2);
}

TEST_CASE("search_device skips unreadable sectors and does not match across them", "[inspect][search]") {
    TempDir tmp;
    const auto img = tmp / "bad.img";
    test::make_sparse_file(img, 1ull << 20);
    const std::string needle = "ABCDEFGH";
    test::write_at(img, 512 - 4, std::vector<std::uint8_t>(needle.begin(), needle.end()));   // spans sectors 0/1
    test::write_at(img, 5 * 512 + 10, std::vector<std::uint8_t>(needle.begin(), needle.end()));  // inside sector 5
    auto inner = open_ro(img);
    test::FaultyWriteDevice dev(std::move(inner));
    dev.bad_read_lbas = {1};
    auto r = search_device(dev, ascii_pattern(needle), 0, UINT64_MAX);
    REQUIRE(r);
    CHECK(r->unreadable_sectors == 1);
    CHECK(r->hits == std::vector<std::uint64_t>{5 * 512 + 10});
    CHECK(r->complete);
}

TEST_CASE("interpret: MBR fields and sanity notes", "[inspect][mbr]") {
    TempDir tmp;
    const auto img = tmp / "m.img";
    test::make_sparse_file(img, 64ull << 20);
    test::write_mbr(img, 512, {{0x0C, 2048, 20000, true}, {0x83, 30000, 10000, false}});
    const auto s0 = sector(img, 0);

    auto det = detect(s0, 0, 512);
    REQUIRE(det);
    CHECK(det->kind == StructKind::mbr);
    auto s = interpret(StructKind::mbr, s0, 512);
    REQUIRE(s);
    CHECK(find_field(*s, "Disk signature")->value.find("0xC0FFEE11") != std::string::npos);
    CHECK(find_field(*s, "Entry 1 status")->note == "bootable");
    CHECK(find_field(*s, "Entry 1 type")->note.find("FAT32") != std::string::npos);
    CHECK(find_field(*s, "Entry 1 first LBA")->value == "2048");
    CHECK(find_field(*s, "Entry 2 sectors")->value == "10000");
    CHECK(find_field(*s, "Entry 3")->value == "(unused)");
    CHECK(find_field(*s, "Boot signature")->note.find("ok") == 0);
    CHECK(s->notes.empty());

    // Break it: bad status byte and overlap.
    auto bad = s0;
    bad[446 + 16] = 0x42;                    // entry 2 status
    put_le32(&bad[446 + 16 + 8], 1000);      // entry 2 starts inside entry 1
    auto s2 = interpret(StructKind::mbr, bad, 512);
    REQUIRE(s2);
    CHECK(has_note(*s2, "invalid status"));
    CHECK(has_note(*s2, "overlap"));

    // Missing signature.
    auto nosig = s0;
    nosig[510] = 0;
    CHECK_FALSE(detect(nosig, 5, 512).has_value());
    auto s3 = interpret(StructKind::mbr, nosig, 512);
    REQUIRE(s3);
    CHECK(has_note(*s3, "boot signature"));

    CHECK_FALSE(interpret(StructKind::mbr, std::span<const std::uint8_t>(s0.data(), 100), 512));
}

TEST_CASE("interpret: GPT header and entries, CRC verdicts", "[inspect][gpt]") {
    TempDir tmp;
    const auto img = tmp / "g.img";
    const std::uint64_t total = (64ull << 20) / 512;
    test::make_sparse_file(img, 64ull << 20);
    test::write_gpt(img, total, 512,
                    {{"C12A7328-F81F-11D2-BA4B-00A0C93EC93B", 2048, 4095, "EFI System"},
                     {"EBD0A0A2-B9E5-4433-87C0-1D6D9D6D1A7C", 4096, 40000, "Data"}},
                    "11111111-2222-3333-4444-555555555555");

    const auto hdr = sector(img, 1);
    auto det = detect(hdr, 1, 512);
    REQUIRE(det);
    CHECK(det->kind == StructKind::gpt_header);
    auto s = interpret(StructKind::gpt_header, hdr, 512);
    REQUIRE(s);
    CHECK(find_field(*s, "Signature")->note == "ok");
    CHECK(find_field(*s, "Header CRC32")->note == "ok");
    CHECK(find_field(*s, "Entry count")->value.find("128") == 0);
    CHECK(find_field(*s, "Entry size")->note == "standard");
    CHECK(find_field(*s, "Disk GUID")->value == "11111111-2222-3333-4444-555555555555");
    CHECK(find_field(*s, "Backup header LBA")->value.find(std::to_string(total - 1)) == 0);

    auto corrupt = hdr;
    corrupt[40] ^= 0x01;  // first usable LBA
    auto s2 = interpret(StructKind::gpt_header, corrupt, 512);
    REQUIRE(s2);
    CHECK(find_field(*s2, "Header CRC32")->note.find("MISMATCH") == 0);
    CHECK(has_note(*s2, "CRC32 does not match"));

    const auto ents = sector(img, 2, 512);
    auto det2 = detect(ents, 2, 512);
    REQUIRE(det2);
    CHECK(det2->kind == StructKind::gpt_entries);
    auto e = interpret(StructKind::gpt_entries, ents, 512);
    REQUIRE(e);
    CHECK(find_field(*e, "Entry 1 type")->note.find("EFI") != std::string::npos);
    CHECK(find_field(*e, "Entry 1 name")->value == "\"EFI System\"");
    CHECK(find_field(*e, "Entry 2 first LBA")->value == "4096");
    CHECK(find_field(*e, "Entry 3 type") == nullptr);  // empty entries are not listed
    CHECK(has_note(*e, "2 of 4 entries"));
}

TEST_CASE("interpret: FAT32 and exFAT boot sectors from the native formatter", "[inspect][fat]") {
    TempDir tmp;
    {
        const auto img = tmp / "f32.img";
        format_image(img, mkfs::FsType::fat32, 256ull << 20);
        const auto bs = sector(img, 0);
        auto det = detect(bs, 0, 512);
        REQUIRE(det);
        CHECK(det->kind == StructKind::fat_boot);
        auto s = interpret(StructKind::fat_boot, bs, 512);
        REQUIRE(s);
        CHECK(find_field(*s, "Bytes per sector")->value.find("512") == 0);
        CHECK(find_field(*s, "File system type")->value == "\"FAT32\"");
        CHECK(find_field(*s, "Volume label")->value == "\"INSPECT\"");
        CHECK(find_field(*s, "Volume ID")->value.find("0x1234ABCD") != std::string::npos);
        CHECK(has_note(*s, "=> FAT32"));
        CHECK(find_field(*s, "Boot signature")->note.find("ok") == 0);
        // A partition-less image holding a VBR is not mistaken for an MBR.
        CHECK(detect(bs, 0, 512)->kind != StructKind::mbr);
    }
    {
        const auto img = tmp / "ex.img";
        format_image(img, mkfs::FsType::exfat, 128ull << 20);
        const auto bs = sector(img, 0);
        auto det = detect(bs, 0, 512);
        REQUIRE(det);
        CHECK(det->kind == StructKind::exfat_boot);
        auto s = interpret(StructKind::exfat_boot, bs, 512);
        REQUIRE(s);
        CHECK(find_field(*s, "File system name")->value == "\"EXFAT\"");
        CHECK(find_field(*s, "Must be zero")->note == "ok");
        CHECK(find_field(*s, "Bytes per sector shift")->value.find("9") == 0);
        CHECK(has_note(*s, "derived: sector 512 B"));
        CHECK(find_field(*s, "File system revision")->value == "1.0");
    }
}

TEST_CASE("interpret: NTFS boot sector, MFT record and ext superblock from synthetic bytes", "[inspect][ntfs][ext]") {
    // NTFS boot sector
    std::vector<std::uint8_t> n(512, 0);
    n[0] = 0xEB; n[1] = 0x52; n[2] = 0x90;
    std::memcpy(&n[3], "NTFS    ", 8);
    put_le16(&n[11], 512);
    n[13] = 8;
    put_le64(&n[40], 2097151);
    put_le64(&n[48], 786432);
    put_le64(&n[56], 2);
    n[64] = 0xF6;  // -10 => 1024 byte records
    n[68] = 1;
    put_le64(&n[72], 0x1122334455667788ull);
    n[510] = 0x55; n[511] = 0xAA;
    auto dn = detect(n, 0, 512);
    REQUIRE(dn);
    CHECK(dn->kind == StructKind::ntfs_boot);
    auto s = interpret(StructKind::ntfs_boot, n, 512);
    REQUIRE(s);
    CHECK(find_field(*s, "Clusters per MFT record")->note == "= 2^10 bytes");
    CHECK(has_note(*s, "$MFT begins at byte 3221225472"));
    CHECK(has_note(*s, "MFT record size 1.00 KiB"));

    // MFT record header
    std::vector<std::uint8_t> m(1024, 0);
    std::memcpy(&m[0], "FILE", 4);
    put_le16(&m[4], 48);
    put_le16(&m[6], 3);
    put_le16(&m[16], 1);
    put_le16(&m[18], 1);
    put_le16(&m[20], 56);
    put_le16(&m[22], 3);  // in use + directory
    put_le32(&m[24], 400);
    put_le32(&m[28], 1024);
    put_le32(&m[44], 5);
    auto dm = detect(m, 100, 512);
    REQUIRE(dm);
    CHECK(dm->kind == StructKind::ntfs_mft_record);
    auto sm = interpret(StructKind::ntfs_mft_record, m, 512);
    REQUIRE(sm);
    CHECK(find_field(*sm, "Flags")->note == "in use directory");
    CHECK(find_field(*sm, "MFT record number")->value.find("5") == 0);
    CHECK(find_field(*sm, "Signature")->note == "ok");

    // ext4 superblock at byte 1024 of a 4096-byte-sector view
    std::vector<std::uint8_t> e(4096, 0);
    auto* sb = &e[1024];
    put_le32(sb + 0, 65536);
    put_le32(sb + 4, 262144);
    put_le32(sb + 24, 2);           // 4 KiB blocks
    put_le32(sb + 40, 8192);
    put_le16(sb + 56, 0xEF53);
    put_le16(sb + 58, 1);
    put_le32(sb + 76, 1);
    put_le16(sb + 88, 256);
    put_le32(sb + 92, 0x4);         // has_journal
    put_le32(sb + 96, 0x2C2);       // filetype, extents, 64bit, flex_bg
    std::memcpy(sb + 120, "rootfs", 6);
    auto de = detect(e, 0, 4096);
    REQUIRE(de);
    CHECK(de->kind == StructKind::ext_superblock);
    CHECK(de->offset == 1024);
    auto se = interpret(StructKind::ext_superblock, std::span<const std::uint8_t>(e.data() + de->offset, 1024), 4096);
    REQUIRE(se);
    CHECK(find_field(*se, "Volume name")->value == "\"rootfs\"");
    CHECK(find_field(*se, "State")->note == "cleanly unmounted");
    CHECK(has_note(*se, "looks like ext4"));
    CHECK(has_note(*se, "1.00 GiB"));
    CHECK(has_note(*se, "8 block groups"));

    // 512-byte sectors: the superblock is the third sector, magic at +56
    std::vector<std::uint8_t> e2(e.begin() + 1024, e.begin() + 1536);
    auto de2 = detect(e2, 2, 512);
    REQUIRE(de2);
    CHECK(de2->kind == StructKind::ext_superblock);
    CHECK(de2->offset == 0);
}

TEST_CASE("detect leaves unknown data alone and names every kind", "[inspect][detect]") {
    std::vector<std::uint8_t> noise(512);
    for (std::size_t i = 0; i < noise.size(); ++i) noise[i] = static_cast<std::uint8_t>(i * 37 + 11);
    CHECK_FALSE(detect(noise, 7, 512).has_value());
    CHECK_FALSE(detect(std::span<const std::uint8_t>(noise.data(), 10), 0, 512).has_value());
    std::vector<std::uint8_t> zeros(512, 0);
    CHECK_FALSE(detect(zeros, 0, 512).has_value());
    for (int i = 0; i < kStructKindCount; ++i) {
        const auto k = static_cast<StructKind>(i);
        CHECK(parse_struct_kind(struct_kind_name(k)) == k);
    }
    CHECK_FALSE(parse_struct_kind("nonsense").has_value());
    CHECK(unix_time_text(0) == "1970-01-01 00:00:00 UTC");
    CHECK(unix_time_text(1700000000) == "2023-11-14 22:13:20 UTC");
}

TEST_CASE("structure_to_text lists offsets, sizes and notes", "[inspect][text]") {
    TempDir tmp;
    const auto img = tmp / "t.img";
    test::make_sparse_file(img, 16ull << 20);
    test::write_mbr(img, 512, {{0x83, 2048, 4096, false}});
    auto s = interpret(StructKind::mbr, sector(img, 0), 512);
    REQUIRE(s);
    const auto text = structure_to_text(*s);
    CHECK(text.find("Master / Extended Boot Record") == 0);
    CHECK(text.find("Entry 1 first LBA") != std::string::npos);
    CHECK(text.find("2048") != std::string::npos);
}
