// SPDX-License-Identifier: TBD
#include "platter/storage/history.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#include "platter/core/sha256.hpp"
#include "platter/reports/bench_report.hpp"
#include "platter/reports/erase_report.hpp"
#include "platter/reports/scan_report.hpp"

namespace fs = std::filesystem;

namespace platter::storage {

std::string_view session_kind_name(SessionKind k) noexcept {
    switch (k) {
        case SessionKind::benchmark: return "benchmark";
        case SessionKind::surface_scan: return "surface-scan";
        case SessionKind::smart: return "smart";
        case SessionKind::erase: return "erase";
        case SessionKind::imaging: return "imaging";
        case SessionKind::qualification: return "qualification";
    }
    return "?";
}

namespace {

std::string iso_utc(std::chrono::system_clock::time_point tp) {
    const auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

std::string now_utc() { return iso_utc(std::chrono::system_clock::now()); }

Error db_error(sqlite3* db, const std::string& what, int rc) {
    const int primary = rc & 0xFF;
    Errc code = Errc::io_error;
    if (primary == SQLITE_NOTADB || primary == SQLITE_CORRUPT) code = Errc::corrupt_data;
    else if (primary == SQLITE_BUSY || primary == SQLITE_LOCKED) code = Errc::device_busy;
    else if (primary == SQLITE_CANTOPEN || primary == SQLITE_PERM || primary == SQLITE_READONLY)
        code = Errc::permission_denied;
    return Error{code, what + ": " + (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc)), rc};
}

// Minimal RAII statement wrapper.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) : db_(db) { rc_ = sqlite3_prepare_v2(db, sql, -1, &s_, nullptr); }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    bool ok() const { return rc_ == SQLITE_OK; }
    int rc() const { return rc_; }
    Stmt& bind(int i, const std::string& v) {
        sqlite3_bind_text(s_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind(int i, std::int64_t v) {
        sqlite3_bind_int64(s_, i, v);
        return *this;
    }
    Stmt& bind(int i, double v) {
        sqlite3_bind_double(s_, i, v);
        return *this;
    }
    int step() { return sqlite3_step(s_); }
    std::string text(int c) const {
        const auto* p = sqlite3_column_text(s_, c);
        return p ? std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(sqlite3_column_bytes(s_, c)))
                 : std::string{};
    }
    std::int64_t i64(int c) const { return sqlite3_column_int64(s_, c); }
    double real(int c) const { return sqlite3_column_double(s_, c); }

private:
    sqlite3* db_;
    sqlite3_stmt* s_ = nullptr;
    int rc_;
};

Status exec(sqlite3* db, const char* sql, const char* what) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = std::string(what) + ": " + (err ? err : sqlite3_errstr(rc));
        sqlite3_free(err);
        auto e = db_error(nullptr, what, rc);
        e.message = msg;
        return e;
    }
    return {};
}

// Called only after a statement already failed: that original error is the one
// reported, so a secondary ROLLBACK failure is deliberately ignored here.
void rollback(sqlite3* db) { static_cast<void>(exec(db, "ROLLBACK", "rollback")); }

constexpr const char* kSchemaV1 = R"SQL(
CREATE TABLE devices(
    id          INTEGER PRIMARY KEY,
    key         TEXT NOT NULL UNIQUE,
    model       TEXT,
    capacity    INTEGER,
    first_seen  TEXT,
    last_seen   TEXT
);
CREATE TABLE sessions(
    id            INTEGER PRIMARY KEY,
    kind          TEXT NOT NULL,
    subtype       TEXT,
    device_id     INTEGER NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    started_utc   TEXT,
    state         TEXT,
    headline      TEXT,
    primary_value REAL,
    document      TEXT NOT NULL
);
CREATE INDEX sessions_by_device ON sessions(device_id, kind, started_utc);
)SQL";

}  // namespace

struct HistoryStore::Impl {
    sqlite3* db = nullptr;
    ~Impl() {
        if (db) sqlite3_close_v2(db);
    }

