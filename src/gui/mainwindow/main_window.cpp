// SPDX-License-Identifier: LicenseRef-Proprietary
#include "../i18n/i18n.hpp"
#include "main_window.hpp"

#include "../tabs/imaging_tab.hpp"
#include "../widgets/partition_editor.hpp"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QPointer>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QThread>
#include <QToolBar>

#include "../tabs/benchmark_tab.hpp"
#include "../tabs/erase_tab.hpp"
#include "../tabs/hex_tab.hpp"
#include "../widgets/checksum_dialog.hpp"
#include "../widgets/help_dialog.hpp"
#include "../widgets/history_dialog.hpp"
#include "../styles/classic_theme.hpp"
#include "../tabs/qualify_tab.hpp"
#include "../tabs/surface_tab.hpp"
#include "../tabs/tabs.hpp"
#include "../widgets/log_model.hpp"
#include "../widgets/table_tools.hpp"
#include "platter/core/block_device.hpp"
#include "platter/platform/enumerate.hpp"
#include "platter/platform/privileges.hpp"
#ifdef PLATTER_HAVE_HELPER
#include "platter/helper/helper.hpp"
#endif
#include "platter/reports/json.hpp"
#include "platter/storage/history.hpp"
#include "platter/version.hpp"

namespace platter::gui {
namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
QString qs(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<int>(s.size())); }

