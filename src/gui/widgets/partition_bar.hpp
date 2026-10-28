// SPDX-License-Identifier: TBD
#pragma once

#include <QWidget>
#include <optional>

#include "platter/partitions/partition_table.hpp"

namespace platter::gui {

// GParted-style horizontal layout of a disk: partitions to scale (with a
// minimum width so tiny ones stay visible), unallocated gaps hatched.
class PartitionBar final : public QWidget {
    Q_OBJECT
public:
    explicit PartitionBar(QWidget* parent = nullptr);
    void setTable(const std::optional<partitions::PartitionTable>& t);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void partitionClicked(int number);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    struct Segment {
        std::uint64_t first = 0, last = 0;
        int number = 0;          // 0 = free space
        QString label;
        QColor color;
        bool logical = false;
        QRect rect;
    };
    void layoutSegments();

    std::optional<partitions::PartitionTable> table_;
    std::vector<Segment> segments_;
};

}  // namespace platter::gui
