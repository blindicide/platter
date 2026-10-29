// SPDX-License-Identifier: LicenseRef-Proprietary
// Benchmark export / import. JSON is canonical (also what the history store
// keeps); text is for humans and records every setting that affects the
// numbers (spec 7.4).
#pragma once

#include <string>

#include "platter/benchmark/benchmark.hpp"
#include "platter/core/error.hpp"
#include "platter/reports/json.hpp"

namespace platter::reports {

std::string bench_to_json(const bench::BenchResult& r, const Privacy& p = {});
Result<bench::BenchResult> bench_from_json(const std::string& text);
std::string bench_to_text(const bench::BenchResult& r, const Privacy& p = {});

}  // namespace platter::reports
