// SPDX-License-Identifier: TBD
#pragma once

#include <QPushButton>
#include <QString>
#include <cstdint>
#include <vector>

class QLineEdit;

namespace platter::gui {

struct DeviceEntry;

// "Partition..." button for tabs with Start/End LBA fields: choosing a
// partition (or the whole device) fills both fields. It never reads the device:
// it only uses the partition table already loaded by the Partitions tab.
class PartitionPickButton final : public QPushButton {
    Q_OBJECT
public:
    // `default_start` is what the start field holds for "whole device" (the
    // benchmark tab keeps "0" there, the others leave it empty).
    PartitionPickButton(QLineEdit* start, QLineEdit* end, const QString& default_start, QWidget* parent = nullptr);

    // Copies the partitions of e->table (if any); call from the tab's setDevice().
    void setDevice(const DeviceEntry* e);

private:
    struct Choice {
        QString label;
        std::uint64_t first = 0, last = 0;
    };
    void choose();

    QLineEdit* start_;
    QLineEdit* end_;
    QString default_start_;
    bool have_table_ = false;
    bool sector_mismatch_ = false;
    std::vector<Choice> choices_;
};

}  // namespace platter::gui
