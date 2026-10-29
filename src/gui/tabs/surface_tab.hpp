// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QWidget>
#include <memory>
#include <optional>

#include "../mainwindow/device_entry.hpp"
#include "../widgets/log_model.hpp"
#include "platter/surface/surface_scan.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;

namespace platter::storage {
class HistoryStore;
}

namespace platter::gui {

class PartitionPickButton;

class SurfaceMap;

// SURFACE tab (spec §6): configure, run, pause, stop a READ surface scan,
// watch the latency map fill in, inspect any cell, export, and reopen saved
// scans. The tab owns the running SurfaceScan; it polls it on a timer and
// never receives per-sector callbacks.
class SurfaceTab final : public QWidget {
    Q_OBJECT
public:
    explicit SurfaceTab(QWidget* parent = nullptr);
    ~SurfaceTab() override;

    void setDevice(const DeviceEntry* e);
    void setHistory(storage::HistoryStore* store, bool record) {
        history_ = store;
        record_ = record;
    }
    bool isRunning() const;
    void stopAndWait();

    // Development / screenshot helpers.
    bool openScanFile(const QString& path);
    void startScan();

signals:
    void logEvent(platter::gui::Severity s, const QString& message, const QString& device);

private slots:
    void onStart();
    void onPauseResume();
    void onStop();
    void onOpenScan();
    void onExport();
    void poll();
    void onCellSelected(quint64 first, quint64 last);
    void resetDefaults();

private:
    QWidget* buildSettings();
    QWidget* buildMapPanel();
    QWidget* buildInfoPanel();
    std::optional<surface::ScanConfig> readConfig(QString& error) const;
    void setSettingsEnabled(bool on);
    void updateButtons();
    void showStats(const surface::ScanStats& s, const OperationProgress& p);
    void showSelection();
    void finishScan();
    surface::BlockResult blockAt(std::uint64_t i) const;
    const surface::ScanLayout* currentLayout() const;

    // Target
    QString target_path_;
    QString target_label_;
    bool target_is_image_ = false;
    std::uint32_t target_sector_ = 512;
    std::uint64_t target_lbas_ = 0;

    storage::HistoryStore* history_ = nullptr;
    bool record_ = true;

    // State
    std::unique_ptr<surface::SurfaceScan> scan_;
    std::optional<surface::ScanSnapshot> view_;  // finished or loaded scan
    QString view_source_;
    QTimer* timer_;
    OperationState last_state_ = OperationState::created;

    // Settings widgets
    QComboBox* mode_;
    QLineEdit* pattern_;
    QLineEdit* start_lba_;
    PartitionPickButton* pick_ = nullptr;
    QLineEdit* end_lba_;
    QComboBox* block_;
    QComboBox* order_;
    QDoubleSpinBox* sample_;
    QSpinBox* retries_;
    QComboBox* on_error_;
    QSpinBox* max_errors_;
    QCheckBox* direct_;
    QSpinBox* thr_[4];
    QPushButton* defaults_;

    // Controls
    QPushButton* start_;
    QPushButton* pause_;
    QPushButton* stop_;
    QPushButton* open_;
    QPushButton* export_;
    QLabel* status_;
    QComboBox* zoom_;

    // Map + info
    SurfaceMap* map_;
    QProgressBar* progress_;
    QLabel* legend_[surface::kLatencyClassCount];
    QLabel* st_tested_;
    QLabel* st_requests_;
    QLabel* st_failed_;
    QLabel* st_slow_;
    QLabel* st_retries_;
    QLabel* st_lat_min_;
    QLabel* st_lat_avg_;
    QLabel* st_lat_med_;
    QLabel* st_lat_p95_;
    QLabel* st_lat_p99_;
    QLabel* st_lat_max_;
    QLabel* st_rate_cur_;
    QLabel* st_rate_avg_;
    QLabel* st_elapsed_;
    QLabel* st_eta_;
    QLabel* st_lba_;
    QLabel* sel_blocks_;
    QLabel* sel_lbas_;
    QLabel* sel_size_;
    QLabel* sel_dur_;
    QLabel* sel_rate_;
    QLabel* sel_err_;
    QLabel* sel_retries_;
    QLabel* sel_time_;
};

}  // namespace platter::gui
