// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_test_macros.hpp>

#include "platter/partitions/partition_edit.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::partitions;
using platter::safety::Kind;
using platter::safety::TestAccess;
using platter::test::TempDir;

namespace {

constexpr std::uint64_t kMiBLbas = 2048;  // 1 MiB in 512-byte sectors

BlockDevicePtr open_rw(const std::filesystem::path& p, std::uint32_t ss = 512) {
    OpenOptions oo;
    oo.mode = OpenMode::read_write;
    auto d = open_image(p.string(), oo, ss);
    REQUIRE(d);
    return std::move(d).value();
}

PartitionTable read_table(const std::filesystem::path& p, std::uint32_t ss = 512) {
    auto d = open_image(p.string(), {}, ss);
    REQUIRE(d);
    auto t = read_partition_table(**d);
    REQUIRE(t);
    return std::move(t).value();
}

// Applies a plan to an image exactly like production code: confirmed ranges =
// the plan's metadata ranges.
Status apply(const std::filesystem::path& img, const PartitionPlan& plan, std::uint32_t ss = 512,
             std::vector<std::string>* notes = nullptr) {
    auto ranges = plan.metadata_ranges();
    REQUIRE(ranges);
    auto target = TestAccess::make(open_rw(img, ss), Kind::partition_edit, *ranges);
    return apply_plan(target, plan, notes);
}

PartitionSpec spec(std::uint64_t first, std::uint64_t count) {
    PartitionSpec s;
    s.first_lba = first;
    s.lba_count = count;
    return s;
}

}  // namespace

TEST_CASE("GPT: a new table with partitions reads back clean", "[partitions][edit]") {
    TempDir tmp;
    const auto img = tmp / "g.img";
    test::make_sparse_file(img, 64ull << 20);
    auto plan = PartitionPlan::blank(TableType::gpt, 512, (64ull << 20) / 512);
    REQUIRE(plan);
    auto a = spec(kMiBLbas, 20 * kMiBLbas);
    a.type_guid = *Guid::parse(find_type_alias("esp")->gpt_guid);
    a.name = "EFI";
    auto n1 = plan->add(a);
    REQUIRE(n1);
    CHECK(*n1 == 1);
    auto b = spec(21 * kMiBLbas, 0);  // fill the rest
    b.name = "root \xC3\xA9\xE2\x82\xAC";  // non-ASCII survives
    auto n2 = plan->add(b);
    REQUIRE(n2);
    CHECK(plan->problems().empty());
    CHECK(plan->warnings().empty());

    REQUIRE(apply(img, *plan));
    const auto t = read_table(img);
    CHECK(t.type == TableType::gpt);
    CHECK(t.warnings.empty());
    CHECK(t.protective_mbr);
    REQUIRE(t.gpt_primary);
    REQUIRE(t.gpt_backup);
    CHECK(t.gpt_primary->header_crc_ok);
    CHECK(t.gpt_primary->entries_crc_ok);
    CHECK(t.gpt_backup->header_crc_ok);
    CHECK(t.gpt_backup->entries_crc_ok);
    CHECK_FALSE(t.used_backup_gpt);
    REQUIRE(t.partitions.size() == 2);
    CHECK(t.partitions[0].first_lba == kMiBLbas);
    CHECK(t.partitions[0].last_lba == 21 * kMiBLbas - 1);
    CHECK(t.partitions[0].name == "EFI");
    CHECK(t.partitions[0].type_name == "EFI System");
    CHECK(t.partitions[1].first_lba == 21 * kMiBLbas);
    CHECK(t.partitions[1].last_lba == t.gpt_primary->last_usable_lba);
    CHECK(t.partitions[1].name == "root \xC3\xA9\xE2\x82\xAC");
    CHECK(t.partitions[0].unique_guid != t.partitions[1].unique_guid);
}