    Result<std::int64_t> upsert_device(const DeviceInfo& d) {
        Stmt st(db,
                "INSERT INTO devices(key, model, capacity, first_seen, last_seen) VALUES(?1, ?2, ?3, ?4, ?4) "
                "ON CONFLICT(key) DO UPDATE SET model = excluded.model, capacity = excluded.capacity, "
                "last_seen = excluded.last_seen RETURNING id");
        if (!st.ok()) return db_error(db, "prepare device upsert", st.rc());
        st.bind(1, HistoryStore::device_key(d))
            .bind(2, d.model)
            .bind(3, static_cast<std::int64_t>(d.capacity_bytes))
            .bind(4, now_utc());
        const int rc = st.step();
        if (rc != SQLITE_ROW) return db_error(db, "upsert device", rc);
        return st.i64(0);
    }

    Result<std::int64_t> insert_session(SessionKind kind, const DeviceInfo& d, const std::string& subtype,
                                        const std::string& started, const std::string& state,
                                        const std::string& headline, double primary, const std::string& doc) {
        if (auto s = exec(db, "BEGIN IMMEDIATE", "begin"); !s) return s.error();
        auto dev = upsert_device(d);
        if (!dev) {
            rollback(db);
            return dev.error();
        }
        Stmt st(db,
                "INSERT INTO sessions(kind, subtype, device_id, started_utc, state, headline, primary_value, document) "
                "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
        if (!st.ok()) {
            rollback(db);
            return db_error(db, "prepare session insert", st.rc());
        }
        st.bind(1, std::string(session_kind_name(kind)))
            .bind(2, subtype)
            .bind(3, *dev)
            .bind(4, started.empty() ? now_utc() : started)
            .bind(5, state)
            .bind(6, headline)
            .bind(7, primary)
            .bind(8, doc);
        if (const int rc = st.step(); rc != SQLITE_DONE) {
            auto e = db_error(db, "insert session", rc);
            rollback(db);
            return e;
        }
        const auto id = sqlite3_last_insert_rowid(db);
        if (auto s = exec(db, "COMMIT", "commit"); !s) return s.error();
        return id;
    }

    Result<std::string> document(SessionKind kind, std::int64_t id) {
        Stmt st(db, "SELECT document FROM sessions WHERE id = ?1 AND kind = ?2");
        if (!st.ok()) return db_error(db, "prepare load", st.rc());
        st.bind(1, id).bind(2, std::string(session_kind_name(kind)));
        const int rc = st.step();
        if (rc == SQLITE_DONE)
            return Error{Errc::not_found, "no " + std::string(session_kind_name(kind)) + " session with id " +
                                              std::to_string(id)};
        if (rc != SQLITE_ROW) return db_error(db, "load session", rc);
        return st.text(0);
    }
};

HistoryStore::~HistoryStore() = default;

std::string HistoryStore::default_path() {
    if (const char* env = std::getenv("PLATTER_HISTORY_DB"); env && *env) return env;
    fs::path base;
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) base = appdata;
    else base = ".";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) base = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home) base = fs::path(home) / ".local" / "share";
    else base = ".";
#endif
    return (base / "platter" / "history.db").string();
}

std::string HistoryStore::device_key(const DeviceInfo& d) {
    // Hashes keep raw stable ids (which may embed serials) out of the database.
    auto h = [](const std::string& s) { return Sha256::hex_of(s).substr(0, 24); };
    if (d.bus == BusType::image)
        return "image:" + fs::path(d.path).filename().string() + ":" + std::to_string(d.capacity_bytes);
    if (!d.stable_id.empty()) return "id:" + h(d.stable_id);
    if (!d.serial.empty()) return "sn:" + h(d.model + "|" + d.serial);
    return "path:" + d.path + ":" + std::to_string(d.capacity_bytes);
}

