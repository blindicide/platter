// SPDX-License-Identifier: TBD
#include "partition_pick_button.hpp"

#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

#include "../i18n/i18n.hpp"
#include "../mainwindow/device_entry.hpp"
#include "platter/core/device.hpp"

namespace platter::gui {

PartitionPickButton::PartitionPickButton(QLineEdit* start, QLineEdit* end, const QString& default_start, QWidget* parent)
    : QPushButton(tr("Partition..."), parent), start_(start), end_(end), default_start_(default_start) {
    setToolTip(tr("Fill in Start LBA and End LBA from a partition of the selected device. Uses the partition table "
                  "already read in the Partitions tab; nothing is read from the device here."));
    connect(this, &QPushButton::clicked, this, &PartitionPickButton::choose);
}

void PartitionPickButton::setDevice(const DeviceEntry* e) {
    choices_.clear();
    have_table_ = false;
    sector_mismatch_ = false;
    if (!e || !e->table) return;
    have_table_ = true;
    const std::uint32_t sector = e->is_image ? e->image_sector_size : e->info.logical_sector_size;
    if (e->table->sector_size != sector) {
        sector_mismatch_ = true;
        return;
    }
    for (const auto& p : e->table->partitions) {
        if (p.extended_container) continue;  // the container only holds the logical partitions
        Choice c;
        c.first = p.first_lba;
        c.last = p.last_lba;
        c.label = tr("%1: %2, %3 (LBA %4 - %5)")
                      .arg(p.number)
                      .arg(i18n::ct(p.type_name))
                      .arg(i18n::units(QString::fromStdString(format_capacity(p.size_bytes))))
                      .arg(p.first_lba)
                      .arg(p.last_lba);
        choices_.push_back(std::move(c));
    }
}

void PartitionPickButton::choose() {
    if (!have_table_) {
        QMessageBox::information(this, tr("Partition"),
                                 tr("The partition table has not been read yet. Open the Partitions tab and read it first; "
                                    "then choose again."));
        return;
    }
    if (sector_mismatch_) {
        QMessageBox::warning(this, tr("Partition"), tr("The partition table uses a different sector size than the device; enter the LBAs by hand."));
        return;
    }
    if (choices_.empty()) {
        QMessageBox::information(this, tr("Partition"), tr("The partition table has no partitions."));
        return;
    }
    QStringList items;
    items << tr("Whole device");
    for (const auto& c : choices_) items << c.label;
    bool ok = false;
    const QString picked = QInputDialog::getItem(this, tr("Partition"), tr("Use the LBA range of:"), items, 0, false, &ok);
    if (!ok) return;
    const int idx = static_cast<int>(items.indexOf(picked));
    if (idx <= 0) {
        start_->setText(default_start_);
        end_->clear();
        return;
    }
    const auto& c = choices_[static_cast<std::size_t>(idx - 1)];
    start_->setText(QString::number(c.first));
    end_->setText(QString::number(c.last));
}

}  // namespace platter::gui
