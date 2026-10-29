// SPDX-License-Identifier: LicenseRef-Proprietary
#include "../i18n/i18n.hpp"
#include "benchmark_tab.hpp"
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
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <cmath>
#include <map>

#include "../widgets/chart.hpp"
#include "../widgets/table_tools.hpp"
#include "platter/reports/bench_report.hpp"
#include "platter/storage/history.hpp"

namespace platter::gui {
namespace {

using namespace platter::bench;


QString size_text(std::uint64_t b) {
    if (b >= (1ull << 30) && b % (1ull << 30) == 0) return i18n::units(QStringLiteral("%1 GiB").arg(b >> 30));
    if (b >= (1u << 20) && b % (1u << 20) == 0) return i18n::units(QStringLiteral("%1 MiB").arg(b >> 20));
    if (b >= 1024 && b % 1024 == 0) return QStringLiteral("%1 KiB").arg(b >> 10);
    return QStringLiteral("%1 B").arg(b);
}

QStandardItem* cell(const QString& t, bool right = false) {
    auto* i = new QStandardItem(t);
    i->setEditable(false);
    i->setTextAlignment((right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    return i;
}

QStandardItem* num(double v, int decimals) {
    auto* i = cell(QString::number(v, 'f', decimals), true);
    i->setData(v, Qt::UserRole);
    return i;
}

std::optional<std::vector<std::uint32_t>> parse_list(const QString& text, bool sizes) {
    std::vector<std::uint32_t> out;
    for (auto part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        part = part.trimmed().toUpper();
        std::uint64_t mult = 1;
        if (sizes) {
            if (part.endsWith(QStringLiteral("K"))) mult = 1024, part.chop(1);
            else if (part.endsWith(QStringLiteral("M"))) mult = 1024 * 1024, part.chop(1);
        }
        bool ok = false;
        const auto v = part.toULongLong(&ok) * mult;
        if (!ok || v == 0 || v > UINT32_MAX) return std::nullopt;
        out.push_back(static_cast<std::uint32_t>(v));
    }
    if (out.empty()) return std::nullopt;
    return out;
}

enum ChartKind { kTimeRate, kTimeIops, kPosition, kHistogram, kSizeRate, kSizeIops, kQdIops, kQdLatency };

const std::uint32_t kBlocks[] = {512, 4096, 16384, 65536, 131072, 262144, 1u << 20, 4u << 20, 16u << 20};

}  // namespace

BenchmarkTab::BenchmarkTab(QWidget* parent) : QWidget(parent), timer_(new QTimer(this)) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(4);

    auto* row = new QHBoxLayout;
    start_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), tr("&Start"), this);
    stop_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), tr("S&top"), this);
    export_ = new QPushButton(tr("&Export..."), this);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1000);
    progress_->setTextVisible(false);
    progress_->setMaximumWidth(220);
    status_ = new QLabel(this);
    row->addWidget(start_);
    row->addWidget(stop_);
    row->addWidget(export_);
    row->addSpacing(8);
    row->addWidget(progress_);
    row->addWidget(status_, 1);
    col->addLayout(row);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    split->addWidget(buildSettings());
    split->addWidget(buildResults());
    split->setStretchFactor(1, 1);
    col->addWidget(split, 1);

    connect(start_, &QPushButton::clicked, this, &BenchmarkTab::onStart);
    connect(stop_, &QPushButton::clicked, this, &BenchmarkTab::onStop);
    connect(export_, &QPushButton::clicked, this, &BenchmarkTab::onExport);
    connect(timer_, &QTimer::timeout, this, &BenchmarkTab::poll);
    onKindChanged();
    updateButtons();
}

BenchmarkTab::~BenchmarkTab() { stopAndWait(); }