QLabel* value_label(QWidget* parent) {
    auto* l = new QLabel(QStringLiteral("-"), parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setMinimumWidth(60);
    return l;
}

// Every running worker thread, so the window can wait for them on shutdown.
std::vector<QPointer<QThread>>& workers() {
    static std::vector<QPointer<QThread>> w;
    return w;
}

template <typename Fn>
void run_worker(QObject* owner, Fn&& fn) {
    QThread* t = QThread::create(std::forward<Fn>(fn));
    QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
    workers().emplace_back(t);
    Q_UNUSED(owner);
    t->start();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), log_(new LogModel(this)) {
    setWindowTitle(QStringLiteral("platter %1 - Storage Operations Suite").arg(QString::fromLatin1(kVersion)));
    buildActions();
    buildMenus();
    buildToolBar();
    buildCentral();
    buildStatusBar();
    restoreSettings();

    log_->add(Severity::info, tr("platter %1 started (%2 privileges; destructive operations ask for typed confirmation)")
                                  .arg(QString::fromLatin1(kVersion), i18n::ct(privilege_label())));
    prefs_ = Preferences::load();
    openHistory();
    applyPreferences(prefs_, /*startup=*/true);
    refreshDevices();
}

MainWindow::~MainWindow() {
    // Tabs may still reference the store while stopping their workers.
    if (surface_tab_) surface_tab_->stopAndWait();
    if (bench_tab_) bench_tab_->stopAndWait();
    if (erase_tab_) erase_tab_->stopAndWait();
    if (imaging_tab_) imaging_tab_->stopAndWait();
    if (hex_tab_) hex_tab_->stopAndWait();
    if (qualify_tab_) qualify_tab_->stopAndWait();
    for (auto& t : workers())
        if (t) t->wait();
}

// ------------------------------------------------------------------ actions

QAction* MainWindow::planned(const QString& text, const QString& milestone) {
    auto* a = new QAction(text, this);
    a->setEnabled(false);
    a->setStatusTip(tr("Planned for %1").arg(milestone));
    a->setToolTip(tr("Planned for %1").arg(milestone));
    return a;
}

void MainWindow::buildActions() {
    auto* st = style();
    act_open_image_ = new QAction(st->standardIcon(QStyle::SP_DialogOpenButton), tr("&Open Disk Image..."), this);
    act_open_image_->setShortcut(QKeySequence::Open);
    act_open_image_->setStatusTip(tr("Open a raw image file read-only"));
    connect(act_open_image_, &QAction::triggered, this, &MainWindow::openImageDialog);

    act_save_report_ = new QAction(st->standardIcon(QStyle::SP_DialogSaveButton), tr("&Save Report..."), this);
    act_save_report_->setShortcut(QKeySequence::Save);
    act_save_report_->setStatusTip(tr("Save a JSON report for the selected device"));
    connect(act_save_report_, &QAction::triggered, this, &MainWindow::saveReport);

    act_exit_ = new QAction(tr("E&xit"), this);
    act_exit_->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
    connect(act_exit_, &QAction::triggered, this, &QWidget::close);

    act_refresh_ = new QAction(st->standardIcon(QStyle::SP_BrowserReload), tr("&Refresh Device List"), this);
    act_refresh_->setShortcut(QKeySequence(QStringLiteral("F5")));
    act_refresh_->setStatusTip(tr("Re-enumerate storage devices (metadata only)"));
    connect(act_refresh_, &QAction::triggered, this, &MainWindow::refreshDevices);

    act_identify_ = new QAction(st->standardIcon(QStyle::SP_DriveHDIcon), tr("&Identify Device"), this);
    act_identify_->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(act_identify_, &QAction::triggered, this, [this] { selectTab(QStringLiteral("INFO")); });

    act_smart_ = new QAction(st->standardIcon(QStyle::SP_FileDialogInfoView), tr("&SMART Information"), this);
    act_smart_->setShortcut(QKeySequence(QStringLiteral("F2")));
    act_smart_->setStatusTip(tr("Query SMART / NVMe health via smartctl (read-only)"));
    connect(act_smart_, &QAction::triggered, this, [this] {
        selectTab(QStringLiteral("SMART"));
        querySmart();
    });

    act_caps_ = new QAction(tr("Device &Capabilities"), this);
    connect(act_caps_, &QAction::triggered, this, [this] { selectTab(QStringLiteral("INFO")); });

    act_partitions_ = new QAction(st->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Read &Partition Table"), this);
    act_partitions_->setShortcut(QKeySequence(QStringLiteral("F3")));
    connect(act_partitions_, &QAction::triggered, this, [this] {
        selectTab(QStringLiteral("PARTITIONS"));
        readPartitions();
    });

    act_surface_ = new QAction(st->standardIcon(QStyle::SP_MediaPlay), tr("&Surface Test..."), this);
    act_surface_->setShortcut(QKeySequence(QStringLiteral("F4")));
    act_surface_->setStatusTip(tr("Read-only surface scan with a latency map"));
    connect(act_surface_, &QAction::triggered, this, [this] { selectTab(QStringLiteral("SURFACE")); });

    act_benchmark_ = new QAction(st->standardIcon(QStyle::SP_ComputerIcon), tr("&Benchmark..."), this);
    act_benchmark_->setShortcut(QKeySequence(QStringLiteral("F6")));
    act_benchmark_->setStatusTip(tr("Read benchmarks: sequential, random, access time, speed curve, sweeps"));
    connect(act_benchmark_, &QAction::triggered, this, [this] { selectTab(QStringLiteral("BENCHMARK")); });

    act_record_ = new QAction(tr("&Record History"), this);
    act_record_->setCheckable(true);
    act_record_->setStatusTip(tr("Save finished benchmarks, scans and SMART queries to the local history database"));
    connect(act_record_, &QAction::toggled, this, [this](bool on) {
        record_history_ = on;
        QSettings().setValue(QStringLiteral("history/record"), on);
        if (bench_tab_) bench_tab_->setHistory(history_.get(), on);
        if (surface_tab_) surface_tab_->setHistory(history_.get(), on);
        if (erase_tab_) erase_tab_->setHistory(history_.get(), on);
        if (imaging_tab_) imaging_tab_->setHistory(history_.get(), on);
        if (qualify_tab_) qualify_tab_->setHistory(history_.get(), on);
    });

    act_show_serials_ = new QAction(tr("Show Serial &Numbers"), this);
    act_show_serials_->setCheckable(true);
    act_show_serials_->setStatusTip(tr("Serial numbers are masked on screen and omitted from reports by default"));
    connect(act_show_serials_, &QAction::toggled, this, [this](bool on) {
        show_serials_ = on;
        if (erase_tab_) erase_tab_->setShowSerials(on);
        if (imaging_tab_) imaging_tab_->setShowSerials(on);
        if (qualify_tab_) qualify_tab_->setShowSerials(on);
        showCurrent();
    });

    act_about_ = new QAction(tr("&About platter..."), this);
    connect(act_about_, &QAction::triggered, this, &MainWindow::about);
    act_license_ = new QAction(tr("&License Information..."), this);
    connect(act_license_, &QAction::triggered, this, &MainWindow::licenseInfo);
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    file->addAction(act_open_image_);
    file->addAction(act_save_report_);
    file->addSeparator();
#ifdef PLATTER_HAVE_HELPER
    act_helper_ = new QAction(tr("Use &Privileged Helper"), this);
    act_helper_->setCheckable(true);
    act_helper_->setEnabled(!running_elevated());
    act_helper_->setStatusTip(running_elevated()
                                  ? tr("Not needed: platter is already running with full privileges")
                                  : tr("Open disks through a small root helper (asks for your password once) instead of running the whole program as root"));
    connect(act_helper_, &QAction::toggled, this, [this](bool on) {
        if (on == helper_on_) return;
        if (!useHelper(on ? QStringLiteral("auto") : QStringLiteral("none"))) {
            QSignalBlocker block(act_helper_);
            act_helper_->setChecked(helper_on_);
        }
    });
    file->addAction(act_helper_);
    file->addSeparator();
#endif
    file->addAction(act_exit_);

    auto* dev = menuBar()->addMenu(tr("&Device"));
    dev->addAction(act_refresh_);
    dev->addSeparator();
    dev->addAction(act_identify_);
    dev->addAction(act_smart_);
    dev->addAction(act_caps_);
    dev->addAction(act_partitions_);

    auto* ops = menuBar()->addMenu(tr("&Operations"));
    ops->addAction(act_surface_);
    ops->addAction(act_benchmark_);
    {
        auto* a = new QAction(tr("&Partition Manager..."), this);
        a->setStatusTip(tr("Edit the partition table and format partitions (asks for typed confirmation)"));
        connect(a, &QAction::triggered, this, &MainWindow::editPartitions);
        ops->addAction(a);
    }
    ops->addSeparator();
    {
        auto* a = new QAction(tr("&Erase Device..."), this);
        a->setStatusTip(tr("Overwrite erase with verification (asks for typed confirmation)"));
        connect(a, &QAction::triggered, this, [this] { selectTab(QStringLiteral("ERASE")); });
        ops->addAction(a);
    }
    auto imaging_action = [this, ops](const QString& text, const QString& tip, int mode) {
        auto* a = new QAction(text, this);
        a->setStatusTip(tip);
        connect(a, &QAction::triggered, this, [this, mode] {
            selectTab(QStringLiteral("CLONE / IMAGE"));
            imaging_tab_->selectMode(mode);
        });
        ops->addAction(a);
    };
    imaging_action(tr("&Clone Device..."), tr("Copy the selected device onto another (asks for typed confirmation)"), 2);
    imaging_action(tr("Create Disk &Image..."), tr("Read the selected device into an image file"), 0);
    {
        auto* a = new QAction(tr("&Qualify Drive..."), this);
        a->setStatusTip(tr("Burn-in: write passes, read-back, read pass, random reads (asks for typed confirmation)"));
        connect(a, &QAction::triggered, this, [this] { selectTab(QStringLiteral("QUALIFY")); });
        ops->addAction(a);
    }

    auto* tools = menuBar()->addMenu(tr("&Tools"));
    {
        auto* hex = new QAction(tr("&Hex Sector Viewer"), this);
        hex->setStatusTip(tr("Inspect sectors read-only, with structure interpretation and search"));
        connect(hex, &QAction::triggered, this, [this] { selectTab(QStringLiteral("HEX")); });
        tools->addAction(hex);
        auto* sum = new QAction(tr("&Checksum Calculator..."), this);
        sum->setStatusTip(tr("SHA-256 of any file"));
        connect(sum, &QAction::triggered, this, [this] {
            ChecksumDialog dlg(this);
            dlg.exec();
        });
        tools->addAction(sum);
        auto* ver = new QAction(tr("Image &Verification"), this);
        ver->setStatusTip(tr("Check an image against its checksum or against a device"));
        connect(ver, &QAction::triggered, this, [this] {
            selectTab(QStringLiteral("CLONE / IMAGE"));
            imaging_tab_->selectMode(3);
        });
        tools->addAction(ver);
        auto* hist = new QAction(tr("Operation &History..."), this);
        hist->setStatusTip(tr("Browse every saved benchmark, scan, erase, imaging and qualification report"));
        connect(hist, &QAction::triggered, this, &MainWindow::showHistory);
        tools->addAction(hist);
    }

    auto* settings = menuBar()->addMenu(tr("&Settings"));
    settings->addAction(act_show_serials_);
    settings->addAction(act_record_);
    settings->addSeparator();
    {
        auto* prefs = new QAction(tr("&Preferences..."), this);
        prefs->setStatusTip(tr("History, look, privileged helper, log file"));
        connect(prefs, &QAction::triggered, this, &MainWindow::showPreferences);
        settings->addAction(prefs);
    }

    auto* help = menuBar()->addMenu(tr("&Help"));
    {
        auto* doc = new QAction(tr("&Documentation"), this);
        doc->setShortcut(QKeySequence(QStringLiteral("F1")));
        doc->setStatusTip(tr("Built-in help (works offline)"));
        connect(doc, &QAction::triggered, this, &MainWindow::showHelp);
        help->addAction(doc);
    }
    help->addSeparator();
    help->addAction(act_about_);
    help->addAction(act_license_);
}

void MainWindow::buildToolBar() {
    auto* tb = addToolBar(tr("Main"));
    toolbar_ = tb;
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setIconSize(QSize(16, 16));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->addAction(act_refresh_);
    tb->addAction(act_open_image_);
    tb->addSeparator();
    tb->addWidget(new QLabel(tr(" Device: "), tb));
    selector_ = new QComboBox(tb);
    selector_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    selector_->setMinimumContentsLength(40);
    selector_->setToolTip(tr("Select a device or image (Ctrl+D)"));
    tb->addWidget(selector_);
    auto* focus = new QAction(this);
    focus->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(focus, &QAction::triggered, selector_, qOverload<>(&QWidget::setFocus));
    addAction(focus);
    connect(selector_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onDeviceSelected);
    tb->addSeparator();
    tb->addAction(act_identify_);
    tb->addAction(act_smart_);
    tb->addAction(act_partitions_);
    tb->addAction(act_surface_);
    tb->addAction(act_benchmark_);
    tb->addSeparator();
    tb->addAction(act_save_report_);
}

namespace {
void addKeyedTab(QTabWidget* tabs, QWidget* page, const QString& key, const QString& title) {
    const int i = tabs->addTab(page, title);
    tabs->tabBar()->setTabData(i, key);
}
}  // namespace

void MainWindow::buildCentral() {
    auto* central = new QWidget(this);
    auto* col = new QVBoxLayout(central);
    col->setContentsMargins(4, 4, 4, 0);
    col->setSpacing(4);

    // ---- device summary panel
    auto* summary = new QGroupBox(tr("Device Summary"), central);
    auto* grid = new QGridLayout(summary);
    grid->setContentsMargins(6, 4, 6, 4);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(2);
    auto pair = [&](int r, int c, const QString& key, QLabel*& value) {
        auto* k = new QLabel(key, summary);
        k->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(k, r, c * 2);
        value = value_label(summary);
        grid->addWidget(value, r, c * 2 + 1);
    };
    pair(0, 0, tr("Model:"), sum_model_);
    pair(0, 1, tr("Capacity:"), sum_capacity_);
    pair(0, 2, tr("Temperature:"), sum_temp_);
    pair(1, 0, tr("Interface:"), sum_iface_);
    pair(1, 1, tr("Sectors:"), sum_geometry_);
    pair(1, 2, tr("Health:"), sum_health_);
    pair(2, 0, tr("Firmware:"), sum_firmware_);
    pair(2, 1, tr("Usage:"), sum_usage_);
    sum_health_->setObjectName(QStringLiteral("healthUnknown"));
    for (int c : {1, 3, 5}) grid->setColumnStretch(c, 1);
    col->addWidget(summary);

    // ---- tabs + activity log
    vsplit_ = new QSplitter(Qt::Vertical, central);
    vsplit_->setChildrenCollapsible(false);
    tabs_ = new QTabWidget(vsplit_);
    tabs_->setDocumentMode(false);
    info_tab_ = new InfoTab(tabs_);
    smart_tab_ = new SmartTab(tabs_);
    part_tab_ = new PartitionsTab(tabs_);
    log_tab_ = new LogTab(log_, tabs_);
    addKeyedTab(tabs_, info_tab_, QStringLiteral("INFO"), tr("INFO"));
    addKeyedTab(tabs_, smart_tab_, QStringLiteral("SMART"), tr("SMART"));
    surface_tab_ = new SurfaceTab(tabs_);
    addKeyedTab(tabs_, surface_tab_, QStringLiteral("SURFACE"), tr("SURFACE"));
    connect(surface_tab_, &SurfaceTab::logEvent, this,
            [this](Severity sev, const QString& msg, const QString& dev) { log_->add(sev, msg, dev); });
    bench_tab_ = new BenchmarkTab(tabs_);
    addKeyedTab(tabs_, bench_tab_, QStringLiteral("BENCHMARK"), tr("BENCHMARK"));
    connect(bench_tab_, &BenchmarkTab::logEvent, this,
            [this](Severity sev, const QString& msg, const QString& dev) { log_->add(sev, msg, dev); });
    addKeyedTab(tabs_, part_tab_, QStringLiteral("PARTITIONS"), tr("PARTITIONS"));
    erase_tab_ = new EraseTab(tabs_);
    addKeyedTab(tabs_, erase_tab_, QStringLiteral("ERASE"), tr("ERASE"));
    connect(erase_tab_, &EraseTab::logEvent, this,
            [this](Severity sev, const QString& msg, const QString& dev) { log_->add(sev, msg, dev); });
    const auto sync_targets = [this] {
        surface_tab_->setDevice(current());
        bench_tab_->setDevice(current());
        erase_tab_->setDevice(current());
    };
    connect(surface_tab_, &SurfaceTab::operationFinished, this, sync_targets, Qt::QueuedConnection);
    connect(bench_tab_, &BenchmarkTab::operationFinished, this, sync_targets, Qt::QueuedConnection);
    connect(erase_tab_, &EraseTab::operationFinished, this, sync_targets, Qt::QueuedConnection);
    qualify_tab_ = new QualifyTab(tabs_);
    addKeyedTab(tabs_, qualify_tab_, QStringLiteral("QUALIFY"), tr("QUALIFY"));
    connect(qualify_tab_, &QualifyTab::logEvent, this,
            [this](Severity sev, const QString& msg, const QString& dev) { log_->add(sev, msg, dev); });
    imaging_tab_ = new ImagingTab(tabs_);
    addKeyedTab(tabs_, imaging_tab_, QStringLiteral("CLONE / IMAGE"), tr("CLONE / IMAGE"));
    connect(imaging_tab_, &ImagingTab::logEvent, this,
            [this](Severity sev, const QString& msg, const QString& dev) { log_->add(sev, msg, dev); });
    hex_tab_ = new HexTab(tabs_);
    addKeyedTab(tabs_, hex_tab_, QStringLiteral("HEX"), tr("HEX"));
    connect(hex_tab_, &HexTab::logEvent, this,
            [this](Severity sev, const QString& msg, const QString& dev) { log_->add(sev, msg, dev); });
    addKeyedTab(tabs_, log_tab_, QStringLiteral("LOG"), tr("LOG"));
    connect(smart_tab_, &SmartTab::queryRequested, this, &MainWindow::querySmart);
    connect(part_tab_, &PartitionsTab::readRequested, this, &MainWindow::readPartitions);
    connect(part_tab_, &PartitionsTab::editRequested, this, &MainWindow::editPartitions);

    auto* actbox = new QGroupBox(tr("Activity Log"), vsplit_);
    auto* al = new QVBoxLayout(actbox);
    al->setContentsMargins(4, 2, 4, 4);
    activity_ = new QTableView(actbox);
    activity_->setModel(log_);
    setup_table(activity_, QStringLiteral("log.activity"), false);
    activity_->setColumnHidden(LogModel::Device, true);
    activity_->horizontalHeader()->setVisible(false);
    activity_->resizeColumnToContents(LogModel::Time);
    activity_->setColumnWidth(LogModel::Time,
                              activity_->fontMetrics().horizontalAdvance(QStringLiteral("0000-00-00 00:00:00.000")) + 16);
    activity_->setColumnWidth(LogModel::Level, activity_->fontMetrics().horizontalAdvance(QStringLiteral("CRITICAL")) + 16);
    connect(log_, &QAbstractItemModel::rowsInserted, activity_, &QTableView::scrollToBottom);
    al->addWidget(activity_);

    vsplit_->addWidget(tabs_);
    vsplit_->addWidget(actbox);
    vsplit_->setStretchFactor(0, 4);
    vsplit_->setStretchFactor(1, 1);
    col->addWidget(vsplit_, 1);
    setCentralWidget(central);
}

void MainWindow::buildStatusBar() {
    auto mk = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setObjectName(QStringLiteral("statusPanel"));
        return l;
    };
    st_device_ = mk(tr("No device"));
    st_mode_ = mk(tr("CONFIRM TO WRITE"));
    st_mode_->setToolTip(tr("Nothing is written to a device without a preview and a typed confirmation"));
    st_priv_ = mk(tr("Privileges: %1").arg(i18n::ct(privilege_label())));
    st_priv_->setToolTip(running_elevated() ? tr("Running elevated")
                                            : tr("Reading sectors and SMART data from physical disks usually "
                                                 "needs root / Administrator"));
    statusBar()->addWidget(st_device_, 1);
    statusBar()->addPermanentWidget(st_mode_);
    statusBar()->addPermanentWidget(st_priv_);
    statusBar()->setSizeGripEnabled(true);
}

void MainWindow::openHistory() {
    // Same database as platter-cli, so sessions saved by either appear in both.
    // $PLATTER_HISTORY_DB wins; otherwise the Preferences override; otherwise the default.
    const auto path = (!prefs_.history_db.isEmpty() && !std::getenv("PLATTER_HISTORY_DB")) ? prefs_.history_db.toStdString()
                                                                                          : storage::HistoryStore::default_path();
    auto store = storage::HistoryStore::open(path);
    if (!store) {
        log_->add(Severity::warning,
                  tr("History database unavailable (%1); results will not be saved").arg(i18n::err(store.error())));
    } else {
        history_ = std::move(store).value();
        log_->add(Severity::debug, tr("History database: %1").arg(QString::fromStdString(path)));
    }
    record_history_ = QSettings().value(QStringLiteral("history/record"), true).toBool();
    act_record_->setChecked(record_history_);
    act_record_->setEnabled(history_ != nullptr);
    bench_tab_->setHistory(history_.get(), record_history_);
    surface_tab_->setHistory(history_.get(), record_history_);
    erase_tab_->setHistory(history_.get(), record_history_);
    imaging_tab_->setHistory(history_.get(), record_history_);
    qualify_tab_->setHistory(history_.get(), record_history_);
}

void MainWindow::restoreSettings() {
    QSettings s;
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray())) resize(980, 680);
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    vsplit_->restoreState(s.value(QStringLiteral("window/vsplit")).toByteArray());
    tabs_->setCurrentIndex(s.value(QStringLiteral("window/tab"), 0).toInt());
    act_show_serials_->setChecked(false);  // never persisted: privacy by default
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (surface_tab_->isRunning() || bench_tab_->isRunning() || erase_tab_->isRunning() || imaging_tab_->isRunning() || qualify_tab_->isRunning()) {
        const auto answer = QMessageBox::question(this, tr("Operation Running"),
                                                  tr("A scan, benchmark or erase is still running. Stop it and exit?"),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        surface_tab_->stopAndWait();
        bench_tab_->stopAndWait();
        erase_tab_->stopAndWait();
        imaging_tab_->stopAndWait();
        qualify_tab_->stopAndWait();
    }
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    s.setValue(QStringLiteral("window/vsplit"), vsplit_->saveState());
    s.setValue(QStringLiteral("window/tab"), tabs_->currentIndex());
    info_tab_->saveState();
    smart_tab_->saveState();
    part_tab_->saveState();
    log_tab_->saveState();
    save_table_state(activity_);
    e->accept();
}

