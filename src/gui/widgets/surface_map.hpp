// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <cstdint>
#include <vector>

namespace platter::gui {

// Victoria-style grid of latency-classified cells (spec §6.3). One cell
// covers `blocksPerCell()` consecutive scan blocks and shows the WORST class
// among them (errors dominate). Only visible rows are painted, so a 1 TB
// disk at 1 MiB blocks (~1M cells) stays cheap to render.
class SurfaceMap final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit SurfaceMap(QWidget* parent = nullptr);

    static QColor classColor(int cls);

    // Replace the per-block classes (1 byte per block, LatencyClass values).
    void setClasses(std::vector<std::uint8_t> classes);
    void clear();

    // 0 = automatic (fit everything in view if possible), else blocks per cell.
    void setZoom(int blocks_per_cell);
    int zoom() const noexcept { return zoom_; }
    std::uint64_t blocksPerCell() const noexcept { return per_cell_; }

    // Selected cell as an inclusive block range; first > last when nothing selected.
    std::uint64_t selectedFirst() const noexcept { return sel_first_; }
    std::uint64_t selectedLast() const noexcept { return sel_last_; }
    void clearSelection();

signals:
    void cellSelected(quint64 first_block, quint64 last_block);
    void zoomChanged(int blocks_per_cell);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    void relayout();
    int columns() const;
    std::uint64_t cellCount() const;
    int worstInCell(std::uint64_t cell) const;
    void selectCell(std::uint64_t cell);

    static constexpr int kCell = 9;  // pixels, plus 1 px gap
    std::vector<std::uint8_t> classes_;
    int zoom_ = 0;
    std::uint64_t per_cell_ = 1;
    std::uint64_t sel_first_ = 1, sel_last_ = 0;
    std::uint64_t sel_cell_ = UINT64_MAX;
};

}  // namespace platter::gui
