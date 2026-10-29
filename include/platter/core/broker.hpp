// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Optional indirection for opening physical devices. With no broker installed
// open_device() talks to the OS directly. The privileged helper
// (include/platter/helper/helper.hpp) installs one so that an unprivileged
// GUI/CLI can work on devices that only root may open.
#pragma once

#include <memory>
#include <string>

#include "platter/core/block_device.hpp"

namespace platter {

class IDeviceBroker {
public:
    virtual ~IDeviceBroker() = default;
    virtual Result<BlockDevicePtr> open(const std::string& path, const OpenOptions& opts) = 0;
    // One line for the UI: "platter-helper pid 1234 (root)".
    virtual std::string describe() const = 0;
};

// Process-wide. Install before starting any work; pass nullptr to remove.
void set_device_broker(std::shared_ptr<IDeviceBroker> broker);
std::shared_ptr<IDeviceBroker> device_broker();

}  // namespace platter
