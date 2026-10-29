// SPDX-License-Identifier: TBD
#include "partition_bar.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFrame>
#include <algorithm>

#include "../i18n/i18n.hpp"
#include "platter/core/device.hpp"

namespace platter::gui {
namespace {

QColor color_for(const partitions::Partition& p) {
    const auto& t = p.type_name;
    if (t.find("EFI") != std::string::npos) return {0xE0, 0xC0, 0x40};
    if (t.find("swap") != std::string::npos) return {0xC0, 0x60, 0x60};
    if (t.find("NTFS") != std::string::npos || t.find("Microsoft basic") != std::string::npos) return {0x40, 0x70, 0xC0};
    if (t.find("FAT") != std::string::npos) return {0x40, 0xA0, 0x40};
    if (t.find("Linux") != std::string::npos) return {0x80, 0x80, 0x30};
    if (t.find("Microsoft reserved") != std::string::npos || t.find("Recovery") != std::string::npos)
        return {0x90, 0x90, 0xB0};
    return {0xA0, 0xA0, 0xA0};
}

QString short_size(std::uint64_t bytes) {
    auto s = format_capacity(bytes);
    auto p = s.find(" (");
    return i18n::units(QString::fromStdString(p == std::string::npos ? s : s.substr(0, p)));
}

}  // namespace

PartitionBar::PartitionBar(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize PartitionBar::sizeHint() const { return {400, fontMetrics().height() * 3 + 12}; }
QSize PartitionBar::minimumSizeHint() const { return {120, fontMetrics().height() * 3 + 12}; }

void PartitionBar::setTable(const std::optional<partitions::PartitionTable>& t) {
    table_ = t;
    layoutSegments();
    update();
}

void PartitionBar::layoutSegments() {
    segments_.clear();
    if (!table_ || table_->total_lbas == 0) return;
    const auto& t = *table_;

    std::vector<const partitions::Partition*> parts;
    for (const auto& p : t.partitions)
        if (!p.extended_container) parts.push_back(&p);
    std::sort(parts.begin(), parts.end(), [](auto* a, auto* b) { return a->first_lba < b->first_lba; });

    // Hide alignment slack (the usual 1 MiB before the first partition, the
    // backup GPT area) so it does not crowd out real partitions.
    const std::uint64_t sixteen_mib = (16ull << 20) / std::max<std::uint32_t>(t.sector_size, 1);
    const std::uint64_t min_gap = std::max<std::uint64_t>(t.total_lbas / 100, sixteen_mib);
    std::uint64_t cursor = 0;
    auto add_free = [&](std::uint64_t from, std::uint64_t to) {
        if (to > from && to - from >= min_gap)
            segments_.push_back({from, to - 1, 0, tr("Unallocated\n%1").arg(short_size((to - from) * t.sector_size)),
                                 Qt::white, false, {}});
    };
    for (auto* p : parts) {
        add_free(cursor, p->first_lba);
        segments_.push_back({p->first_lba, p->last_lba, static_cast<int>(p->number),
                             QStringLiteral("#%1 %2\n%3")
                                 .arg(p->number)
                                 .arg((p->name.empty() ? i18n::ct(p->type_name) : QString::fromStdString(p->name)))
                                 .arg(short_size(p->size_bytes)),
                             color_for(*p), p->logical, {}});
        cursor = std::max(cursor, p->last_lba + 1);
    }
    add_free(cursor, t.total_lbas);
}

void PartitionBar::paintEvent(QPaintEvent*) {
    QPainter g(this);
    QRect frame = rect().adjusted(0, 0, -1, -1);
    // Sunken classic frame.
    g.setPen(palette().color(QPalette::Mid));
    g.drawLine(frame.topLeft(), frame.topRight());
    g.drawLine(frame.topLeft(), frame.bottomLeft());
    g.setPen(palette().color(QPalette::Light));
    g.drawLine(frame.bottomLeft(), frame.bottomRight());
    g.drawLine(frame.topRight(), frame.bottomRight());
    const QRect inner = rect().adjusted(2, 2, -2, -2);
    g.fillRect(inner, palette().color(QPalette::Base));

    if (!table_) {
        g.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        g.drawText(inner, Qt::AlignCenter, tr("Partition table not read"));
        return;
    }
    if (segments_.empty()) {
        g.setPen(palette().color(QPalette::Text));
        g.drawText(inner, Qt::AlignCenter, tr("Unpartitioned"));
        return;
    }

    // Proportional widths with a minimum so small partitions remain clickable.
    const int min_w = 24;
    const double total = static_cast<double>(table_->total_lbas);
    std::vector<int> widths;
    int fixed = 0;
    for (const auto& s : segments_) {
        const int w = std::max(min_w, static_cast<int>(inner.width() * (static_cast<double>(s.last - s.first + 1) / total)));
        widths.push_back(w);
        fixed += w;
    }
    // Shrink the widest segments if minimums overflowed the bar.
    while (fixed > inner.width()) {
        auto it = std::max_element(widths.begin(), widths.end());
        if (*it <= min_w) break;
        --*it;
        --fixed;
    }
    int x = inner.left();
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        auto& s = segments_[i];
        const int w = (i + 1 == segments_.size()) ? inner.right() - x + 1 : widths[i];
        s.rect = QRect(x, inner.top(), w, inner.height());
        QRect r = s.rect.adjusted(1, 1, -1, -1);
        if (s.number == 0) {
            g.fillRect(r, QBrush(QColor(0xC0, 0xC0, 0xC0), Qt::BDiagPattern));
        } else {
            g.fillRect(r, Qt::white);
            g.fillRect(QRect(r.left(), r.top(), r.width(), 5), s.color);  // type stripe
            if (s.logical) {
                g.setPen(QPen(QColor(0x40, 0x40, 0x80), 1, Qt::DashLine));
                g.drawRect(r.adjusted(0, 0, -1, -1));
            }
        }
        g.setPen(Qt::black);
        g.drawRect(r.adjusted(0, 0, -1, -1));
        g.setPen(palette().color(QPalette::Text));
        g.drawText(r.adjusted(3, 6, -2, -1), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                   fontMetrics().elidedText(s.label.section(QLatin1Char('\n'), 0, 0), Qt::ElideRight, r.width() - 5) +
                       QLatin1Char('\n') +
                       fontMetrics().elidedText(s.label.section(QLatin1Char('\n'), 1), Qt::ElideRight, r.width() - 5));
        x += w;
    }
}

void PartitionBar::mousePressEvent(QMouseEvent* e) {
    for (const auto& s : segments_)
        if (s.number && s.rect.contains(e->position().toPoint())) emit partitionClicked(s.number);
}

}  // namespace platter::gui