// ------------------------------------------------------------------ devices

int MainWindow::indexOfPath(const std::string& path) const {
    for (std::size_t i = 0; i < devices_.size(); ++i)
        if (devices_[i].info.path == path) return static_cast<int>(i);
    return -1;
}

DeviceEntry* MainWindow::current() {
    return current_ >= 0 && current_ < static_cast<int>(devices_.size()) ? &devices_[static_cast<std::size_t>(current_)]
                                                                         : nullptr;
}

void MainWindow::refreshDevices() {
    const std::string keep = current() ? current()->info.path : std::string{};
    // Physical devices are re-enumerated; opened images are kept (listed last).
    std::vector<DeviceEntry> next;
    auto devs = enumerate_devices();
    if (!devs) {
        log_->add(Severity::error, tr("Device enumeration failed: %1").arg(i18n::err(devs.error())));
    } else {
        for (auto& d : *devs) {
            DeviceEntry e;
            e.info = std::move(d);
            // Carry SMART / partition results across a refresh only when the
            // identity still matches (path alone is not identity, spec §17.2).
            for (const auto& old : devices_)
                if (!old.is_image && old.info.path == e.info.path && old.info.stable_id == e.info.stable_id &&
                    old.info.capacity_bytes == e.info.capacity_bytes && old.info.model == e.info.model) {
                    e.smart = old.smart;
                    e.health = old.health;
                    e.table = old.table;
                    if (e.smart) smart::apply_capabilities(*e.smart, e.info);
                }
            next.push_back(std::move(e));
        }
        log_->add(Severity::info, tr("Found %n storage device(s)", nullptr, static_cast<int>(devs->size())));
    }
    for (auto& e : devices_)
        if (e.is_image) next.push_back(std::move(e));
    devices_ = std::move(next);

    const QSignalBlocker block(selector_);
    selector_->clear();
    for (const auto& e : devices_) {
        auto icon = style()->standardIcon(e.is_image ? QStyle::SP_FileIcon
                                                     : (e.info.removable == Tri::yes ? QStyle::SP_DriveFDIcon
                                                                                     : QStyle::SP_DriveHDIcon));
        selector_->addItem(icon, e.displayName());
    }
    // Never silently switch targets: if the previously selected device is
    // gone, select nothing rather than whichever device took its slot.
    int idx = keep.empty() ? (devices_.empty() ? -1 : 0) : indexOfPath(keep);
    if (!keep.empty() && idx < 0)
        log_->add(Severity::warning, tr("Previously selected device %1 is no longer present").arg(qs(keep)));
    selector_->setCurrentIndex(idx);
    onDeviceSelected(idx);
}