Result<std::unique_ptr<HistoryStore>> HistoryStore::open(const std::string& path) {
    std::error_code ec;
    const auto parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    if (ec) return Error{Errc::permission_denied, "cannot create " + parent.string() + ": " + ec.message()};

    std::unique_ptr<HistoryStore> store(new HistoryStore());
    store->impl_ = std::make_unique<Impl>();
    store->path_ = path;
    const int rc = sqlite3_open_v2(path.c_str(), &store->impl_->db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) return db_error(store->impl_->db, "open " + path, rc);
    sqlite3* db = store->impl_->db;
    sqlite3_busy_timeout(db, 3000);

    // Probe first: a non-database file fails here with SQLITE_NOTADB.
    if (auto s = exec(db, "PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL;", "configure database"); !s)
        return s.error();
    if (auto s = exec(db, "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)", "create meta");
        !s)
        return s.error();

    int version = 0;
    {
        Stmt st(db, "SELECT value FROM meta WHERE key = 'schema_version'");
        if (!st.ok()) return db_error(db, "read schema version", st.rc());
        if (st.step() == SQLITE_ROW) version = std::atoi(st.text(0).c_str());
    }
    if (version > kSchemaVersion)
        return Error{Errc::unsupported, "history database " + path + " has schema " + std::to_string(version) +
                                            ", newer than this platter (" + std::to_string(kSchemaVersion) +
                                            "); refusing to modify it"};
    if (version < 1) {
        if (auto s = exec(db, "BEGIN IMMEDIATE", "begin migration"); !s) return s.error();
        auto m = exec(db, kSchemaV1, "create schema v1");
        if (m) m = exec(db, "INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', '1')", "set version");
        if (!m) {
            rollback(db);
            return m.error();
        }
        if (auto s = exec(db, "COMMIT", "commit migration"); !s) return s.error();
        version = 1;
    }
    // Future migrations: if (version < 2) { ... } with the same transaction pattern.
    store->version_ = version;
    return store;
}

Result<std::int64_t> HistoryStore::record_benchmark(const bench::BenchResult& r) {
    using bench::BenchKind;
    double primary = 0;
    auto best = [&](auto proj) {
        double v = 0;
        for (const auto& p : r.phases) v = std::max(v, proj(p.metrics));
        return v;
    };
    switch (r.config.kind) {
        case BenchKind::sequential:
        case BenchKind::sequential_write:
        case BenchKind::block_sizes: primary = best([](const bench::Metrics& m) { return m.mb_per_s; }); break;
        case BenchKind::random:
        case BenchKind::random_write:
        case BenchKind::queue_depths: primary = best([](const bench::Metrics& m) { return m.iops; }); break;
        case BenchKind::access_time:
            primary = r.phases.empty() ? 0 : r.phases.front().metrics.lat_mean_us / 1000.0;
            break;
        case BenchKind::speed_curve: {
            double sum = 0;
            for (const auto& p : r.positions) sum += p.bytes_per_sec / 1e6;
            primary = r.positions.empty() ? 0 : sum / static_cast<double>(r.positions.size());
            break;
        }
    }
    return impl_->insert_session(SessionKind::benchmark, r.device, std::string(bench::bench_kind_name(r.config.kind)),
                                 r.started_utc, std::string(operation_state_name(r.state)), r.headline(), primary,
                                 reports::bench_to_json(r));
}

Result<std::int64_t> HistoryStore::record_scan(const surface::ScanSnapshot& s) {
    const auto& st = s.stats;
    char head[128];
    std::snprintf(head, sizeof head, "%llu failed, %llu slow of %llu blocks, %.1f MB/s",
                  static_cast<unsigned long long>(st.failed), static_cast<unsigned long long>(st.slow),
                  static_cast<unsigned long long>(st.requests), st.average_bytes_per_sec / 1e6);
    const auto started = s.progress.start_time == std::chrono::system_clock::time_point{}
                             ? std::string{}
                             : iso_utc(s.progress.start_time);
    return impl_->insert_session(SessionKind::surface_scan, s.device, std::string(surface::scan_mode_name(s.config.mode)),
                                 started, std::string(operation_state_name(s.progress.state)), head,
                                 static_cast<double>(st.failed), reports::scan_to_json(s));
}

Result<std::int64_t> HistoryStore::record_smart(const DeviceInfo& d, const smart::Report& r,
                                                const smart::Assessment& a) {
    nlohmann::json doc = reports::envelope("smart-snapshot");
    doc["device"] = reports::to_json(d);
    doc["smart"] = reports::to_json(r);
    doc["assessment"] = reports::to_json(a);
    std::string head(smart::health_name(a.status));
    if (r.temperature_c) head += ", " + std::to_string(*r.temperature_c) + " C";
    if (r.power_on_hours) head += ", " + std::to_string(*r.power_on_hours) + " h";
    return impl_->insert_session(SessionKind::smart, d, std::string(smart::health_name(a.status)), {},
                                 std::string(smart::health_name(a.status)), head,
                                 r.temperature_c ? static_cast<double>(*r.temperature_c) : 0.0, doc.dump());
}

