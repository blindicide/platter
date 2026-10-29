// SPDX-License-Identifier: TBD
#include "log_model.hpp"

#include <QBrush>
#include <QColor>

namespace platter::gui {

QString severity_name(Severity s) {
    switch (s) {
        case Severity::debug: return QStringLiteral("DEBUG");
        case Severity::info: return QStringLiteral("INFO");
        case Severity::warning: return QStringLiteral("WARNING");
        case Severity::error: return QStringLiteral("ERROR");
        case Severity::critical: return QStringLiteral("CRITICAL");
    }
    return QStringLiteral("?");
}

QString LogModel::severity_label(Severity s) {
    switch (s) {
        case Severity::debug: return tr("DEBUG");
        case Severity::info: return tr("INFO");
        case Severity::warning: return tr("WARNING");
        case Severity::error: return tr("ERROR");
        case Severity::critical: return tr("CRITICAL");
    }
    return QStringLiteral("?");
}

LogModel::LogModel(QObject* parent) : QAbstractTableModel(parent) {}

int LogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(events_.size());
}

int LogModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant LogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(events_.size())) return {};
    const auto& e = events_[static_cast<std::size_t>(index.row())];
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
        switch (index.column()) {
            case Time: return e.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            case Level: return severity_label(e.severity);
            case Device: return e.device;
            case Message: return e.message;
        }
    }
    if (role == Qt::ForegroundRole) {
        switch (e.severity) {
            case Severity::warning: return QBrush(QColor(0x80, 0x60, 0x00));
            case Severity::error:
            case Severity::critical: return QBrush(QColor(0xC0, 0x00, 0x00));
            case Severity::debug: return QBrush(QColor(0x80, 0x80, 0x80));
            default: break;
        }
    }
    return {};
}

QVariant LogModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case Time: return tr("Time");
        case Level: return tr("Severity");
        case Device: return tr("Device");
        case Message: return tr("Description");
    }
    return {};
}

void LogModel::add(Severity s, const QString& message, const QString& device) {
    if (events_.size() >= kMaxEvents) {
        beginRemoveRows({}, 0, 999);
        events_.erase(events_.begin(), events_.begin() + 1000);
        endRemoveRows();
    }
    const int row = static_cast<int>(events_.size());
    beginInsertRows({}, row, row);
    events_.push_back({QDateTime::currentDateTime(), s, device, message});
    endInsertRows();
    emit eventAdded(events_.back());
}

}  // namespace platter::gui
