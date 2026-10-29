// SPDX-License-Identifier: LicenseRef-Proprietary
// Command entry points implemented outside main.cpp.
#pragma once

#include <string>
#include <vector>

#include "common.hpp"

namespace platter::cli {

// cmd_erase.cpp
int cmd_erase_run(const Options& o, const std::string& target);
int cmd_erase_methods(const Options& o);
int cmd_erase_history(const Options& o);
int cmd_erase_show(const Options& o, const std::string& id_text);

// cmd_partitions.cpp
int cmd_partitions_types(const Options& o);
int cmd_partitions_init(const Options& o, const std::string& target);
int cmd_partitions_add(const Options& o, const std::string& target);
int cmd_partitions_delete(const Options& o, const std::string& target, const std::vector<std::string>& rest);
int cmd_partitions_set(const Options& o, const std::string& target, const std::vector<std::string>& rest);
int cmd_partitions_resize(const Options& o, const std::string& target, const std::vector<std::string>& rest);

// cmd_format.cpp
int cmd_format(const Options& o, const std::string& target);

// cmd_imaging.cpp
int cmd_image_create(const Options& o, const std::string& device, const std::string& file);
int cmd_image_restore(const Options& o, const std::string& file, const std::string& device);
int cmd_image_verify(const Options& o, const std::string& file);
int cmd_image_history(const Options& o);
int cmd_image_show(const Options& o, const std::string& id_text);
int cmd_clone(const Options& o, const std::string& source, const std::string& target);

// cmd_history.cpp
int cmd_history_list(const Options& o, const std::string* device);
int cmd_history_show(const Options& o, const std::string& id_text);
int cmd_history_delete(const Options& o, const std::string& id_text);

// cmd_qualify.cpp
int cmd_qualify_run(const Options& o, const std::string& target);
int cmd_qualify_history(const Options& o);
int cmd_qualify_show(const Options& o, const std::string& id_text);

// cmd_inspect.cpp
int cmd_hex(const Options& o, const std::string& target);
int cmd_find(const Options& o, const std::string& target);

}  // namespace platter::cli
