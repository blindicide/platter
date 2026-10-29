// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QString>
#include <vector>

namespace platter::gui {

enum class Severity { debug, info, warning, error, critical };
QString severity_name(Severity s);

struct LogEvent {
    QDateTime time;
    Severity severity = Severity::info;
    QString device;
    QString message;
};

// Activity log shared by the bottom pane and the LOG tab (spec §14.1). Bounded
// so a long session cannot grow memory without limit.
class LogModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Time, Level, Device, Message, ColumnCount };

    explicit LogModel(QObject* parent = nullptr);
    // Translated level name for display (severity_name() stays English for files).
    static QString severity_label(Severity s);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    void add(Severity s, const QString& message, const QString& device = {});
    const std::vector<LogEvent>& events() const { return events_; }

signals:
    void eventAdded(const platter::gui::LogEvent& e);

private:
    static constexpr std::size_t kMaxEvents = 20000;
    std::vector<LogEvent> events_;
};

}  // namespace platter::gui