void MainWindow::openImage(const QString& path, std::uint32_t sector_size) {
    const auto p = QFileInfo(path).absoluteFilePath().toStdString();
    if (int existing = indexOfPath(p); existing >= 0) {
        selector_->setCurrentIndex(existing);
        return;
    }
    auto dev = open_image(p, {}, sector_size);
    if (!dev) {
        log_->add(Severity::error, tr("Cannot open image: %1").arg(i18n::err(dev.error())), path);
        QMessageBox::warning(this, tr("Open Disk Image"), i18n::err(dev.error()));
        return;
    }
    DeviceEntry e;
    e.info = (*dev)->info();
    e.is_image = true;
    e.image_sector_size = sector_size;
    devices_.push_back(std::move(e));
    selector_->addItem(style()->standardIcon(QStyle::SP_FileIcon), devices_.back().displayName());
    log_->add(Severity::info, tr("Opened image read-only (%1-byte sectors)").arg(sector_size), path);
    selector_->setCurrentIndex(static_cast<int>(devices_.size()) - 1);
}

void MainWindow::openImageDialog() {
    QSettings s;
    const auto dir = s.value(QStringLiteral("paths/lastImageDir")).toString();
    const auto path = QFileDialog::getOpenFileName(this, tr("Open Disk Image"), dir,
                                                   tr("Disk images (*.img *.raw *.bin *.dd);;All files (*)"));
    if (path.isEmpty()) return;
    s.setValue(QStringLiteral("paths/lastImageDir"), QFileInfo(path).absolutePath());
    openImage(path);
}

