// SPDX-License-Identifier: LicenseRef-Proprietary
#include "../i18n/i18n.hpp"
#include "surface_tab.hpp"
#include "../widgets/confirm_dialog.hpp"
#include "../widgets/partition_pick_button.hpp"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStyle>
#include <QTimer>
#include <cinttypes>

#include "../widgets/surface_map.hpp"
#include "platter/reports/scan_report.hpp"
#include "platter/storage/history.hpp"

namespace platter::gui {
namespace {

using namespace platter::surface;


QString ms_text(std::uint64_t us) { return QString::number(static_cast<double>(us) / 1000.0, 'f', 3) + QStringLiteral(" ms"); }

QString rate_text(double bps) { return i18n::units(QString::number(bps / 1e6, 'f', 2) + QStringLiteral(" MB/s")); }

QString hms(std::int64_t s) {
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg(s / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString size_text(std::uint64_t bytes) {
    const auto s = format_capacity(bytes);
    return i18n::units(QString::fromStdString(s.substr(0, s.find(" ("))));
}

QLabel* val(QWidget* parent) {
    auto* l = new QLabel(QStringLiteral("-"), parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setMinimumWidth(90);
    return l;
}

QLabel* swatch(int cls, QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setFixedSize(11, 11);
    l->setStyleSheet(QStringLiteral("background:%1; border:1px solid #808080;")
                         .arg(SurfaceMap::classColor(cls).name()));
    return l;
}

const std::uint32_t kBlockSizes[] = {4096, 16384, 65536, 262144, 1u << 20, 4u << 20, 16u << 20};

}  // namespace

SurfaceTab::SurfaceTab(QWidget* parent) : QWidget(parent), timer_(new QTimer(this)) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(4);

    // ---- control row
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    start_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), tr("&Start"), this);
    pause_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPause), tr("&Pause"), this);
    stop_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), tr("S&top"), this);
    open_ = new QPushButton(tr("Open Scan..."), this);
    export_ = new QPushButton(tr("&Export..."), this);
    status_ = new QLabel(this);
    start_->setToolTip(tr("Start a READ surface scan (never writes)"));
    open_->setToolTip(tr("Load a saved JSON scan for viewing or comparison"));
    for (auto* b : {start_, pause_, stop_}) row->addWidget(b);
    row->addSpacing(8);
    row->addWidget(open_);
    row->addWidget(export_);
    row->addSpacing(8);
    row->addWidget(status_, 1);
    col->addLayout(row);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    split->addWidget(buildSettings());
    split->addWidget(buildMapPanel());
    split->addWidget(buildInfoPanel());
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    col->addWidget(split, 1);

    connect(start_, &QPushButton::clicked, this, &SurfaceTab::onStart);
    connect(pause_, &QPushButton::clicked, this, &SurfaceTab::onPauseResume);
    connect(stop_, &QPushButton::clicked, this, &SurfaceTab::onStop);
    connect(open_, &QPushButton::clicked, this, &SurfaceTab::onOpenScan);
    connect(export_, &QPushButton::clicked, this, &SurfaceTab::onExport);
    connect(timer_, &QTimer::timeout, this, &SurfaceTab::poll);
    connect(map_, &SurfaceMap::cellSelected, this, &SurfaceTab::onCellSelected);

    resetDefaults();
    setDevice(nullptr);
}

SurfaceTab::~SurfaceTab() { stopAndWait(); }

