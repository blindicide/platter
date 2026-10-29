// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <mutex>

#include "platter/core/safety.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::safety;
using platter::test::TempDir;

namespace {

Request image_request(const std::filesystem::path& img, std::vector<LbaRange> ranges = {}, Kind k = Kind::erase) {
    Request r;
    r.kind = k;
    r.target.path = img.string();
    r.target.is_image = true;
    r.ranges = std::move(ranges);
    r.summary = "unit test";
    return r;
}

struct AuditCapture {
    std::mutex m;
    std::vector<AuditRecord> records;
    AuditCapture() {
        set_audit_sink([this](const AuditRecord& r) {
            std::lock_guard lk(m);
            records.push_back(r);
        });
    }
    ~AuditCapture() { set_audit_sink({}); }
};

DeviceInfo fake_disk() {
    DeviceInfo d;
    d.path = "/dev/sdz";
    d.name = "sdz";
    d.vendor = "ACME";
    d.model = "Platter 9000";
    d.serial = "SN12345678AB";
    d.stable_id = "wwn-0x5000c500deadbeef";
    d.capacity_bytes = 1'000'000'000;
    d.logical_sector_size = 512;
    d.physical_sector_size = 4096;
    d.bus = BusType::ata;
    d.media = MediaKind::rotational;
    d.read_only = Tri::no;
    d.capabilities.set(Capability::read);
    d.capabilities.set(Capability::write);
    return d;
}

}  // namespace

TEST_CASE("preview of an image: whole device, confirmation is the file name", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "victim.img";
    test::make_sparse_file(img, 4 << 20);

    auto p = make_preview(image_request(img));
    REQUIRE(p);
    CHECK(p->allowed());
    CHECK(p->whole_device);
    CHECK(p->affected_bytes == 4u << 20);
    REQUIRE(p->ranges.size() == 1);
    CHECK(p->ranges[0].count == (4u << 20) / 512);
    REQUIRE(p->accepted_confirmations.size() == 1);
    CHECK(p->accepted_confirmations[0] == "victim.img");
    CHECK(p->confirmation_ok("victim.img"));
    CHECK(p->confirmation_ok("  VICTIM.IMG \n"));
    CHECK_FALSE(p->confirmation_ok("victim"));
    CHECK_FALSE(p->confirmation_ok(""));
    CHECK_FALSE(p->confirmation_ok("   "));
}

TEST_CASE("preview rejects ranges outside the device and empty ranges", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "a.img";
    test::make_sparse_file(img, 1 << 20);  // 2048 sectors

    auto beyond = make_preview(image_request(img, {LbaRange{2000, 100}}));
    REQUIRE(beyond);
    CHECK_FALSE(beyond->allowed());

    auto empty = make_preview(image_request(img, {LbaRange{10, 0}}));
    REQUIRE(empty);
    CHECK_FALSE(empty->allowed());

    auto ok = make_preview(image_request(img, {LbaRange{10, 20}, LbaRange{100, 5}}));
    REQUIRE(ok);
    CHECK(ok->allowed());
    CHECK_FALSE(ok->whole_device);
    CHECK(ok->affected_bytes == 25 * 512);

    auto overflow = make_preview(image_request(img, {LbaRange{UINT64_MAX - 3, 100}}));
    REQUIRE(overflow);
    CHECK_FALSE(overflow->allowed());
}

TEST_CASE("missing image is reported, not authorized", "[safety]") {
    TempDir tmp;
    auto p = make_preview(image_request(tmp / "nope.img"));
    REQUIRE_FALSE(p);
    CHECK(p.error().code == Errc::not_found);
}

TEST_CASE("blockers: mounted, held, system disk, read-only", "[safety]") {
    Request req;
    req.kind = Kind::erase;
    req.target.path = "/dev/sdz";
    req.backend_dismounts_volumes = false;  // the Windows default would make mounted volumes an acknowledgment

    auto clean = preview_for(fake_disk(), req);
    CHECK(clean.allowed());
    CHECK(clean.whole_device);

    auto mounted = fake_disk();
    mounted.mountpoints = {"/mnt/data"};
    auto p1 = preview_for(mounted, req);
    CHECK_FALSE(p1.allowed());
    CHECK(p1.blockers.front().find("/mnt/data") != std::string::npos);

    auto held = fake_disk();
    held.holders = {"dm-crypt:cryptroot"};
    CHECK_FALSE(preview_for(held, req).allowed());

    auto sys = fake_disk();
    sys.is_system_disk = true;
    CHECK_FALSE(preview_for(sys, req).allowed());

    auto ro = fake_disk();
    ro.read_only = Tri::yes;
    CHECK_FALSE(preview_for(ro, req).allowed());

    auto nowrite = fake_disk();
    nowrite.capabilities.set(Capability::write, false);
    CHECK_FALSE(preview_for(nowrite, req).allowed());

    auto nocap = fake_disk();
    nocap.capacity_bytes = 0;
    CHECK_FALSE(preview_for(nocap, req).allowed());
}

