// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Persistent operation history in SQLite (spec §14.2): benchmark sessions,
// surface scans and SMART snapshots, keyed by device.
//
// Privacy: the database never stores a raw serial number. Devices are keyed by
// their stable id when available, otherwise by a SHA-256 hash of model+serial;
// stored JSON documents are written with serials omitted.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "platter/benchmark/benchmark.hpp"
#include "platter/core/error.hpp"
#include "platter/erase/erase.hpp"
#include "platter/imaging/imaging.hpp"
#include "platter/qualify/qualify.hpp"
#include "platter/smart/smart.hpp"
#include "platter/surface/surface_scan.hpp"

namespace platter::storage {

enum class SessionKind : std::uint8_t { benchmark, surface_scan, smart, erase, imaging, qualification };
std::string_view session_kind_name(SessionKind k) noexcept;
inline constexpr int kSessionKindCount = 6;
std::optional<SessionKind> parse_session_kind(std::string_view s) noexcept;

struct SessionSummary {
    std::int64_t id = 0;
    SessionKind kind = SessionKind::benchmark;
    std::string subtype;        // benchmark kind ("sequential", ...), "READ", or SMART health
    std::string device_key;
    std::string device_model;
    std::string started_utc;
    std::string state;          // OperationState name, or health for SMART
    std::string headline;       // e.g. "512.3 MB/s", "3 failed of 4800", "WARNING"
    double primary_value = 0;   // the number to plot over time
};

struct DeviceRecord {
    std::int64_t id = 0;
    std::string key;
    std::string model;
    std::uint64_t capacity_bytes = 0;
    std::string first_seen_utc;
    std::string last_seen_utc;
};

class HistoryStore {
public:
    ~HistoryStore();
    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    // Opens (creating if needed, including parent directories) and migrates.
    static Result<std::unique_ptr<HistoryStore>> open(const std::string& path);
    // $PLATTER_HISTORY_DB, else <data dir>/platter/history.db
    // (%APPDATA% on Windows, $XDG_DATA_HOME or ~/.local/share elsewhere).
    static std::string default_path();
    // Stable identity for history purposes; never contains a raw serial.
    static std::string device_key(const DeviceInfo& d);

    static constexpr int kSchemaVersion = 1;
    int schema_version() const noexcept { return version_; }
    const std::string& path() const noexcept { return path_; }

    Result<std::int64_t> record_benchmark(const bench::BenchResult& r);
    Result<std::int64_t> record_scan(const surface::ScanSnapshot& s);
    Result<std::int64_t> record_smart(const DeviceInfo& d, const smart::Report& r, const smart::Assessment& a);
    Result<std::int64_t> record_erase(const erase::EraseResult& r);
    Result<std::int64_t> record_imaging(const imaging::ImagingResult& r);
    Result<std::int64_t> record_qualification(const qualify::QualifyResult& r);
    // Generic path for sessions whose typed loader lives elsewhere (imaging,
    // qualification): the JSON document is stored verbatim, so callers must
    // already have removed serial numbers from it.
    Result<std::int64_t> record_document(SessionKind kind, const DeviceInfo& d, const std::string& subtype,
                                         const std::string& started_utc, const std::string& state,
                                         const std::string& headline, double primary_value,
                                         const std::string& json_document);

    // Newest first. Empty device_key = all devices.
    Result<std::vector<SessionSummary>> list(SessionKind kind, const std::string& device_key = {}, int limit = 200);
    // Every kind at once, newest first (the history browser). Empty device_key = all devices.
    Result<std::vector<SessionSummary>> list_all(const std::string& device_key = {}, int limit = 500);
    // One session by id (ids are unique across kinds).
    Result<SessionSummary> find(std::int64_t id);
    // Human-readable report for any session. Serial numbers appear only if asked for
    // and only where the stored document has them (documents are stored without).
    Result<std::string> render_text(const SessionSummary& s, bool include_serial = false);
    Result<std::vector<DeviceRecord>> devices();

    Result<bench::BenchResult> load_benchmark(std::int64_t id);
    Result<surface::ScanSnapshot> load_scan(std::int64_t id);
    Result<std::string> load_smart_json(std::int64_t id);
    Result<erase::EraseResult> load_erase(std::int64_t id);
    Result<imaging::ImagingResult> load_imaging(std::int64_t id);
    Result<qualify::QualifyResult> load_qualification(std::int64_t id);
    Result<std::string> load_document(SessionKind kind, std::int64_t id);

    Status remove(SessionKind kind, std::int64_t id);

private:
    HistoryStore() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string path_;
    int version_ = 0;
};

}  // namespace platter::storage
