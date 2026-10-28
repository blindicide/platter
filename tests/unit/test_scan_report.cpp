// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "faulty_device.hpp"
#include "platter/reports/scan_report.hpp"
#include "test_support.hpp"

using namespace platter;
using namespace platter::surface;
using platter::test::FaultyDevice;
using platter::test::TempDir;

namespace {

ScanSnapshot scanned(const TempDir& tmp) {
    const auto img = tmp / "r.img";
    test::make_sparse_file(img, 4ull << 20);  // 64 blocks of 64 KiB
    auto dev = open_image(img.string());
    REQUIRE(dev);
    auto* fd = new FaultyDevice(std::move(dev).value());
    fd->bad_lbas = {1000};                      // block 7
    fd->flaky_lbas = {{2000, 1}};               // block 15, 1 retry
    fd->latency_ns_at = {{3000, 120'000'000}};  // block 23: slow
    ScanConfig c;
    c.block_bytes = 64 * 1024;
    c.direct_io = false;
    c.retries = 1;
    c.thresholds.us = {2000, 10000, 60000, 300000};
    SurfaceScan scan(BlockDevicePtr(fd), c);
    REQUIRE(scan.validate());
    REQUIRE(scan.start());
    scan.wait();
    auto snap = scan.snapshot();
    snap.device.serial = "SER1234567890";
    return snap;
}

std::size_t lines(const std::string& s) {
    std::size_t n = 0;
    for (char ch : s) n += ch == '\n';
    return n;
}

}  // namespace

TEST_CASE("scan JSON export round-trips exactly", "[scan_report]") {
    TempDir tmp;
    const auto snap = scanned(tmp);
    const auto text = reports::scan_to_json(snap);
    CHECK(text.find("SER1234567890") == std::string::npos);  // serial omitted by default

    auto back = reports::scan_from_json(text);
    REQUIRE(back);
    const auto& b = *back;
    CHECK(b.layout.block_count == snap.layout.block_count);
    CHECK(b.layout.lbas_per_block == snap.layout.lbas_per_block);
    CHECK(b.config.block_bytes == snap.config.block_bytes);
    CHECK(b.config.thresholds.us == snap.config.thresholds.us);
    CHECK(b.config.retries == 1);
    CHECK(b.progress.state == OperationState::completed);
    CHECK(b.stats.requests == snap.stats.requests);
    CHECK(b.stats.failed == 1);
    CHECK(b.stats.per_class == snap.stats.per_class);
    REQUIRE(b.blocks.size() == snap.blocks.size());
    for (std::size_t i = 0; i < b.blocks.size(); ++i) {
        INFO("block " << i);
        CHECK(b.blocks[i].cls == snap.blocks[i].cls);
        CHECK(b.blocks[i].duration_us == snap.blocks[i].duration_us);
        CHECK(b.blocks[i].errc == snap.blocks[i].errc);
        CHECK(b.blocks[i].retries == snap.blocks[i].retries);
        CHECK(b.blocks[i].time_ms == snap.blocks[i].time_ms);
    }
    CHECK(b.blocks[7].cls == LatencyClass::error);
    CHECK(b.blocks[15].retries == 1);
    CHECK(b.blocks[23].cls == LatencyClass::slow);

    auto with_serial = reports::scan_to_json(snap, {true});
    CHECK(with_serial.find("SER1234567890") != std::string::npos);
    CHECK(reports::scan_from_json(with_serial)->device.serial == "SER1234567890");
}

TEST_CASE("sampled scan exports only tested blocks", "[scan_report]") {
    TempDir tmp;
    const auto img = tmp / "s.img";
    test::make_sparse_file(img, 4ull << 20);
    ScanConfig c;
    c.block_bytes = 64 * 1024;
    c.direct_io = false;
    c.order = ScanOrder::sampled;
    c.sample_percent = 25;
    auto scan = create_scan(img.string(), true, c);
    REQUIRE(scan);
    REQUIRE((*scan)->validate());
    REQUIRE((*scan)->start());
    (*scan)->wait();
    const auto snap = (*scan)->snapshot();
    auto back = reports::scan_from_json(reports::scan_to_json(snap));
    REQUIRE(back);
    CHECK(back->config.order == ScanOrder::sampled);
    CHECK(back->stats.per_class[0] == 48);  // 64 - 16 untested
    CHECK(lines(reports::scan_to_csv(snap)) == 1 + 16);
}

TEST_CASE("corrupted scan files are rejected, not half-loaded", "[scan_report][safety]") {
    TempDir tmp;
    auto good = nlohmann::json::parse(reports::scan_to_json(scanned(tmp)));
    auto rejects = [&](auto mutate, const std::string& why) {
        auto j = good;
        mutate(j);
        auto r = reports::scan_from_json(j.dump());
        INFO(why);
        REQUIRE_FALSE(r);
        CHECK(r.error().code == Errc::corrupt_data);
    };
    rejects([](auto& j) { j["kind"] = "smart"; }, "wrong kind");
    rejects([](auto& j) { j["scan_schema"] = 99; }, "future schema");
    rejects([](auto& j) { j["layout"]["block_count"] = 5; }, "geometry mismatch");
    rejects([](auto& j) { j["blocks"][0][0] = 1000000; }, "index out of range");
    rejects([](auto& j) { j["blocks"][0][5] = 42; }, "bad class");
    rejects([](auto& j) { j["blocks"].push_back(j["blocks"][0]); }, "duplicate row");
    rejects([](auto& j) { j["blocks"].erase(0); }, "row count mismatch");
    rejects([](auto& j) { j["config"]["thresholds_us"] = {9, 8, 7, 6}; }, "bad thresholds");
    rejects([](auto& j) { j["config"].erase("block_bytes"); }, "missing field");
    rejects([](auto& j) { j["layout"]["lbas_per_block"] = "lots"; }, "mistyped field");
    CHECK(reports::scan_from_json("{nope").error().code == Errc::corrupt_data);
}

TEST_CASE("CSV export: one row per tested block with exact LBA ranges", "[scan_report]") {
    TempDir tmp;
    const auto snap = scanned(tmp);
    const auto csv = reports::scan_to_csv(snap);
    CHECK(lines(csv) == 1 + 64);
    std::istringstream in(csv);
    std::string header, row;
    std::getline(in, header);
    CHECK(header == "block,first_lba,last_lba,lba_count,bytes,duration_us,mb_per_s,class,error,retries,time_ms");
    for (int i = 0; i <= 7; ++i) std::getline(in, row);
    CHECK(row.rfind("7,896,1023,128,65536,", 0) == 0);
    CHECK(row.find(",error,io_error,1,") != std::string::npos);
}

TEST_CASE("text report summarises results and states its limits", "[scan_report]") {
    TempDir tmp;
    const auto snap = scanned(tmp);
    const auto t = reports::scan_to_text(snap);
    CHECK(t.find("COMPLETED") != std::string::npos);
    CHECK(t.find("LBA 896 - 1023  io_error") != std::string::npos);
    CHECK(t.find("not the\nphysical health") != std::string::npos);
    CHECK(t.find("SER1234567890") == std::string::npos);
    CHECK(t.find("SER1") != std::string::npos);  // masked: prefix only
}
