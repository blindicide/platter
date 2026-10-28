// SPDX-License-Identifier: TBD
#include "platter/reports/scan_report.hpp"

#include <cinttypes>
#include <cstdio>
#include <sstream>

using nlohmann::json;

namespace platter::reports {
namespace {

using namespace platter::surface;

constexpr int kSchema = 1;

double seconds(std::chrono::nanoseconds ns) { return std::chrono::duration<double>(ns).count(); }

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

std::string mbps(double bytes_per_sec) { return fmt("%.2f MB/s", bytes_per_sec / 1e6); }

std::string ms(std::uint64_t us) { return fmt("%.3f ms", static_cast<double>(us) / 1000.0); }

OperationState state_from(const std::string& s) {
    for (int i = 0; i <= static_cast<int>(OperationState::cancelled); ++i) {
        auto st = static_cast<OperationState>(i);
        if (operation_state_name(st) == s) return st;
    }
    return OperationState::failed;
}

template <typename E, int N>
std::optional<E> enum_from(const std::string& s, std::string_view (*name)(E) noexcept) {
    for (int i = 0; i < N; ++i)
        if (name(static_cast<E>(i)) == s) return static_cast<E>(i);
    return std::nullopt;
}

}  // namespace

// ------------------------------------------------------------------ JSON

std::string scan_to_json(const ScanSnapshot& s, const Privacy& p) {
    const auto& c = s.config;
    json cls = json::array();
    for (int i = 0; i < kLatencyClassCount; ++i)
        cls.push_back({{"class", std::string(latency_class_name(static_cast<LatencyClass>(i)))},
                       {"blocks", s.stats.per_class[static_cast<std::size_t>(i)]}});

    // Tested blocks only, as compact rows: untested blocks are implied.
    json blocks = json::array();
    for (std::size_t i = 0; i < s.blocks.size(); ++i) {
        const auto& b = s.blocks[i];
        if (b.cls == LatencyClass::untested) continue;
        blocks.push_back({i, b.duration_us, b.time_ms, b.errc, b.retries, static_cast<int>(b.cls)});
    }

    json j = envelope("surface-scan");
    j["scan_schema"] = kSchema;
    j["device"] = to_json(s.device, p);
    j["config"] = {
        {"mode", std::string(scan_mode_name(c.mode))},
        {"start_lba", c.start_lba},
        {"end_lba", s.layout.end_lba},
        {"block_bytes", c.block_bytes},
        {"order", std::string(scan_order_name(c.order))},
        {"sample_percent", c.sample_percent},
        {"seed", c.seed},
        {"retries", c.retries},
        {"direct_io", c.direct_io},
        {"error_policy", c.error_policy == ErrorPolicy::stop_after_n ? "stop_after_n" : "continue"},
        {"max_errors", c.max_errors},
        {"thresholds_us", c.thresholds.us},
    };
    j["layout"] = {{"start_lba", s.layout.start_lba},
                   {"end_lba", s.layout.end_lba},
                   {"sector_size", s.layout.sector_size},
                   {"lbas_per_block", s.layout.lbas_per_block},
                   {"block_count", s.layout.block_count}};
    j["result"] = {{"state", std::string(operation_state_name(s.progress.state))},
                   {"message", s.progress.message},
                   {"elapsed_s", seconds(s.progress.elapsed)},
                   {"completed_bytes", s.progress.completed_bytes},
                   {"total_bytes", s.progress.total_bytes}};
    const auto& st = s.stats;
    j["stats"] = {{"total_bytes", st.total_bytes},     {"tested_bytes", st.tested_bytes},
                  {"device_bytes", st.device_bytes},   {"requests", st.requests},
                  {"ok", st.ok},                       {"failed", st.failed},
                  {"slow", st.slow},                   {"retries", st.retries},
                  {"min_us", st.min_us},               {"max_us", st.max_us},
                  {"mean_us", st.mean_us},             {"median_us", st.median_us},
                  {"p95_us", st.p95_us},               {"p99_us", st.p99_us},
                  {"average_bytes_per_sec", st.average_bytes_per_sec},
                  {"per_class", cls}};
    j["warnings"] = s.warnings;
    j["note"] = "Latency classes describe observed I/O response time, not physical sector health.";
    j["blocks_format"] = "[index, duration_us, time_ms, errc, retries, class]";
    j["blocks"] = std::move(blocks);
    return j.dump();  // compact: ~30 bytes per tested block
}

Result<ScanSnapshot> scan_from_json(const std::string& text) {
    auto bad = [](const std::string& why) { return Error{Errc::corrupt_data, "invalid scan file: " + why}; };
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        return bad(std::string("not JSON (") + e.what() + ")");
    }
    try {
        if (!j.is_object() || j.value("kind", "") != "surface-scan") return bad("not a platter surface-scan export");
        if (j.value("scan_schema", 0) != kSchema) return bad("unsupported scan_schema");

        ScanSnapshot s;
        const auto& L = j.at("layout");
        s.layout.start_lba = L.at("start_lba").get<std::uint64_t>();
        s.layout.end_lba = L.at("end_lba").get<std::uint64_t>();
        s.layout.sector_size = L.at("sector_size").get<std::uint32_t>();
        s.layout.lbas_per_block = L.at("lbas_per_block").get<std::uint64_t>();
        s.layout.block_count = L.at("block_count").get<std::uint64_t>();
        if (s.layout.sector_size == 0 || s.layout.lbas_per_block == 0 || s.layout.end_lba < s.layout.start_lba)
            return bad("impossible geometry");
        const std::uint64_t span = s.layout.end_lba - s.layout.start_lba + 1;
        if (s.layout.block_count != (span + s.layout.lbas_per_block - 1) / s.layout.lbas_per_block)
            return bad("block_count does not match the LBA range");
        if (s.layout.block_count > 256ull * 1024 * 1024) return bad("too many blocks");

        const auto& c = j.at("config");
        auto mode = enum_from<ScanMode, 5>(c.at("mode").get<std::string>(), scan_mode_name);
        auto order = enum_from<ScanOrder, 4>(c.at("order").get<std::string>(), scan_order_name);
        if (!mode || !order) return bad("unknown mode or order");
        s.config.mode = *mode;
        s.config.order = *order;
        s.config.start_lba = c.at("start_lba").get<std::uint64_t>();
        s.config.end_lba = c.at("end_lba").get<std::uint64_t>();
        s.config.block_bytes = c.at("block_bytes").get<std::uint32_t>();
        s.config.sample_percent = c.value("sample_percent", 100.0);
        s.config.seed = c.value("seed", std::uint64_t{0});
        s.config.retries = c.value("retries", 0u);
        s.config.direct_io = c.value("direct_io", false);
        s.config.error_policy = c.value("error_policy", "continue") == "stop_after_n" ? ErrorPolicy::stop_after_n
                                                                                     : ErrorPolicy::continue_scan;
        s.config.max_errors = c.value("max_errors", std::uint64_t{0});
        s.config.thresholds.us = c.at("thresholds_us").get<std::array<std::uint32_t, 4>>();
        if (!s.config.thresholds.valid()) return bad("thresholds are not increasing");

        const auto& d = j.at("device");
        s.device.path = d.value("path", "");
        s.device.name = d.value("name", "");
        s.device.vendor = d.value("vendor", "");
        s.device.model = d.value("model", "");
        s.device.firmware = d.value("firmware", "");
        s.device.stable_id = d.value("stable_id", "");
        s.device.serial = d.value("serial", "");
        s.device.capacity_bytes = d.value("capacity_bytes", std::uint64_t{0});
        s.device.logical_sector_size = d.value("logical_sector_size", s.layout.sector_size);
        s.device.physical_sector_size = d.value("physical_sector_size", s.layout.sector_size);

        const auto& r = j.at("result");
        s.progress.state = state_from(r.value("state", "FAILED"));
        s.progress.message = r.value("message", "");
        s.progress.elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(r.value("elapsed_s", 0.0)));
        s.progress.completed_bytes = r.value("completed_bytes", std::uint64_t{0});
        s.progress.total_bytes = r.value("total_bytes", std::uint64_t{0});
        s.progress.type = "surface-scan";
        s.progress.device_id = s.device.path;

