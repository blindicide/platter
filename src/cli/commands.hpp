// SPDX-License-Identifier: TBD
// Command entry points implemented outside main.cpp.
#pragma once

#include <string>

#include "common.hpp"

namespace platter::cli {

// cmd_erase.cpp
int cmd_erase_run(const Options& o, const std::string& target);
int cmd_erase_methods(const Options& o);
int cmd_erase_history(const Options& o);
int cmd_erase_show(const Options& o, const std::string& id_text);

}  // namespace platter::cli
