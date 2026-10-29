// SPDX-License-Identifier: TBD
// Erase report export / import (spec §9.6). JSON is canonical and carries a
// SHA-256 digest of its own body so accidental edits are detectable; the text
// form is the human-readable record.
#pragma once

#include <string>

#include "platter/core/error.hpp"
#include "platter/erase/erase.hpp"
#include "platter/reports/json.hpp"

namespace platter::reports {

std::string erase_to_json(const erase::EraseResult& r, const Privacy& p = {});
Result<erase::EraseResult> erase_from_json(const std::string& text);
std::string erase_to_text(const erase::EraseResult& r, const Privacy& p = {});

// True when the "report_sha256" member matches the rest of the document.
bool erase_digest_ok(const std::string& json_text);

}  // namespace platter::reports