        const auto& st = j.at("stats");
        s.stats.total_bytes = st.value("total_bytes", std::uint64_t{0});
        s.stats.tested_bytes = st.value("tested_bytes", std::uint64_t{0});
        s.stats.device_bytes = st.value("device_bytes", std::uint64_t{0});
        s.stats.requests = st.value("requests", std::uint64_t{0});
        s.stats.ok = st.value("ok", std::uint64_t{0});
        s.stats.failed = st.value("failed", std::uint64_t{0});
        s.stats.slow = st.value("slow", std::uint64_t{0});
        s.stats.retries = st.value("retries", std::uint64_t{0});
        s.stats.min_us = st.value("min_us", std::uint64_t{0});
        s.stats.max_us = st.value("max_us", std::uint64_t{0});
        s.stats.mean_us = st.value("mean_us", 0.0);
        s.stats.median_us = st.value("median_us", std::uint64_t{0});
        s.stats.p95_us = st.value("p95_us", std::uint64_t{0});
        s.stats.p99_us = st.value("p99_us", std::uint64_t{0});
        s.stats.average_bytes_per_sec = st.value("average_bytes_per_sec", 0.0);

        if (j.contains("warnings")) s.warnings = j.at("warnings").get<std::vector<std::string>>();

        s.blocks.assign(static_cast<std::size_t>(s.layout.block_count), BlockResult{});
        std::array<std::uint64_t, kLatencyClassCount> counted{};
        for (const auto& row : j.at("blocks")) {
            if (!row.is_array() || row.size() != 6) return bad("malformed block row");
            const auto idx = row[0].get<std::uint64_t>();
            const auto cls = row[5].get<int>();
            if (idx >= s.layout.block_count) return bad("block index out of range");
            if (cls <= 0 || cls >= kLatencyClassCount) return bad("invalid latency class");
            auto& b = s.blocks[static_cast<std::size_t>(idx)];
            if (b.cls != LatencyClass::untested) return bad("duplicate block index");
            b.duration_us = row[1].get<std::uint32_t>();
            b.time_ms = row[2].get<std::uint32_t>();
            b.errc = row[3].get<std::uint16_t>();
            b.retries = row[4].get<std::uint8_t>();
            b.cls = static_cast<LatencyClass>(cls);
            ++counted[static_cast<std::size_t>(cls)];
        }
        std::uint64_t tested = 0;
        for (int i = 1; i < kLatencyClassCount; ++i) tested += counted[static_cast<std::size_t>(i)];
        counted[0] = s.layout.block_count - tested;
        if (tested != s.stats.requests) return bad("block rows do not match the recorded request count");
        s.stats.per_class = counted;
        return s;
    } catch (const json::exception& e) {
        return bad(std::string("missing or mistyped field (") + e.what() + ")");
    }
}

