// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sqlite3.h>

#include "platter/storage/history.hpp"
#include "test_support.hpp"

using namespace platter;
using namespace platter::storage;
using platter::test::TempDir;

namespace {

DeviceInfo disk(const std::string& serial = "WD-WCC6Y1234567") {
    DeviceInfo d;
    d.path = "/dev/sdq";
    d.name = "sdq";
    d.model = "WDC WD10EZEX";
    d.serial = serial;
    d.stable_id = "ata-WDC_WD10EZEX_" + serial;  // by-id style: embeds the serial
    d.capacity_bytes = 1000204886016ull;
    d.bus = BusType::ata;
    return d;
}

bench::BenchResult bench_result(double mbps, const std::string& when) {
    bench::BenchResult r;
    r.config.kind = bench::BenchKind::sequential;
    r.config.block_bytes = 1u << 20;
    r.device = disk();
    r.started_utc = when;
    r.state = OperationState::completed;
    bench::Metrics m;
    m.ops = 100;
    m.bytes = 100ull << 20;
    m.wall_ns = 1'000'000'000;
    m.mb_per_s = mbps;
    m.iops = 100;
    r.phases.push_back({"Sequential", 1u << 20, 1, m});
    r.timeline = {{0.25, mbps * 1e6}, {0.5, mbps * 1e6}};
    return r;
}

std::string file_text(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

}  // namespace

TEST_CASE("device keys never contain a raw serial", "[history][privacy]") {
    const auto k = HistoryStore::device_key(disk());
    CHECK(k.rfind("id:", 0) == 0);
    CHECK(k.find("WD-WCC6Y1234567") == std::string::npos);
    CHECK(k == HistoryStore::device_key(disk()));             // stable
    CHECK(k != HistoryStore::device_key(disk("OTHER-SERIAL")));  // distinguishes drives
    auto no_id = disk();
    no_id.stable_id.clear();
    CHECK(HistoryStore::device_key(no_id).rfind("sn:", 0) == 0);
    CHECK(HistoryStore::device_key(no_id).find("WD-WCC6Y") == std::string::npos);
    DeviceInfo img;
    img.bus = BusType::image;
    img.path = "/some/where/disk.img";
    img.capacity_bytes = 4096;
    CHECK(HistoryStore::device_key(img) == "image:disk.img:4096");
}

TEST_CASE("benchmarks persist across reopen, newest first", "[history]") {
    TempDir tmp;
    const auto path = (tmp / "nested" / "dir" / "history.db").string();  // parents are created
    {
        auto st = HistoryStore::open(path);
        REQUIRE(st);
        CHECK((*st)->schema_version() == HistoryStore::kSchemaVersion);
        REQUIRE((*st)->record_benchmark(bench_result(150.5, "2026-10-01T10:00:00Z")));
        REQUIRE((*st)->record_benchmark(bench_result(148.0, "2026-10-20T10:00:00Z")));
    }
    auto st = HistoryStore::open(path);
    REQUIRE(st);
    auto rows = (*st)->list(SessionKind::benchmark);
    REQUIRE(rows);
    REQUIRE(rows->size() == 2);
    CHECK((*rows)[0].started_utc == "2026-10-20T10:00:00Z");
    CHECK((*rows)[0].primary_value == 148.0);
    CHECK((*rows)[0].subtype == "sequential");
    CHECK((*rows)[0].state == "COMPLETED");
    CHECK((*rows)[0].headline == "148.0 MB/s");
    CHECK((*rows)[0].device_model == "WDC WD10EZEX");

    auto back = (*st)->load_benchmark((*rows)[1].id);
    REQUIRE(back);
    CHECK(back->phases.at(0).metrics.mb_per_s == 150.5);
    CHECK(back->timeline.size() == 2);
    CHECK(back->device.serial.empty());  // stored without serial

    auto devs = (*st)->devices();
    REQUIRE(devs);
    REQUIRE(devs->size() == 1);  // same drive, one device row
    CHECK(devs->front().capacity_bytes == 1000204886016ull);

    // Filter by device.
    CHECK((*st)->list(SessionKind::benchmark, HistoryStore::device_key(disk()))->size() == 2);
    CHECK((*st)->list(SessionKind::benchmark, "id:nope")->empty());
    CHECK((*st)->list(SessionKind::surface_scan)->empty());
}

TEST_CASE("the database file holds no serial number", "[history][privacy]") {
    TempDir tmp;
    const auto path = tmp / "h.db";
    {
        auto st = HistoryStore::open(path.string());
        REQUIRE(st);
        REQUIRE((*st)->record_benchmark(bench_result(100, "2026-10-01T00:00:00Z")));
        smart::Report rep;
        rep.serial = "WD-WCC6Y1234567";
        rep.model = "WDC WD10EZEX";
        rep.temperature_c = 35;
        smart::Assessment a;
        a.status = smart::Health::good;
        REQUIRE((*st)->record_smart(disk(), rep, a));
    }
    // Checkpoint the WAL so everything is in the main file, then grep the bytes.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.string().c_str(), &db) == SQLITE_OK);
    sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE)", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    const auto bytes = file_text(path);
    CHECK_FALSE(bytes.empty());
    CHECK(bytes.find("WD-WCC6Y1234567") == std::string::npos);
    CHECK(bytes.find("WDC WD10EZEX") != std::string::npos);  // sanity: data is really there
}