TEST_CASE("device confirmation accepts the model or the serial tail", "[safety]") {
    Request req;
    req.target.path = "/dev/sdz";
    auto p = preview_for(fake_disk(), req);
    CHECK(p.confirmation_ok("Platter 9000"));
    CHECK(p.confirmation_ok("platter 9000"));
    CHECK(p.confirmation_ok("78AB"));
    CHECK_FALSE(p.confirmation_ok("sdz"));
    CHECK_FALSE(p.confirmation_ok("AB"));
    CHECK(p.prompt.find("78AB") != std::string::npos);

    auto bare = fake_disk();
    bare.model.clear();
    bare.serial.clear();
    auto p2 = preview_for(bare, req);
    CHECK(p2.confirmation_ok("sdz"));
}

TEST_CASE("flash and weak identity produce warnings", "[safety]") {
    Request req;
    req.target.path = "/dev/sdz";
    auto ssd = fake_disk();
    ssd.media = MediaKind::solid_state;
    ssd.bus = BusType::nvme;
    auto p = preview_for(ssd, req);
    bool flash = false;
    for (const auto& w : p.warnings) flash |= w.find("Flash") != std::string::npos;
    CHECK(flash);

    auto weak = fake_disk();
    weak.serial.clear();
    weak.stable_id.clear();
    auto p2 = preview_for(weak, req);
    bool identical = false;
    for (const auto& w : p2.warnings) identical |= w.find("identical drive") != std::string::npos;
    CHECK(identical);
}

TEST_CASE("fingerprint matching is strict", "[safety]") {
    const auto a = Fingerprint::of(fake_disk());
    CHECK(a.matches(a));
    auto d = fake_disk();
    d.capacity_bytes += 512;
    CHECK_FALSE(a.matches(Fingerprint::of(d)));
    d = fake_disk();
    d.serial = "OTHER";
    CHECK_FALSE(a.matches(Fingerprint::of(d)));
    d = fake_disk();
    d.stable_id = "wwn-0x1";
    CHECK_FALSE(a.matches(Fingerprint::of(d)));
    d = fake_disk();
    d.logical_sector_size = 4096;
    CHECK_FALSE(a.matches(Fingerprint::of(d)));
    d = fake_disk();
    d.model = "Other";
    CHECK_FALSE(a.matches(Fingerprint::of(d)));
    CHECK(a.describe().find("SN12345678AB") == std::string::npos);  // never the serial
}

TEST_CASE("authorize refuses a wrong confirmation and audits it", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "v.img";
    test::make_sparse_file(img, 1 << 20);
    AuditCapture audit;

    auto p = make_preview(image_request(img));
    REQUIRE(p);
    auto bad = authorize(*p, "wrong");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code == Errc::refused);
    REQUIRE(audit.records.size() == 1);
    CHECK(audit.records[0].event == "refused");
    CHECK(audit.records[0].kind == Kind::erase);
    CHECK(audit.records[0].model == "Raw disk image");
}

TEST_CASE("authorize refuses a blocked preview before touching anything", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "v.img";
    test::make_sparse_file(img, 1 << 20);
    auto p = make_preview(image_request(img));
    REQUIRE(p);
    p->blockers.push_back("a volume is mounted at /mnt/x");
    auto r = authorize(*p, "v.img");
    REQUIRE_FALSE(r);
    CHECK(r.error().code == Errc::refused);
    CHECK(r.error().message.find("/mnt/x") != std::string::npos);
}

TEST_CASE("mounted volumes: hard blocker without a dismounting backend, acknowledgment with one", "[safety]") {
    Request req;
    req.kind = Kind::erase;
    req.target.path = "/dev/sdz";
    auto d = fake_disk();
    d.mountpoints = {"E:\\", "H:\\"};

    req.backend_dismounts_volumes = false;
    auto hard = preview_for(d, req);
    CHECK_FALSE(hard.allowed());
    CHECK(hard.mounted.empty());

    req.backend_dismounts_volumes = true;
    auto soft = preview_for(d, req);
    CHECK(soft.allowed());
    CHECK(soft.needs_dismount_ack());
    REQUIRE(soft.mounted.size() == 2);
    CHECK(preview_text(soft, false).find("DISMOUNT:") != std::string::npos);

    // the system disk and holders stay hard refusals either way
    auto sys = d;
    sys.is_system_disk = true;
    CHECK_FALSE(preview_for(sys, req).allowed());
    auto held = d;
    held.holders = {"dm-crypt:x"};
    CHECK_FALSE(preview_for(held, req).allowed());
}