TEST_CASE("GPT: 4096-byte sectors", "[partitions][edit]") {
    TempDir tmp;
    const auto img = tmp / "g4k.img";
    test::make_sparse_file(img, 64ull << 20);
    auto plan = PartitionPlan::blank(TableType::gpt, 4096, (64ull << 20) / 4096);
    REQUIRE(plan);
    REQUIRE(plan->add(spec(256, 1000)));
    REQUIRE(apply(img, *plan, 4096));
    const auto t = read_table(img, 4096);
    CHECK(t.warnings.empty());
    REQUIRE(t.partitions.size() == 1);
    CHECK(t.partitions[0].first_lba == 256);
    CHECK(t.sector_size == 4096);
}

TEST_CASE("MBR: primaries, extended and logicals round-trip", "[partitions][edit]") {
    TempDir tmp;
    const auto img = tmp / "m.img";
    test::make_sparse_file(img, 128ull << 20);
    auto plan = PartitionPlan::blank(TableType::mbr, 512, (128ull << 20) / 512);
    REQUIRE(plan);
    auto p1 = spec(kMiBLbas, 10 * kMiBLbas);
    p1.mbr_type = 0x0C;
    p1.bootable = true;
    REQUIRE(plan->add(p1));
    auto ext = spec(11 * kMiBLbas, 60 * kMiBLbas);
    ext.extended = true;
    auto en = plan->add(ext);
    REQUIRE(en);
    CHECK(*en == 2);
    // Logicals need an EBR sector before them.
    auto l1 = spec(12 * kMiBLbas, 10 * kMiBLbas);
    l1.logical = true;
    l1.mbr_type = 0x83;
    REQUIRE(plan->add(l1));
    auto l2 = spec(23 * kMiBLbas, 10 * kMiBLbas);
    l2.logical = true;
    l2.mbr_type = 0x07;
    REQUIRE(plan->add(l2));
    auto l3 = spec(40 * kMiBLbas, 0);  // fill to the end of the container
    l3.logical = true;
    l3.mbr_type = 0x82;
    REQUIRE(plan->add(l3));
    CHECK(plan->problems().empty());

    REQUIRE(apply(img, *plan));
    const auto t = read_table(img);
    CHECK(t.type == TableType::mbr);
    CHECK(t.warnings.empty());
    REQUIRE(t.partitions.size() == 5);
    CHECK(t.partitions[0].mbr_type == 0x0C);
    CHECK(t.partitions[0].bootable);
    CHECK(t.partitions[1].extended_container);
    CHECK(t.partitions[2].logical);
    CHECK(t.partitions[2].number == 5);
    CHECK(t.partitions[2].first_lba == 12 * kMiBLbas);
    CHECK(t.partitions[3].first_lba == 23 * kMiBLbas);
    CHECK(t.partitions[3].mbr_type == 0x07);
    CHECK(t.partitions[4].first_lba == 40 * kMiBLbas);
    CHECK(t.partitions[4].last_lba == 71 * kMiBLbas - 1);
    CHECK(t.partitions[4].mbr_type == 0x82);
    CHECK(t.mbr_disk_signature != 0);
}

TEST_CASE("MBR: existing boot code and signature survive an edit", "[partitions][edit]") {
    TempDir tmp;
    const auto img = tmp / "b.img";
    test::make_sparse_file(img, 64ull << 20);
    test::write_mbr(img, 512, {{0x83, 2048, 20000}}, {}, 0xAABBCCDD);
    std::vector<std::uint8_t> code(440);
    for (std::size_t i = 0; i < code.size(); ++i) code[i] = static_cast<std::uint8_t>(i * 7 + 1);
    test::write_at(img, 0, code);
    // write_at overwrote the first 440 bytes only: signature at 440 is intact.

    auto dev = open_image(img.string(), {}, 512);
    REQUIRE(dev);
    auto plan = PartitionPlan::from_device(**dev);
    REQUIRE(plan);
    REQUIRE(plan->add(spec(30000, 10000)));
    REQUIRE(apply(img, *plan));
    CHECK(test::read_at(img, 0, 440) == code);
    const auto t = read_table(img);
    CHECK(t.mbr_disk_signature == 0xAABBCCDD);
    CHECK(t.partitions.size() == 2);
}

