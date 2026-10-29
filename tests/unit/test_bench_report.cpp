// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_test_macros.hpp>

#include "platter/reports/bench_report.hpp"
#include "test_support.hpp"

using namespace platter;
using namespace platter::bench;
using platter::test::TempDir;

namespace {
BenchResult measured(BenchConfig c) {
    TempDir tmp;
    const auto img = tmp / "r.img";
    test::make_sparse_file(img, 32ull << 20);
    c.direct_io = false;
    auto b = create_benchmark(img.string(), true, c);
    REQUIRE(b);
    REQUIRE((*b)->validate());
    REQUIRE((*b)->start());
    (*b)->wait();
    auto r = (*b)->result();
    r.device.serial = "SERIAL-0123456789";
    return r;
}
}  // namespace

TEST_CASE("benchmark JSON round-trips", "[bench_report]") {
    BenchConfig c{.kind = BenchKind::block_sizes, .max_bytes = 2ull << 20};
    c.sweep_block_sizes = {4096, 1u << 20};
    const auto r = measured(c);
    const auto text = reports::bench_to_json(r);
    CHECK(text.find("SERIAL-0123456789") == std::string::npos);
    auto back = reports::bench_from_json(text);
    REQUIRE(back);
    CHECK(back->config.kind == BenchKind::block_sizes);
    CHECK(back->config.sweep_block_sizes == r.config.sweep_block_sizes);
    CHECK(back->state == OperationState::completed);
    CHECK(back->started_utc == r.started_utc);
    REQUIRE(back->phases.size() == 2);
    CHECK(back->phases[1].label == "1 MiB");
    CHECK(back->phases[1].metrics.bytes == r.phases[1].metrics.bytes);
    CHECK(back->phases[1].metrics.lat_p99_us == r.phases[1].metrics.lat_p99_us);
    CHECK(back->timeline.size() == r.timeline.size());
    CHECK(back->latency_buckets == r.latency_buckets);
    CHECK(back->warnings == r.warnings);
    CHECK(back->headline() == r.headline());
    CHECK(reports::bench_from_json(reports::bench_to_json(r, {true}))->device.serial == "SERIAL-0123456789");
}

TEST_CASE("speed curve positions survive the round trip", "[bench_report]") {
    const auto r = measured({.kind = BenchKind::speed_curve, .curve_points = 5, .curve_bytes_per_point = 1ull << 20});
    auto back = reports::bench_from_json(reports::bench_to_json(r));
    REQUIRE(back);
    REQUIRE(back->positions.size() == 5);
    CHECK(back->positions[4].lba == r.positions[4].lba);
}

TEST_CASE("malformed benchmark files are rejected", "[bench_report][safety]") {
    const auto good = nlohmann::json::parse(reports::bench_to_json(measured({.kind = BenchKind::access_time, .max_seconds = 0.05})));
    auto rejects = [&](auto mutate) {
        auto j = good;
        mutate(j);
        auto r = reports::bench_from_json(j.dump());
        REQUIRE_FALSE(r);
        CHECK(r.error().code == Errc::corrupt_data);
    };
    rejects([](auto& j) { j["kind"] = "surface-scan"; });
    rejects([](auto& j) { j["bench_schema"] = 7; });
    rejects([](auto& j) { j["config"]["kind"] = "write_everything"; });
    rejects([](auto& j) { j["config"]["queue_depth"] = 0; });
    rejects([](auto& j) { j["phases"][0].erase("metrics"); });
    rejects([](auto& j) { j["timeline"] = {{1}}; });
    CHECK(reports::bench_from_json("nope").error().code == Errc::corrupt_data);
}

TEST_CASE("benchmark text report records the settings", "[bench_report]") {
    const auto r = measured({.kind = BenchKind::random, .queue_depth = 2, .max_seconds = 0.05});
    const auto t = reports::bench_to_text(r);
    CHECK(t.find("Random read") != std::string::npos);
    CHECK(t.find("Queue depth         2") != std::string::npos);
    CHECK(t.find("buffered") != std::string::npos);
    CHECK(t.find("Caveats") != std::string::npos);
    CHECK(t.find(r.headline()) != std::string::npos);
    CHECK(t.find("SERIAL-0123456789") == std::string::npos);
}

TEST_CASE("serials embedded in by-id stable ids are masked in exports", "[bench_report][privacy]") {
    DeviceInfo d;
    d.serial = "WD-WCC6Y1234567";
    d.stable_id = "ata-WDC_WD10EZEX-08W_WD-WCC6Y1234567";
    const auto j = reports::to_json(d).dump();
    CHECK(j.find("WD-WCC6Y1234567") == std::string::npos);
    CHECK(reports::redact_stable_id(d, {}) == "ata-WDC_WD10EZEX-08W_WD-W***********");
    CHECK(reports::redact_stable_id(d, {true}) == d.stable_id);
    d.stable_id = "wwn-0x50014ee2b5a1c3d4";  // no serial inside: unchanged
    CHECK(reports::redact_stable_id(d, {}) == d.stable_id);
}
