// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <string>

#include "platter/core/block_device.hpp"

namespace platter {

// The platform backend, bypassing any installed device broker. Used by
// open_device() and by the privileged helper itself.
Result<BlockDevicePtr> open_device_direct(const std::string& path, const OpenOptions& opts = {});

}  // namespace platter