void MainWindow::onDeviceSelected(int index) {
    current_ = index;
    auto* e = current();
    if (e && e->is_image && !e->table && e->table_error.isEmpty()) readPartitionsNow();  // images: cheap and safe
    showCurrent();
}

void MainWindow::showCurrent() {
    auto* e = current();
    info_tab_->setDevice(e, show_serials_);
    smart_tab_->setDevice(e);
    smart_tab_->setBusy(busy_smart_);
    part_tab_->setDevice(e);
    part_tab_->setBusy(busy_parts_);
    surface_tab_->setDevice(e);
    bench_tab_->setDevice(e);
    erase_tab_->setDevice(e);
    imaging_tab_->setDevice(e);
    imaging_tab_->setChoices(devices_);
    hex_tab_->setDevice(e);
    qualify_tab_->setDevice(e);
    updateSummary();
    const bool has = e != nullptr;
    act_save_report_->setEnabled(has);
    act_smart_->setEnabled(has && !e->is_image && !busy_smart_);
    act_partitions_->setEnabled(has && !busy_parts_);
    st_device_->setText(has ? qs(e->info.path) : tr("No device"));
}

void MainWindow::updateSummary() {
    auto* e = current();
    auto set = [](QLabel* l, const QString& v) { l->setText(v.isEmpty() ? QStringLiteral("-") : v); };
    if (!e) {
        for (auto* l : {sum_model_, sum_capacity_, sum_iface_, sum_geometry_, sum_firmware_, sum_usage_, sum_temp_})
            set(l, {});
        set(sum_health_, tr("UNKNOWN"));
        return;
    }
    const auto& d = e->info;
    set(sum_model_, d.bus == BusType::image ? i18n::ct(d.model) : qs(d.vendor.empty() || d.vendor == "ATA" ? d.model : d.vendor + " " + d.model));
    set(sum_capacity_, i18n::units(qs(format_capacity(d.capacity_bytes))));
    set(sum_iface_, QStringLiteral("%1 / %2").arg(i18n::ct(bus_type_name(d.bus)), i18n::ct(media_kind_name(d.media))));
    set(sum_geometry_, tr("%1 x %2 B (phys %3 B)")
                           .arg(d.total_lbas())
                           .arg(d.logical_sector_size)
                           .arg(d.physical_sector_size));
    set(sum_firmware_, qs(d.firmware));
    QString usage;
    if (d.is_system_disk) usage = tr("SYSTEM DISK  ");
    if (!d.mountpoints.empty()) {
        QStringList m;
        for (const auto& x : d.mountpoints) m << qs(x);
        usage += tr("Mounted: %1").arg(m.join(QStringLiteral(", ")));
    } else if (!d.holders.empty()) {
        usage += tr("In use by %1").arg(qs(d.holders.front()));
    } else if (!e->is_image) {
        usage += tr("Not mounted");
    }
    set(sum_usage_, usage);
    set(sum_temp_, e->smart && e->smart->temperature_c ? tr("%1 °C").arg(*e->smart->temperature_c) : QString());
    const auto h = e->health ? e->health->status : smart::Health::unknown;
    sum_health_->setText(e->is_image ? tr("N/A") : i18n::ct(smart::health_name(h)));
    switch (h) {
        case smart::Health::good: sum_health_->setObjectName(QStringLiteral("healthGood")); break;
        case smart::Health::warning: sum_health_->setObjectName(QStringLiteral("healthWarning")); break;
        case smart::Health::critical: sum_health_->setObjectName(QStringLiteral("healthCritical")); break;
        default: sum_health_->setObjectName(QStringLiteral("healthUnknown")); break;
    }
    sum_health_->style()->unpolish(sum_health_);
    sum_health_->style()->polish(sum_health_);
}

