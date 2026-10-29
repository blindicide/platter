// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QWidget>
#include <memory>
#include <optional>

#include "../mainwindow/device_entry.hpp"
#include "../widgets/log_model.hpp"
#include "platter/erase/erase.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;

namespace platter::storage {
class HistoryStore;
}

namespace platter::gui {

class PartitionPickButton;

// ERASE tab (spec §9): overwrite erasure with verification. Starting always
// goes through confirm_and_authorize(): preview, typed confirmation, identity
// re-check, exclusive lease.
class EraseTab final : public QWidget {
    Q_OBJECT
public:
    explicit EraseTab(QWidget* parent = nullptr);
    ~EraseTab() override;

    void setDevice(const DeviceEntry* e);
    void setHistory(storage::HistoryStore* store, bool record);
    void setShowSerials(bool on) { show_serial_ = on; }
    bool isRunning() const;
    void stopAndWait();

signals:
    void operationFinished();
    void logEvent(platter::gui::Severity s, const QString& message, const QString& device);

private slots:
    void onStart();
    void onStop();
    void onSaveReport();
    void onMethodChanged();
    void poll();

private:
    std::optional<erase::EraseConfig> readConfig(QString& error) const;
    void finish();
    void setSettingsEnabled(bool on);
    void updateButtons();

    QString target_path_;
    QString range_identity_;
    QString target_label_;
    bool target_is_image_ = false;
    std::uint32_t target_sector_ = 512;
    std::uint64_t target_lbas_ = 0;
    bool have_target_ = false;
    bool show_serial_ = false;

    storage::HistoryStore* history_ = nullptr;
    bool record_ = true;

    std::unique_ptr<erase::Erase> op_;
    std::optional<erase::EraseResult> last_;
    QTimer* timer_;

    QComboBox* method_;
    QSpinBox* passes_;
    QLineEdit* pattern_;
    QComboBox* verify_;
    QLineEdit* start_lba_;
    PartitionPickButton* pick_ = nullptr;
    QLineEdit* end_lba_;

    QPushButton* start_;
    QPushButton* stop_;
    QPushButton* save_;
    QProgressBar* progress_;
    QLabel* status_;
    QPlainTextEdit* report_;
};

}  // namespace platter::gui
