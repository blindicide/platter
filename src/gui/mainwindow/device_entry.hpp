// SPDX-License-Identifier: TBD
#pragma once

#include <QString>
#include <optional>

#include "platter/core/device.hpp"
#include "platter/partitions/partition_table.hpp"
#include "platter/smart/smart.hpp"

namespace platter::gui {

// Everything the GUI knows about one listed device or image.
struct DeviceEntry {
    DeviceInfo info;
    bool is_image = false;
    std::uint32_t image_sector_size = 512;

    std::optional<smart::Report> smart;
    std::optional<smart::Assessment> health;
    QString smart_error;

    std::optional<partitions::PartitionTable> table;
    QString table_error;

    QString displayName() const;
};

}  // namespace platter::gui