QWidget* BenchmarkTab::buildSettings() {
    auto* box = new QGroupBox(tr("Benchmark Settings"), this);
    auto* f = new QFormLayout(box);
    f->setContentsMargins(6, 4, 6, 4);
    f->setVerticalSpacing(3);
    f->setLabelAlignment(Qt::AlignRight);

    kind_ = new QComboBox(box);
    for (int k = 0; k < kBenchKindCount; ++k) kind_->addItem(i18n::ct(bench_kind_title(static_cast<BenchKind>(k))), k);
    f->addRow(tr("Test:"), kind_);

    block_ = new QComboBox(box);
    block_->addItem(tr("Default"), 0u);
    for (auto b : kBlocks) block_->addItem(size_text(b), b);
    f->addRow(tr("Block size:"), block_);

    qd_ = new QSpinBox(box);
    qd_->setRange(1, 64);
    qd_->setToolTip(tr("Queue depth: worker threads, each with its own handle"));
    f->addRow(tr("Queue depth:"), qd_);

    duration_ = new QDoubleSpinBox(box);
    duration_->setRange(0, 3600);
    duration_->setDecimals(1);
    duration_->setSuffix(tr(" s"));
    duration_->setSpecialValueText(tr("Default"));
    f->addRow(tr("Time limit:"), duration_);

    size_limit_ = new QComboBox(box);
    size_limit_->addItem(tr("Default"), QVariant::fromValue<qulonglong>(0));
    for (std::uint64_t b : {64ull << 20, 256ull << 20, 1ull << 30, 4ull << 30, 16ull << 30})
        size_limit_->addItem(size_text(b), QVariant::fromValue<qulonglong>(b));
    f->addRow(tr("Size limit:"), size_limit_);

    points_ = new QSpinBox(box);
    points_->setRange(2, 10000);
    points_->setValue(100);
    f->addRow(tr("Curve points:"), points_);
    point_size_ = new QComboBox(box);
    for (std::uint64_t b : {4ull << 20, 16ull << 20, 64ull << 20, 256ull << 20})
        point_size_->addItem(size_text(b), QVariant::fromValue<qulonglong>(b));
    point_size_->setCurrentIndex(1);
    f->addRow(tr("Per point:"), point_size_);

    sizes_ = new QLineEdit(QStringLiteral("4K,16K,64K,256K,1M,4M"), box);
    f->addRow(tr("Sizes:"), sizes_);
    qds_ = new QLineEdit(QStringLiteral("1,2,4,8,16,32"), box);
    f->addRow(tr("Depths:"), qds_);

    auto* digits = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,20}")), box);
    start_lba_ = new QLineEdit(QStringLiteral("0"), box);
    start_lba_->setValidator(digits);
    end_lba_ = new QLineEdit(box);
    end_lba_->setValidator(digits);
    end_lba_->setPlaceholderText(tr("end of device"));
    f->addRow(tr("Start LBA:"), start_lba_);
    f->addRow(tr("End LBA:"), end_lba_);
    pick_ = new PartitionPickButton(start_lba_, end_lba_, QStringLiteral("0"), box);
    f->addRow(QString(), pick_);

    direct_ = new QCheckBox(tr("Direct I/O (bypass OS cache)"), box);
    direct_->setChecked(true);
    f->addRow(QString(), direct_);

    auto* note = new QLabel(tr("Read tests are read-only. Write tests DESTROY data in the LBA range given here (both ends required) and ask for confirmation. Throughput = bytes / wall time; latency = per request."), box);
    note->setObjectName(QStringLiteral("placeholder"));
    note->setWordWrap(true);
    f->addRow(QString(), note);

    connect(kind_, qOverload<int>(&QComboBox::currentIndexChanged), this, &BenchmarkTab::onKindChanged);
    box->setMaximumWidth(i18n::scale(270));
    return box;
}