TEST_CASE("authorize needs the dismount acknowledgment when volumes were shown as mounted", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "m.img";
    test::make_sparse_file(img, 1 << 20);
    auto p = make_preview(image_request(img));
    REQUIRE(p);
    p->mounted.push_back("E:\\");

    auto no = authorize(*p, "m.img");
    REQUIRE_FALSE(no);
    CHECK(no.error().code == Errc::refused);
    CHECK(no.error().message.find("not acknowledged") != std::string::npos);

    AuthorizeOptions ack;
    ack.acknowledge_dismount = true;
    CHECK(authorize(*p, "m.img", ack));
}

TEST_CASE("authorize succeeds, writes only inside the confirmed range", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "v.img";
    test::make_sparse_file(img, 1 << 20);  // 2048 sectors
    AuditCapture audit;

    auto p = make_preview(image_request(img, {LbaRange{100, 10}}));
    REQUIRE(p);
    // This tests range enforcement, independently of direct-I/O buffer alignment.
    AuthorizeOptions options;
    options.direct_io = false;
    auto t = authorize(*p, "v.img", options);
    REQUIRE(t);
    REQUIRE(audit.records.size() == 1);
    CHECK(audit.records[0].event == "authorized");
    CHECK(audit.records[0].affected_bytes == 10 * 512);

    std::vector<std::uint8_t> sector(512, 0xEE);
    // inside
    CHECK(t->write(100 * 512, sector.data(), 512));
    CHECK(t->write(109 * 512, sector.data(), 512));
    // spans the end of the range
    std::vector<std::uint8_t> two(1024, 0xEE);
    auto over = t->write(109 * 512, two.data(), 1024);
    REQUIRE_FALSE(over);
    CHECK(over.error().code == Errc::refused);
    // outside
    CHECK_FALSE(t->write(99 * 512, sector.data(), 512));
    CHECK_FALSE(t->write(110 * 512, sector.data(), 512));
    CHECK_FALSE(t->write(0, sector.data(), 512));
    CHECK_FALSE(t->discard(0, 512));
    // reads are allowed anywhere
    std::vector<std::uint8_t> got(512);
    CHECK(t->read(0, got.data(), 512));

    // Nothing outside was touched.
    CHECK(test::read_at(img, 99 * 512, 512) == std::vector<std::uint8_t>(512, 0));
    CHECK(test::read_at(img, 110 * 512, 512) == std::vector<std::uint8_t>(512, 0));
    CHECK(test::read_at(img, 100 * 512, 512) == sector);
}

TEST_CASE("authorize refuses when the device changed after the preview", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "v.img";
    test::make_sparse_file(img, 1 << 20);
    auto p = make_preview(image_request(img));
    REQUIRE(p);

    std::filesystem::resize_file(img, 2 << 20);  // "a different disk is now at this path"
    auto r = authorize(*p, "v.img");
    REQUIRE_FALSE(r);
    CHECK(r.error().code == Errc::refused);
    CHECK(r.error().message.find("identity changed") != std::string::npos);
}

TEST_CASE("an authorization holds the device exclusively", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "v.img";
    test::make_sparse_file(img, 1 << 20);
    auto p = make_preview(image_request(img));
    REQUIRE(p);

    DeviceLeaseRegistry registry;
    AuthorizeOptions o;
    o.registry = &registry;
    auto first = authorize(*p, "v.img", o);
    REQUIRE(first);
    CHECK(registry.holders(p->device.path) == 1);

    // A second destructive request and a reader are both turned away.
    auto second = authorize(*p, "v.img", o);
    REQUIRE_FALSE(second);
    CHECK(second.error().code == Errc::refused);
    CHECK_FALSE(registry.acquire(p->device.path, AccessKind::shared_read));

    {
        auto moved = std::move(*first);
        CHECK(registry.holders(p->device.path) == 1);
    }
    CHECK(registry.holders(p->device.path) == 0);
    CHECK(authorize(*p, "v.img", o));
}

TEST_CASE("a running reader blocks authorization", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "v.img";
    test::make_sparse_file(img, 1 << 20);
    auto p = make_preview(image_request(img));
    REQUIRE(p);

    DeviceLeaseRegistry registry;
    auto reader = registry.acquire(p->device.path, AccessKind::shared_read);
    REQUIRE(reader);
    AuthorizeOptions o;
    o.registry = &registry;
    auto r = authorize(*p, "v.img", o);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == Errc::refused);
}

TEST_CASE("image paths are normalised so spellings share one lease key", "[safety]") {
    TempDir tmp;
    const auto img = tmp / "v.img";
    test::make_sparse_file(img, 1 << 20);
    auto a = make_preview(image_request(img));
    auto b = make_preview(image_request(tmp.path() / "." / "v.img"));
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->device.path == b->device.path);
}