// ------------------------------------------------------------------ operations

void MainWindow::querySmart() {
    auto* e = current();
    if (!e || e->is_image || busy_smart_) return;
    busy_smart_ = true;
    showCurrent();
    const std::string path = e->info.path;
    log_->add(Severity::info, tr("Querying SMART via smartctl"), qs(path));
    QPointer<MainWindow> self(this);
    smart::RunOptions options;
    options.device_type = smart_tab_->deviceType().toStdString();
    run_worker(this, [self, path, options] {
        auto rep = smart::query(path, options);
        QMetaObject::invokeMethod(
            qApp,
            [self, path, rep = std::move(rep)]() mutable {
                if (!self) return;
                self->busy_smart_ = false;
                const int idx = self->indexOfPath(path);
                if (idx < 0) {
                    self->log_->add(Severity::warning, tr("SMART result discarded: device disappeared"), qs(path));
                    self->showCurrent();
                    return;
                }
                auto& entry = self->devices_[static_cast<std::size_t>(idx)];
                if (!rep) {
                    entry.smart_error = i18n::err(rep.error());
                    self->log_->add(Severity::error, tr("SMART query failed: %1").arg(entry.smart_error), qs(path));
                } else {
                    entry.smart_error.clear();
                    entry.smart = std::move(rep).value();
                    entry.health = smart::assess(*entry.smart);
                    smart::apply_capabilities(*entry.smart, entry.info);
                    const auto sev = entry.health->status == smart::Health::critical ? Severity::critical
                                     : entry.health->status == smart::Health::warning ? Severity::warning
                                                                                      : Severity::info;
                    self->log_->add(sev, tr("SMART health: %1").arg(i18n::ct(smart::health_name(entry.health->status))),
                                    qs(path));
                    if (self->history_ && self->record_history_) {
                        if (auto id = self->history_->record_smart(entry.info, *entry.smart, *entry.health); !id)
                            self->log_->add(Severity::warning,
                                            tr("SMART snapshot not saved: %1").arg(i18n::err(id.error())), qs(path));
                    }
                }
                self->showCurrent();
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::readPartitionsNow() {
    auto* e = current();
    if (!e) return;
    OpenOptions oo;
    oo.mode = OpenMode::read_only;
    auto dev = e->is_image ? open_image(e->info.path, oo, e->image_sector_size) : open_device(e->info.path, oo);
    if (!dev) {
        e->table_error = tr("Cannot open device: %1").arg(i18n::err(dev.error()));
        log_->add(Severity::error, e->table_error, qs(e->info.path));
        return;
    }
    auto t = partitions::read_partition_table(**dev);
    if (!t) {
        e->table_error = i18n::err(t.error());
        log_->add(Severity::error, tr("Partition table read failed: %1").arg(e->table_error), qs(e->info.path));
        return;
    }
    e->table_error.clear();
    e->table = std::move(t).value();
    log_->add(e->table->warnings.empty() ? Severity::info : Severity::warning,
              tr("Partition table: %1, %2 partition(s), %3 warning(s)")
                  .arg(qs(partitions::table_type_name(e->table->type)).toUpper())
                  .arg(e->table->partitions.size())
                  .arg(e->table->warnings.size()),
              qs(e->info.path));
}

void MainWindow::readPartitions() {
    auto* e = current();
    if (!e || busy_parts_) return;
    if (e->is_image) {
        readPartitionsNow();
        showCurrent();
        return;
    }
    busy_parts_ = true;
    showCurrent();
    const std::string path = e->info.path;
    QPointer<MainWindow> self(this);
    run_worker(this, [self, path] {
        OpenOptions oo;
        oo.mode = OpenMode::read_only;
        Result<partitions::PartitionTable> result = Error{Errc::internal, "not run"};
        auto dev = open_device(path, oo);
        if (!dev) result = Error{dev.error().code, "cannot open device: " + dev.error().describe()};
        else result = partitions::read_partition_table(**dev);
        QMetaObject::invokeMethod(
            qApp,
            [self, path, result = std::move(result)]() mutable {
                if (!self) return;
                self->busy_parts_ = false;
                const int idx = self->indexOfPath(path);
                if (idx >= 0) {
                    auto& entry = self->devices_[static_cast<std::size_t>(idx)];
                    if (!result) {
                        entry.table_error = i18n::err(result.error());
                        self->log_->add(Severity::error, tr("Partition table read failed: %1").arg(entry.table_error),
                                        qs(path));
                    } else {
                        entry.table_error.clear();
                        entry.table = std::move(result).value();
                        self->log_->add(Severity::info,
                                        tr("Partition table: %1, %2 partition(s)")
                                            .arg(qs(partitions::table_type_name(entry.table->type)).toUpper())
                                            .arg(entry.table->partitions.size()),
                                        qs(path));
                    }
                }
                self->showCurrent();
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::showPreferences() {
    PreferencesDialog dlg(Preferences::load(), this);
    connect(&dlg, &PreferencesDialog::applied, this, [this](const Preferences& p) { applyPreferences(p, false); });
    if (dlg.exec() == QDialog::Accepted) applyPreferences(dlg.result(), false);
}

void MainWindow::showHelp() {
    HelpDialog dlg(this);
    dlg.exec();
}

void MainWindow::writeLogLine(const LogEvent& e) {
    if (!log_file_ || !log_file_->isOpen()) return;
    const auto line = QStringLiteral("%1\t%2\t%3\t%4\n")
                          .arg(e.time.toString(Qt::ISODate), severity_name(e.severity), e.device, QString(e.message).replace(QLatin1Char('\n'), QLatin1Char(' ')))
                          .toUtf8();
    log_file_->write(line);
    log_file_->flush();
}

void MainWindow::applyPreferences(const Preferences& p, bool startup) {
    const Preferences old = prefs_;
    prefs_ = p;
    p.save();

    if (startup || p.record_history != old.record_history) act_record_->setChecked(p.record_history);
    if (startup || p.classic_theme != old.classic_theme) platter::gui::apply_theme(*qApp, p.classic_theme);
    if (toolbar_) toolbar_->setVisible(p.show_toolbar);
    statusBar()->setVisible(p.show_statusbar);

    if (startup || p.log_file != old.log_file) {
        if (log_file_) disconnect(log_, &LogModel::eventAdded, this, nullptr);
        log_file_.reset();
        if (!p.log_file.isEmpty()) {
            auto f = std::make_unique<QFile>(p.log_file);
            if (f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                log_file_ = std::move(f);
                connect(log_, &LogModel::eventAdded, this, [this](const LogEvent& e) { writeLogLine(e); });
                log_->add(Severity::info, tr("Logging to %1").arg(p.log_file));
            } else {
                log_->add(Severity::warning, tr("Cannot open the log file %1: %2").arg(p.log_file, f->errorString()));
            }
        }
    }
    if (!startup && p.language != old.language)
        QMessageBox::information(this, tr("Language"), tr("The new language is used the next time platter starts."));
    if (!startup && p.history_db != old.history_db)
        log_->add(Severity::info, tr("The history database location changes after restarting platter"));
    if (startup && p.helper_at_startup && !running_elevated()) {
        const auto spec = p.helper_command.isEmpty() ? QStringLiteral("auto") : p.helper_command;
        useHelper(spec);
    }
}

bool MainWindow::useHelper(const QString& spec) {
#ifdef PLATTER_HAVE_HELPER
    platter::set_device_broker(nullptr);
    std::string note;
    const bool on = !spec.isEmpty() && spec != QStringLiteral("none");
    if (auto s = platter::helper::configure_helper(spec.toStdString(), platter::helper::executable_dir(), note); !s) {
        QMessageBox::warning(this, tr("Privileged Helper"), i18n::err(s.error()));
        log_->add(Severity::warning, tr("Privileged helper not enabled"), i18n::err(s.error()));
        helper_on_ = false;
        return false;
    }
    helper_on_ = on && platter::device_broker() != nullptr;
    if (act_helper_) {
        QSignalBlocker block(act_helper_);
        act_helper_->setChecked(helper_on_);
    }
    st_priv_->setText(helper_on_ ? tr("Privileges: user + helper") : tr("Privileges: %1").arg(i18n::ct(privilege_label())));
    log_->add(Severity::info, helper_on_ ? tr("Privileged helper enabled") : tr("Privileged helper off"), qs(note));
    return true;
#else
    Q_UNUSED(spec);
    return false;
#endif
}

void MainWindow::showHistory() {
    const auto* e = current();
    HistoryDialog dlg(history_.get(), e ? QString::fromStdString(storage::HistoryStore::device_key(e->info)) : QString(),
                      show_serials_, this);
    dlg.exec();
}

void MainWindow::editPartitions() {
    auto* e = current();
    if (!e || busy_parts_) return;
    if (surface_tab_->isRunning() || bench_tab_->isRunning() || erase_tab_->isRunning() || imaging_tab_->isRunning() || qualify_tab_->isRunning()) {
        QMessageBox::information(this, tr("Partition Manager"), tr("Wait for the running operation to finish first."));
        return;
    }
    EditorTarget t;
    t.path = qs(e->info.path);
    t.label = e->displayName();
    t.is_image = e->is_image;
    t.sector_size = e->is_image ? e->image_sector_size : e->info.logical_sector_size;
    t.show_serial = show_serials_;
    const std::string path = e->info.path;
    PartitionEditorDialog dlg(t, this);
    connect(&dlg, &PartitionEditorDialog::logEvent, this,
            [this](Severity sev, const QString& msg, const QString& dev) { log_->add(sev, msg, dev); });
    connect(&dlg, &PartitionEditorDialog::tableWritten, this, [this] {
        readPartitionsNow();
        showCurrent();
    });
    dlg.exec();
    readPartitionsNow();
    showCurrent();
}

void MainWindow::saveReport() {
    auto* e = current();
    if (!e) return;
    const auto include = QMessageBox::question(this, tr("Save Report"),
                                               tr("Include serial numbers in the report?\n\n"
                                                  "They are omitted by default because reports are often shared."),
                                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    reports::Privacy priv{include == QMessageBox::Yes};
    const auto path = QFileDialog::getSaveFileName(this, tr("Save Report"),
                                                   QString::fromStdString(e->info.name) + QStringLiteral("-report.json"),
                                                   tr("JSON report (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    auto j = reports::envelope("device-report");
    j["device"] = reports::to_json(e->info, priv);
    if (e->smart) {
        j["smart"] = reports::to_json(*e->smart, priv);
        if (e->health) j["assessment"] = reports::to_json(*e->health);
    }
    if (e->table) j["partitions"] = reports::to_json(*e->table);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save Report"), tr("Cannot write %1:\n%2").arg(path, f.errorString()));
        return;
    }
    f.write(QByteArray::fromStdString(j.dump(2)));
    log_->add(Severity::info, tr("Report saved to %1").arg(path), qs(e->info.path));
}

void MainWindow::selectIndex(int index) {
    if (index >= 0 && index < selector_->count()) selector_->setCurrentIndex(index);
}

void MainWindow::selectTab(const QString& name) {
    // Tabs are found by their untranslated key, so --tab HEX and the menu
    // actions keep working in every language; the visible title also matches.
    for (int i = 0; i < tabs_->count(); ++i) {
        const auto key = tabs_->tabBar()->tabData(i).toString();
        if (key.compare(name, Qt::CaseInsensitive) == 0 ||
            tabs_->tabText(i).remove(QLatin1Char('&')).compare(name, Qt::CaseInsensitive) == 0) {
            tabs_->setCurrentIndex(i);
            return;
        }
    }
}

void MainWindow::about() {
    QMessageBox::about(this, tr("About platter"),
                       tr("<b>platter %1</b><br>Storage diagnostics, maintenance and management utility.<br><br>"
                          "It identifies devices, reads SMART data via smartmontools, scans surfaces, runs "
                          "benchmarks and inspects sectors without writing anything. Erasing, qualifying, "
                          "partitioning, formatting, restoring and cloning WRITE to a device, and each one "
                          "shows a preview and asks for a typed confirmation first.<br><br>"
                          "No telemetry. No network access. No registration.")
                           .arg(QString::fromLatin1(kVersion)));
}

void MainWindow::licenseInfo() {
    QMessageBox::information(this, tr("License Information"),
                             tr("platter: Copyright (c) 2026 Ruslan Yakunin. All rights reserved.\n\n"
                                "Third-party components:\n"
                                "  Qt 6 (LGPL v3)\n"
                                "  nlohmann/json (MIT)\n"
                                "  smartmontools (GPL v2, invoked as an external program)\n"));
}

}  // namespace platter::gui