QWidget* BenchmarkTab::buildResults() {
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    auto* top = new QHBoxLayout;
    headline_ = new QLabel(tr("-"), w);
    QFont hf = headline_->font();
    hf.setBold(true);
    hf.setPointSize(hf.pointSize() + 3);
    headline_->setFont(hf);
    chart_kind_ = new QComboBox(w);
    compare_with_ = new QComboBox(w);
    compare_with_->setMinimumContentsLength(28);
    compare_with_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    top->addWidget(headline_, 1);
    top->addWidget(new QLabel(tr("Chart:"), w));
    top->addWidget(chart_kind_);
    top->addWidget(new QLabel(tr("Compare:"), w));
    top->addWidget(compare_with_);
    v->addLayout(top);

    auto* split = new QSplitter(Qt::Vertical, w);
    split->setChildrenCollapsible(false);
    chart_ = new Chart(split);

    auto* lower = new QTabWidget(split);
    // Phase metrics
    auto* res = new QWidget(lower);
    auto* rl = new QHBoxLayout(res);
    rl->setContentsMargins(2, 2, 2, 2);
    phases_ = new QStandardItemModel(0, 12, this);
    phases_->setHorizontalHeaderLabels({tr("Phase"), tr("Block"), tr("QD"), tr("MB/s"), tr("MiB/s"), tr("IOPS"),
                                        tr("Mean ms"), tr("P50 ms"), tr("P95 ms"), tr("P99 ms"), tr("Max ms"),
                                        tr("Errors")});
    phases_->setSortRole(Qt::UserRole);
    phases_view_ = new QTableView(res);
    phases_view_->setModel(phases_);
    setup_table(phases_view_, QStringLiteral("bench.phases"), false);
    caveats_ = new QListWidget(res);
    caveats_->setMaximumWidth(i18n::scale(330));
    caveats_->setWordWrap(true);
    rl->addWidget(phases_view_, 3);
    rl->addWidget(caveats_, 2);
    lower->addTab(res, tr("Results"));

    // History
    auto* hist = new QWidget(lower);
    auto* hl = new QVBoxLayout(hist);
    hl->setContentsMargins(2, 2, 2, 2);
    hist_model_ = new QStandardItemModel(0, 5, this);
    hist_model_->setHorizontalHeaderLabels({tr("#"), tr("Started (UTC)"), tr("Test"), tr("Result"), tr("State")});
    hist_view_ = new QTableView(hist);
    hist_view_->setModel(hist_model_);
    setup_table(hist_view_, QStringLiteral("bench.history"), false);
    hist_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    auto* hrow = new QHBoxLayout;
    hist_load_ = new QPushButton(tr("&Load"), hist);
    hist_delete_ = new QPushButton(tr("&Delete"), hist);
    hist_status_ = new QLabel(hist);
    hist_status_->setObjectName(QStringLiteral("placeholder"));
    hrow->addWidget(hist_load_);
    hrow->addWidget(hist_delete_);
    hrow->addWidget(hist_status_, 1);
    hl->addWidget(hist_view_, 1);
    hl->addLayout(hrow);
    lower->addTab(hist, tr("History"));

    split->addWidget(chart_);
    split->addWidget(lower);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    v->addWidget(split, 1);

    connect(chart_kind_, qOverload<int>(&QComboBox::currentIndexChanged), this, &BenchmarkTab::refreshChart);
    connect(compare_with_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        compare_.reset();
        const auto id = compare_with_->currentData().toLongLong();
        if (id > 0 && history_) {
            auto r = history_->load_benchmark(id);
            if (r) compare_ = std::move(r).value();
        }
        refreshChart();
    });
    connect(hist_load_, &QPushButton::clicked, this, &BenchmarkTab::onHistoryLoad);
    connect(hist_delete_, &QPushButton::clicked, this, &BenchmarkTab::onHistoryDelete);
    connect(hist_view_, &QTableView::doubleClicked, this, &BenchmarkTab::onHistoryLoad);
    return w;
}

void BenchmarkTab::selectCompare(qlonglong id) {
    for (int i = 0; i < compare_with_->count(); ++i)
        if (compare_with_->itemData(i).toLongLong() == id) compare_with_->setCurrentIndex(i);
}

void BenchmarkTab::selectKind(int kind) {
    for (int i = 0; i < kind_->count(); ++i)
        if (kind_->itemData(i).toInt() == kind) kind_->setCurrentIndex(i);
}

void BenchmarkTab::onKindChanged() {
    const auto k = static_cast<BenchKind>(kind_->currentData().toInt());
    const bool running = isRunning();
    points_->setEnabled(!running && k == BenchKind::speed_curve);
    point_size_->setEnabled(!running && k == BenchKind::speed_curve);
    sizes_->setEnabled(!running && k == BenchKind::block_sizes);
    qds_->setEnabled(!running && k == BenchKind::queue_depths);
    qd_->setEnabled(!running && k != BenchKind::access_time && k != BenchKind::queue_depths);
    size_limit_->setEnabled(!running && (k == BenchKind::sequential || k == BenchKind::block_sizes || is_write_kind(k)));
    block_->setEnabled(!running && k != BenchKind::block_sizes);
    refreshHistory();
}

