// SPDX-License-Identifier: LicenseRef-Proprietary
// Drive qualification reports. JSON carries a SHA-256 digest of its own body
// (like erase and imaging reports); the text form is qualify::result_to_text().
#pragma once

#include <string>

#include "platter/core/error.hpp"
#include "platter/qualify/qualify.hpp"
#include "platter/reports/json.hpp"

namespace platter::reports {

std::string qualify_to_json(const qualify::QualifyResult& r, const Privacy& p = {});
Result<qualify::QualifyResult> qualify_from_json(const std::string& text);
bool qualify_digest_ok(const std::string& json_text);
// One line for lists: "PASS, 2 passes, 118.4 MB/s".
std::string qualify_headline(const qualify::QualifyResult& r);

}  // namespace platter::reports
