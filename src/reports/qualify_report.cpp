// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/reports/qualify_report.hpp"

#include <cstdio>

#include "platter/core/sha256.hpp"

using nlohmann::json;

namespace platter::reports {
namespace {

using namespace platter::qualify;
constexpr int kSchema = 1;

json ranges_json(const std::vector<LbaRange>& v) {
    json a = json::array();
    for (const auto& r : v) a.push_back({r.first, r.count});
    return a;
}

std::vector<LbaRange> ranges_from(const json& j) {
    std::vector<LbaRange> v;
    for (const auto& e : j) v.push_back(LbaRange{e.at(0).get<std::uint64_t>(), e.at(1).get<std::uint64_t>()});
    return v;
}

DeviceInfo device_from(const json& d) {
    DeviceInfo i;
    i.path = d.value("path", "");
    i.name = d.value("name", "");
    i.vendor = d.value("vendor", "");
    i.model = d.value("model", "");
    i.firmware = d.value("firmware", "");
    i.stable_id = d.value("stable_id", "");
    i.serial = d.value("serial", "");
    i.capacity_bytes = d.value("capacity_bytes", std::uint64_t{0});
    i.logical_sector_size = d.value("logical_sector_size", 512u);
    i.physical_sector_size = d.value("physical_sector_size", 512u);
    const auto bus = d.value("bus", "unknown");
    for (auto b : {BusType::unknown, BusType::ata, BusType::scsi, BusType::nvme, BusType::usb, BusType::mmc,
                   BusType::virtio, BusType::loop, BusType::raid, BusType::image})
        if (bus_type_name(b) == bus) i.bus = b;
    return i;
}

OperationState state_from(const std::string& s) {
    for (int i = 0; i <= static_cast<int>(OperationState::cancelled); ++i)
        if (operation_state_name(static_cast<OperationState>(i)) == s) return static_cast<OperationState>(i);
    return OperationState::failed;
}

Verdict verdict_from(const std::string& s) {
    for (int i = 0; i <= static_cast<int>(Verdict::incomplete); ++i)
        if (verdict_name(static_cast<Verdict>(i)) == s) return static_cast<Verdict>(i);
    return Verdict::incomplete;
}

StageKind stage_kind_from(const std::string& s) {
    for (auto k : {StageKind::write, StageKind::verify, StageKind::read, StageKind::seek})
        if (stage_kind_name(k) == s) return k;
    return StageKind::write;
}

template <typename T>
json opt(const std::optional<T>& v) {
    return v ? json(*v) : json(nullptr);
}

json smart_json(const SmartSnapshot& s) {
    json j = {{"valid", s.valid}};
    j["overall_passed"] = opt(s.overall_passed);
    j["reallocated_sectors"] = opt(s.reallocated_sectors);
    j["reported_uncorrectable"] = opt(s.reported_uncorrectable);
    j["pending_sectors"] = opt(s.pending_sectors);
    j["offline_uncorrectable"] = opt(s.offline_uncorrectable);
    j["udma_crc_errors"] = opt(s.udma_crc_errors);
    j["nvme_media_errors"] = opt(s.nvme_media_errors);
    j["nvme_critical_warning"] = opt(s.nvme_critical_warning);
    j["nvme_percentage_used"] = opt(s.nvme_percentage_used);
    j["temperature_c"] = opt(s.temperature_c);
    j["power_on_hours"] = opt(s.power_on_hours);
    return j;
}

template <typename T>
std::optional<T> get_opt(const json& j, const char* k) {
    if (!j.contains(k) || j.at(k).is_null()) return std::nullopt;
    return j.at(k).get<T>();
}

SmartSnapshot smart_from(const json& j) {
    SmartSnapshot s;
    s.valid = j.value("valid", false);
    s.overall_passed = get_opt<bool>(j, "overall_passed");
    s.reallocated_sectors = get_opt<std::uint64_t>(j, "reallocated_sectors");
    s.reported_uncorrectable = get_opt<std::uint64_t>(j, "reported_uncorrectable");
    s.pending_sectors = get_opt<std::uint64_t>(j, "pending_sectors");
    s.offline_uncorrectable = get_opt<std::uint64_t>(j, "offline_uncorrectable");
    s.udma_crc_errors = get_opt<std::uint64_t>(j, "udma_crc_errors");
    s.nvme_media_errors = get_opt<std::uint64_t>(j, "nvme_media_errors");
    s.nvme_critical_warning = get_opt<std::uint32_t>(j, "nvme_critical_warning");
    s.nvme_percentage_used = get_opt<std::uint64_t>(j, "nvme_percentage_used");
    s.temperature_c = get_opt<int>(j, "temperature_c");
    s.power_on_hours = get_opt<std::uint64_t>(j, "power_on_hours");
    return s;
}

std::string body_digest(json j) {
    j.erase("report_sha256");
    return Sha256::hex_of(j.dump());
}

}  // namespace

std::string qualify_to_json(const QualifyResult& r, const Privacy& p) {
    json stages = json::array();
    for (const auto& s : r.stages) {
        json e = {{"kind", std::string(stage_kind_name(s.kind))}, {"pass", s.pass}, {"name", s.name},
                  {"completed", s.completed}, {"bytes", s.bytes}, {"blocks", s.blocks},
                  {"io_errors", s.io_errors}, {"mismatches", s.mismatches}, {"slow_blocks", s.slow_blocks},
                  {"min_mbps", s.min_mbps}, {"avg_mbps", s.avg_mbps}, {"max_mbps", s.max_mbps},
                  {"head_mbps", s.head_mbps}, {"tail_mbps", s.tail_mbps},
                  {"median_us", s.median_us}, {"p95_us", s.p95_us}, {"p99_us", s.p99_us}, {"max_us", s.max_us},
                  {"duration_ms", s.duration_ms}};
        e["first_bad_offset"] = opt(s.first_bad_offset);
        stages.push_back(std::move(e));
    }
    json findings = json::array();
    for (const auto& f : r.findings) findings.push_back({{"severity", std::string(severity_name(f.severity))}, {"text", f.text}});

    json j = envelope("qualification-report");
    j["qualify_schema"] = kSchema;
    j["device"] = to_json(r.device, p);
    const auto& c = r.config;
    j["config"] = {{"write_passes", c.write_passes}, {"verify_each_pass", c.verify_each_pass}, {"final_read", c.final_read},
                   {"seek_samples", c.seek_samples}, {"block_bytes", c.block_bytes}, {"max_errors", c.max_errors},
                   {"slow_block_ms", c.slow_block_ms}, {"seek_slow_ms", c.seek_slow_ms},
                   {"slow_fraction_warn", c.slow_fraction_warn}, {"slow_fraction_fail", c.slow_fraction_fail},
                   {"tail_head_warn", c.tail_head_warn}, {"seed", c.seed}};
    j["target"] = {{"ranges", ranges_json(r.ranges)}, {"affected_bytes", r.affected_bytes}, {"whole_device", r.whole_device}};
    j["result"] = {{"state", std::string(operation_state_name(r.state))}, {"verdict", std::string(verdict_name(r.verdict))},
                   {"failure", r.failure}, {"started_utc", r.started_utc}, {"finished_utc", r.finished_utc},
                   {"duration_ms", r.duration_ms}, {"direct_io", r.direct_io}};
    j["stages"] = stages;
    j["findings"] = findings;
    j["bad_ranges"] = ranges_json(r.bad_ranges);
    j["smart_before"] = smart_json(r.smart_before);
    j["smart_after"] = smart_json(r.smart_after);
    j["warnings"] = r.warnings;
    j["statement"] =
        "This report records what platter observed during one run. It does not predict failure, and a passing run "
        "is not a guarantee. It is not signed.";
    j["report_sha256"] = body_digest(j);
    return j.dump();
}

bool qualify_digest_ok(const std::string& text) {
    try {
        const auto j = json::parse(text);
        return j.contains("report_sha256") && j.at("report_sha256").get<std::string>() == body_digest(j);
    } catch (...) {
        return false;
    }
}

Result<QualifyResult> qualify_from_json(const std::string& text) {
    try {
        const auto j = json::parse(text);
        if (j.value("kind", "") != "qualification-report") return Error{Errc::corrupt_data, "not a qualification report"};
        if (j.value("qualify_schema", 0) != kSchema) return Error{Errc::unsupported, "unsupported qualification report schema"};
        QualifyResult r;
        r.device = device_from(j.at("device"));
        const auto& c = j.at("config");
        r.config.write_passes = c.at("write_passes").get<std::uint32_t>();
        r.config.verify_each_pass = c.at("verify_each_pass").get<bool>();
        r.config.final_read = c.at("final_read").get<bool>();
        r.config.seek_samples = c.at("seek_samples").get<std::uint32_t>();
        r.config.block_bytes = c.at("block_bytes").get<std::uint32_t>();
        r.config.max_errors = c.value("max_errors", std::uint64_t{1024});
        r.config.slow_block_ms = c.value("slow_block_ms", 200u);
        r.config.seek_slow_ms = c.value("seek_slow_ms", 100u);
        r.config.slow_fraction_warn = c.value("slow_fraction_warn", 0.001);
        r.config.slow_fraction_fail = c.value("slow_fraction_fail", 0.02);
        r.config.tail_head_warn = c.value("tail_head_warn", 0.25);
        r.config.seed = c.value("seed", std::uint64_t{0x5EED});

        const auto& t = j.at("target");
        r.ranges = ranges_from(t.at("ranges"));
        r.affected_bytes = t.at("affected_bytes").get<std::uint64_t>();
        r.whole_device = t.value("whole_device", false);

        const auto& res = j.at("result");
        r.state = state_from(res.at("state").get<std::string>());
        r.verdict = verdict_from(res.at("verdict").get<std::string>());
        r.failure = res.value("failure", "");
        r.started_utc = res.value("started_utc", "");
        r.finished_utc = res.value("finished_utc", "");
        r.duration_ms = res.value("duration_ms", std::uint64_t{0});
        r.direct_io = res.value("direct_io", false);

        for (const auto& e : j.at("stages")) {
            StageResult s;
            s.kind = stage_kind_from(e.at("kind").get<std::string>());
            s.pass = e.value("pass", 0u);
            s.name = e.value("name", "");
            s.completed = e.value("completed", false);
            s.bytes = e.value("bytes", std::uint64_t{0});
            s.blocks = e.value("blocks", std::uint64_t{0});
            s.io_errors = e.value("io_errors", std::uint64_t{0});
            s.mismatches = e.value("mismatches", std::uint64_t{0});
            s.slow_blocks = e.value("slow_blocks", std::uint64_t{0});
            s.min_mbps = e.value("min_mbps", 0.0);
            s.avg_mbps = e.value("avg_mbps", 0.0);
            s.max_mbps = e.value("max_mbps", 0.0);
            s.head_mbps = e.value("head_mbps", 0.0);
            s.tail_mbps = e.value("tail_mbps", 0.0);
            s.median_us = e.value("median_us", std::uint64_t{0});
            s.p95_us = e.value("p95_us", std::uint64_t{0});
            s.p99_us = e.value("p99_us", std::uint64_t{0});
            s.max_us = e.value("max_us", std::uint64_t{0});
            s.duration_ms = e.value("duration_ms", std::uint64_t{0});
            s.first_bad_offset = get_opt<std::uint64_t>(e, "first_bad_offset");
            r.stages.push_back(std::move(s));
        }
        for (const auto& f : j.at("findings")) {
            Finding x;
            const auto sev = f.at("severity").get<std::string>();
            x.severity = sev == "failure" ? Finding::Severity::failure : (sev == "warning" ? Finding::Severity::warning : Finding::Severity::info);
            x.text = f.at("text").get<std::string>();
            r.findings.push_back(std::move(x));
        }
        r.bad_ranges = ranges_from(j.at("bad_ranges"));
        r.smart_before = smart_from(j.at("smart_before"));
        r.smart_after = smart_from(j.at("smart_after"));
        r.warnings = j.value("warnings", std::vector<std::string>{});
        return r;
    } catch (const std::exception& e) {
        return Error{Errc::corrupt_data, std::string("invalid qualification report: ") + e.what()};
    }
}

std::string qualify_headline(const QualifyResult& r) {
    char b[160];
    double avg = 0;
    int n = 0;
    for (const auto& s : r.stages)
        if (s.kind == StageKind::write && s.avg_mbps > 0) {
            avg += s.avg_mbps;
            ++n;
        }
    if (n) avg /= n;
    std::snprintf(b, sizeof b, "%s, %u pass%s%s", std::string(verdict_name(r.verdict)).c_str(), r.config.write_passes,
                  r.config.write_passes == 1 ? "" : "es", n ? (", " + std::string([&] {
                                                                   char t[32];
                                                                   std::snprintf(t, sizeof t, "%.1f MB/s write", avg);
                                                                   return std::string(t);
                                                               }())).c_str() : "");
    return b;
}

}  // namespace platter::reports
