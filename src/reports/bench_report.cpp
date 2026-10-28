// SPDX-License-Identifier: TBD
#include "platter/reports/bench_report.hpp"

#include <cstdio>
#include <sstream>

using nlohmann::json;

namespace platter::reports {
namespace {

using namespace platter::bench;
constexpr int kSchema = 1;

std::optional<BenchKind> kind_from(const std::string& s) {
    for (int i = 0; i < kBenchKindCount; ++i)
        if (bench_kind_name(static_cast<BenchKind>(i)) == s) return static_cast<BenchKind>(i);
    return std::nullopt;
}

OperationState state_from(const std::string& s) {
    for (int i = 0; i <= static_cast<int>(OperationState::cancelled); ++i)
        if (operation_state_name(static_cast<OperationState>(i)) == s) return static_cast<OperationState>(i);
    return OperationState::failed;
}

json metrics_json(const Metrics& m) {
    return {{"ops", m.ops},           {"bytes", m.bytes},         {"errors", m.errors},
            {"wall_ns", m.wall_ns},   {"mb_per_s", m.mb_per_s},   {"mib_per_s", m.mib_per_s},
            {"iops", m.iops},         {"lat_mean_us", m.lat_mean_us}, {"lat_min_us", m.lat_min_us},
            {"lat_median_us", m.lat_median_us}, {"lat_p95_us", m.lat_p95_us}, {"lat_p99_us", m.lat_p99_us},
            {"lat_max_us", m.lat_max_us}};
}

Metrics metrics_from(const json& j) {
    Metrics m;
    m.ops = j.at("ops").get<std::uint64_t>();
    m.bytes = j.at("bytes").get<std::uint64_t>();
    m.errors = j.value("errors", std::uint64_t{0});
    m.wall_ns = j.at("wall_ns").get<std::uint64_t>();
    m.mb_per_s = j.at("mb_per_s").get<double>();
    m.mib_per_s = j.value("mib_per_s", m.mb_per_s * 1e6 / 1048576.0);
    m.iops = j.at("iops").get<double>();
    m.lat_mean_us = j.value("lat_mean_us", 0.0);
    m.lat_min_us = j.value("lat_min_us", std::uint64_t{0});
    m.lat_median_us = j.value("lat_median_us", std::uint64_t{0});
    m.lat_p95_us = j.value("lat_p95_us", std::uint64_t{0});
    m.lat_p99_us = j.value("lat_p99_us", std::uint64_t{0});
    m.lat_max_us = j.value("lat_max_us", std::uint64_t{0});
    return m;
}

std::string f(const char* fmt, double v) {
    char b[64];
    std::snprintf(b, sizeof b, fmt, v);
    return b;
}

}  // namespace

std::string bench_to_json(const BenchResult& r, const Privacy& p) {
    const auto& c = r.config;
    json phases = json::array();
    for (const auto& ph : r.phases)
        phases.push_back({{"label", ph.label}, {"block_bytes", ph.block_bytes}, {"queue_depth", ph.queue_depth},
                          {"metrics", metrics_json(ph.metrics)}});
    json timeline = json::array();
    for (const auto& t : r.timeline) timeline.push_back({t.t_s, t.bytes_per_sec});
    json positions = json::array();
    for (const auto& pp : r.positions) positions.push_back({pp.lba, pp.bytes_per_sec});
    json buckets = json::array();
    for (const auto& [up, n] : r.latency_buckets) buckets.push_back({up, n});

    json j = envelope("benchmark");
    j["bench_schema"] = kSchema;
    j["device"] = to_json(r.device, p);
    j["config"] = {{"kind", std::string(bench_kind_name(c.kind))},
                   {"start_lba", c.start_lba},
                   {"end_lba", c.end_lba},
                   {"block_bytes", c.block_bytes},
                   {"queue_depth", c.queue_depth},
                   {"max_bytes", c.max_bytes},
                   {"max_seconds", c.max_seconds},
                   {"curve_points", c.curve_points},
                   {"curve_bytes_per_point", c.curve_bytes_per_point},
                   {"sweep_block_sizes", c.sweep_block_sizes},
                   {"sweep_queue_depths", c.sweep_queue_depths},
                   {"seed", c.seed},
                   {"direct_io", c.direct_io}};
    j["started_utc"] = r.started_utc;
    j["state"] = std::string(operation_state_name(r.state));
    j["message"] = r.message;
    j["headline"] = r.headline();
    j["phases"] = phases;
    j["timeline"] = timeline;
    j["positions"] = positions;
    j["latency_buckets_us"] = buckets;
    j["warnings"] = r.warnings;
    return j.dump();
}

Result<BenchResult> bench_from_json(const std::string& text) {
    auto bad = [](const std::string& why) { return Error{Errc::corrupt_data, "invalid benchmark file: " + why}; };
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        return bad(std::string("not JSON (") + e.what() + ")");
    }
    try {
        if (!j.is_object() || j.value("kind", "") != "benchmark") return bad("not a platter benchmark export");
        if (j.value("bench_schema", 0) != kSchema) return bad("unsupported bench_schema");
        BenchResult r;
        const auto& c = j.at("config");
        auto kind = kind_from(c.at("kind").get<std::string>());
        if (!kind) return bad("unknown benchmark kind");
        r.config.kind = *kind;
        r.config.start_lba = c.at("start_lba").get<std::uint64_t>();
        r.config.end_lba = c.at("end_lba").get<std::uint64_t>();
        r.config.block_bytes = c.at("block_bytes").get<std::uint32_t>();
        r.config.queue_depth = c.at("queue_depth").get<std::uint32_t>();
        r.config.max_bytes = c.value("max_bytes", std::uint64_t{0});
        r.config.max_seconds = c.value("max_seconds", 0.0);
        r.config.curve_points = c.value("curve_points", 100u);
        r.config.curve_bytes_per_point = c.value("curve_bytes_per_point", std::uint64_t{16} << 20);
        r.config.sweep_block_sizes = c.value("sweep_block_sizes", r.config.sweep_block_sizes);
        r.config.sweep_queue_depths = c.value("sweep_queue_depths", r.config.sweep_queue_depths);
        r.config.seed = c.value("seed", std::uint64_t{0});
        r.config.direct_io = c.value("direct_io", false);
        if (r.config.queue_depth < 1 || r.config.queue_depth > 64) return bad("queue depth out of range");

        const auto& d = j.at("device");
        r.device.path = d.value("path", "");
        r.device.name = d.value("name", "");
        r.device.vendor = d.value("vendor", "");
        r.device.model = d.value("model", "");
        r.device.firmware = d.value("firmware", "");
        r.device.stable_id = d.value("stable_id", "");
        r.device.serial = d.value("serial", "");
        r.device.capacity_bytes = d.value("capacity_bytes", std::uint64_t{0});
        r.device.logical_sector_size = d.value("logical_sector_size", 512u);
        r.device.physical_sector_size = d.value("physical_sector_size", r.device.logical_sector_size);

        r.started_utc = j.value("started_utc", "");
        r.state = state_from(j.value("state", "FAILED"));
        r.message = j.value("message", "");
        for (const auto& ph : j.at("phases")) {
            Phase p;
            p.label = ph.at("label").get<std::string>();
            p.block_bytes = ph.at("block_bytes").get<std::uint32_t>();
            p.queue_depth = ph.at("queue_depth").get<std::uint32_t>();
            p.metrics = metrics_from(ph.at("metrics"));
            r.phases.push_back(std::move(p));
        }
        for (const auto& t : j.at("timeline")) {
            if (!t.is_array() || t.size() != 2) return bad("malformed timeline point");
            r.timeline.push_back({t[0].get<double>(), t[1].get<double>()});
        }
        for (const auto& t : j.at("positions")) {
            if (!t.is_array() || t.size() != 2) return bad("malformed position point");
            r.positions.push_back({t[0].get<std::uint64_t>(), t[1].get<double>()});
        }
        for (const auto& b : j.at("latency_buckets_us")) {
            if (!b.is_array() || b.size() != 2) return bad("malformed latency bucket");
            r.latency_buckets.emplace_back(b[0].get<std::uint64_t>(), b[1].get<std::uint64_t>());
        }
        if (j.contains("warnings")) r.warnings = j.at("warnings").get<std::vector<std::string>>();
        return r;
    } catch (const json::exception& e) {
        return bad(std::string("missing or mistyped field (") + e.what() + ")");
    }
}