QWidget* SurfaceTab::buildSettings() {
    auto* box = new QGroupBox(tr("Scan Settings"), this);
    auto* f = new QFormLayout(box);
    f->setContentsMargins(6, 4, 6, 4);
    f->setVerticalSpacing(3);
    f->setLabelAlignment(Qt::AlignRight);

    mode_ = new QComboBox(box);
    mode_->addItem(tr("READ"));
    mode_->addItem(tr("WRITE (destructive)"));
    mode_->addItem(tr("WRITE + READBACK (destructive)"));
    mode_->addItem(tr("CUSTOM PATTERN (destructive)"));
    f->addRow(tr("Mode:"), mode_);
    pattern_ = new QLineEdit(box);
    pattern_->setPlaceholderText(tr("hex bytes, e.g. DEADBEEF"));
    pattern_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("([0-9A-Fa-f]{2}){0,2048}")), box));
    pattern_->setToolTip(tr("Repeating byte pattern for the write modes (default A5 5A)"));
    pattern_->setEnabled(false);
    f->addRow(tr("Pattern:"), pattern_);
    connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) { pattern_->setEnabled(i != 0 && !isRunning()); });

    auto* digits = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,20}")), box);
    start_lba_ = new QLineEdit(box);
    start_lba_->setValidator(digits);
    end_lba_ = new QLineEdit(box);
    end_lba_->setValidator(digits);
    end_lba_->setPlaceholderText(tr("end of device"));
    f->addRow(tr("Start LBA:"), start_lba_);
    f->addRow(tr("End LBA:"), end_lba_);
    pick_ = new PartitionPickButton(start_lba_, end_lba_, QString(), box);
    f->addRow(QString(), pick_);

    block_ = new QComboBox(box);
    for (auto b : kBlockSizes)
        block_->addItem(i18n::units(b >= (1u << 20) ? QStringLiteral("%1 MiB").arg(b >> 20) : QStringLiteral("%1 KiB").arg(b >> 10)), b);
    f->addRow(tr("Block size:"), block_);

    order_ = new QComboBox(box);
    order_->addItems({tr("Forward"), tr("Reverse"), tr("Random"), tr("Sampled")});
    f->addRow(tr("Order:"), order_);
    sample_ = new QDoubleSpinBox(box);
    sample_->setRange(0.1, 100.0);
    sample_->setDecimals(1);
    sample_->setSuffix(QStringLiteral(" %"));
    f->addRow(tr("Sample:"), sample_);
    connect(order_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) { sample_->setEnabled(i == 3 && !isRunning()); });

    retries_ = new QSpinBox(box);
    retries_->setRange(0, 16);
    f->addRow(tr("Retries:"), retries_);

    auto* err_row = new QWidget(box);
    auto* el = new QHBoxLayout(err_row);
    el->setContentsMargins(0, 0, 0, 0);
    el->setSpacing(3);
    on_error_ = new QComboBox(err_row);
    on_error_->addItems({tr("Continue"), tr("Stop after")});
    max_errors_ = new QSpinBox(err_row);
    max_errors_->setRange(1, 1000000);
    el->addWidget(on_error_, 1);
    el->addWidget(max_errors_);
    f->addRow(tr("On errors:"), err_row);
    connect(on_error_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) { max_errors_->setEnabled(i == 1 && !isRunning()); });

    direct_ = new QCheckBox(tr("Direct I/O (bypass OS cache)"), box);
    f->addRow(QString(), direct_);

    auto* thr_box = new QWidget(box);
    auto* tl = new QGridLayout(thr_box);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setHorizontalSpacing(3);
    tl->setVerticalSpacing(2);
    for (int i = 0; i < 4; ++i) {
        thr_[i] = new QSpinBox(thr_box);
        thr_[i]->setRange(1, 3'600'000);
        thr_[i]->setSuffix(tr(" ms"));
        tl->addWidget(swatch(i + 1, thr_box), i, 0);
        tl->addWidget(new QLabel(QStringLiteral("<"), thr_box), i, 1);
        tl->addWidget(thr_[i], i, 2);
    }
    tl->addWidget(swatch(5, thr_box), 4, 0);
    tl->addWidget(new QLabel(tr("above"), thr_box), 4, 1, 1, 2);
    tl->addWidget(swatch(6, thr_box), 5, 0);
    tl->addWidget(new QLabel(tr("read error"), thr_box), 5, 1, 1, 2);
    f->addRow(tr("Classes:"), thr_box);

    defaults_ = new QPushButton(tr("Defaults"), box);
    connect(defaults_, &QPushButton::clicked, this, &SurfaceTab::resetDefaults);
    f->addRow(QString(), defaults_);
    box->setMaximumWidth(i18n::scale(260));
    return box;
}

