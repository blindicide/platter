// SPDX-License-Identifier: TBD
#include <filesystem>

#include "platter/platform/enumerate.hpp"
#include "platter/platform/direct_open.hpp"
#include "platter/platform/linux_sysfs.hpp"
#include "../file_device.hpp"

namespace platter {

Result<std::vector<DeviceInfo>> enumerate_devices() { return sysfs::enumerate(sysfs::Roots{}); }

Result<DeviceInfo> describe_device(const std::string& path) {
    // Resolve symlinks such as /dev/disk/by-id/wwn-... to the kernel name.
    std::error_code ec;
    auto canon = std::filesystem::canonical(path, ec);
    if (ec) return Error{Errc::not_found, "cannot resolve '" + path + "': " + ec.message()};
    if (canon.parent_path() != "/dev")
        return Error{Errc::invalid_argument, "'" + path + "' is not a /dev block device node"};
    return sysfs::describe(sysfs::Roots{}, canon.filename().string());
}

Result<BlockDevicePtr> open_device_direct(const std::string& path, const OpenOptions& opts) {
    auto info = describe_device(path);
    if (!info) return info.error();
    auto d = std::move(info).value();
    if (opts.mode == OpenMode::read_write && d.read_only == Tri::yes)
        return Error{Errc::read_only, d.path + " is read-only"};
    const auto dev_path = d.path;
    return detail::open_file_device(dev_path, opts, std::move(d), /*is_image=*/false);
}

}  // namespace platter
