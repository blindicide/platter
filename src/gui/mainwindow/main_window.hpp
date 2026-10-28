// SPDX-License-Identifier: TBD
#pragma once

#include <QMainWindow>
#include <memory>
#include <vector>

#include "device_entry.hpp"

class QComboBox;
class QLabel;
class QSplitter;
class QTabWidget;
class QTableView;
class QAction;

namespace platter::storage {
class HistoryStore;
}

namespace platter::gui {

class LogModel;
class InfoTab;
class SmartTab;
class PartitionsTab;
class SurfaceTab;
class BenchmarkTab;
class LogTab;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Adds an image file to the device list (read-only) and selects it.
    void openImage(const QString& path, std::uint32_t sector_size = 512);
    void selectTab(const QString& name);
    LogModel* log() const { return log_; }

    // Synchronous variants used by --screenshot (no threads, deterministic).
    void readPartitionsNow();
    void selectIndex(int index);
    void querySmartNow() { querySmart(); }
    SurfaceTab* surfaceTab() const { return surface_tab_; }
    BenchmarkTab* benchmarkTab() const { return bench_tab_; }

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void refreshDevices();
    void onDeviceSelected(int index);
    void querySmart();
    void readPartitions();
    void openImageDialog();
    void saveReport();
    void about();
    void licenseInfo();

private:
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void buildCentral();
    void buildStatusBar();
    void openHistory();
    void restoreSettings();
    void showCurrent();
    void updateSummary();
    DeviceEntry* current();
    int indexOfPath(const std::string& path) const;
    QAction* planned(const QString& text, const QString& milestone);

    std::vector<DeviceEntry> devices_;
    int current_ = -1;
    bool show_serials_ = false;

    LogModel* log_;
    QComboBox* selector_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QSplitter* vsplit_ = nullptr;
    QTableView* activity_ = nullptr;
    InfoTab* info_tab_ = nullptr;
    SmartTab* smart_tab_ = nullptr;
    PartitionsTab* part_tab_ = nullptr;
    SurfaceTab* surface_tab_ = nullptr;
    BenchmarkTab* bench_tab_ = nullptr;
    std::unique_ptr<storage::HistoryStore> history_;
    bool record_history_ = true;
    LogTab* log_tab_ = nullptr;

    // Summary panel
    QLabel* sum_model_ = nullptr;
    QLabel* sum_capacity_ = nullptr;
    QLabel* sum_iface_ = nullptr;
    QLabel* sum_geometry_ = nullptr;
    QLabel* sum_firmware_ = nullptr;
    QLabel* sum_usage_ = nullptr;
    QLabel* sum_temp_ = nullptr;
    QLabel* sum_health_ = nullptr;

    // Status bar
    QLabel* st_device_ = nullptr;
    QLabel* st_mode_ = nullptr;
    QLabel* st_priv_ = nullptr;

    QAction* act_open_image_ = nullptr;
    QAction* act_save_report_ = nullptr;
    QAction* act_exit_ = nullptr;
    QAction* act_refresh_ = nullptr;
    QAction* act_identify_ = nullptr;
    QAction* act_smart_ = nullptr;
    QAction* act_caps_ = nullptr;
    QAction* act_partitions_ = nullptr;
    QAction* act_surface_ = nullptr;
    QAction* act_benchmark_ = nullptr;
    QAction* act_record_ = nullptr;
    QAction* act_show_serials_ = nullptr;
    QAction* act_about_ = nullptr;
    QAction* act_license_ = nullptr;

    bool busy_smart_ = false;
    bool busy_parts_ = false;
};

}  // namespace platter::gui
