// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Runs an external tool WITHOUT a shell (no injection surface): argv is passed
// to execvp / CreateProcessW directly.
#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "platter/core/error.hpp"

namespace platter {

struct ProcessResult {
    int exit_code = -1;
    std::string out;   // stdout
    std::string err;   // stderr
};

// argv[0] is the program (path or name looked up in PATH). Output is capped at
// `max_output` bytes per stream; the process is killed after `timeout`.
Result<ProcessResult> run_process(const std::vector<std::string>& argv, std::chrono::milliseconds timeout,
                                  std::size_t max_output = 16 * 1024 * 1024);

// Finds an executable by name in PATH. Returns empty if not found.
std::string find_in_path(const std::string& name);

}  // namespace platter