void BenchmarkTab::setDevice(const DeviceEntry* e) {
    if (isRunning()) return;
    const QString identity = e ? QString::fromStdString(storage::HistoryStore::device_key(e->info)) : QString();
    if (range_identity_ != identity || target_path_ != (e ? QString::fromStdString(e->info.path) : QString()) ||
        (e && target_sector_ != (e->is_image ? e->image_sector_size : e->info.logical_sector_size))) {
        start_lba_->setText(QStringLiteral("0"));
        end_lba_->clear();
    }
    range_identity_ = identity;
    if (!e) {
        target_path_.clear();
        target_label_.clear();
        target_key_.clear();
        pick_->setDevice(nullptr);
    } else {
        target_path_ = QString::fromStdString(e->info.path);
        target_label_ = e->displayName();
        target_is_image_ = e->is_image;
        target_sector_ = e->is_image ? e->image_sector_size : e->info.logical_sector_size;
        target_key_ = storage::HistoryStore::device_key(e->info);
        pick_->setDevice(e);
        end_lba_->setPlaceholderText(e->info.total_lbas() ? tr("end (%1)").arg(e->info.total_lbas() - 1) : tr("end of device"));
    }
    if (!shown_) status_->setText(e ? tr("Ready: %1 (read-only).").arg(target_label_) : tr("No device selected."));
    refreshHistory();
    updateButtons();
}

void BenchmarkTab::setHistory(storage::HistoryStore* store, bool record) {
    history_ = store;
    record_ = record;
    refreshHistory();
}

bool BenchmarkTab::isRunning() const { return bench_ && !is_terminal(bench_->state()); }

void BenchmarkTab::stopAndWait() {
    if (bench_) {
        bench_->cancel();
        bench_->wait();
    }
}

std::optional<BenchConfig> BenchmarkTab::readConfig(QString& error) const {
    BenchConfig c;
    c.kind = static_cast<BenchKind>(kind_->currentData().toInt());
    c.block_bytes = block_->currentData().toUInt();
    c.queue_depth = static_cast<std::uint32_t>(qd_->value());
    c.max_seconds = duration_->value();
    c.max_bytes = size_limit_->currentData().toULongLong();
    c.curve_points = static_cast<std::uint32_t>(points_->value());
    c.curve_bytes_per_point = point_size_->currentData().toULongLong();
    c.direct_io = direct_->isChecked();
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
    if (c.kind == BenchKind::block_sizes) {
        auto l = parse_list(sizes_->text(), true);
        if (!l) {
            error = tr("Sizes must look like 4K,64K,1M.");
            return std::nullopt;
        }
        c.sweep_block_sizes = *l;
    }
    if (c.kind == BenchKind::queue_depths) {
        auto l = parse_list(qds_->text(), false);
        if (!l) {
            error = tr("Depths must look like 1,4,16,32.");
            return std::nullopt;
        }
        c.sweep_queue_depths = *l;
    }
    return c;
}

void BenchmarkTab::setSettingsEnabled(bool on) {
    for (QWidget* w : std::initializer_list<QWidget*>{kind_, duration_, start_lba_, end_lba_, pick_, direct_}) w->setEnabled(on);
    onKindChanged();
}

void BenchmarkTab::updateButtons() {
    const bool running = isRunning();
    start_->setEnabled(!running && !target_path_.isEmpty());
    stop_->setEnabled(running);
    export_->setEnabled(!running && shown_.has_value());
    hist_load_->setEnabled(!running && history_);
    hist_delete_->setEnabled(!running && history_);
    setSettingsEnabled(!running);
}

// ------------------------------------------------------------------ running

void BenchmarkTab::onStart() { startBenchmark(); }

