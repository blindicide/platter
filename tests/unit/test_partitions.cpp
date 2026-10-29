// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "platter/core/bytes.hpp"
#include "platter/partitions/partition_table.hpp"
#include "test_support.hpp"

using namespace platter;
using namespace platter::partitions;
using platter::test::TempDir;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr const char* kEsp = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B";
constexpr const char* kLinux = "0FC63DAF-8483-4772-8E79-3D69D8477DE4";
constexpr const char* kBasic = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7";
constexpr const char* kDisk = "A1B2C3D4-E5F6-4711-8899-AABBCCDDEEFF";

bool has_warning(const PartitionTable& t, const std::string& needle) {
    for (const auto& w : t.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

Result<PartitionTable> table_of(const std::filesystem::path& img, std::uint32_t ss = 512) {
    auto dev = open_image(img.string(), {}, ss);
    if (!dev) return dev.error();
    return read_partition_table(**dev);
}

}  // namespace

TEST_CASE("GUID text round trip uses mixed-endian disk order", "[partitions][guid]") {
    auto g = Guid::parse(kEsp);
    REQUIRE(g);
    CHECK(g->bytes[0] == 0x28);  // 0xC12A7328 stored little-endian
    CHECK(g->bytes[3] == 0xC1);
    CHECK(g->bytes[8] == 0xBA);  // last 8 bytes stored as-is
    CHECK(g->to_string() == kEsp);
    CHECK(gpt_type_name(*g) == "EFI System");
    CHECK_FALSE(Guid::parse("not-a-guid"));
    CHECK_FALSE(Guid::parse("C12A7328-F81F-11D2-BA4B-00A0C93EC93G"));
    CHECK(Guid::parse("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")->to_string() == kEsp);
}

TEST_CASE("blank image has no partition table", "[partitions]") {
    TempDir tmp;
    test::make_sparse_file(tmp / "blank.img", 8 << 20);
    auto t = table_of(tmp / "blank.img");
    REQUIRE(t);
    CHECK(t->type == TableType::none);
    CHECK(t->partitions.empty());
}

TEST_CASE("valid GPT is parsed with names, types and alignment", "[partitions][gpt]") {
    for (std::uint32_t ss : {512u, 4096u}) {
        DYNAMIC_SECTION("sector size " << ss) {
            TempDir tmp;
            const auto img = tmp / "gpt.img";
            const std::uint64_t total = (64ull << 20) / ss;
            const std::uint64_t mib = (1 << 20) / ss;
            test::make_sparse_file(img, total * ss);
            test::write_gpt(img, total, ss,
                            {{kEsp, mib, 9 * mib - 1, "EFI"},
                             {kLinux, 9 * mib, 40 * mib - 1, "root"},
                             {kBasic, 40 * mib + 1, 60 * mib - 1, "data"}},
                            kDisk);
            auto t = table_of(img, ss);
            REQUIRE(t);
            INFO(t->warnings.size() << " warnings");
            CHECK(t->type == TableType::gpt);
            CHECK(t->protective_mbr);
            CHECK_FALSE(t->used_backup_gpt);
            REQUIRE(t->gpt_primary);
            REQUIRE(t->gpt_backup);
            CHECK(t->gpt_primary->header_crc_ok);
            CHECK(t->gpt_primary->entries_crc_ok);
            CHECK(t->gpt_backup->header_crc_ok);
            CHECK(t->gpt_primary->disk_guid.to_string() == kDisk);
            CHECK(t->warnings.empty());

            REQUIRE(t->partitions.size() == 3);
            CHECK(t->partitions[0].number == 1);
            CHECK(t->partitions[0].name == "EFI");
            CHECK(t->partitions[0].type_name == "EFI System");
            CHECK(t->partitions[0].size_bytes == 8ull << 20);
            CHECK(t->partitions[0].aligned_1mib);
            CHECK(t->partitions[1].type_name == "Linux filesystem");
            CHECK(t->partitions[2].type_name == "Microsoft basic data");
            CHECK_FALSE(t->partitions[2].aligned_1mib);  // starts one sector past a MiB boundary
        }
    }
}

TEST_CASE("corrupt primary GPT falls back to the backup", "[partitions][gpt]") {
    TempDir tmp;
    const auto img = tmp / "gpt.img";
    const std::uint64_t total = (32ull << 20) / 512;
    test::make_sparse_file(img, total * 512);
    test::write_gpt(img, total, 512, {{kLinux, 2048, 10239, "root"}}, kDisk);
    test::write_at(img, 512 + 40, {0xFF, 0xFF});  // scribble on primary header (first_usable)

    auto t = table_of(img);
    REQUIRE(t);
    CHECK(t->type == TableType::gpt);
    CHECK(t->used_backup_gpt);
    CHECK_FALSE(t->gpt_primary->header_crc_ok);
    CHECK(has_warning(*t, "header CRC32 mismatch"));
    CHECK(has_warning(*t, "read from the backup GPT"));
    REQUIRE(t->partitions.size() == 1);
    CHECK(t->partitions[0].name == "root");
}

TEST_CASE("corrupt entry array is detected via CRC", "[partitions][gpt]") {
    TempDir tmp;
    const auto img = tmp / "gpt.img";
    const std::uint64_t total = (32ull << 20) / 512;
    test::make_sparse_file(img, total * 512);
    test::write_gpt(img, total, 512, {{kLinux, 2048, 10239, "root"}}, kDisk);
    test::write_at(img, 2 * 512 + 56, {'X'});  // primary entries: first name char

    auto t = table_of(img);
    REQUIRE(t);
    CHECK_FALSE(t->gpt_primary->entries_crc_ok);
    CHECK(t->used_backup_gpt);
    CHECK(t->partitions.at(0).name == "root");
}

TEST_CASE("both GPT copies corrupt yields no trusted partitions", "[partitions][gpt][safety]") {
    TempDir tmp;
    const auto img = tmp / "gpt.img";
    const std::uint64_t total = (32ull << 20) / 512;
    test::make_sparse_file(img, total * 512);
    test::write_gpt(img, total, 512, {{kLinux, 2048, 10239, "root"}}, kDisk);
    test::write_at(img, 512 + 16, {0, 0, 0, 0});
    test::write_at(img, (total - 1) * 512 + 16, {0, 0, 0, 0});

    auto t = table_of(img);
    REQUIRE(t);
    CHECK(t->type == TableType::gpt);
    CHECK(t->partitions.empty());
    CHECK(has_warning(*t, "no valid GPT copy"));
}

TEST_CASE("overlapping and out-of-usable-area GPT entries are flagged", "[partitions][gpt]") {
    TempDir tmp;
    const auto img = tmp / "gpt.img";
    const std::uint64_t total = (32ull << 20) / 512;
    test::make_sparse_file(img, total * 512);
    test::write_gpt(img, total, 512,
                    {{kLinux, 2048, 20000, "a"}, {kLinux, 10000, 30000, "b"}, {kLinux, 10, 30, "too-early"}},
                    kDisk);
    auto t = table_of(img);
    REQUIRE(t);
    CHECK(has_warning(*t, "overlap"));
    CHECK(has_warning(*t, "outside the GPT usable area"));
}

TEST_CASE("missing protective MBR is a warning, not a failure", "[partitions][gpt]") {
    TempDir tmp;
    const auto img = tmp / "gpt.img";
    const std::uint64_t total = (32ull << 20) / 512;
    test::make_sparse_file(img, total * 512);
    test::write_gpt(img, total, 512, {{kLinux, 2048, 10239, "root"}}, kDisk);
    test::write_at(img, 0, std::vector<std::uint8_t>(512, 0));
    auto t = table_of(img);
    REQUIRE(t);
    CHECK(t->type == TableType::gpt);
    CHECK_FALSE(t->protective_mbr);
    CHECK(has_warning(*t, "protective MBR"));
    CHECK(t->partitions.size() == 1);
}

TEST_CASE("MBR with primary and logical partitions", "[partitions][mbr]") {
    TempDir tmp;
    const auto img = tmp / "mbr.img";
    test::make_sparse_file(img, 64ull << 20);
    test::write_mbr(img, 512,
                    {{0x07, 2048, 20480, true}, {0x0F, 30720, 100000}},
                    {{0x83, 32768, 20000}, {0x82, 55296, 10000}});
    auto t = table_of(img);
    REQUIRE(t);
    CHECK(t->type == TableType::mbr);
    CHECK(t->mbr_disk_signature == 0xC0FFEE11);
    INFO("warnings: " << (t->warnings.empty() ? "" : t->warnings.front()));
    CHECK(t->warnings.empty());
    REQUIRE(t->partitions.size() == 4);
    CHECK(t->partitions[0].bootable);
    CHECK(t->partitions[0].type_name == "NTFS/exFAT/HPFS");
    CHECK(t->partitions[1].extended_container);
    CHECK(t->partitions[2].logical);
    CHECK(t->partitions[2].number == 5);
    CHECK(t->partitions[2].first_lba == 32768);
    CHECK(t->partitions[2].type_name == "Linux");
    CHECK(t->partitions[3].number == 6);
    CHECK(t->partitions[3].first_lba == 55296);
    CHECK(t->partitions[3].type_name == "Linux swap");
}

TEST_CASE("EBR chain loops are detected and do not hang", "[partitions][mbr][safety]") {
    TempDir tmp;
    const auto img = tmp / "loop.img";
    test::make_sparse_file(img, 64ull << 20);
    // Extended starts at 32768 (= first EBR). Second EBR at 38912 for the logical at 40960.
    test::write_mbr(img, 512, {{0x0F, 32768, 100000}}, {{0x83, 34816, 1000}, {0x83, 40960, 1000}});
    // Make the second EBR point at itself as "next".
    std::vector<std::uint8_t> link(16, 0);
    link[4] = 0x05;
    put_le32(&link[8], 38912 - 32768);  // relative to extended start => same EBR
    put_le32(&link[12], 1000);
    test::write_at(img, std::uint64_t{38912} * 512 + 462, link);

    auto t = table_of(img);
    REQUIRE(t);
    CHECK(has_warning(*t, "loops back"));
    CHECK(t->partitions.size() == 3);  // container + two logicals, no duplicates
}

TEST_CASE("MBR partition past end of device is flagged", "[partitions][mbr]") {
    TempDir tmp;
    const auto img = tmp / "small.img";
    test::make_sparse_file(img, 4ull << 20);  // 8192 sectors
    test::write_mbr(img, 512, {{0x83, 2048, 100000}});
    auto t = table_of(img);
    REQUIRE(t);
    CHECK(has_warning(*t, "extends past the end"));
}

TEST_CASE("MBR type names", "[partitions][mbr]") {
    CHECK(mbr_type_name(0x83) == "Linux");
    CHECK(mbr_type_name(0xEE) == "GPT protective");
    CHECK_THAT(mbr_type_name(0x99), ContainsSubstring("0x99"));
}

#ifndef _WIN32
#include <catch2/catch_message.hpp>
#include <fstream>
#include "platter/core/process.hpp"

// Cross-check against an independent implementation: util-linux sfdisk writes
// the table, platter reads it back. Skipped when sfdisk is not installed.
TEST_CASE("tables written by sfdisk are read back identically", "[partitions][crosscheck]") {
    const auto sfdisk = find_in_path("sfdisk").empty() ? std::string("/usr/sbin/sfdisk") : find_in_path("sfdisk");
    if (find_in_path(sfdisk).empty()) { SUCCEED("sfdisk not available"); return; }

    TempDir tmp;
    auto run_sfdisk = [&](const std::filesystem::path& img, const std::string& script) {
        const auto sp = tmp / "script.sfdisk";
        std::ofstream(sp) << script;
        // sfdisk reads the script from stdin; use sh only to redirect a file we wrote.
        auto r = run_process({"sh", "-c", "\"$0\" -q \"$1\" < \"$2\"", sfdisk, img.string(), sp.string()},
                             std::chrono::seconds(30));
        REQUIRE(r);
        INFO(r->err);
        REQUIRE(r->exit_code == 0);
    };

    SECTION("GPT") {
        const auto img = tmp / "gpt.img";
        test::make_sparse_file(img, 256ull << 20);
        run_sfdisk(img,
                   "label: gpt\n"
                   "label-id: A1B2C3D4-E5F6-4711-8899-AABBCCDDEEFF\n"
                   "size=64MiB, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name=\"EFI system\"\n"
                   "size=128MiB, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name=\"root\"\n"
                   "type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, name=\"data\"\n");
        auto t = table_of(img);
        REQUIRE(t);
        CHECK(t->type == TableType::gpt);
        CHECK(t->warnings.empty());
        CHECK(t->protective_mbr);
        CHECK(t->gpt_primary->disk_guid.to_string() == "A1B2C3D4-E5F6-4711-8899-AABBCCDDEEFF");
        REQUIRE(t->partitions.size() == 3);
        CHECK(t->partitions[0].first_lba == 2048);
        CHECK(t->partitions[0].last_lba == 133119);
        CHECK(t->partitions[0].name == "EFI system");
        CHECK(t->partitions[1].first_lba == 133120);
        CHECK(t->partitions[1].last_lba == 395263);
        CHECK(t->partitions[2].first_lba == 395264);
        CHECK(t->partitions[2].last_lba == 522239);
        CHECK(t->partitions[2].type_name == "Microsoft basic data");
    }
    SECTION("MBR with logical partitions") {
        const auto img = tmp / "mbr.img";
        test::make_sparse_file(img, 128ull << 20);
        run_sfdisk(img,
                   "label: dos\nlabel-id: 0x3d74374d\n"
                   "size=32MiB, type=7, bootable\ntype=f\nsize=24MiB, type=83\ntype=82\n");
        auto t = table_of(img);
        REQUIRE(t);
        CHECK(t->type == TableType::mbr);
        CHECK(t->mbr_disk_signature == 0x3d74374du);
        CHECK(t->warnings.empty());
        REQUIRE(t->partitions.size() == 4);
        CHECK(t->partitions[0].bootable);
        CHECK(t->partitions[1].extended_container);
        CHECK(t->partitions[2].number == 5);
        CHECK(t->partitions[2].first_lba == 69632);
        CHECK(t->partitions[2].last_lba == 118783);
        CHECK(t->partitions[3].number == 6);
        CHECK(t->partitions[3].first_lba == 120832);
        CHECK(t->partitions[3].last_lba == 262143);
    }
}
#endif