TEST_CASE("plans reject invalid edits and leave the plan unchanged", "[partitions][edit]") {
    const std::uint64_t total = (64ull << 20) / 512;
    auto g = PartitionPlan::blank(TableType::gpt, 512, total);
    REQUIRE(g);
    REQUIRE(g->add(spec(kMiBLbas, 10 * kMiBLbas)));
    // Overlap
    CHECK_FALSE(g->add(spec(5 * kMiBLbas, 10 * kMiBLbas)));
    // Before the first usable LBA
    CHECK_FALSE(g->add(spec(10, 100)));
    // Past the last usable LBA
    CHECK_FALSE(g->add(spec(total - 10, 100)));
    // Too-long name
    PartitionSpec named = spec(20 * kMiBLbas, 100);
    named.name = std::string(37, 'x');
    CHECK_FALSE(g->add(named));
    // No logicals / extended on GPT
    PartitionSpec ext = spec(20 * kMiBLbas, 100);
    ext.extended = true;
    CHECK_FALSE(g->add(ext));
    CHECK(g->partitions().size() == 1);
    CHECK(g->problems().empty());

    auto m = PartitionPlan::blank(TableType::mbr, 512, total);
    REQUIRE(m);
    for (int i = 0; i < 4; ++i) REQUIRE(m->add(spec(static_cast<std::uint64_t>(i + 1) * kMiBLbas, 1000)));
    CHECK_FALSE(m->add(spec(10 * kMiBLbas, 1000)));  // fifth primary
    PartitionSpec lg = spec(20 * kMiBLbas, 100);
    lg.logical = true;
    CHECK_FALSE(m->add(lg));  // no extended partition
    PartitionSpec zero = spec(30 * kMiBLbas, 100);
    zero.mbr_type = 0;
    CHECK_FALSE(m->add(zero));
    CHECK(m->partitions().size() == 4);
}

TEST_CASE("remove, resize and property edits", "[partitions][edit]") {
    const std::uint64_t total = (64ull << 20) / 512;
    auto g = PartitionPlan::blank(TableType::gpt, 512, total);
    REQUIRE(g);
    REQUIRE(g->add(spec(kMiBLbas, 10 * kMiBLbas)));
    REQUIRE(g->add(spec(11 * kMiBLbas, 10 * kMiBLbas)));
    REQUIRE(g->add(spec(21 * kMiBLbas, 10 * kMiBLbas)));
    REQUIRE(g->remove(2));
    CHECK(g->find(2) == nullptr);
    auto n = g->add(spec(11 * kMiBLbas, 5 * kMiBLbas));  // reuses slot 2
    REQUIRE(n);
    CHECK(*n == 2);
    CHECK_FALSE(g->resize(2, 11 * kMiBLbas, 25 * kMiBLbas));  // would overlap #3
    CHECK(g->find(2)->last_lba == 16 * kMiBLbas - 1);
    CHECK(g->resize(2, 11 * kMiBLbas, 20 * kMiBLbas));
    CHECK(g->set_name(2, "data"));
    CHECK(g->find(2)->name == "data");
    CHECK(g->set_type_guid(2, *Guid::parse(find_type_alias("swap")->gpt_guid)));
    CHECK(g->find(2)->type_name == "Linux swap");
    CHECK_FALSE(g->set_mbr_type(2, 0x83));
    CHECK_FALSE(g->set_bootable(2, true));
    CHECK_FALSE(g->remove(99));
    CHECK(g->set_attributes(2, 1ull << 2));

    auto m = PartitionPlan::blank(TableType::mbr, 512, total);
    REQUIRE(m);
    REQUIRE(m->add(spec(kMiBLbas, 1000)));
    REQUIRE(m->add(spec(2 * kMiBLbas, 1000)));
    CHECK(m->set_bootable(1, true));
    CHECK(m->set_bootable(2, true));
    CHECK_FALSE(m->find(1)->bootable);  // only one active primary
    CHECK(m->set_mbr_type(2, 0x82));
    CHECK_FALSE(m->set_mbr_type(2, 0x0F));  // cannot turn a data partition extended
    CHECK_FALSE(m->set_name(1, "x"));
}