// ------------------------------------------------------------------ CSV

std::string scan_to_csv(const ScanSnapshot& s) {
    std::ostringstream o;
    o << "block,first_lba,last_lba,lba_count,bytes,duration_us,mb_per_s,class,error,retries,time_ms\n";
    for (std::size_t i = 0; i < s.blocks.size(); ++i) {
        const auto& b = s.blocks[i];
        if (b.cls == LatencyClass::untested) continue;
        const auto first = s.layout.first_lba(i), count = s.layout.lba_count(i);
        const auto bytes = count * s.layout.sector_size;
        const double rate = b.cls != LatencyClass::error && b.duration_us
                                ? static_cast<double>(bytes) / static_cast<double>(b.duration_us)
                                : 0.0;  // bytes/us == MB/s
        o << i << ',' << first << ',' << first + count - 1 << ',' << count << ',' << bytes << ',' << b.duration_us
          << ',' << fmt("%.2f", rate) << ',' << latency_class_name(b.cls) << ','
          << (b.errc ? errc_name(static_cast<Errc>(b.errc)) : std::string_view{}) << ','
          << static_cast<int>(b.retries) << ',' << b.time_ms << '\n';
    }
    return o.str();
}

// ------------------------------------------------------------------ text

std::string scan_to_text(const ScanSnapshot& s, const Privacy& p) {
    const auto& c = s.config;
    const auto& st = s.stats;
    std::ostringstream o;
    auto row = [&](const char* k, const std::string& v) {
        char key[28];
        std::snprintf(key, sizeof key, "%-24s", k);
        o << "  " << key << v << "\n";
    };
    o << "platter surface scan report\n===========================\n\nDevice\n";
    row("Path", s.device.path);
    row("Model", s.device.model.empty() ? "-" : s.device.model);
    const auto serial = p.include_serials ? s.device.serial : mask_serial(s.device.serial);
    row("Serial", serial.empty() ? "-" : serial);
    row("Firmware", s.device.firmware.empty() ? "-" : s.device.firmware);
    row("Capacity", format_capacity(s.device.capacity_bytes));
    row("Sector size", std::to_string(s.layout.sector_size) + " bytes");

    o << "\nConfiguration\n";
    row("Mode", std::string(scan_mode_name(c.mode)));
    row("LBA range", std::to_string(s.layout.start_lba) + " - " + std::to_string(s.layout.end_lba));
    row("Block size", std::to_string(c.block_bytes) + " bytes (" + std::to_string(s.layout.lbas_per_block) + " LBAs)");
    row("Blocks", std::to_string(s.layout.block_count));
    row("Order", std::string(scan_order_name(c.order)) +
                     (c.order == ScanOrder::sampled ? fmt(" (%.1f %%)", c.sample_percent) : ""));
    row("Retries", std::to_string(c.retries));
    row("I/O", c.direct_io ? "direct (OS cache bypassed)" : "buffered (results may reflect the OS cache)");
    row("Thresholds", ms(c.thresholds.us[0]) + " / " + ms(c.thresholds.us[1]) + " / " + ms(c.thresholds.us[2]) +
                          " / " + ms(c.thresholds.us[3]));

    o << "\nResult\n";
    row("State", std::string(operation_state_name(s.progress.state)));
    if (!s.progress.message.empty()) row("Message", s.progress.message);
    row("Elapsed", fmt("%.1f s", seconds(s.progress.elapsed)));
    row("Tested", format_capacity(st.tested_bytes) + " of " + format_capacity(st.total_bytes));
    row("Requests", std::to_string(st.requests) + " (" + std::to_string(st.ok) + " ok, " +
                        std::to_string(st.failed) + " failed, " + std::to_string(st.slow) + " slow)");
    row("Retries used", std::to_string(st.retries));
    row("Average throughput", mbps(st.average_bytes_per_sec));
    row("Latency min / mean", ms(st.min_us) + " / " + ms(static_cast<std::uint64_t>(st.mean_us)));
    row("Latency median", ms(st.median_us));
    row("Latency p95 / p99", ms(st.p95_us) + " / " + ms(st.p99_us));
    row("Latency max", ms(st.max_us));

    o << "\nLatency distribution\n";
    const char* labels[] = {"untested", "< t1 (fast)", "< t2", "< t3", "< t4 (slow)", ">= t4 (very slow)", "I/O error"};
    for (int i = 0; i < kLatencyClassCount; ++i) row(labels[i], std::to_string(st.per_class[static_cast<std::size_t>(i)]));

    std::size_t shown = 0;
    std::ostringstream errs;
    for (std::size_t i = 0; i < s.blocks.size(); ++i) {
        const auto& b = s.blocks[i];
        if (b.cls != LatencyClass::error) continue;
        if (shown++ < 100) {
            const auto first = s.layout.first_lba(i);
            errs << "  LBA " << first << " - " << first + s.layout.lba_count(i) - 1 << "  "
                 << errc_name(static_cast<Errc>(b.errc)) << "  (retries " << static_cast<int>(b.retries) << ")\n";
        }
    }
    if (shown) {
        o << "\nUnreadable ranges";
        if (shown > 100) o << " (first 100 of " << shown << ")";
        o << "\n" << errs.str();
    }
    if (!s.warnings.empty()) {
        o << "\nWarnings\n";
        for (const auto& w : s.warnings) o << "  ! " << w << "\n";
    }
    o << "\nNote: latency classes describe how long the device took to answer, not the\n"
         "physical health of the sectors. Slow responses can come from caching, other\n"
         "I/O, power management or the connection. Unreadable ranges are reported as the\n"
         "device returned them.\n";
    return o.str();
}

}  // namespace platter::reports
