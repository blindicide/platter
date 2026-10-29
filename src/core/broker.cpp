// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/core/broker.hpp"

#include <mutex>

#include "platter/platform/direct_open.hpp"

namespace platter {
namespace {
std::mutex g_mutex;
std::shared_ptr<IDeviceBroker> g_broker;
}  // namespace

void set_device_broker(std::shared_ptr<IDeviceBroker> broker) {
    std::lock_guard lock(g_mutex);
    g_broker = std::move(broker);
}

std::shared_ptr<IDeviceBroker> device_broker() {
    std::lock_guard lock(g_mutex);
    return g_broker;
}

Result<BlockDevicePtr> open_device(const std::string& path, const OpenOptions& opts) {
    if (auto b = device_broker()) return b->open(path, opts);
    return open_device_direct(path, opts);
}

}  // namespace platter