QWidget* SurfaceTab::buildMapPanel() {
    auto* box = new QGroupBox(tr("Surface Map"), this);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(3);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Zoom:"), box));
    zoom_ = new QComboBox(box);
    zoom_->addItem(tr("Fit"), 0);
    for (int z = 1; z <= 4096; z *= 2) zoom_->addItem(tr("%1 block(s)/cell").arg(z), z);
    top->addWidget(zoom_);
    top->addStretch(1);
    auto* hint = new QLabel(tr("Click a cell for details. Ctrl+wheel zooms."), box);
    hint->setObjectName(QStringLiteral("placeholder"));
    top->addWidget(hint);
    v->addLayout(top);

    map_ = new SurfaceMap(box);
    v->addWidget(map_, 1);
    connect(zoom_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { map_->setZoom(zoom_->currentData().toInt()); });

    progress_ = new QProgressBar(box);
    progress_->setRange(0, 1000);
    progress_->setTextVisible(false);  // classic chunked bar; percentage shown in the status line
    v->addWidget(progress_);

    auto* legend = new QHBoxLayout;
    legend->setSpacing(4);
    const QString names[] = {tr("untested"), tr("fast"), tr("good"), tr("medium"), tr("slow"), tr("very slow"), tr("error")};
    for (int i = 0; i < kLatencyClassCount; ++i) {
        legend->addWidget(swatch(i, box));
        legend_[i] = new QLabel(QStringLiteral("%1: 0").arg(names[i]), box);
        legend->addWidget(legend_[i]);
        legend->addSpacing(6);
    }
    legend->addStretch(1);
    v->addLayout(legend);
    return box;
}

QWidget* SurfaceTab::buildInfoPanel() {
    auto* panel = new QWidget(this);
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    auto* sbox = new QGroupBox(tr("Statistics"), panel);
    auto* g = new QFormLayout(sbox);
    g->setContentsMargins(6, 4, 6, 4);
    g->setVerticalSpacing(1);
    auto add = [&](QFormLayout* fl, QGroupBox* b, const QString& k, QLabel*& l) {
        l = val(b);
        fl->addRow(k, l);
    };
    add(g, sbox, tr("Tested:"), st_tested_);
    add(g, sbox, tr("Requests:"), st_requests_);
    add(g, sbox, tr("Failed:"), st_failed_);
    add(g, sbox, tr("Slow:"), st_slow_);
    add(g, sbox, tr("Retries:"), st_retries_);
    add(g, sbox, tr("Latency min:"), st_lat_min_);
    add(g, sbox, tr("Latency mean:"), st_lat_avg_);
    add(g, sbox, tr("Latency median:"), st_lat_med_);
    add(g, sbox, tr("Latency P95:"), st_lat_p95_);
    add(g, sbox, tr("Latency P99:"), st_lat_p99_);
    add(g, sbox, tr("Latency max:"), st_lat_max_);
    add(g, sbox, tr("Current speed:"), st_rate_cur_);
    add(g, sbox, tr("Average speed:"), st_rate_avg_);
    add(g, sbox, tr("Elapsed:"), st_elapsed_);
    add(g, sbox, tr("Remaining:"), st_eta_);
    add(g, sbox, tr("Current LBA:"), st_lba_);
    v->addWidget(sbox);

    auto* cbox = new QGroupBox(tr("Selected Cell"), panel);
    auto* c = new QFormLayout(cbox);
    c->setContentsMargins(6, 4, 6, 4);
    c->setVerticalSpacing(1);
    add(c, cbox, tr("Block(s):"), sel_blocks_);
    add(c, cbox, tr("LBA range:"), sel_lbas_);
    add(c, cbox, tr("Transfer size:"), sel_size_);
    add(c, cbox, tr("Slowest read:"), sel_dur_);
    add(c, cbox, tr("Throughput:"), sel_rate_);
    add(c, cbox, tr("Errors:"), sel_err_);
    add(c, cbox, tr("Retries:"), sel_retries_);
    add(c, cbox, tr("Tested at:"), sel_time_);
    v->addWidget(cbox);
    v->addStretch(1);
    panel->setMaximumWidth(i18n::scale(260));
    return panel;
}