TEST_CASE("deleting an MBR extended partition removes its logicals", "[partitions][edit]") {
    auto m = PartitionPlan::blank(TableType::mbr, 512, (64ull << 20) / 512);
    REQUIRE(m);
    PartitionSpec ext = spec(kMiBLbas, 40 * kMiBLbas);
    ext.extended = true;
    REQUIRE(m->add(ext));
    PartitionSpec l = spec(2 * kMiBLbas, 5 * kMiBLbas);
    l.logical = true;
    REQUIRE(m->add(l));
    l.first_lba = 10 * kMiBLbas;
    REQUIRE(m->add(l));
    CHECK(m->partitions().size() == 3);
    REQUIRE(m->remove(1));
    CHECK(m->partitions().empty());
}

TEST_CASE("free space helpers", "[partitions][edit]") {
    const std::uint64_t total = (64ull << 20) / 512;
    auto g = PartitionPlan::blank(TableType::gpt, 512, total);
    REQUIRE(g);
    CHECK(g->first_fit_start() == kMiBLbas);  // first usable is 34, 1 MiB aligned start is 2048
    REQUIRE(g->add(spec(kMiBLbas, 10 * kMiBLbas)));
    CHECK(g->first_fit_start() == 11 * kMiBLbas);
    const auto spans = g->free_spans();
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].first == 34);
    CHECK(spans[0].count == kMiBLbas - 34);
    CHECK(spans[1].first == 11 * kMiBLbas);
    CHECK(spans[1].first + spans[1].count - 1 == g->usable().last());
}

TEST_CASE("alignment is a warning, not a blocker", "[partitions][edit]") {
    auto g = PartitionPlan::blank(TableType::gpt, 512, (64ull << 20) / 512);
    REQUIRE(g);
    REQUIRE(g->add(spec(100, 1000)));
    CHECK(g->problems().empty());
    REQUIRE(g->warnings().size() == 1);
    CHECK(g->warnings()[0].find("1 MiB") != std::string::npos);
}

TEST_CASE("apply writes only the table areas and refuses anything else", "[partitions][edit][safety]") {
    TempDir tmp;
    const auto img = tmp / "s.img";
    const std::uint64_t total = (64ull << 20) / 512;
    test::make_sparse_file(img, total * 512);
    // Data in the middle must never be touched.
    test::write_at(img, 5 << 20, std::vector<std::uint8_t>(4096, 0xAB));

    auto plan = PartitionPlan::blank(TableType::gpt, 512, total);
    REQUIRE(plan);
    REQUIRE(plan->add(spec(kMiBLbas, 10 * kMiBLbas)));
    auto ranges = plan->metadata_ranges();
    REQUIRE(ranges);
    REQUIRE(ranges->size() == 2);  // LBA 0..33 and the last 33 LBAs
    CHECK((*ranges)[0] == LbaRange{0, 34});
    CHECK((*ranges)[1] == LbaRange{total - 33, 33});

    {
        auto dev = std::make_unique<test::FaultyWriteDevice>(open_rw(img));
        auto* raw = dev.get();
        auto target = TestAccess::make(std::move(dev), Kind::partition_edit, *ranges);
        REQUIRE(apply_plan(target, *plan));
        for (const auto& rec : raw->write_log()) {
            const bool in_head = rec.offset + rec.length <= 34 * 512;
            const bool in_tail = rec.offset >= (total - 33) * 512;
            CHECK((in_head || in_tail));
        }
    }
    CHECK(test::read_at(img, 5 << 20, 4096) == std::vector<std::uint8_t>(4096, 0xAB));

    // A narrower authorization makes the same plan fail instead of writing.
    {
        auto target = TestAccess::make(open_rw(img), Kind::partition_edit, {LbaRange{0, 2}});
        auto s = apply_plan(target, *plan);
        REQUIRE_FALSE(s);
        CHECK(s.error().code == Errc::refused);
    }
    // The wrong kind of authorization too.
    {
        auto target = TestAccess::make(open_rw(img), Kind::erase, *ranges);
        auto s = apply_plan(target, *plan);
        REQUIRE_FALSE(s);
        CHECK(s.error().code == Errc::refused);
    }
    // And a plan made for a different geometry.
    {
        auto other = PartitionPlan::blank(TableType::gpt, 512, total + 8192);
        REQUIRE(other);
        auto target = TestAccess::make(open_rw(img), Kind::partition_edit, {LbaRange{0, 34}});
        auto s = apply_plan(target, *other);
        REQUIRE_FALSE(s);
        CHECK(s.error().code == Errc::refused);
    }
}

