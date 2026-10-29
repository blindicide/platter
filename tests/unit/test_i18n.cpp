// SPDX-License-Identifier: LicenseRef-Proprietary
//
// The Russian translation of the phrases the core libraries produce. Built only
// when Qt's linguist tools are available; loads the compiled .qm from the build tree.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTranslator>
#include <memory>

#include "i18n.hpp"
#include "platter/core/safety.hpp"
#include "platter/smart/smart.hpp"

using namespace platter;
namespace i18n = platter::gui::i18n;

namespace {

struct Russian {
    Russian() {
        static int argc = 1;
        static char name[] = "i18n-test";
        static char* argv[] = {name, nullptr};
        if (!QCoreApplication::instance()) app = std::make_unique<QCoreApplication>(argc, argv);
        REQUIRE(tr.load(QStringLiteral(PLATTER_TEST_QM)));
        QCoreApplication::installTranslator(&tr);
    }
    ~Russian() { QCoreApplication::removeTranslator(&tr); }
    std::unique_ptr<QCoreApplication> app;
    QTranslator tr;
};

bool has_cyrillic(const QString& s) {
    for (QChar c : s)
        if (c.unicode() >= 0x0400 && c.unicode() <= 0x04FF) return true;
    return false;
}

DeviceInfo fake_disk() {
    DeviceInfo d;
    d.path = "/dev/sdz";
    d.name = "sdz";
    d.model = "Platter 9000";
    d.serial = "SN12345678AB";
    d.stable_id = "wwn-0x5000c500deadbeef";
    d.capacity_bytes = 1'000'000'000;
    d.logical_sector_size = 512;
    d.physical_sector_size = 4096;
    d.bus = BusType::usb;
    d.media = MediaKind::solid_state;
    d.removable = Tri::yes;
    d.read_only = Tri::no;
    d.capabilities.set(Capability::read);
    d.capabilities.set(Capability::write);
    return d;
}

}  // namespace

TEST_CASE("fixed core phrases are translated, unknown ones pass through", "[i18n]") {
    Russian ru;
    CHECK(i18n::ct("Yes") == QStringLiteral("Да"));
    CHECK(i18n::ct("PASS WITH WARNINGS") == QStringLiteral("ПРОЙДЕНО С ПРЕДУПРЕЖДЕНИЯМИ"));
    CHECK(i18n::ct("something the catalogue has never heard of") == QStringLiteral("something the catalogue has never heard of"));
    CHECK(i18n::ct("").isEmpty());
}

TEST_CASE("phrases with variable parts keep their numbers and names", "[i18n]") {
    Russian ru;
    const auto mounted = i18n::ct("a volume is mounted at /mnt/data");
    CHECK(mounted == QStringLiteral("том смонтирован в /mnt/data"));
    const auto used = i18n::ct("in use by dm-0 (volume manager / encryption / RAID)");
    CHECK(used.contains(QStringLiteral("dm-0")));
    CHECK(has_cyrillic(used));
    const auto rose = i18n::ct("Reallocated sector count rose from 3 to 9 (the drive remapped sectors during the run)");
    CHECK(has_cyrillic(rose));
    CHECK(rose.contains(QStringLiteral("3")));
    CHECK(rose.contains(QStringLiteral("9")));
    CHECK(rose.contains(QStringLiteral("Число переназначенных секторов")));
    CHECK(i18n::ct("Pass 2 write (complement of stream 1)").contains(QStringLiteral("дополнение потока 1")));
    // Sub-phrases inside an outer pattern are translated too.
    CHECK(i18n::ct("the operation is blocked: this is the system disk").contains(QStringLiteral("системный диск")));
}

TEST_CASE("every blocker, warning and prompt of a real preview is translated", "[i18n][safety]") {
    Russian ru;
    auto d = fake_disk();
    d.mountpoints = {"/mnt/usb"};
    d.holders = {"dm-3"};
    d.is_system_disk = true;
    safety::Request req;
    req.kind = safety::Kind::erase;
    req.target.path = d.path;
    req.backend_dismounts_volumes = false;
    const auto p = safety::preview_for(d, req);
    REQUIRE_FALSE(p.blockers.empty());
    REQUIRE_FALSE(p.warnings.empty());
    for (const auto& b : p.blockers) {
        INFO(b);
        CHECK(has_cyrillic(i18n::ct(b)));
    }
    for (const auto& w : p.warnings) {
        INFO(w);
        CHECK(has_cyrillic(i18n::ct(w)));
    }
    CHECK(has_cyrillic(i18n::ct(p.prompt)));
    CHECK(i18n::ct(p.prompt).contains(QStringLiteral("Platter 9000")));
    CHECK(i18n::ct(p.prompt).contains(QStringLiteral("78AB")));
}

TEST_CASE("every SMART finding of an assessment is translated", "[i18n][smart]") {
    Russian ru;
    smart::Report r;
    r.smart_available = Tri::yes;
    r.smart_enabled = Tri::yes;
    r.overall_passed = false;
    smart::Attribute a;
    a.id = 5;
    a.name = "Reallocated_Sector_Ct";
    a.raw_value = 12;
    a.when_failed = "now";
    r.attributes.push_back(a);
    r.temperature_c = 71;
    const auto as = smart::assess(r);
    REQUIRE_FALSE(as.findings.empty());
    for (const auto& f : as.findings) {
        INFO(f.text);
        CHECK(has_cyrillic(i18n::ct(f.text)));
    }
}

TEST_CASE("unit suffixes are localized after numbers only", "[i18n]") {
    Russian ru;
    CHECK(i18n::units(QStringLiteral("20.48 MB (19.53 MiB)")) == QStringLiteral("20.48 МБ (19.53 МиБ)"));
    CHECK(i18n::units(QStringLiteral("118.4 MB/s")) == QStringLiteral("118.4 МБ/с"));
    CHECK(i18n::units(QStringLiteral("512 B")) == QStringLiteral("512 Б"));
    CHECK(i18n::units(QStringLiteral("Samsung 860 EVO")) == QStringLiteral("Samsung 860 EVO"));
    CHECK(i18n::units(QStringLiteral("MB of cache")) == QStringLiteral("MB of cache"));
}
