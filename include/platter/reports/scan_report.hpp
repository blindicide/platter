// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Surface-scan export / import (spec §6.5). JSON is the canonical format and
// can be loaded back for comparison; CSV has one row per tested block; the
// text report is for humans.
#pragma once

#include <string>

#include "platter/core/error.hpp"
#include "platter/reports/json.hpp"
#include "platter/surface/surface_scan.hpp"

namespace platter::reports {

std::string scan_to_json(const surface::ScanSnapshot& s, const Privacy& p = {});
std::string scan_to_csv(const surface::ScanSnapshot& s);
std::string scan_to_text(const surface::ScanSnapshot& s, const Privacy& p = {});

// Loads a JSON export. Validates structure, geometry and per-block indices;
// anything inconsistent is rejected with Errc::corrupt_data rather than
// partially loaded. The returned snapshot's `progress` holds the recorded
// final state, not a live operation.
Result<surface::ScanSnapshot> scan_from_json(const std::string& text);

}  // namespace platter::reports