TEST_CASE("editing an existing GPT keeps its disk GUID and partitions", "[partitions][edit]") {
    TempDir tmp;
    const auto img = tmp / "e.img";
    const std::uint64_t total = (64ull << 20) / 512;
    test::make_sparse_file(img, total * 512);
    test::write_gpt(img, total, 512,
                    {{"0FC63DAF-8483-4772-8E79-3D69D8477DE4", 2048, 40959, "one"},
                     {"C12A7328-F81F-11D2-BA4B-00A0C93EC93B", 40960, 81919, "two"}},
                    "11111111-2222-3333-4444-555555555555");
    auto dev = open_image(img.string(), {}, 512);
    REQUIRE(dev);
    auto plan = PartitionPlan::from_device(**dev);
    REQUIRE(plan);
    REQUIRE(plan->partitions().size() == 2);
    REQUIRE(plan->add(spec(81920, 40960)));
    REQUIRE(apply(img, *plan));
    const auto t = read_table(img);
    CHECK(t.warnings.empty());
    CHECK(t.gpt_primary->disk_guid.to_string() == "11111111-2222-3333-4444-555555555555");
    REQUIRE(t.partitions.size() == 3);
    CHECK(t.partitions[0].name == "one");
    CHECK(t.partitions[1].name == "two");
    CHECK(t.partitions[2].first_lba == 81920);
}

TEST_CASE("diff lists additions, removals and changes", "[partitions][edit]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    const std::uint64_t total = (64ull << 20) / 512;
    test::make_sparse_file(img, total * 512);
    auto plan = PartitionPlan::blank(TableType::gpt, 512, total);
    REQUIRE(plan);
    REQUIRE(plan->add(spec(kMiBLbas, 10 * kMiBLbas)));
    REQUIRE(plan->add(spec(11 * kMiBLbas, 10 * kMiBLbas)));
    REQUIRE(apply(img, *plan));

    auto before = read_table(img);
    auto dev = open_image(img.string(), {}, 512);
    auto next = PartitionPlan::from_device(**dev);
    REQUIRE(next);
    CHECK(next->diff(before).find("no changes") != std::string::npos);
    REQUIRE(next->remove(1));
    REQUIRE(next->set_name(2, "renamed"));
    REQUIRE(next->add(spec(30 * kMiBLbas, kMiBLbas)));
    const auto d = next->diff(before);
    CHECK(d.find("  - #1") != std::string::npos);
    CHECK(d.find("  ~ #2") != std::string::npos);
    CHECK(d.find("renamed") != std::string::npos);
    CHECK(d.find("  + #1") != std::string::npos);  // new partition reuses slot 1

    // Switching table type says so.
    auto mbr = PartitionPlan::blank(TableType::mbr, 512, total);
    REQUIRE(mbr);
    CHECK(mbr->diff(before).find("gpt -> mbr") != std::string::npos);
}

TEST_CASE("blank tables refuse tiny devices and bad sector sizes", "[partitions][edit]") {
    CHECK_FALSE(PartitionPlan::blank(TableType::gpt, 512, 40));
    CHECK_FALSE(PartitionPlan::blank(TableType::mbr, 512, 1));
    CHECK_FALSE(PartitionPlan::blank(TableType::gpt, 1024, 100000));
    CHECK_FALSE(PartitionPlan::blank(TableType::none, 512, 100000));
}

TEST_CASE("type aliases resolve", "[partitions][edit]") {
    CHECK(find_type_alias("ESP"));
    CHECK(find_type_alias("linux")->mbr_type == 0x83);
    CHECK_FALSE(find_type_alias("nonsense"));
    for (const auto& a : type_aliases()) CHECK(Guid::parse(a.gpt_guid));
    CHECK(random_guid() != random_guid());
}
