// SPDX-License-Identifier: TBD
//
// Internal: the concrete IBlockDevice used for both image files and (on
// Linux) block devices. Windows has its own implementation with the same
// factory signature.
#pragma once

#include "platter/core/block_device.hpp"

namespace platter::detail {

// Opens `path` and wraps it. `info` is a pre-populated description (from
// sysfs, SetupAPI, or image defaults); capacity and sector sizes in it are
// overwritten by what the open handle reports when the OS can tell us.
Result<BlockDevicePtr> open_file_device(const std::string& path, const OpenOptions& opts,
                                        DeviceInfo info, bool is_image);

// Fills the image-file defaults (bus = image, name = basename, capabilities).
DeviceInfo image_device_info(const std::string& path, std::uint32_t sector_size, bool writable);

}  // namespace platter::detail
