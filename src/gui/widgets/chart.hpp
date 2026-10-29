// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QPointF>
#include <QString>
#include <QWidget>
#include <optional>
#include <utility>
#include <vector>

namespace platter::gui {

// Minimal classic chart: white plot area in a sunken frame, grey grid, navy
// primary series and an optional grey dashed comparison series. Two modes:
// a line chart over a numeric X axis, or a bar chart over labelled
// categories. No animation, no anti-aliased gradients: it should look like a
// 2003 disk utility.
class Chart final : public QWidget {
    Q_OBJECT
public:
    explicit Chart(QWidget* parent = nullptr);

    struct Axis {
        QString label;
        // printf-style format for tick labels, e.g. "%.0f", "%.1f".
        QString tick_format = QStringLiteral("%.0f");
        // Fixed axis range for line charts (both set), e.g. 0..100 % position.
        std::optional<double> fixed_min = std::nullopt;
        std::optional<double> fixed_max = std::nullopt;
    };

    void setLine(const QString& title, Axis x, Axis y, std::vector<QPointF> main, std::vector<QPointF> compare = {},
                 const QString& compare_label = {});
    void setBars(const QString& title, Axis y, std::vector<std::pair<QString, double>> main,
                 std::vector<std::pair<QString, double>> compare = {}, const QString& compare_label = {});
    void clear(const QString& message);

    QSize sizeHint() const override { return {520, 280}; }
    QSize minimumSizeHint() const override { return {240, 160}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    enum class Mode { empty, line, bars };
    static double nice_ceiling(double v);
    Mode mode_ = Mode::empty;
    QString title_, message_, compare_label_;
    Axis x_, y_;
    std::vector<QPointF> line_, line_cmp_;
    std::vector<std::pair<QString, double>> bars_, bars_cmp_;
};

}  // namespace platter::gui
