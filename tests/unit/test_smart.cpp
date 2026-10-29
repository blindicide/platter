// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <sstream>

#include "platter/core/process.hpp"
#include "platter/smart/smart.hpp"
#include "test_support.hpp"

using namespace platter;
using namespace platter::smart;
using Catch::Matchers::ContainsSubstring;

namespace {

std::string fixture(const std::string& name) {
    std::ifstream in(std::string(PLATTER_FIXTURES_DIR) + "/smart/" + name, std::ios::binary);
    REQUIRE(in);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Report load(const std::string& name) {
    auto r = parse_smartctl_json(fixture(name));
    REQUIRE(r);
    return std::move(r).value();
}

bool mentions(const Assessment& a, const std::string& needle) {
    for (const auto& f : a.findings)
        if (f.text.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST_CASE("parse healthy ATA report", "[smart]") {
    auto r = load("ata_healthy.json");
    CHECK(r.protocol == Protocol::ata);
    CHECK(r.device_type == "sat");
    CHECK(r.smartctl_version == "7.4");
    CHECK(r.model == "WDC WD10EZEX-08WN4A0");
    CHECK(r.firmware == "02.01A02");
    CHECK(r.capacity_bytes == 1000204886016ull);
    CHECK(r.rotation_rate == 7200);
    CHECK(r.smart_available == Tri::yes);
    CHECK(r.overall_passed == true);
    CHECK(r.temperature_c == 33);
    CHECK(r.power_on_hours == 9120u);
    CHECK(r.power_cycles == 1342u);
    CHECK(r.attributes.size() == 10);
    REQUIRE(r.attribute(194));
    CHECK(r.attribute(194)->raw_value == 33);
    CHECK(r.attribute(5)->prefailure);
    CHECK(r.attribute(5)->threshold == 140);
    CHECK(r.self_tests.size() == 1);
    CHECK(r.self_test_supported == true);
    CHECK(r.ata_error_count == 0u);

    auto a = assess(r);
    CHECK(a.status == Health::good);
    CHECK(mentions(a, "does not predict future reliability"));
}

TEST_CASE("pending/reallocated sectors give WARNING even though SMART says PASSED", "[smart]") {
    auto r = load("ata_pending.json");
    CHECK(r.overall_passed == true);
    auto a = assess(r);
    CHECK(a.status == Health::warning);
    CHECK(mentions(a, "24 reallocated sectors"));
    CHECK(mentions(a, "8 sectors pending reallocation"));
    CHECK(mentions(a, "interface CRC errors"));
    CHECK(mentions(a, "cabling"));
    CHECK(mentions(a, "error log contains 2"));
}

TEST_CASE("failing drive is CRITICAL with every reason listed", "[smart]") {
    auto a = assess(load("ata_failing.json"));
    CHECK(a.status == Health::critical);
    CHECK(mentions(a, "FAILED"));
    CHECK(mentions(a, "below its threshold now"));
    CHECK(mentions(a, "self-test failed"));
    CHECK(mentions(a, "4012 reallocated"));
}

TEST_CASE("vendor-packed raw values do not raise false alarms", "[smart]") {
    auto r = load("ata_packed_raw.json");
    CHECK(r.attribute(5)->raw_value == 0x123400000000ull);
    CHECK(assess(r).status == Health::good);
}

TEST_CASE("hot drive is a WARNING", "[smart]") {
    auto a = assess(load("ata_hot.json"));
    CHECK(a.status == Health::warning);
    CHECK(mentions(a, "61"));
}

TEST_CASE("NVMe health log", "[smart][nvme]") {
    auto r = load("nvme_healthy.json");
    CHECK(r.protocol == Protocol::nvme);
    REQUIRE(r.nvme);
    CHECK(r.nvme->percentage_used == 4);
    CHECK(r.nvme->unsafe_shutdowns == 57u);
    CHECK(r.power_on_hours == 6543u);
    auto a = assess(r);
    CHECK(a.status == Health::good);
    CHECK(mentions(a, "57 unsafe shutdowns"));  // informational only

    auto w = assess(load("nvme_worn.json"));
    CHECK(w.status == Health::critical);
    CHECK(mentions(w, "reliability-degraded"));
    CHECK(mentions(w, "below the threshold"));
    CHECK(mentions(w, "endurance exceeded"));
    CHECK(mentions(w, "2 media"));
}

TEST_CASE("unsupported USB bridge yields UNKNOWN with smartctl's explanation", "[smart]") {
    auto r = load("usb_unknown_bridge.json");
    CHECK(r.smartctl_exit_status == 1);
    CHECK(r.messages.size() == 2);
    auto a = assess(r);
    CHECK(a.status == Health::unknown);
    CHECK(mentions(a, "Unknown USB bridge"));
}

TEST_CASE("PASSED alone never means GOOD", "[smart][safety]") {
    auto a = assess(load("scsi_passed_only.json"));
    CHECK(a.status == Health::unknown);
    CHECK(mentions(a, "no attribute or health-log data"));
}

TEST_CASE("capabilities come from the report, not the bus", "[smart]") {
    DeviceInfo d;
    apply_capabilities(load("ata_healthy.json"), d);
    CHECK(d.capabilities.has(Capability::smart));
    CHECK(d.capabilities.has(Capability::self_test));
    DeviceInfo u;
    apply_capabilities(load("usb_unknown_bridge.json"), u);
    CHECK_FALSE(u.capabilities.has(Capability::smart));
    CHECK_FALSE(u.capabilities.has(Capability::self_test));
}

TEST_CASE("garbage input is rejected cleanly", "[smart]") {
    CHECK(parse_smartctl_json("not json").error().code == Errc::corrupt_data);
    CHECK(parse_smartctl_json("[1,2]").error().code == Errc::corrupt_data);
    auto r = parse_smartctl_json("{}");
    REQUIRE(r);
    CHECK(assess(*r).status == Health::unknown);
    // Wrong types are tolerated, not thrown.
    auto t = parse_smartctl_json(R"({"temperature":{"current":"hot"},"ata_smart_attributes":{"table":[{"id":"x"}]}})");
    REQUIRE(t);
    CHECK_FALSE(t->temperature_c);
}

TEST_CASE("raw value formatting", "[smart]") {
    CHECK(format_raw(1234) == "1234 (0x0000000004D2)");
    CHECK(format_raw(0) == "0 (0x000000000000)");
}

#ifndef _WIN32
TEST_CASE("run_process captures output, exit code and enforces timeout", "[process]") {
    auto r = run_process({"sh", "-c", "echo hi; echo err >&2; exit 3"}, std::chrono::seconds(10));
    REQUIRE(r);
    CHECK(r->out == "hi\n");
    CHECK(r->err == "err\n");
    CHECK(r->exit_code == 3);

    auto t = run_process({"sleep", "5"}, std::chrono::milliseconds(200));
    REQUIRE_FALSE(t);
    CHECK(t.error().code == Errc::timeout);

    auto n = run_process({"platter-definitely-not-a-program"}, std::chrono::seconds(1));
    REQUIRE_FALSE(n);
    CHECK(n.error().code == Errc::not_found);

    // Arguments are passed verbatim: no shell interpretation.
    auto q = run_process({"printf", "%s|", "a b", "$(rm -rf /)", ";"}, std::chrono::seconds(5));
    REQUIRE(q);
    CHECK(q->out == "a b|$(rm -rf /)|;|");
}

TEST_CASE("smart::query runs a read-only smartctl invocation", "[smart][process]") {
    platter::test::TempDir tmp;
    const auto args_file = tmp / "args.txt";
    const auto fake = tmp / "smartctl";
    {
        std::ofstream s(fake);
        s << "#!/bin/sh\n"
          << "printf '%s\\n' \"$@\" > '" << args_file.string() << "'\n"
          << "cat '" << PLATTER_FIXTURES_DIR << "/smart/ata_pending.json'\n"
          << "exit 64\n";  // bit 6: error log has entries
    }
    std::filesystem::permissions(fake, std::filesystem::perms::owner_all);

    RunOptions o;
    o.smartctl_path = fake.string();
    auto r = query("/dev/sdq", o);
    REQUIRE(r);
    CHECK(r->smartctl_exit_status == 64);
    CHECK(assess(*r).status == Health::warning);

    std::ifstream in(args_file);
    std::string args((std::istreambuf_iterator<char>(in)), {});
    CHECK(args == "--json=c\n-a\n/dev/sdq\n");
}
#endif