void SurfaceTab::resetDefaults() {
    const ScanConfig d;
    mode_->setCurrentIndex(0);
    start_lba_->setText(QStringLiteral("0"));
    end_lba_->clear();
    for (int i = 0; i < block_->count(); ++i)
        if (block_->itemData(i).toUInt() == d.block_bytes) block_->setCurrentIndex(i);
    order_->setCurrentIndex(0);
    sample_->setValue(d.sample_percent);
    sample_->setEnabled(false);
    retries_->setValue(static_cast<int>(d.retries));
    on_error_->setCurrentIndex(0);
    max_errors_->setValue(100);
    max_errors_->setEnabled(false);
    direct_->setChecked(true);
    for (int i = 0; i < 4; ++i) thr_[i]->setValue(static_cast<int>(d.thresholds.us[static_cast<std::size_t>(i)] / 1000));
}

// ------------------------------------------------------------------ target

void SurfaceTab::setDevice(const DeviceEntry* e) {
    if (isRunning()) return;  // a running scan stays bound to its own device
    const QString identity = e ? QString::fromStdString(storage::HistoryStore::device_key(e->info)) : QString();
    if (range_identity_ != identity || target_path_ != (e ? QString::fromStdString(e->info.path) : QString()) ||
        (e && target_sector_ != (e->is_image ? e->image_sector_size : e->info.logical_sector_size))) {
        start_lba_->setText(QStringLiteral(""));
        end_lba_->clear();
    }
    range_identity_ = identity;
    if (!e) {
        target_path_.clear();
        target_label_.clear();
        target_lbas_ = 0;
        pick_->setDevice(nullptr);
    } else {
        target_path_ = QString::fromStdString(e->info.path);
        target_label_ = e->displayName();
        target_is_image_ = e->is_image;
        target_sector_ = e->is_image ? e->image_sector_size : e->info.logical_sector_size;
        target_lbas_ = e->info.total_lbas();
        pick_->setDevice(e);
        end_lba_->setPlaceholderText(target_lbas_ ? tr("end (%1)").arg(target_lbas_ - 1) : tr("end of device"));
    }
    updateButtons();
    if (!view_ && !scan_) status_->setText(e ? tr("Ready to scan %1 (read-only).").arg(target_label_) : tr("No device selected."));
}

bool SurfaceTab::isRunning() const { return scan_ && !is_terminal(scan_->state()); }

void SurfaceTab::stopAndWait() {
    if (scan_) {
        scan_->cancel();
        scan_->wait();
    }
}

