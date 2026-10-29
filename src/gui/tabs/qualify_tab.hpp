// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QWidget>
#include <atomic>
#include <memory>
#include <optional>
#include <thread>

#include "../mainwindow/device_entry.hpp"
#include "../widgets/log_model.hpp"
#include "platter/qualify/qualify.hpp"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace platter::storage {
class HistoryStore;
}

namespace platter::gui {

// QUALIFY tab (spec §12): destructive burn-in with a verdict and the reasons
// behind it. Starting always goes through confirm_and_authorize().
class QualifyTab final : public QWidget {
    Q_OBJECT
public:
    explicit QualifyTab(QWidget* parent = nullptr);
    ~QualifyTab() override;

    void setDevice(const DeviceEntry* e);
    void setHistory(storage::HistoryStore* store, bool record);
    void setShowSerials(bool on) { show_serial_ = on; }
    bool isRunning() const;
    void stopAndWait();

signals:
    void logEvent(platter::gui::Severity s, const QString& message, const QString& device);

private slots:
    void onStart();
    void onStop();
    void onSaveReport();
    void poll();

private:
    struct SmartJob;

    void finishRun();
    void finishReport();
    void refreshStages();
    void updateButtons();
    void setSettingsEnabled(bool on);

    QString target_path_, target_label_;
    bool target_is_image_ = false;
    std::uint32_t target_sector_ = 512;
    std::uint64_t target_lbas_ = 0;
    bool have_target_ = false;
    bool show_serial_ = false;
    qualify::SmartSnapshot smart_before_;

    storage::HistoryStore* history_ = nullptr;
    bool record_ = true;

    std::unique_ptr<qualify::Qualification> op_;
    std::shared_ptr<SmartJob> smart_job_;
    std::optional<qualify::QualifyResult> last_;
    QTimer* timer_;

    QSpinBox* passes_;
    QCheckBox* verify_;
    QCheckBox* final_read_;
    QSpinBox* samples_;
    QSpinBox* slow_ms_;

    QPushButton* start_;
    QPushButton* stop_;
    QPushButton* save_;
    QProgressBar* progress_;
    QLabel* status_;
    QTableWidget* stages_;
    QPlainTextEdit* report_;
};

}  // namespace platter::gui
