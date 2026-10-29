// SPDX-License-Identifier: LicenseRef-Proprietary
//
// JSON serialization shared by the CLI (--json) and GUI exports, so both
// produce identical reports (spec §16.3).
#pragma once

#include <nlohmann/json.hpp>

#include "platter/core/device.hpp"
#include "platter/partitions/partition_table.hpp"
#include "platter/smart/smart.hpp"

namespace platter::reports {

struct Privacy {
    // Serial numbers are omitted unless explicitly requested (spec §4.2).
    bool include_serials = false;
};

nlohmann::json to_json(const DeviceInfo& d, const Privacy& p = {});
nlohmann::json to_json(const partitions::PartitionTable& t);
nlohmann::json to_json(const smart::Report& r, const Privacy& p = {});
nlohmann::json to_json(const smart::Assessment& a);

// Envelope added to every top-level report: tool name/version, schema version
// and a UTC timestamp.
nlohmann::json envelope(const char* kind);

// "WD-WCC6Y1234567" -> "WD-W***********" (keeps a short prefix for
// disambiguation on screen); empty stays empty.
std::string mask_serial(const std::string& serial);

// The device's stable id with any embedded serial masked unless `p` allows it.
std::string redact_stable_id(const DeviceInfo& d, const Privacy& p);

}  // namespace platter::reports