void BenchmarkTab::startBenchmark() {
    if (isRunning() || target_path_.isEmpty()) return;
    QString error;
    auto cfg = readConfig(error);
    if (!cfg) {
        QMessageBox::warning(this, tr("Benchmark"), error);
        return;
    }
    std::unique_ptr<bench::Benchmark> b;
    if (is_write_kind(cfg->kind)) {
        // DESTRUCTIVE: explicit region, then the full preview + typed confirmation.
        if (end_lba_->text().isEmpty()) {
            QMessageBox::warning(this, tr("Benchmark"),
                                 tr("A write benchmark needs an explicit region. Enter both Start LBA and End LBA."));
            return;
        }
        safety::Request req;
        req.kind = safety::Kind::write_test;
        req.target.path = target_path_.toStdString();
        req.target.is_image = target_is_image_;
        req.target.image_sector_size = target_sector_;
        req.ranges.push_back(LbaRange{cfg->start_lba, cfg->end_lba - cfg->start_lba + 1});
        req.summary = std::string(bench_kind_title(cfg->kind)) + " benchmark";
        auto out = confirm_and_authorize(this, tr("Confirm Write Benchmark"), req, false,
                                         tr("%1 benchmark").arg(i18n::ct(bench_kind_title(cfg->kind))));
        if (out.cancelled) return;
        if (!out.target) {
            emit logEvent(Severity::error, tr("Write benchmark refused: %1").arg(out.error), target_path_);
            QMessageBox::warning(this, tr("Benchmark"), out.error);
            return;
        }
        emit logEvent(Severity::warning, tr("Write benchmark authorized (destroys data in the chosen range)"), target_path_);
        b = std::make_unique<bench::Benchmark>(std::move(*out.target), *cfg);
    } else {
        auto created = create_benchmark(target_path_.toStdString(), target_is_image_, *cfg, target_sector_);
        if (!created) {
            const auto msg = i18n::err(created.error());
            emit logEvent(Severity::error, tr("Cannot open device for benchmarking: %1").arg(msg), target_path_);
            QMessageBox::warning(this, tr("Benchmark"),
                                 tr("Cannot open %1 for reading:\n%2%3")
                                     .arg(target_path_, msg,
                                          created.error().code == Errc::permission_denied
                                              ? tr("\n\nBenchmarking a physical disk needs root / Administrator.")
                                              : QString()));
            return;
        }
        b = std::move(created).value();
    }
    if (auto v = b->validate(); !v) {
        const auto msg = i18n::err(v.error());
        emit logEvent(Severity::error, tr("Invalid benchmark settings: %1").arg(msg), target_path_);
        QMessageBox::warning(this, tr("Benchmark"), msg);
        return;
    }
    if (auto s = b->start(); !s) {
        emit logEvent(Severity::error, i18n::err(s.error()), target_path_);
        return;
    }
    const auto& ec = b->config();
    emit logEvent(Severity::info,
                  tr("%1 started (block %2, QD %3, %4)")
                      .arg(i18n::ct(bench_kind_title(ec.kind)), size_text(ec.block_bytes))
                      .arg(ec.queue_depth)
                      .arg(ec.direct_io ? tr("direct I/O") : tr("buffered I/O")),
                  target_path_);
    bench_ = std::move(b);
    shown_.reset();
    compare_.reset();
    timer_->start(250);
    updateButtons();
    poll();
}

void BenchmarkTab::onStop() {
    if (!isRunning()) return;
    bench_->cancel();
    status_->setText(tr("Stopping..."));
}

void BenchmarkTab::poll() {
    if (!bench_) return;
    const auto r = bench_->result();
    const auto p = bench_->progress();
    progress_->setValue(static_cast<int>(bench_->fraction() * 1000));
    status_->setText(tr("%1 on %2 ...  %3 %   %4 MB/s")
                         .arg(i18n::ct(bench_kind_title(r.config.kind)), target_label_)
                         .arg(bench_->fraction() * 100.0, 0, 'f', 1)
                         .arg(p.current_bytes_per_sec / 1e6, 0, 'f', 1));
    showResult(r);
    if (is_terminal(p.state)) finish();
}

void BenchmarkTab::finish() {
    timer_->stop();
    const auto r = bench_->result();
    shown_ = r;
    QString summary = tr("%1: %2").arg(i18n::ct(operation_state_name(r.state)), QString::fromStdString(r.headline()));
    if (r.state != OperationState::completed && !r.message.empty())
        summary += QStringLiteral(" (") + i18n::ct(r.message) + QStringLiteral(")");
    status_->setText(summary);
    emit logEvent(r.state == OperationState::completed ? Severity::info : Severity::warning,
                  tr("%1 finished: %2").arg(i18n::ct(bench_kind_title(r.config.kind)), summary), target_path_);
    if (r.state == OperationState::completed && history_ && record_) {
        auto id = history_->record_benchmark(r);
        if (id) hist_status_->setText(tr("Saved as #%1").arg(*id));
        else emit logEvent(Severity::warning, tr("Benchmark not saved to history: %1")
                                                  .arg(i18n::err(id.error())), target_path_);
    }
    bench_.reset();
    progress_->setValue(1000);
    refreshHistory();
    showResult(*shown_);
    updateButtons();
    emit operationFinished();
}

