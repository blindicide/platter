// SPDX-License-Identifier: TBD
#include "platter/reports/erase_report.hpp"

#include <cstdio>
#include <sstream>

#include "platter/core/sha256.hpp"

using nlohmann::json;

namespace platter::reports {
namespace {

using namespace platter::erase;
constexpr int kSchema = 1;

constexpr const char* kStatement =
    "This report records what platter wrote and read back. Overwriting cannot reach sectors the device has remapped "
    "or over-provisioned (bad-sector reallocation, flash wear levelling, SMR zones). It is not a cryptographic "
    "proof of erasure and is not signed.";

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

std::string hex(const std::vector<std::uint8_t>& b) {
    std::string s;
    char t[4];
    for (auto x : b) {
        std::snprintf(t, sizeof t, "%02X", x);
        s += t;
    }
    return s;
}

std::vector<std::uint8_t> unhex(const std::string& s) {
    std::vector<std::uint8_t> v;
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) v.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
    return v;
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

std::string body_digest(json j) {
    j.erase("report_sha256");
    return Sha256::hex_of(j.dump());
}

}  // namespace

std::string erase_to_json(const EraseResult& r, const Privacy& p) {
    json passes = json::array();
    for (const auto& ps : r.passes) {
        json e = {{"description", ps.description},
                  {"bytes_written", ps.bytes_written},
                  {"unwritable_sectors", ps.unwritable_sectors},
                  {"verified", ps.verified},
                  {"verify_bytes", ps.verify_bytes},
                  {"mismatched_sectors", ps.mismatched_sectors},
                  {"unreadable_sectors", ps.unreadable_sectors},
                  {"duration_ms", ps.duration_ms},
                  {"kind", static_cast<int>(ps.kind)}};
        if (ps.first_bad_offset) e["first_bad_offset"] = *ps.first_bad_offset;
        if (ps.reads_back_zero) e["reads_back_zero"] = *ps.reads_back_zero;
        passes.push_back(std::move(e));
    }
    json j = envelope("erase-report");
    j["erase_schema"] = kSchema;
    j["device"] = to_json(r.device, p);
    j["config"] = {{"method", std::string(method_name(r.config.method))},
                   {"verify", std::string(verify_mode_name(r.config.verify))},
                   {"block_bytes", r.config.block_bytes},
                   {"random_passes", r.config.random_passes},
                   {"pattern", hex(r.config.pattern)},
                   {"max_errors", r.config.max_errors}};
    j["target"] = {{"ranges", ranges_json(r.ranges)}, {"affected_bytes", r.affected_bytes},
                   {"whole_device", r.whole_device}};
    j["result"] = {{"state", std::string(operation_state_name(r.state))},
                   {"verdict", std::string(verdict_name(r.verdict))},
                   {"failure", r.failure},
                   {"started_utc", r.started_utc},
                   {"finished_utc", r.finished_utc},
                   {"duration_ms", r.duration_ms},
                   {"direct_io", r.direct_io}};
    j["passes"] = passes;
    j["unwritable"] = ranges_json(r.unwritable);
    j["bad_verify"] = ranges_json(r.bad_verify);
    j["warnings"] = r.warnings;
    j["statement"] = kStatement;
    j["report_sha256"] = body_digest(j);
    return j.dump();
}

bool erase_digest_ok(const std::string& text) {
    try {
        const auto j = json::parse(text);
        return j.contains("report_sha256") && j.at("report_sha256").get<std::string>() == body_digest(j);
    } catch (...) {
        return false;
    }
}

Result<EraseResult> erase_from_json(const std::string& text) {
    try {
        const auto j = json::parse(text);
        if (j.value("kind", "") != "erase-report") return Error{Errc::corrupt_data, "not an erase report"};
        if (j.value("erase_schema", 0) != kSchema) return Error{Errc::unsupported, "unsupported erase report schema"};
        EraseResult r;
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
        r.device.physical_sector_size = d.value("physical_sector_size", 512u);
        const auto bus = d.value("bus", "unknown");
        for (auto b : {BusType::unknown, BusType::ata, BusType::scsi, BusType::nvme, BusType::usb, BusType::mmc,
                       BusType::virtio, BusType::loop, BusType::raid, BusType::image})
            if (bus_type_name(b) == bus) r.device.bus = b;

        const auto& c = j.at("config");
        auto m = parse_method(c.at("method").get<std::string>());
        auto v = parse_verify_mode(c.at("verify").get<std::string>());
        if (!m || !v) return Error{Errc::corrupt_data, "unknown method or verify mode"};
        r.config.method = *m;
        r.config.verify = *v;
        r.config.block_bytes = c.at("block_bytes").get<std::uint32_t>();
        r.config.random_passes = c.value("random_passes", 1u);
        r.config.pattern = unhex(c.value("pattern", ""));
        r.config.max_errors = c.value("max_errors", std::uint64_t{1024});

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

        for (const auto& e : j.at("passes")) {
            PassResult p;
            p.description = e.at("description").get<std::string>();
            p.kind = static_cast<Pass::Kind>(e.value("kind", 0));
            p.bytes_written = e.at("bytes_written").get<std::uint64_t>();
            p.unwritable_sectors = e.value("unwritable_sectors", std::uint64_t{0});
            p.verified = e.value("verified", false);
            p.verify_bytes = e.value("verify_bytes", std::uint64_t{0});
            p.mismatched_sectors = e.value("mismatched_sectors", std::uint64_t{0});
            p.unreadable_sectors = e.value("unreadable_sectors", std::uint64_t{0});
            p.duration_ms = e.value("duration_ms", std::uint64_t{0});
            if (e.contains("first_bad_offset")) p.first_bad_offset = e.at("first_bad_offset").get<std::uint64_t>();
            if (e.contains("reads_back_zero")) p.reads_back_zero = e.at("reads_back_zero").get<bool>();
            r.passes.push_back(std::move(p));
        }
        r.unwritable = ranges_from(j.at("unwritable"));
        r.bad_verify = ranges_from(j.at("bad_verify"));
        r.warnings = j.value("warnings", std::vector<std::string>{});
        return r;
    } catch (const std::exception& e) {
        return Error{Errc::corrupt_data, std::string("invalid erase report: ") + e.what()};
    }
}

std::string erase_to_text(const EraseResult& r, const Privacy& p) {
    std::ostringstream o;
    const auto& d = r.device;
    o << "platter erase report\n=====================\n\n";
    o << "Result:    " << verdict_name(r.verdict) << "  (operation " << operation_state_name(r.state) << ")\n";
    if (!r.failure.empty()) o << "Failure:   " << r.failure << "\n";
    o << "Device:    " << (d.model.empty() ? "(unknown model)" : d.model) << ", " << format_capacity(d.capacity_bytes)
      << ", " << bus_type_name(d.bus) << ", " << d.logical_sector_size << "/" << d.physical_sector_size
      << " B sectors\n";
    o << "Path:      " << d.path << "\n";
    if (p.include_serials) o << "Serial:    " << d.serial << "\n";
    else o << "Serial:    (omitted)\n";
    if (r.whole_device) {
        o << "Range:     whole device (" << d.total_lbas() << " sectors)\n";
    } else {
        o << "Range:    ";
        for (const auto& rg : r.ranges) o << " LBA " << rg.first << ".." << rg.last();
        o << "  (" << format_capacity(r.affected_bytes) << ")\n";
    }
    o << "Method:    " << method_label(r.config.method) << "; verification: " << verify_mode_name(r.config.verify) << "\n";
    o << "I/O:       " << (r.direct_io ? "direct (cache bypassed)" : "buffered (OS cache in use)") << ", "
      << r.config.block_bytes / 1024 << " KiB requests\n";
    o << "Started:   " << r.started_utc << "\nFinished:  " << r.finished_utc << "\nDuration:  " << r.duration_ms / 1000
      << " s\n\n";

    o << "Pass  Action                        Written     Unwritable  Verify\n";
    int n = 0;
    for (const auto& ps : r.passes) {
        char line[200];
        std::string ver = "-";
        if (ps.verified) {
            if (ps.reads_back_zero) ver = *ps.reads_back_zero ? "reads back zeros" : "NOT zeros";
            else if (ps.mismatched_sectors || ps.unreadable_sectors)
                ver = std::to_string(ps.mismatched_sectors) + " mismatched, " + std::to_string(ps.unreadable_sectors) + " unreadable";
            else ver = "OK";
        }
        std::snprintf(line, sizeof line, "%4d  %-28s  %-10s  %-10llu  %s\n", ++n, ps.description.c_str(),
                      format_capacity(ps.bytes_written).substr(0, format_capacity(ps.bytes_written).find(" (")).c_str(),
                      static_cast<unsigned long long>(ps.unwritable_sectors), ver.c_str());
        o << line;
    }
    if (!r.unwritable.empty()) {
        o << "\nSectors that could not be written (first ranges, LBA first+count):\n";
        for (const auto& rg : r.unwritable) o << "  " << rg.first << " +" << rg.count << "\n";
    }
    if (!r.bad_verify.empty()) {
        o << "\nSectors that did not verify (first ranges, LBA first+count):\n";
        for (const auto& rg : r.bad_verify) o << "  " << rg.first << " +" << rg.count << "\n";
    }
    if (!r.warnings.empty()) {
        o << "\nNotes:\n";
        for (const auto& w : r.warnings) o << "  - " << w << "\n";
    }
    o << "\n" << kStatement << "\n";
    return o.str();
}

}  // namespace platter::reports
