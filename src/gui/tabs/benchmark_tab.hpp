// SPDX-License-Identifier: TBD
#pragma once

#include <QWidget>
#include <memory>
#include <optional>

#include "../mainwindow/device_entry.hpp"
#include "../widgets/log_model.hpp"
#include "platter/benchmark/benchmark.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStandardItemModel;
class QTableView;
class QTimer;

namespace platter::storage {
class HistoryStore;
}

namespace platter::gui {

class Chart;

// BENCHMARK tab (spec §7): configure and run read benchmarks, watch live
// throughput, inspect per-phase metrics and caveats, and browse / compare /
// delete saved sessions from the history database.
class BenchmarkTab final : public QWidget {
    Q_OBJECT
public:
    explicit BenchmarkTab(QWidget* parent = nullptr);
    ~BenchmarkTab() override;

    void setDevice(const DeviceEntry* e);
    // History store owned by the main window (may be null: history disabled).
    void setHistory(storage::HistoryStore* store, bool record);
    bool isRunning() const;
    void stopAndWait();

    // Development / screenshot helpers.
    void startBenchmark();
    void selectKind(int kind);
    void selectCompare(qlonglong id);

signals:
    void logEvent(platter::gui::Severity s, const QString& message, const QString& device);

private slots:
    void onStart();
    void onStop();
    void poll();
    void onKindChanged();
    void refreshChart();
    void refreshHistory();
    void onHistoryLoad();
    void onHistoryDelete();
    void onExport();

private:
    QWidget* buildSettings();
    QWidget* buildResults();
    std::optional<bench::BenchConfig> readConfig(QString& error) const;
    void setSettingsEnabled(bool on);
    void showResult(const bench::BenchResult& r);
    void finish();
    void updateButtons();

    // Target
    QString target_path_;
    QString target_label_;
    bool target_is_image_ = false;
    std::uint32_t target_sector_ = 512;
    std::string target_key_;

    storage::HistoryStore* history_ = nullptr;
    bool record_ = true;

    std::unique_ptr<bench::Benchmark> bench_;
    std::optional<bench::BenchResult> shown_;    // finished, or loaded from history
    std::optional<bench::BenchResult> compare_;  // overlay
    QTimer* timer_;

    // Settings
    QComboBox* kind_;
    QComboBox* block_;
    QSpinBox* qd_;
    QDoubleSpinBox* duration_;
    QComboBox* size_limit_;
    QSpinBox* points_;
    QComboBox* point_size_;
    QLineEdit* sizes_;
    QLineEdit* qds_;
    QLineEdit* start_lba_;
    QLineEdit* end_lba_;
    QCheckBox* direct_;

    // Controls / results
    QPushButton* start_;
    QPushButton* stop_;
    QPushButton* export_;
    QProgressBar* progress_;
    QLabel* status_;
    QLabel* headline_;
    QComboBox* chart_kind_;
    QComboBox* compare_with_;
    Chart* chart_;
    QStandardItemModel* phases_;
    QTableView* phases_view_;
    QListWidget* caveats_;
    QStandardItemModel* hist_model_;
    QTableView* hist_view_;
    QPushButton* hist_load_;
    QPushButton* hist_delete_;
    QLabel* hist_status_;
};

}  // namespace platter::gui
