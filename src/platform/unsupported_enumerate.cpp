// SPDX-License-Identifier: LicenseRef-Proprietary
// Fallback for platforms without a device backend (e.g. macOS). Image files
// still work through open_image().
#include "platter/platform/enumerate.hpp"
#include "platter/platform/direct_open.hpp"
#include "platter/core/block_device.hpp"

namespace platter {

Result<std::vector<DeviceInfo>> enumerate_devices() { return std::vector<DeviceInfo>{}; }

Result<DeviceInfo> describe_device(const std::string&) {
    return Error{Errc::unsupported, "physical devices are not supported on this platform"};
}

Result<BlockDevicePtr> open_device_direct(const std::string&, const OpenOptions&) {
    return Error{Errc::unsupported, "physical devices are not supported on this platform"};
}

}  // namespace platter
