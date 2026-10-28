// SPDX-License-Identifier: TBD
#include "chart.hpp"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace platter::gui {
namespace {

const QColor kNavy(0x0A, 0x24, 0x6A);
const QColor kCompare(0x80, 0x80, 0x80);
const QColor kGrid(0xE0, 0xE0, 0xE0);

QString tick(const QString& fmt, double v) {
    char b[48];
    std::snprintf(b, sizeof b, fmt.toLatin1().constData(), v);
    return QString::fromLatin1(b);
}

}  // namespace

Chart::Chart(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    clear(tr("No results yet"));
}

double Chart::nice_ceiling(double v) {
    if (v <= 0) return 1;
    const double mag = std::pow(10.0, std::floor(std::log10(v)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (v <= m * mag) return m * mag;
    return 10 * mag;
}

void Chart::setLine(const QString& title, Axis x, Axis y, std::vector<QPointF> main, std::vector<QPointF> compare,
                    const QString& compare_label) {
    mode_ = main.empty() && compare.empty() ? Mode::empty : Mode::line;
    title_ = title;
    x_ = std::move(x);
    y_ = std::move(y);
    line_ = std::move(main);
    line_cmp_ = std::move(compare);
    compare_label_ = compare_label;
    message_ = tr("No data");
    update();
}

void Chart::setBars(const QString& title, Axis y, std::vector<std::pair<QString, double>> main,
                    std::vector<std::pair<QString, double>> compare, const QString& compare_label) {
    mode_ = main.empty() ? Mode::empty : Mode::bars;
    title_ = title;
    y_ = std::move(y);
    bars_ = std::move(main);
    bars_cmp_ = std::move(compare);
    compare_label_ = compare_label;
    message_ = tr("No data");
    update();
}

void Chart::clear(const QString& message) {
    mode_ = Mode::empty;
    message_ = message;
    title_.clear();
    line_.clear();
    line_cmp_.clear();
    bars_.clear();
    bars_cmp_.clear();
    update();
}

void Chart::paintEvent(QPaintEvent*) {
    QPainter g(this);
    const QRect frame = rect().adjusted(0, 0, -1, -1);
    g.fillRect(rect(), palette().color(QPalette::Window));
    // Sunken classic frame around a white canvas.
    g.setPen(palette().color(QPalette::Mid));
    g.drawLine(frame.topLeft(), frame.topRight());
    g.drawLine(frame.topLeft(), frame.bottomLeft());
    g.setPen(palette().color(QPalette::Light));
    g.drawLine(frame.bottomLeft(), frame.bottomRight());
    g.drawLine(frame.topRight(), frame.bottomRight());
    const QRect canvas = rect().adjusted(2, 2, -2, -2);
    g.fillRect(canvas, Qt::white);

    const QFontMetrics fm = fontMetrics();
    if (mode_ == Mode::empty) {
        g.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        g.drawText(canvas, Qt::AlignCenter, message_);
        return;
    }

    // Value range.
    double ymax = 0, xmin = 0, xmax = 1;
    if (mode_ == Mode::line) {
        bool first = true;
        for (const auto* s : {&line_, &line_cmp_})
            for (const auto& p : *s) {
                ymax = std::max(ymax, p.y());
                xmin = first ? p.x() : std::min(xmin, p.x());
                xmax = first ? p.x() : std::max(xmax, p.x());
                first = false;
            }
        if (x_.fixed_min && x_.fixed_max) {
            xmin = *x_.fixed_min;
            xmax = *x_.fixed_max;
        }
        if (xmax <= xmin) xmax = xmin + 1;
    } else {
        for (const auto* s : {&bars_, &bars_cmp_})
            for (const auto& b : *s) ymax = std::max(ymax, b.second);
    }
    ymax = nice_ceiling(ymax * 1.05);

    // Layout.
    const int ytick_w = std::max(fm.horizontalAdvance(tick(y_.tick_format, ymax)), fm.horizontalAdvance(QStringLiteral("0")));
    const int left = canvas.left() + ytick_w + 10 + fm.height();
    const int top = canvas.top() + fm.height() + 8;
    const int right = canvas.right() - 10;
    const int bottom = canvas.bottom() - (fm.height() * 2 + 10);
    const QRect plot(QPoint(left, top), QPoint(right, bottom));
    if (plot.width() < 40 || plot.height() < 30) return;

    g.setPen(Qt::black);
    g.drawText(QRect(canvas.left(), canvas.top() + 2, canvas.width(), fm.height()), Qt::AlignHCenter, title_);

    // Y grid + ticks.
    const int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        const double v = ymax * i / ticks;
        const int y = plot.bottom() - static_cast<int>(std::lround(plot.height() * static_cast<double>(i) / ticks));
        g.setPen(kGrid);
        if (i > 0) g.drawLine(plot.left(), y, plot.right(), y);
        g.setPen(Qt::black);
        g.drawText(QRect(canvas.left() + fm.height() + 2, y - fm.height() / 2, ytick_w + 4, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, tick(y_.tick_format, v));
    }
    // Y axis label, rotated.
    g.save();
    g.translate(canvas.left() + fm.height() - 2, plot.center().y());
    g.rotate(-90);
    g.drawText(QRect(-plot.height() / 2, -fm.height(), plot.height(), fm.height()), Qt::AlignCenter, y_.label);
    g.restore();

    auto ymap = [&](double v) { return plot.bottom() - plot.height() * std::clamp(v / ymax, 0.0, 1.0); };

    if (mode_ == Mode::line) {
        auto xmap = [&](double v) { return plot.left() + plot.width() * (v - xmin) / (xmax - xmin); };
        for (int i = 0; i <= ticks; ++i) {
            const double v = xmin + (xmax - xmin) * i / ticks;
            const int x = static_cast<int>(std::lround(xmap(v)));
            g.setPen(kGrid);
            if (i > 0 && i < ticks) g.drawLine(x, plot.top(), x, plot.bottom());
            g.setPen(Qt::black);
            g.drawText(QRect(x - 40, plot.bottom() + 3, 80, fm.height()), Qt::AlignHCenter, tick(x_.tick_format, v));
        }
        g.drawText(QRect(plot.left(), plot.bottom() + fm.height() + 5, plot.width(), fm.height()), Qt::AlignHCenter,
                   x_.label);
        auto draw = [&](const std::vector<QPointF>& s, const QPen& pen) {
            if (s.empty()) return;
            g.setPen(pen);
            if (s.size() == 1) {
                g.drawEllipse(QPointF(xmap(s[0].x()), ymap(s[0].y())), 2, 2);
                return;
            }
            QPainterPath path(QPointF(xmap(s[0].x()), ymap(s[0].y())));
            for (std::size_t i = 1; i < s.size(); ++i) path.lineTo(QPointF(xmap(s[i].x()), ymap(s[i].y())));
            g.drawPath(path);
        };
        draw(line_cmp_, QPen(kCompare, 1, Qt::DashLine));
        draw(line_, QPen(kNavy, 2));
    } else {
        const std::size_t n = bars_.size();
        const double slot = static_cast<double>(plot.width()) / static_cast<double>(n);
        const bool cmp = !bars_cmp_.empty();
        for (std::size_t i = 0; i < n; ++i) {
            const int x0 = plot.left() + static_cast<int>(slot * static_cast<double>(i) + slot * 0.15);
            const int w = std::max(2, static_cast<int>(slot * 0.7));
            const int bw = cmp ? w / 2 : w;
            const int y = static_cast<int>(ymap(bars_[i].second));
            g.fillRect(QRect(x0, y, bw, plot.bottom() - y), kNavy);
            if (cmp) {
                const auto it = std::find_if(bars_cmp_.begin(), bars_cmp_.end(),
                                             [&](const auto& b) { return b.first == bars_[i].first; });
                if (it != bars_cmp_.end()) {
                    const int yc = static_cast<int>(ymap(it->second));
                    g.fillRect(QRect(x0 + bw, yc, w - bw, plot.bottom() - yc), kCompare);
                }
            }
            g.setPen(Qt::black);
            g.drawText(QRect(x0 - 20, plot.bottom() + 3, w + 40, fm.height()), Qt::AlignHCenter,
                       fm.elidedText(bars_[i].first, Qt::ElideRight, static_cast<int>(slot) + 10));
            g.drawText(QRect(x0 - 20, y - fm.height() - 1, w + 40, fm.height()), Qt::AlignHCenter,
                       tick(y_.tick_format, bars_[i].second));
        }
    }
    g.setPen(Qt::black);
    g.drawRect(plot);

    // Legend for comparisons: a small boxed key inside the plot's top-right corner.
    if (!compare_label_.isEmpty() && (!line_cmp_.empty() || !bars_cmp_.empty())) {
        const QString a_txt = tr("this run");
        const int w = 18 + fm.horizontalAdvance(a_txt) + 16 + 18 + fm.horizontalAdvance(compare_label_) + 8;
        const QRect box(plot.right() - w - 4, plot.top() + 4, w, fm.height() + 6);
        g.fillRect(box, Qt::white);
        g.setPen(Qt::black);
        g.drawRect(box);
        int lx = box.left() + 4;
        const int mid = box.top() + box.height() / 2;
        g.fillRect(QRect(lx, mid - 2, 14, 4), kNavy);
        g.drawText(lx + 18, box.top() + 3 + fm.ascent(), a_txt);
        lx += 18 + fm.horizontalAdvance(a_txt) + 16;
        g.fillRect(QRect(lx, mid - 2, 14, 4), kCompare);
        g.drawText(lx + 18, box.top() + 3 + fm.ascent(), compare_label_);
    }
}

}  // namespace platter::gui
