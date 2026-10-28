// SPDX-License-Identifier: TBD
#include <filesystem>

#include "platter/core/range.hpp"
#include "../file_device.hpp"

namespace platter::detail {

DeviceInfo image_device_info(const std::string& path, std::uint32_t sector_size, bool writable) {
    DeviceInfo info;
    info.path = path;
    info.name = std::filesystem::path(path).filename().string();
    info.model = "Raw disk image";
    info.bus = BusType::image;
    info.media = MediaKind::unknown;
    info.removable = Tri::no;
    info.read_only = writable ? Tri::no : Tri::yes;
    info.trim_supported = Tri::no;
    info.logical_sector_size = sector_size;
    info.physical_sector_size = sector_size;
    info.capabilities.set(Capability::read);
    info.capabilities.set(Capability::benchmark);
    info.capabilities.set(Capability::raw_sector_access);
    if (writable) {
        info.capabilities.set(Capability::write);
        info.capabilities.set(Capability::partition_edit);
    }
    return info;
}

Result<BlockDevicePtr> open_image_common(const std::string& path, const OpenOptions& opts,
                                         std::uint32_t sector_size) {
    if (sector_size != 512 && sector_size != 4096)
        return Error{Errc::invalid_argument, "image sector size must be 512 or 4096"};
    auto info = image_device_info(path, sector_size, opts.mode == OpenMode::read_write);
    return open_file_device(path, opts, std::move(info), /*is_image=*/true);
}

}  // namespace platter::detail

namespace platter {

Result<BlockDevicePtr> open_image(const std::string& path, const OpenOptions& opts,
                                  std::uint32_t sector_size) {
    return detail::open_image_common(path, opts, sector_size);
}

Status read_sectors(IBlockDevice& dev, std::uint64_t lba, std::uint64_t count, void* buffer,
                    std::size_t buffer_size) {
    const auto ss = dev.sector_size();
    auto off = checked_mul(lba, ss);
    auto len = checked_mul(count, ss);
    if (!off || !len) return Error{Errc::overflow, "sector request overflows"};
    if (*len > buffer_size) return Error{Errc::invalid_argument, "buffer too small for sectors"};
    auto r = dev.read(*off, buffer, static_cast<std::size_t>(*len));
    if (!r) return r.error();
    return {};
}

}  // namespace platter