Result<std::int64_t> HistoryStore::record_erase(const erase::EraseResult& r) {
    const std::string head = std::string(erase::verdict_name(r.verdict)) + ", " + std::string(erase::method_label(r.config.method)) +
                             ", " + format_capacity(r.affected_bytes);
    return impl_->insert_session(SessionKind::erase, r.device, std::string(erase::method_name(r.config.method)),
                                 r.started_utc, std::string(operation_state_name(r.state)), head,
                                 static_cast<double>(r.passes.size()), reports::erase_to_json(r));
}

Result<std::int64_t> HistoryStore::record_document(SessionKind kind, const DeviceInfo& d, const std::string& subtype,
                                                   const std::string& started_utc, const std::string& state,
                                                   const std::string& headline, double primary_value,
                                                   const std::string& json_document) {
    return impl_->insert_session(kind, d, subtype, started_utc, state, headline, primary_value, json_document);
}

Result<std::vector<SessionSummary>> HistoryStore::list(SessionKind kind, const std::string& device_key, int limit) {
    Stmt st(impl_->db,
            "SELECT s.id, s.subtype, d.key, d.model, s.started_utc, s.state, s.headline, s.primary_value "
            "FROM sessions s JOIN devices d ON d.id = s.device_id "
            "WHERE s.kind = ?1 AND (?2 = '' OR d.key = ?2) "
            "ORDER BY s.started_utc DESC, s.id DESC LIMIT ?3");
    if (!st.ok()) return db_error(impl_->db, "prepare list", st.rc());
    st.bind(1, std::string(session_kind_name(kind))).bind(2, device_key).bind(3, static_cast<std::int64_t>(limit));
    std::vector<SessionSummary> out;
    int rc;
    while ((rc = st.step()) == SQLITE_ROW) {
        SessionSummary s;
        s.id = st.i64(0);
        s.kind = kind;
        s.subtype = st.text(1);
        s.device_key = st.text(2);
        s.device_model = st.text(3);
        s.started_utc = st.text(4);
        s.state = st.text(5);
        s.headline = st.text(6);
        s.primary_value = st.real(7);
        out.push_back(std::move(s));
    }
    if (rc != SQLITE_DONE) return db_error(impl_->db, "list sessions", rc);
    return out;
}

Result<std::vector<DeviceRecord>> HistoryStore::devices() {
    Stmt st(impl_->db, "SELECT id, key, model, capacity, first_seen, last_seen FROM devices ORDER BY last_seen DESC");
    if (!st.ok()) return db_error(impl_->db, "prepare devices", st.rc());
    std::vector<DeviceRecord> out;
    int rc;
    while ((rc = st.step()) == SQLITE_ROW)
        out.push_back({st.i64(0), st.text(1), st.text(2), static_cast<std::uint64_t>(st.i64(3)), st.text(4), st.text(5)});
    if (rc != SQLITE_DONE) return db_error(impl_->db, "list devices", rc);
    return out;
}

Result<bench::BenchResult> HistoryStore::load_benchmark(std::int64_t id) {
    auto doc = impl_->document(SessionKind::benchmark, id);
    if (!doc) return doc.error();
    return reports::bench_from_json(*doc);
}

Result<surface::ScanSnapshot> HistoryStore::load_scan(std::int64_t id) {
    auto doc = impl_->document(SessionKind::surface_scan, id);
    if (!doc) return doc.error();
    return reports::scan_from_json(*doc);
}

Result<std::string> HistoryStore::load_smart_json(std::int64_t id) { return impl_->document(SessionKind::smart, id); }

Result<erase::EraseResult> HistoryStore::load_erase(std::int64_t id) {
    auto doc = impl_->document(SessionKind::erase, id);
    if (!doc) return doc.error();
    return reports::erase_from_json(*doc);
}

Result<std::string> HistoryStore::load_document(SessionKind kind, std::int64_t id) { return impl_->document(kind, id); }

Status HistoryStore::remove(SessionKind kind, std::int64_t id) {
    Stmt st(impl_->db, "DELETE FROM sessions WHERE id = ?1 AND kind = ?2");
    if (!st.ok()) return db_error(impl_->db, "prepare delete", st.rc());
    st.bind(1, id).bind(2, std::string(session_kind_name(kind)));
    if (const int rc = st.step(); rc != SQLITE_DONE) return db_error(impl_->db, "delete session", rc);
    if (sqlite3_changes(impl_->db) == 0)
        return Error{Errc::not_found, "no " + std::string(session_kind_name(kind)) + " session with id " + std::to_string(id)};
    return {};
}

}  // namespace platter::storage
