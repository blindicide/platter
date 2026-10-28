// SPDX-License-Identifier: TBD
#include "surface_map.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>

namespace platter::gui {

namespace {
constexpr int kPitch = 10;  // cell + gap
}

SurfaceMap::SurfaceMap(QWidget* parent) : QAbstractScrollArea(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setAutoFillBackground(true);
    QPalette p = viewport()->palette();
    p.setColor(QPalette::Base, Qt::white);
    viewport()->setPalette(p);
    setMinimumSize(200, 120);
    verticalScrollBar()->setSingleStep(kPitch);
}

QColor SurfaceMap::classColor(int cls) {
    switch (cls) {
        case 1: return {0x00, 0xB0, 0x00};  // < t1   green
        case 2: return {0x90, 0xE0, 0x80};  // < t2   light green
        case 3: return {0xF0, 0xE0, 0x00};  // < t3   yellow
        case 4: return {0xFF, 0x90, 0x00};  // < t4   orange
        case 5: return {0xE0, 0x00, 0x00};  // >= t4  red
        case 6: return {0x70, 0x00, 0x00};  // error  dark red
        default: return {0xC8, 0xC8, 0xC8}; // untested grey
    }
}

void SurfaceMap::setClasses(std::vector<std::uint8_t> classes) {
    const bool size_changed = classes.size() != classes_.size();
    classes_ = std::move(classes);
    if (size_changed) relayout();
    viewport()->update();
}

void SurfaceMap::clear() {
    classes_.clear();
    clearSelection();
    relayout();
    viewport()->update();
}

void SurfaceMap::clearSelection() {
    sel_first_ = 1;
    sel_last_ = 0;
    sel_cell_ = UINT64_MAX;
}

void SurfaceMap::setZoom(int blocks_per_cell) {
    zoom_ = std::max(0, blocks_per_cell);
    clearSelection();
    relayout();
    viewport()->update();
}

int SurfaceMap::columns() const { return std::max(1, (viewport()->width() - 2) / kPitch); }

std::uint64_t SurfaceMap::cellCount() const {
    return classes_.empty() ? 0 : (classes_.size() + per_cell_ - 1) / per_cell_;
}

void SurfaceMap::relayout() {
    const std::uint64_t n = classes_.size();
    if (zoom_ > 0) {
        per_cell_ = static_cast<std::uint64_t>(zoom_);
    } else {
        // Automatic: the smallest power of two that fits the whole map in view.
        const auto rows_visible = static_cast<std::uint64_t>(std::max(1, (viewport()->height() - 2) / kPitch));
        const auto capacity = static_cast<std::uint64_t>(columns()) * rows_visible;
        per_cell_ = 1;
        while (n > 0 && (n + per_cell_ - 1) / per_cell_ > capacity && per_cell_ < (1ull << 40)) per_cell_ *= 2;
    }
    const auto rows = (cellCount() + columns() - 1) / static_cast<std::uint64_t>(columns());
    const auto content = static_cast<int>(std::min<std::uint64_t>(rows * kPitch + 2, INT32_MAX / 2));
    verticalScrollBar()->setRange(0, std::max(0, content - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    emit zoomChanged(static_cast<int>(std::min<std::uint64_t>(per_cell_, INT32_MAX)));
}

void SurfaceMap::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    relayout();
}

int SurfaceMap::worstInCell(std::uint64_t cell) const {
    const std::uint64_t first = cell * per_cell_;
    const std::uint64_t last = std::min<std::uint64_t>(classes_.size(), first + per_cell_);
    int worst = 0;
    for (std::uint64_t i = first; i < last; ++i) {
        const int c = classes_[static_cast<std::size_t>(i)];
        if (c > worst) worst = c;
        if (worst == 6) break;
    }
    return worst;
}

void SurfaceMap::paintEvent(QPaintEvent*) {
    QPainter g(viewport());
    g.fillRect(viewport()->rect(), Qt::white);
    if (classes_.empty()) {
        g.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        g.drawText(viewport()->rect(), Qt::AlignCenter, tr("No scan data"));
        return;
    }
    const int cols = columns();
    const int top = verticalScrollBar()->value();
    const std::uint64_t first_row = static_cast<std::uint64_t>(top / kPitch);
    const std::uint64_t last_row = static_cast<std::uint64_t>((top + viewport()->height()) / kPitch);
    const std::uint64_t cells = cellCount();
    for (std::uint64_t r = first_row; r <= last_row; ++r) {
        for (int c = 0; c < cols; ++c) {
            const std::uint64_t cell = r * static_cast<std::uint64_t>(cols) + static_cast<std::uint64_t>(c);
            if (cell >= cells) return;
            const QRect rc(1 + c * kPitch, 1 + static_cast<int>(r * kPitch) - top, kCell, kCell);
            g.fillRect(rc, classColor(worstInCell(cell)));
            if (cell == sel_cell_) {
                g.setPen(QPen(Qt::black, 1));
                g.drawRect(rc.adjusted(-1, -1, 0, 0));
                g.setPen(QPen(Qt::white, 1));
                g.drawRect(rc.adjusted(0, 0, -1, -1));
            }
        }
    }
}

void SurfaceMap::selectCell(std::uint64_t cell) {
    if (cell >= cellCount()) return;
    sel_cell_ = cell;
    sel_first_ = cell * per_cell_;
    sel_last_ = std::min<std::uint64_t>(classes_.size(), sel_first_ + per_cell_) - 1;
    viewport()->update();
    emit cellSelected(sel_first_, sel_last_);
}

void SurfaceMap::mousePressEvent(QMouseEvent* e) {
    const QPoint p = e->position().toPoint();
    const int c = (p.x() - 1) / kPitch;
    if (c < 0 || c >= columns() || p.x() < 1) return;
    const auto r = static_cast<std::uint64_t>((p.y() - 1 + verticalScrollBar()->value()) / kPitch);
    selectCell(r * static_cast<std::uint64_t>(columns()) + static_cast<std::uint64_t>(c));
}

void SurfaceMap::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        // Ctrl+wheel: zoom in (fewer blocks per cell) / out.
        std::uint64_t z = per_cell_;
        if (e->angleDelta().y() > 0 && z > 1) z /= 2;
        else if (e->angleDelta().y() < 0 && z < (1ull << 30)) z *= 2;
        setZoom(static_cast<int>(z));
        e->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(e);
}

void SurfaceMap::keyPressEvent(QKeyEvent* e) {
    if (sel_cell_ == UINT64_MAX) {
        if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Down) selectCell(0);
        else QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    const auto cols = static_cast<std::uint64_t>(columns());
    switch (e->key()) {
        case Qt::Key_Right: selectCell(sel_cell_ + 1); break;
        case Qt::Key_Left: if (sel_cell_ > 0) selectCell(sel_cell_ - 1); break;
        case Qt::Key_Down: selectCell(sel_cell_ + cols); break;
        case Qt::Key_Up: if (sel_cell_ >= cols) selectCell(sel_cell_ - cols); break;
        default: QAbstractScrollArea::keyPressEvent(e); return;
    }
    // Keep the selection visible.
    const int y = static_cast<int>(sel_cell_ / cols) * kPitch;
    auto* vs = verticalScrollBar();
    if (y < vs->value()) vs->setValue(y);
    else if (y + kPitch > vs->value() + viewport()->height()) vs->setValue(y + kPitch - viewport()->height());
}

}  // namespace platter::gui