std::optional<ScanConfig> SurfaceTab::readConfig(QString& error) const {
    ScanConfig c;
    switch (mode_->currentIndex()) {
        case 1: c.mode = ScanMode::write; break;
        case 2: c.mode = ScanMode::write_readback; break;
        case 3: c.mode = ScanMode::custom_pattern; break;
        default: c.mode = ScanMode::read; break;
    }
    if (is_write_mode(c.mode)) {
        const auto hex = pattern_->text();
        if (hex.size() % 2 != 0) {
            error = tr("The pattern needs an even number of hex digits.");
            return std::nullopt;
        }
        for (int i = 0; i + 1 < hex.size(); i += 2) c.pattern.push_back(static_cast<std::uint8_t>(hex.mid(i, 2).toUInt(nullptr, 16)));
        if (c.mode == ScanMode::custom_pattern && c.pattern.empty()) {
            error = tr("Enter the pattern to write.");
            return std::nullopt;
        }
    }
    bool ok = true;
    c.start_lba = start_lba_->text().isEmpty() ? 0 : start_lba_->text().toULongLong(&ok);
    if (!ok) {
        error = tr("Start LBA is not a number.");
        return std::nullopt;
    }
    if (!end_lba_->text().isEmpty()) {
        c.end_lba = end_lba_->text().toULongLong(&ok);
        if (!ok) {
            error = tr("End LBA is not a number.");
            return std::nullopt;
        }
    }
    c.block_bytes = block_->currentData().toUInt();
    c.order = static_cast<ScanOrder>(order_->currentIndex());
    c.sample_percent = sample_->value();
    c.retries = static_cast<std::uint32_t>(retries_->value());
    if (on_error_->currentIndex() == 1) {
        c.error_policy = ErrorPolicy::stop_after_n;
        c.max_errors = static_cast<std::uint64_t>(max_errors_->value());
    }
    c.direct_io = direct_->isChecked();
    for (int i = 0; i < 4; ++i) c.thresholds.us[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(thr_[i]->value()) * 1000u;
    if (!c.thresholds.valid()) {
        error = tr("Latency class bounds must increase from top to bottom.");
        return std::nullopt;
    }
    return c;
}

void SurfaceTab::setSettingsEnabled(bool on) {
    pattern_->setEnabled(on && mode_->currentIndex() != 0);
    for (QWidget* w : std::initializer_list<QWidget*>{mode_, start_lba_, end_lba_, pick_, block_, order_, retries_, on_error_,
                                                     direct_, defaults_, thr_[0], thr_[1], thr_[2], thr_[3]})
        w->setEnabled(on);
    sample_->setEnabled(on && order_->currentIndex() == 3);
    max_errors_->setEnabled(on && on_error_->currentIndex() == 1);
}

void SurfaceTab::updateButtons() {
    const bool running = isRunning();
    const bool paused = scan_ && scan_->state() == OperationState::paused;
    start_->setEnabled(!running && !target_path_.isEmpty());
    pause_->setEnabled(running);
    pause_->setText(paused ? tr("&Resume") : tr("&Pause"));
    pause_->setIcon(style()->standardIcon(paused ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    stop_->setEnabled(running);
    open_->setEnabled(!running);
    export_->setEnabled(!running && view_.has_value());
    setSettingsEnabled(!running);
}

// ------------------------------------------------------------------ actions

void SurfaceTab::onStart() { startScan(); }

void SurfaceTab::startScan() {
    if (isRunning() || target_path_.isEmpty()) return;
    QString error;
    auto cfg = readConfig(error);
    if (!cfg) {
        QMessageBox::warning(this, tr("Surface Scan"), error);
        return;
    }
    std::unique_ptr<SurfaceScan> scan;
    if (is_write_mode(cfg->mode)) {
        // DESTRUCTIVE: preview + typed confirmation; the range is whatever the
        // user entered (whole device when End LBA is blank).
        safety::Request req;
        req.kind = safety::Kind::write_test;
        req.target.path = target_path_.toStdString();
        req.target.is_image = target_is_image_;
        req.target.image_sector_size = target_sector_;
        if (cfg->end_lba != UINT64_MAX) req.ranges.push_back(LbaRange{cfg->start_lba, cfg->end_lba - cfg->start_lba + 1});
        else if (cfg->start_lba != 0) {
            QMessageBox::warning(this, tr("Surface Scan"), tr("Enter an End LBA too, or leave both Start and End at their defaults for the whole device."));
            return;
        }
        req.summary = std::string(scan_mode_name(cfg->mode)) + " surface scan";
        auto out = confirm_and_authorize(this, tr("Confirm Write Scan"), req, false,
                                         tr("%1 surface scan").arg(i18n::ct(scan_mode_name(cfg->mode))));
        if (out.cancelled) return;
        if (!out.target) {
            emit logEvent(Severity::error, tr("Write scan refused: %1").arg(out.error), target_path_);
            QMessageBox::warning(this, tr("Surface Scan"), out.error);
            return;
        }
        emit logEvent(Severity::warning, tr("Write scan authorized (destroys data in the chosen range)"), target_path_);
        scan = std::make_unique<SurfaceScan>(std::move(*out.target), *cfg);
    } else {
        auto created = create_scan(target_path_.toStdString(), target_is_image_, *cfg, target_sector_);
        if (!created) {
            const auto msg = i18n::err(created.error());
            emit logEvent(Severity::error, tr("Cannot open device for scanning: %1").arg(msg), target_path_);
            QMessageBox::warning(this, tr("Surface Scan"),
                                 tr("Cannot open %1 for reading:\n%2%3")
                                     .arg(target_path_, msg,
                                          created.error().code == Errc::permission_denied
                                              ? tr("\n\nScanning a physical disk needs root / Administrator.")
                                              : QString()));
            return;
        }
        scan = std::move(created).value();
    }
    if (auto v = scan->validate(); !v) {
        const auto msg = i18n::err(v.error());
        emit logEvent(Severity::error, tr("Invalid scan settings: %1").arg(msg), target_path_);
        QMessageBox::warning(this, tr("Surface Scan"), msg);
        return;
    }
    for (const auto& w : scan->warnings()) emit logEvent(Severity::warning, i18n::ct(w), target_path_);
    if (auto s = scan->start(); !s) {
        emit logEvent(Severity::error, i18n::err(s.error()), target_path_);
        return;
    }
    const auto& L = scan->layout();
    emit logEvent(Severity::info,
                  tr("Surface scan started (%6, %1 blocks of %2 bytes, LBA %3-%4, %5)")
                      .arg(L.block_count)
                      .arg(scan->config().block_bytes)
                      .arg(L.start_lba)
                      .arg(L.end_lba)
                      .arg(scan->config().direct_io ? tr("direct I/O") : tr("buffered I/O"))
                      .arg(QString::fromUtf8(scan_mode_name(scan->config().mode).data(),
                                             static_cast<qsizetype>(scan_mode_name(scan->config().mode).size()))),
                  target_path_);
    scan_ = std::move(scan);
    view_.reset();
    view_source_.clear();
    map_->clear();
    last_state_ = OperationState::running;
    status_->setText(tr("Scanning %1 ...").arg(target_label_));
    timer_->start(250);
    updateButtons();
    poll();
}

void SurfaceTab::onPauseResume() {
    if (!scan_) return;
    if (scan_->state() == OperationState::paused) {
        scan_->resume();
        emit logEvent(Severity::info, tr("Surface scan resumed"), target_path_);
    } else {
        scan_->pause();
        emit logEvent(Severity::info, tr("Surface scan paused"), target_path_);
    }
    updateButtons();
}

void SurfaceTab::onStop() {
    if (!isRunning()) return;
    scan_->cancel();
    status_->setText(tr("Stopping after the current request..."));
}

void SurfaceTab::poll() {
    if (!scan_) return;
    std::vector<std::uint8_t> cls;
    scan_->copy_classes(cls);
    map_->setClasses(std::move(cls));
    const auto p = scan_->progress();
    showStats(scan_->stats(), p);
    if (p.state == OperationState::paused)
        status_->setText(tr("Paused at %1 %.").arg(p.percent(), 0, 'f', 1));
    else if (p.state == OperationState::running)
        status_->setText(tr("Scanning %1 ...  %2 %").arg(target_label_).arg(p.percent(), 0, 'f', 1));
    if (p.state != last_state_) {
        last_state_ = p.state;
        updateButtons();
    }
    if (is_terminal(p.state)) finishScan();
    showSelection();
}

void SurfaceTab::finishScan() {
    timer_->stop();
    view_ = scan_->snapshot();
    view_source_ = target_path_;
    const auto& s = view_->stats;
    const auto st = view_->progress.state;
    QString summary = tr("%1: %2 requests, %3 failed, %4 slow, %5 average")
                          .arg(i18n::ct(operation_state_name(st)))
                          .arg(s.requests)
                          .arg(s.failed)
                          .arg(s.slow)
                          .arg(rate_text(s.average_bytes_per_sec));
    if (!view_->progress.message.empty() && st != OperationState::completed)
        summary += QStringLiteral(" (") + i18n::ct(view_->progress.message) + QStringLiteral(")");
    status_->setText(summary);
    const Severity sev = st == OperationState::failed || s.failed ? Severity::warning : Severity::info;
    emit logEvent(sev, tr("Surface scan finished: %1").arg(summary), target_path_);
    // Cancelled scans are partial by the user's choice; keep history to finished ones.
    if (history_ && record_ && st != OperationState::cancelled) {
        if (auto id = history_->record_scan(*view_); !id)
            emit logEvent(Severity::warning,
                          tr("Scan not saved to history: %1").arg(i18n::err(id.error())),
                          target_path_);
    }
    scan_.reset();
    updateButtons();
    emit operationFinished();
}

void SurfaceTab::showStats(const ScanStats& s, const OperationProgress& p) {
    progress_->setValue(static_cast<int>(p.percent() * 10.0));
    const QString names[] = {tr("untested"), tr("fast"), tr("good"), tr("medium"), tr("slow"), tr("very slow"), tr("error")};
    std::uint64_t tested_blocks = 0;
    for (int i = 1; i < kLatencyClassCount; ++i) tested_blocks += s.per_class[static_cast<std::size_t>(i)];
    const auto* L = currentLayout();
    const std::uint64_t untested = L ? L->block_count - tested_blocks : 0;
    for (int i = 0; i < kLatencyClassCount; ++i)
        legend_[i]->setText(QStringLiteral("%1: %2").arg(names[i]).arg(i == 0 ? untested : s.per_class[static_cast<std::size_t>(i)]));
    st_tested_->setText(QStringLiteral("%1 / %2").arg(size_text(s.tested_bytes), size_text(s.total_bytes)));
    st_requests_->setText(QString::number(s.requests));
    st_failed_->setText(QString::number(s.failed));
    st_failed_->setStyleSheet(s.failed ? QStringLiteral("color:#C00000; font-weight:bold") : QString());
    st_slow_->setText(QString::number(s.slow));
    st_retries_->setText(QString::number(s.retries));
    st_lat_min_->setText(ms_text(s.min_us));
    st_lat_avg_->setText(ms_text(static_cast<std::uint64_t>(s.mean_us)));
    st_lat_med_->setText(ms_text(s.median_us));
    st_lat_p95_->setText(ms_text(s.p95_us));
    st_lat_p99_->setText(ms_text(s.p99_us));
    st_lat_max_->setText(ms_text(s.max_us));
    st_rate_cur_->setText(is_terminal(p.state) ? QStringLiteral("-") : rate_text(p.current_bytes_per_sec));
    st_rate_avg_->setText(rate_text(s.average_bytes_per_sec ? s.average_bytes_per_sec : p.average_bytes_per_sec));
    st_elapsed_->setText(hms(std::chrono::duration_cast<std::chrono::seconds>(p.elapsed).count()));
    st_eta_->setText(p.eta ? hms(p.eta->count()) : QStringLiteral("-"));
    st_lba_->setText(scan_ ? QString::number(p.current_lba) : QStringLiteral("-"));  // meaningless for saved scans
}

const ScanLayout* SurfaceTab::currentLayout() const {
    if (scan_) return &scan_->layout();
    if (view_) return &view_->layout;
    return nullptr;
}

BlockResult SurfaceTab::blockAt(std::uint64_t i) const {
    if (scan_) return scan_->block(i);
    if (view_ && i < view_->blocks.size()) return view_->blocks[static_cast<std::size_t>(i)];
    return {};
}

void SurfaceTab::onCellSelected(quint64, quint64) { showSelection(); }

void SurfaceTab::showSelection() {
    const auto* L = currentLayout();
    const std::uint64_t first = map_->selectedFirst(), last = map_->selectedLast();
    QLabel* all[] = {sel_blocks_, sel_lbas_, sel_size_, sel_dur_, sel_rate_, sel_err_, sel_retries_, sel_time_};
    if (!L || first > last || last >= L->block_count) {
        for (auto* l : all) l->setText(QStringLiteral("-"));
        return;
    }
    std::uint64_t bytes = 0, tested_bytes = 0, sum_us = 0, max_us = 0, errors = 0, tested = 0;
    int max_retries = 0;
    std::uint32_t latest_ms = 0;
    std::uint16_t err_code = 0;
    for (std::uint64_t i = first; i <= last; ++i) {
        const auto b = blockAt(i);
        const auto len = L->lba_count(i) * L->sector_size;
        bytes += len;
        if (b.cls == LatencyClass::untested) continue;
        ++tested;
        max_retries = std::max<int>(max_retries, b.retries);
        latest_ms = std::max(latest_ms, b.time_ms);
        if (b.cls == LatencyClass::error) {
            ++errors;
            err_code = b.errc;
            continue;
        }
        tested_bytes += len;
        sum_us += b.duration_us;
        max_us = std::max<std::uint64_t>(max_us, b.duration_us);
    }
    const auto last_lba = L->first_lba(last) + L->lba_count(last) - 1;
    sel_blocks_->setText(first == last ? QString::number(first) : QStringLiteral("%1 - %2").arg(first).arg(last));
    sel_lbas_->setText(QStringLiteral("%1 - %2").arg(L->first_lba(first)).arg(last_lba));
    sel_size_->setText(size_text(bytes));
    if (!tested) {
        for (auto* l : {sel_dur_, sel_rate_, sel_err_, sel_retries_, sel_time_}) l->setText(tr("not tested"));
        return;
    }
    sel_dur_->setText(tested_bytes ? ms_text(max_us) : QStringLiteral("-"));
    sel_rate_->setText(sum_us ? rate_text(static_cast<double>(tested_bytes) * 1e6 / static_cast<double>(sum_us))
                              : QStringLiteral("-"));
    sel_err_->setText(errors ? QStringLiteral("%1 (%2)").arg(errors).arg(i18n::ct(errc_name(static_cast<Errc>(err_code))))
                             : tr("none"));
    sel_err_->setStyleSheet(errors ? QStringLiteral("color:#C00000; font-weight:bold") : QString());
    sel_retries_->setText(QString::number(max_retries));
    sel_time_->setText(QStringLiteral("+") + hms(latest_ms / 1000));
}

// ------------------------------------------------------------------ files

bool SurfaceTab::openScanFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open Scan"), tr("Cannot read %1:\n%2").arg(path, f.errorString()));
        return false;
    }
    auto snap = reports::scan_from_json(f.readAll().toStdString());
    if (!snap) {
        const auto msg = i18n::err(snap.error());
        emit logEvent(Severity::error, tr("Cannot load scan: %1").arg(msg), path);
        QMessageBox::warning(this, tr("Open Scan"), msg);
        return false;
    }
    view_ = std::move(snap).value();
    view_source_ = path;
    std::vector<std::uint8_t> cls(view_->blocks.size());
    for (std::size_t i = 0; i < cls.size(); ++i) cls[i] = static_cast<std::uint8_t>(view_->blocks[i].cls);
    map_->clearSelection();
    map_->setClasses(std::move(cls));
    showStats(view_->stats, view_->progress);
    showSelection();
    status_->setText(tr("Viewing saved scan of %1 (%2, %3)")
                         .arg(QString::fromStdString(view_->device.model.empty() ? view_->device.path : view_->device.model),
                              QFileInfo(path).fileName(), i18n::ct(operation_state_name(view_->progress.state))));
    emit logEvent(Severity::info, tr("Loaded saved scan %1").arg(QFileInfo(path).fileName()),
                  QString::fromStdString(view_->device.path));
    updateButtons();
    return true;
}

void SurfaceTab::onOpenScan() {
    QSettings s;
    const auto path = QFileDialog::getOpenFileName(this, tr("Open Scan"), s.value(QStringLiteral("paths/scans")).toString(),
                                                   tr("platter scan (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    s.setValue(QStringLiteral("paths/scans"), QFileInfo(path).absolutePath());
    openScanFile(path);
}

void SurfaceTab::onExport() {
    if (!view_) return;
    QSettings s;
    const QString json_f = tr("platter scan, JSON (*.json)"), csv_f = tr("Per-block CSV (*.csv)"),
                  txt_f = tr("Text report (*.txt)");
    QString chosen;
    const auto base = QString::fromStdString(view_->device.name.empty() ? "scan" : view_->device.name) +
                      QStringLiteral("-surface");
    const auto path = QFileDialog::getSaveFileName(this, tr("Export Scan"),
                                                   s.value(QStringLiteral("paths/scans")).toString() + QLatin1Char('/') + base,
                                                   json_f + QStringLiteral(";;") + csv_f + QStringLiteral(";;") + txt_f, &chosen);
    if (path.isEmpty()) return;
    s.setValue(QStringLiteral("paths/scans"), QFileInfo(path).absolutePath());
    std::string content;
    QString final_path = path;
    if (chosen == csv_f || path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
        content = reports::scan_to_csv(*view_);
        if (!final_path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) final_path += QStringLiteral(".csv");
    } else if (chosen == txt_f || path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) {
        content = reports::scan_to_text(*view_);
        if (!final_path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) final_path += QStringLiteral(".txt");
    } else {
        content = reports::scan_to_json(*view_);
        if (!final_path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) final_path += QStringLiteral(".json");
    }
    QFile f(final_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(content.data(), static_cast<qint64>(content.size())) < 0) {
        QMessageBox::warning(this, tr("Export Scan"), tr("Cannot write %1:\n%2").arg(final_path, f.errorString()));
        return;
    }
    emit logEvent(Severity::info, tr("Scan exported to %1 (serial numbers omitted)").arg(final_path),
                  QString::fromStdString(view_->device.path));
}

}  // namespace platter::gui
