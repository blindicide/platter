// SPDX-License-Identifier: TBD
#pragma once

#include <string>
#include <vector>

#include "platter/core/device.hpp"
#include "platter/core/error.hpp"

namespace platter {

// Lists whole-disk block devices visible to the current user. Read-only: it
// never opens a device for reading data. On Windows it opens
// \\.\PhysicalDriveN with zero access rights (metadata queries only).
Result<std::vector<DeviceInfo>> enumerate_devices();

// Describes a single device by path (/dev/sda, \\.\PhysicalDrive1).
Result<DeviceInfo> describe_device(const std::string& path);

}  // namespace platter
