// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QDialog>
#include <vector>

#include "platter/storage/history.hpp"

class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace platter::gui {

// Browser over every saved session: benchmarks, scans, SMART snapshots,
// erase, imaging and qualification reports. Deleting removes the saved record
// only; nothing on a device is touched.
class HistoryDialog final : public QDialog {
    Q_OBJECT
public:
    // `device_key` pre-selects "this device" in the filter (empty = all).
    HistoryDialog(storage::HistoryStore* store, const QString& device_key, bool show_serial, QWidget* parent = nullptr);

private slots:
    void reload();
    void onSelectionChanged();
    void onSave();
    void onDelete();

private:
    const storage::SessionSummary* selected() const;

    storage::HistoryStore* store_;
    bool show_serial_;
    QString initial_device_key_;
    std::vector<storage::SessionSummary> rows_;

    QComboBox* kind_;
    QComboBox* device_;
    QTableWidget* table_;
    QPlainTextEdit* detail_;
    QPushButton* save_;
    QPushButton* delete_;
};

}  // namespace platter::gui