TEST_CASE("scans and SMART snapshots", "[history]") {
    TempDir tmp;
    auto st = HistoryStore::open((tmp / "h.db").string());
    REQUIRE(st);

    surface::ScanSnapshot snap;
    snap.device = disk();
    snap.config.block_bytes = 1u << 20;
    snap.layout = {0, 4095, 512, 2048, 2};
    snap.blocks.resize(2);
    snap.blocks[0].cls = surface::LatencyClass::fast;
    snap.blocks[1].cls = surface::LatencyClass::error;
    snap.stats.requests = 2;
    snap.stats.failed = 1;
    snap.stats.per_class[1] = 1;
    snap.stats.per_class[6] = 1;
    snap.progress.state = OperationState::completed;
    snap.progress.start_time = std::chrono::system_clock::now();
    auto sid = (*st)->record_scan(snap);
    REQUIRE(sid);
    auto scans = (*st)->list(SessionKind::surface_scan);
    REQUIRE(scans->size() == 1);
    CHECK(scans->front().primary_value == 1.0);  // failed blocks
    CHECK(scans->front().headline.rfind("1 failed", 0) == 0);
    auto loaded = (*st)->load_scan(*sid);
    REQUIRE(loaded);
    CHECK(loaded->blocks[1].cls == surface::LatencyClass::error);

    smart::Report rep;
    rep.temperature_c = 41;
    rep.power_on_hours = 9120;
    smart::Assessment a;
    a.status = smart::Health::warning;
    REQUIRE((*st)->record_smart(disk(), rep, a));
    auto sm = (*st)->list(SessionKind::smart);
    REQUIRE(sm->size() == 1);
    CHECK(sm->front().headline == "WARNING, 41 C, 9120 h");
    CHECK(sm->front().primary_value == 41.0);
    auto doc = (*st)->load_smart_json(sm->front().id);
    REQUIRE(doc);
    CHECK(doc->find("\"assessment\"") != std::string::npos);
}

TEST_CASE("remove and not-found", "[history]") {
    TempDir tmp;
    auto st = HistoryStore::open((tmp / "h.db").string());
    REQUIRE(st);
    auto id = (*st)->record_benchmark(bench_result(1, "2026-01-01T00:00:00Z"));
    REQUIRE(id);
    CHECK((*st)->remove(SessionKind::surface_scan, *id).error().code == Errc::not_found);  // kind must match
    REQUIRE((*st)->remove(SessionKind::benchmark, *id));
    CHECK((*st)->list(SessionKind::benchmark)->empty());
    CHECK((*st)->load_benchmark(*id).error().code == Errc::not_found);
    CHECK((*st)->remove(SessionKind::benchmark, *id).error().code == Errc::not_found);
}

TEST_CASE("bad and future databases are refused", "[history][safety]") {
    TempDir tmp;
    const auto junk = tmp / "junk.db";
    std::ofstream(junk, std::ios::binary) << std::string(4096, 'x');
    auto bad = HistoryStore::open(junk.string());
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code == Errc::corrupt_data);

    const auto future = (tmp / "future.db").string();
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(future.c_str(), &db) == SQLITE_OK);
        sqlite3_exec(db, "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
                         "INSERT INTO meta VALUES('schema_version','99');", nullptr, nullptr, nullptr);
        sqlite3_close(db);
    }
    auto f = HistoryStore::open(future);
    REQUIRE_FALSE(f);
    CHECK(f.error().code == Errc::unsupported);
}

TEST_CASE("default path honours PLATTER_HISTORY_DB", "[history]") {
    const auto p = HistoryStore::default_path();
    CHECK(p.find("history.db") != std::string::npos);
}

TEST_CASE("list_all merges every kind newest first and find/render_text work", "[history]") {
    TempDir tmp;
    auto store = HistoryStore::open((tmp / "h.db").string());
    REQUIRE(store);
    auto& s = **store;
    const auto a = s.record_benchmark(bench_result(100, "2026-01-01T00:00:00Z"));
    REQUIRE(a);
    const auto b = s.record_document(SessionKind::imaging, disk(), "create", "2026-02-01T00:00:00Z", "completed",
                                     "image made", 1.0, R"({"x":1})");
    REQUIRE(b);
    CHECK(*b != *a);  // ids are unique across kinds

    auto all = s.list_all();
    REQUIRE(all);
    REQUIRE(all->size() == 2);
    CHECK((*all)[0].id == *b);  // newest first
    CHECK((*all)[1].id == *a);
    CHECK(s.list_all("id:no-such-device")->empty());

    auto one = s.find(*a);
    REQUIRE(one);
    CHECK(one->kind == SessionKind::benchmark);
    CHECK_FALSE(s.find(987654));

    auto text = s.render_text(*one);
    REQUIRE(text);
    CHECK_FALSE(text->empty());
    CHECK(text->find("WD-WCC6Y1234567") == std::string::npos);  // serials are never stored
}

TEST_CASE("parse_session_kind round-trips the names", "[history]") {
    for (int i = 0; i < kSessionKindCount; ++i) {
        const auto k = static_cast<SessionKind>(i);
        const auto parsed = parse_session_kind(session_kind_name(k));
        REQUIRE(parsed);
        CHECK(*parsed == k);
    }
    CHECK_FALSE(parse_session_kind("nonsense"));
}