// ------------------------------------------------------------------ display

void BenchmarkTab::showResult(const BenchResult& r) {
    headline_->setText(QStringLiteral("%1:  %2").arg(i18n::ct(bench_kind_title(r.config.kind)), QString::fromStdString(r.headline())));
    phases_->removeRows(0, phases_->rowCount());
    for (const auto& ph : r.phases) {
        const auto& m = ph.metrics;
        auto* label = cell(i18n::ct(ph.label));
        auto* err = num(static_cast<double>(m.errors), 0);
        if (m.errors) err->setForeground(QColor(0xC0, 0, 0));
        phases_->appendRow({label, cell(size_text(ph.block_bytes), true), num(ph.queue_depth, 0), num(m.mb_per_s, 1),
                            num(m.mib_per_s, 1), num(m.iops, 0), num(m.lat_mean_us / 1000.0, 3),
                            num(static_cast<double>(m.lat_median_us) / 1000.0, 3),
                            num(static_cast<double>(m.lat_p95_us) / 1000.0, 3),
                            num(static_cast<double>(m.lat_p99_us) / 1000.0, 3),
                            num(static_cast<double>(m.lat_max_us) / 1000.0, 3), err});
    }
    phases_view_->resizeColumnsToContents();
    caveats_->clear();
    if (r.warnings.empty()) caveats_->addItem(tr("No caveats."));
    for (const auto& w : r.warnings) caveats_->addItem(QStringLiteral("! ") + i18n::ct(w));

    // Chart choices depend on the kind.
    const auto prev = chart_kind_->currentData().toInt();
    const QSignalBlocker block(chart_kind_);
    chart_kind_->clear();
    switch (r.config.kind) {
        case BenchKind::sequential:
        case BenchKind::sequential_write:
            chart_kind_->addItem(tr("Throughput vs time"), kTimeRate);
            chart_kind_->addItem(tr("Throughput vs position"), kPosition);
            break;
        case BenchKind::random:
        case BenchKind::random_write:
        case BenchKind::access_time: chart_kind_->addItem(tr("IOPS vs time"), kTimeIops); break;
        case BenchKind::speed_curve: chart_kind_->addItem(tr("Read speed vs position"), kPosition); break;
        case BenchKind::block_sizes:
            chart_kind_->addItem(tr("Throughput by block size"), kSizeRate);
            chart_kind_->addItem(tr("IOPS by block size"), kSizeIops);
            break;
        case BenchKind::queue_depths:
            chart_kind_->addItem(tr("IOPS by queue depth"), kQdIops);
            chart_kind_->addItem(tr("Mean latency by queue depth"), kQdLatency);
            break;
    }
    chart_kind_->addItem(tr("Latency histogram"), kHistogram);
    for (int i = 0; i < chart_kind_->count(); ++i)
        if (chart_kind_->itemData(i).toInt() == prev) chart_kind_->setCurrentIndex(i);
    refreshChart();
}

