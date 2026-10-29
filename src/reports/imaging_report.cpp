// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/reports/imaging_report.hpp"

#include "platter/core/sha256.hpp"

using nlohmann::json;

namespace platter::reports {
namespace {

using namespace platter::imaging;
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

std::string body_digest(json j) {
    j.erase("report_sha256");
    return Sha256::hex_of(j.dump());
}

}  // namespace

std::string imaging_to_json(const ImagingResult& r, const Privacy& p) {
    json j = envelope("imaging-report");
    j["imaging_schema"] = kSchema;
    j["job"] = std::string(kind_name(r.kind));
    if (r.source) j["source"] = to_json(*r.source, p);
    if (r.target) j["target"] = to_json(*r.target, p);
    j["image"] = {{"path", r.image_path}, {"compression", std::string(compression_name(r.compression))},
                  {"file_bytes", r.image_file_bytes}};
    j["region"] = {{"sector_size", r.sector_size}, {"source_first_lba", r.source_first_lba},
                   {"target_first_lba", r.target_first_lba}, {"sectors", r.sectors}, {"bytes", r.bytes},
                   {"block_bytes", r.block_bytes}, {"recover", r.recover}};
    j["result"] = {{"state", std::string(operation_state_name(r.state))},
                   {"verdict", static_cast<int>(r.verdict)},
                   {"verdict_text", std::string(verdict_name(r.verdict))},
                   {"failure", r.failure},
                   {"started_utc", r.started_utc},
                   {"finished_utc", r.finished_utc},
                   {"duration_ms", r.duration_ms}};
    j["checksums"] = {{"sha256", r.sha256}, {"expected_sha256", r.expected_sha256}};
    if (r.sha_matches) j["checksums"]["matches"] = *r.sha_matches;
    if (r.readback_ok) j["readback_ok"] = *r.readback_ok;
    j["unreadable"] = ranges_json(r.unreadable);
    j["unreadable_sectors"] = r.unreadable_sectors;
    j["mismatches"] = ranges_json(r.mismatches);
    j["warnings"] = r.warnings;
    j["report_sha256"] = body_digest(j);
    return j.dump();
}

bool imaging_digest_ok(const std::string& text) {
    try {
        const auto j = json::parse(text);
        return j.contains("report_sha256") && j.at("report_sha256").get<std::string>() == body_digest(j);
    } catch (...) {
        return false;
    }
}

Result<ImagingResult> imaging_from_json(const std::string& text) {
    try {
        const auto j = json::parse(text);
        if (j.value("kind", "") != "imaging-report") return Error{Errc::corrupt_data, "not an imaging report"};
        if (j.value("imaging_schema", 0) != kSchema) return Error{Errc::unsupported, "unsupported imaging report schema"};
        ImagingResult r;
        const auto job = j.at("job").get<std::string>();
        bool known = false;
        for (auto k : {JobKind::create, JobKind::restore, JobKind::clone, JobKind::verify})
            if (kind_name(k) == job) {
                r.kind = k;
                known = true;
            }
        if (!known) return Error{Errc::corrupt_data, "unknown job kind"};
        if (j.contains("source")) r.source = device_from(j.at("source"));
        if (j.contains("target")) r.target = device_from(j.at("target"));
        const auto& im = j.at("image");
        r.image_path = im.value("path", "");
        r.compression = im.value("compression", "none") == "zstd" ? Compression::zstd : Compression::none;
        r.image_file_bytes = im.value("file_bytes", std::uint64_t{0});
        const auto& g = j.at("region");
        r.sector_size = g.value("sector_size", 512u);
        r.source_first_lba = g.value("source_first_lba", std::uint64_t{0});
        r.target_first_lba = g.value("target_first_lba", std::uint64_t{0});
        r.sectors = g.value("sectors", std::uint64_t{0});
        r.bytes = g.value("bytes", std::uint64_t{0});
        r.block_bytes = g.value("block_bytes", 0u);
        r.recover = g.value("recover", false);
        const auto& res = j.at("result");
        r.state = state_from(res.at("state").get<std::string>());
        const int v = res.value("verdict", 3);
        r.verdict = v >= 0 && v <= 3 ? static_cast<Verdict>(v) : Verdict::incomplete;
        r.failure = res.value("failure", "");
        r.started_utc = res.value("started_utc", "");
        r.finished_utc = res.value("finished_utc", "");
        r.duration_ms = res.value("duration_ms", std::uint64_t{0});
        const auto& c = j.at("checksums");
        r.sha256 = c.value("sha256", "");
        r.expected_sha256 = c.value("expected_sha256", "");
        if (c.contains("matches")) r.sha_matches = c.at("matches").get<bool>();
        if (j.contains("readback_ok")) r.readback_ok = j.at("readback_ok").get<bool>();
        r.unreadable = ranges_from(j.at("unreadable"));
        r.unreadable_sectors = j.value("unreadable_sectors", std::uint64_t{0});
        r.mismatches = ranges_from(j.at("mismatches"));
        r.warnings = j.value("warnings", std::vector<std::string>{});
        return r;
    } catch (const std::exception& e) {
        return Error{Errc::corrupt_data, std::string("invalid imaging report: ") + e.what()};
    }
}

std::string imaging_headline(const ImagingResult& r) {
    std::string s = std::string(verdict_name(r.verdict)) + ", " + format_capacity(r.bytes);
    if (r.compression == Compression::zstd) s += ", zstd";
    if (r.unreadable_sectors) s += ", " + std::to_string(r.unreadable_sectors) + " unreadable";
    return s;
}

}  // namespace platter::reports
