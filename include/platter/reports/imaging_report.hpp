// SPDX-License-Identifier: TBD
// Imaging / clone / verify reports. JSON carries a SHA-256 digest of its own
// body (like erase reports); the text form is imaging::result_to_text().
#pragma once

#include <string>

#include "platter/core/error.hpp"
#include "platter/imaging/imaging.hpp"
#include "platter/reports/json.hpp"

namespace platter::reports {

std::string imaging_to_json(const imaging::ImagingResult& r, const Privacy& p = {});
Result<imaging::ImagingResult> imaging_from_json(const std::string& text);
bool imaging_digest_ok(const std::string& json_text);
// One line for lists: "OK, 14.20 GB, zstd".
std::string imaging_headline(const imaging::ImagingResult& r);

}  // namespace platter::reports