std::string bench_to_text(const BenchResult& r, const Privacy& p) {
    std::ostringstream o;
    const auto& c = r.config;
    auto row = [&](const char* k, const std::string& v) {
        char key[24];
        std::snprintf(key, sizeof key, "%-20s", k);
        o << "  " << key << v << "\n";
    };
    o << "platter benchmark report: " << bench_kind_title(c.kind) << "\n";
    o << std::string(26 + bench_kind_title(c.kind).size(), '=') << "\n\nDevice\n";
    row("Path", r.device.path);
    row("Model", r.device.model.empty() ? "-" : r.device.model);
    const auto serial = p.include_serials ? r.device.serial : mask_serial(r.device.serial);
    row("Serial", serial.empty() ? "-" : serial);
    row("Capacity", format_capacity(r.device.capacity_bytes));
    row("Interface", std::string(bus_type_name(r.device.bus)) + " / " + std::string(media_kind_name(r.device.media)));

    o << "\nConfiguration\n";
    row("Test", std::string(bench_kind_title(c.kind)));
    row("Region (LBA)", std::to_string(c.start_lba) + " - " +
                            (c.end_lba == UINT64_MAX ? std::string("end") : std::to_string(c.end_lba)));
    row("Block size", std::to_string(c.block_bytes) + " bytes");
    row("Queue depth", std::to_string(c.queue_depth) + " (threads with independent handles)");
    if (c.max_bytes) row("Size limit / phase", format_capacity(c.max_bytes));
    row("Time limit / phase", f("%.1f s", c.max_seconds));
    if (c.kind == BenchKind::speed_curve)
        row("Curve", std::to_string(c.curve_points) + " points x " + format_capacity(c.curve_bytes_per_point));
    row("I/O mode", c.direct_io ? "direct (OS cache bypassed)" : "buffered (OS cache may serve reads)");
    row("Started", r.started_utc.empty() ? "-" : r.started_utc);
    row("State", std::string(operation_state_name(r.state)) + (r.message.empty() ? "" : " (" + r.message + ")"));

    o << "\nResult: " << r.headline() << "\n\n";
    o << "  Phase            MB/s      IOPS   mean ms    p50 ms    p95 ms    p99 ms    max ms  errors\n";
    o << "  ------------ -------- --------- --------- --------- --------- --------- --------- -------\n";
    for (const auto& ph : r.phases) {
        const auto& m = ph.metrics;
        char line[200];
        std::snprintf(line, sizeof line, "  %-12s %8.1f %9.0f %9.3f %9.3f %9.3f %9.3f %9.3f %7llu\n",
                      ph.label.substr(0, 12).c_str(), m.mb_per_s, m.iops, m.lat_mean_us / 1000.0,
                      static_cast<double>(m.lat_median_us) / 1000.0, static_cast<double>(m.lat_p95_us) / 1000.0,
                      static_cast<double>(m.lat_p99_us) / 1000.0, static_cast<double>(m.lat_max_us) / 1000.0,
                      static_cast<unsigned long long>(m.errors));
        o << line;
    }
    if (!r.warnings.empty()) {
        o << "\nCaveats\n";
        for (const auto& w : r.warnings) o << "  ! " << w << "\n";
    }
    o << "\nThroughput is bytes divided by wall-clock time; latency is the duration of\n"
         "each individual read. Results vary with OS, connection, cache state and\n"
         "background activity; compare runs made with identical settings only.\n";
    return o.str();
}

}  // namespace platter::reports