void BenchmarkTab::refreshChart() {
    const BenchResult* r = nullptr;
    std::optional<BenchResult> live;
    if (bench_) {
        live = bench_->result();
        r = &*live;
    } else if (shown_) {
        r = &*shown_;
    }
    if (!r) {
        chart_->clear(tr("Run a benchmark or load one from History"));
        return;
    }
    const BenchResult* c = compare_ && compare_->config.kind == r->config.kind ? &*compare_ : nullptr;
    const QString clabel = c ? tr("#%1 %2").arg(compare_with_->currentData().toLongLong()).arg(
                                   QString::fromStdString(c->started_utc.substr(0, 10)))
                             : QString();
    const auto kind = static_cast<ChartKind>(chart_kind_->currentData().toInt());

    auto timeline = [](const BenchResult& x, bool iops) {
        std::vector<QPointF> pts;
        const double blk = x.config.block_bytes ? x.config.block_bytes : 1;
        for (const auto& t : x.timeline) pts.emplace_back(t.t_s, iops ? t.bytes_per_sec / blk : t.bytes_per_sec / 1e6);
        return pts;
    };
    auto positions = [](const BenchResult& x) {
        std::vector<QPointF> pts;
        const double cap = static_cast<double>(x.device.capacity_bytes);
        const double ss = x.device.logical_sector_size ? x.device.logical_sector_size : 512;
        for (const auto& p : x.positions)
            pts.emplace_back(cap > 0 ? static_cast<double>(p.lba) * ss / cap * 100.0 : static_cast<double>(p.lba),
                             p.bytes_per_sec / 1e6);
        return pts;
    };
    auto bars = [](const BenchResult& x, auto proj) {
        std::vector<std::pair<QString, double>> b;
        for (const auto& p : x.phases) b.emplace_back(i18n::ct(p.label), proj(p.metrics));
        return b;
    };

    switch (kind) {
        case kTimeRate:
            chart_->setLine(tr("Throughput over time"), {tr("seconds"), QStringLiteral("%.1f")},
                            {tr("MB/s"), QStringLiteral("%.0f")}, timeline(*r, false),
                            c ? timeline(*c, false) : std::vector<QPointF>{}, clabel);
            break;
        case kTimeIops:
            chart_->setLine(tr("IOPS over time"), {tr("seconds"), QStringLiteral("%.1f")}, {tr("IOPS"), QStringLiteral("%.0f")},
                            timeline(*r, true), c ? timeline(*c, true) : std::vector<QPointF>{}, clabel);
            break;
        case kPosition:
            chart_->setLine(tr("Read speed vs position"), {tr("position (% of device)"), QStringLiteral("%.0f"), 0.0, 100.0},
                            {tr("MB/s"), QStringLiteral("%.0f")}, positions(*r),
                            c ? positions(*c) : std::vector<QPointF>{}, clabel);
            break;
        case kSizeRate:
            chart_->setBars(tr("Throughput by block size"), {tr("MB/s"), QStringLiteral("%.0f")},
                            bars(*r, [](const Metrics& m) { return m.mb_per_s; }),
                            c ? bars(*c, [](const Metrics& m) { return m.mb_per_s; }) : decltype(bars(*r, [](const Metrics&) { return 0.0; })){},
                            clabel);
            break;
        case kSizeIops:
        case kQdIops: {
            auto proj = [](const Metrics& m) { return m.iops; };
            chart_->setBars(kind == kSizeIops ? tr("IOPS by block size") : tr("IOPS by queue depth"),
                            {tr("IOPS"), QStringLiteral("%.0f")}, bars(*r, proj),
                            c ? bars(*c, proj) : decltype(bars(*r, proj)){}, clabel);
            break;
        }
        case kQdLatency: {
            auto proj = [](const Metrics& m) { return m.lat_mean_us / 1000.0; };
            chart_->setBars(tr("Mean latency by queue depth"), {tr("ms"), QStringLiteral("%.2f")}, bars(*r, proj),
                            c ? bars(*c, proj) : decltype(bars(*r, proj)){}, clabel);
            break;
        }
        case kHistogram: {
            // Collapse fine buckets into octaves for display; values in % of requests.
            auto octaves = [](const BenchResult& x) {
                std::map<int, std::uint64_t> m;
                std::uint64_t total = 0;
                for (const auto& [up, n] : x.latency_buckets) {
                    const int oct = up == 0 ? 0 : static_cast<int>(std::ceil(std::log2(static_cast<double>(up) + 1)));
                    m[oct] += n;
                    total += n;
                }
                std::vector<std::pair<QString, double>> b;
                for (const auto& [oct, n] : m) {
                    const double upper_us = std::pow(2.0, oct);
                    const QString lab = upper_us >= 1000 ? QStringLiteral("<%1ms").arg(upper_us / 1000.0, 0, 'g', 3)
                                                         : QStringLiteral("<%1us").arg(upper_us, 0, 'g', 3);
                    b.emplace_back(lab, total ? 100.0 * static_cast<double>(n) / static_cast<double>(total) : 0.0);
                }
                return b;
            };
            chart_->setBars(tr("Latency distribution"), {tr("% of requests"), QStringLiteral("%.1f")}, octaves(*r),
                            c ? octaves(*c) : std::vector<std::pair<QString, double>>{}, clabel);
            break;
        }
    }
}

// ------------------------------------------------------------------ history

void BenchmarkTab::refreshHistory() {
    hist_model_->removeRows(0, hist_model_->rowCount());
    const QSignalBlocker block(compare_with_);
    const auto keep = compare_with_->currentData().toLongLong();
    compare_with_->clear();
    compare_with_->addItem(tr("(none)"), 0);
    if (!history_) {
        hist_status_->setText(tr("History is disabled."));
        compare_with_->setEnabled(false);
        return;
    }
    auto rows = history_->list(storage::SessionKind::benchmark, target_key_, 500);
    if (!rows) {
        hist_status_->setText(i18n::err(rows.error()));
        return;
    }
    const auto kind = std::string(bench_kind_name(static_cast<BenchKind>(std::max(0, kind_->currentData().toInt()))));
    for (const auto& r : *rows) {
        auto* id = cell(QString::number(r.id), true);
        id->setData(static_cast<qlonglong>(r.id), Qt::UserRole);
        hist_model_->appendRow({id, cell(QString::fromStdString(r.started_utc)), cell(QString::fromStdString(r.subtype)),
                                cell(QString::fromStdString(r.headline)), cell(QString::fromStdString(r.state))});
        if (r.subtype == kind)
            compare_with_->addItem(QStringLiteral("#%1  %2  %3")
                                       .arg(r.id)
                                       .arg(QString::fromStdString(r.started_utc.substr(0, 16)),
                                            QString::fromStdString(r.headline)),
                                   static_cast<qlonglong>(r.id));
    }
    for (int i = 0; i < compare_with_->count(); ++i)
        if (compare_with_->itemData(i).toLongLong() == keep) compare_with_->setCurrentIndex(i);
    compare_with_->setEnabled(compare_with_->count() > 1);
    hist_view_->resizeColumnsToContents();
    if (hist_status_->text().isEmpty() || rows->empty())
        hist_status_->setText(tr("%n saved session(s) for this device", nullptr, static_cast<int>(rows->size())));
}

void BenchmarkTab::onHistoryLoad() {
    if (!history_) return;
    const auto sel = hist_view_->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    const auto id = hist_model_->item(sel.front().row(), 0)->data(Qt::UserRole).toLongLong();
    auto r = history_->load_benchmark(id);
    if (!r) {
        QMessageBox::warning(this, tr("History"), i18n::err(r.error()));
        return;
    }
    shown_ = std::move(r).value();
    selectKind(static_cast<int>(shown_->config.kind));
    status_->setText(tr("Viewing saved session #%1 from %2").arg(id).arg(QString::fromStdString(shown_->started_utc)));
    showResult(*shown_);
    updateButtons();
}

void BenchmarkTab::onHistoryDelete() {
    if (!history_) return;
    const auto sel = hist_view_->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    const auto id = hist_model_->item(sel.front().row(), 0)->data(Qt::UserRole).toLongLong();
    if (QMessageBox::question(this, tr("Delete Session"), tr("Delete saved benchmark session #%1?").arg(id),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (auto s = history_->remove(storage::SessionKind::benchmark, id); !s)
        QMessageBox::warning(this, tr("History"), i18n::err(s.error()));
    refreshHistory();
}

void BenchmarkTab::onExport() {
    if (!shown_) return;
    QSettings s;
    const QString json_f = tr("platter benchmark, JSON (*.json)"), txt_f = tr("Text report (*.txt)");
    QString chosen;
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Export Benchmark"),
        s.value(QStringLiteral("paths/bench")).toString() + QLatin1Char('/') +
            QString::fromStdString((shown_->device.name.empty() ? std::string("device") : shown_->device.name) + "-" +
                                   std::string(bench_kind_name(shown_->config.kind))),
        json_f + QStringLiteral(";;") + txt_f, &chosen);
    if (path.isEmpty()) return;
    s.setValue(QStringLiteral("paths/bench"), QFileInfo(path).absolutePath());
    const bool text = chosen == txt_f || path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive);
    QString final_path = path;
    const QString ext = text ? QStringLiteral(".txt") : QStringLiteral(".json");
    if (!final_path.endsWith(ext, Qt::CaseInsensitive)) final_path += ext;
    const auto content = text ? reports::bench_to_text(*shown_) : reports::bench_to_json(*shown_);
    QFile f(final_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        f.write(content.data(), static_cast<qint64>(content.size())) < 0) {
        QMessageBox::warning(this, tr("Export Benchmark"), tr("Cannot write %1:\n%2").arg(final_path, f.errorString()));
        return;
    }
    emit logEvent(Severity::info, tr("Benchmark exported to %1 (serial numbers omitted)").arg(final_path),
                  QString::fromStdString(shown_->device.path));
}

}  // namespace platter::gui
