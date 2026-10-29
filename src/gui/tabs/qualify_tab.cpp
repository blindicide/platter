// SPDX-License-Identifier: LicenseRef-Proprietary
#include "../i18n/i18n.hpp"
#include "qualify_tab.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "../widgets/confirm_dialog.hpp"
#include "platter/reports/qualify_report.hpp"
#include "platter/smart/smart.hpp"
#include "platter/storage/history.hpp"

namespace platter::gui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

struct QualifyTab::SmartJob {
    std::thread worker;
    std::atomic<bool> done{false};
    qualify::SmartSnapshot snapshot;
    ~SmartJob() {
        if (worker.joinable()) worker.join();
    }
};

QualifyTab::QualifyTab(QWidget* parent) : QWidget(parent), timer_(new QTimer(this)) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 4, 4, 4);

    auto* row = new QHBoxLayout;
    start_ = new QPushButton(tr("&Qualify..."), this);
    stop_ = new QPushButton(tr("S&top"), this);
    save_ = new QPushButton(tr("&Save Report..."), this);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1000);
    progress_->setMaximumWidth(220);
    status_ = new QLabel(this);
    row->addWidget(start_);
    row->addWidget(stop_);
    row->addWidget(save_);
    row->addSpacing(8);
    row->addWidget(progress_);
    row->addWidget(status_, 1);
    col->addLayout(row);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);

    auto* box = new QGroupBox(tr("Qualification Settings"), split);
    auto* f = new QFormLayout(box);
    f->setContentsMargins(6, 4, 6, 4);
    f->setVerticalSpacing(3);
    passes_ = new QSpinBox(box);
    passes_->setRange(1, static_cast<int>(qualify::kMaxWritePasses));
    passes_->setValue(2);
    passes_->setToolTip(tr("Pass 1 writes a random stream, pass 2 its complement, pass 3 a new stream, pass 4 its complement"));
    f->addRow(tr("Write passes:"), passes_);
    verify_ = new QCheckBox(tr("Read back after every pass"), box);
    verify_->setChecked(true);
    f->addRow(verify_);
    final_read_ = new QCheckBox(tr("Final sequential read"), box);
    final_read_->setChecked(true);
    f->addRow(final_read_);
    samples_ = new QSpinBox(box);
    samples_->setRange(0, 100000);
    samples_->setValue(256);
    samples_->setSpecialValueText(tr("off"));
    f->addRow(tr("Random reads:"), samples_);
    slow_ms_ = new QSpinBox(box);
    slow_ms_->setRange(1, 60000);
    slow_ms_->setValue(200);
    slow_ms_->setSuffix(tr(" ms"));
    slow_ms_->setToolTip(tr("A 1 MiB request taking at least this long counts as slow"));
    f->addRow(tr("Slow block at:"), slow_ms_);
    auto* note = new QLabel(tr("Qualification OVERWRITES THE WHOLE DEVICE, several times. Use it on a drive you "
                               "are about to put into service, not on one holding data. The verdict lists every "
                               "reason; it is an observation of this run, not a prediction."),
                            box);
    note->setObjectName(QStringLiteral("placeholder"));
    note->setWordWrap(true);
    f->addRow(note);
    box->setMaximumWidth(i18n::scale(270));

    auto* right = new QSplitter(Qt::Vertical, split);
    stages_ = new QTableWidget(0, 7, right);
    stages_->setHorizontalHeaderLabels({tr("Stage"), tr("Done"), tr("Avg"), tr("Min / p99"), tr("Slow"), tr("Errors"), tr("Mismatch")});
    stages_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stages_->setSelectionBehavior(QAbstractItemView::SelectRows);
    stages_->verticalHeader()->setVisible(false);
    stages_->horizontalHeader()->setStretchLastSection(true);
    stages_->setWordWrap(false);
    report_ = new QPlainTextEdit(right);
    report_->setReadOnly(true);
    report_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    report_->setLineWrapMode(QPlainTextEdit::NoWrap);
    report_->setPlaceholderText(tr("The qualification report appears here when the run ends."));
    right->addWidget(stages_);
    right->addWidget(report_);
    right->setStretchFactor(1, 1);

    split->addWidget(box);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    col->addWidget(split, 1);

    connect(start_, &QPushButton::clicked, this, &QualifyTab::onStart);
    connect(stop_, &QPushButton::clicked, this, &QualifyTab::onStop);
    connect(save_, &QPushButton::clicked, this, &QualifyTab::onSaveReport);
    connect(timer_, &QTimer::timeout, this, &QualifyTab::poll);
    updateButtons();
}

QualifyTab::~QualifyTab() { stopAndWait(); }

bool QualifyTab::isRunning() const { return (op_ && !is_terminal(op_->state())) || smart_job_; }

void QualifyTab::stopAndWait() {
    timer_->stop();
    if (op_) {
        op_->cancel();
        op_->wait();
    }
    smart_job_.reset();
}

void QualifyTab::setHistory(storage::HistoryStore* store, bool record) {
    history_ = store;
    record_ = record;
}

void QualifyTab::setDevice(const DeviceEntry* e) {
    if (isRunning()) return;
    have_target_ = e != nullptr;
    smart_before_ = {};
    if (!e) {
        target_path_.clear();
        target_label_.clear();
        status_->setText(tr("No device selected."));
    } else {
        target_path_ = qs(e->info.path);
        target_label_ = e->displayName();
        target_is_image_ = e->is_image;
        target_sector_ = e->is_image ? e->image_sector_size : e->info.logical_sector_size;
        target_lbas_ = e->info.total_lbas();
        if (e->smart) smart_before_ = qualify::smart_snapshot(*e->smart);
        status_->setText(smart_before_.valid ? tr("Ready. The SMART data already read for this device is the baseline.")
                                             : tr("Ready. Read SMART first (SMART tab) to include a before/after comparison."));
    }
    updateButtons();
}

void QualifyTab::setSettingsEnabled(bool on) {
    for (QWidget* w : std::initializer_list<QWidget*>{passes_, verify_, final_read_, samples_, slow_ms_}) w->setEnabled(on);
}

void QualifyTab::updateButtons() {
    const bool running = isRunning();
    start_->setEnabled(!running && have_target_);
    stop_->setEnabled(running && op_ && !is_terminal(op_->state()));
    save_->setEnabled(!running && last_.has_value());
    setSettingsEnabled(!running);
}

void QualifyTab::onStart() {
    if (isRunning() || !have_target_) return;
    qualify::QualifyConfig cfg;
    cfg.write_passes = static_cast<std::uint32_t>(passes_->value());
    cfg.verify_each_pass = verify_->isChecked();
    cfg.final_read = final_read_->isChecked();
    cfg.seek_samples = static_cast<std::uint32_t>(samples_->value());
    cfg.slow_block_ms = static_cast<std::uint32_t>(slow_ms_->value());

    safety::Request req;
    req.kind = safety::Kind::qualification;
    req.target.path = target_path_.toStdString();
    req.target.is_image = target_is_image_;
    req.target.image_sector_size = target_sector_;
    std::string summary = std::to_string(cfg.write_passes) + " write pass(es)";
    if (cfg.verify_each_pass) summary += " with read-back";
    if (cfg.final_read) summary += ", final read";
    if (cfg.seek_samples) summary += ", " + std::to_string(cfg.seek_samples) + " random reads";
    req.summary = summary;
    QString local_summary = tr("%n write pass(es)", nullptr, static_cast<int>(cfg.write_passes));
    if (cfg.verify_each_pass) local_summary += tr(" with read-back");
    if (cfg.final_read) local_summary += tr(", final read");
    if (cfg.seek_samples) local_summary += tr(", %n random read(s)", nullptr, static_cast<int>(cfg.seek_samples));

    auto out = confirm_and_authorize(this, tr("Confirm Qualification"), req, show_serial_, local_summary);
    if (out.cancelled) return;
    if (!out.target) {
        emit logEvent(Severity::error, tr("Qualification refused: %1").arg(out.error), target_path_);
        QMessageBox::warning(this, tr("Qualification"), out.error);
        return;
    }
    for (const auto& w : out.target->warnings()) emit logEvent(Severity::warning, i18n::ct(w), target_path_);

    auto op = std::make_unique<qualify::Qualification>(std::move(*out.target), cfg);
    if (auto v = op->validate(); !v) {
        const auto msg = i18n::err(v.error());
        emit logEvent(Severity::error, tr("Invalid qualification settings: %1").arg(msg), target_path_);
        QMessageBox::warning(this, tr("Qualification"), msg);
        return;
    }
    op->set_smart_before(smart_before_);
    if (auto s = op->start(); !s) {
        emit logEvent(Severity::error, i18n::err(s.error()), target_path_);
        return;
    }
    emit logEvent(Severity::warning, tr("Qualification started: %1").arg(local_summary), target_path_);
    op_ = std::move(op);
    last_.reset();
    report_->clear();
    stages_->setRowCount(0);
    timer_->start(250);
    updateButtons();
    poll();
}

void QualifyTab::onStop() {
    if (!op_ || is_terminal(op_->state())) return;
    op_->cancel();
    status_->setText(tr("Stopping after the current request..."));
}

void QualifyTab::refreshStages() {
    if (!op_) return;
    const auto r = op_->result();
    stages_->setRowCount(static_cast<int>(r.stages.size()));
    int row = 0;
    for (const auto& s : r.stages) {
        auto add = [&](int c, const QString& t) { stages_->setItem(row, c, new QTableWidgetItem(t)); };
        add(0, i18n::ct(s.name));
        add(1, s.completed ? tr("yes") : (s.blocks ? tr("running") : QString()));
        if (s.kind == qualify::StageKind::seek) {
            add(2, s.blocks ? tr("p50 %1 ms").arg(static_cast<double>(s.median_us) / 1000.0, 0, 'f', 2) : QString());
            add(3, s.blocks ? tr("p99 %1 ms").arg(static_cast<double>(s.p99_us) / 1000.0, 0, 'f', 2) : QString());
        } else {
            add(2, s.avg_mbps > 0 ? tr("%1 MB/s").arg(s.avg_mbps, 0, 'f', 1) : QString());
            add(3, s.avg_mbps > 0 ? tr("%1 MB/s").arg(s.min_mbps, 0, 'f', 1) : QString());
        }
        add(4, QString::number(s.slow_blocks));
        add(5, QString::number(s.io_errors));
        add(6, QString::number(s.mismatches));
        ++row;
    }
    stages_->resizeColumnsToContents();
}

void QualifyTab::poll() {
    if (smart_job_) {
        if (smart_job_->done.load()) finishReport();
        return;
    }
    if (!op_) return;
    const auto p = op_->progress();
    const auto [cur, total] = op_->stage_position();
    progress_->setValue(static_cast<int>(p.percent() * 10));
    status_->setText(tr("Stage %1/%2  %3 %  %4 MB/s  errors %5")
                         .arg(cur)
                         .arg(total)
                         .arg(p.percent(), 0, 'f', 1)
                         .arg(p.current_bytes_per_sec / 1e6, 0, 'f', 1)
                         .arg(p.errors));
    refreshStages();
    if (is_terminal(p.state)) finishRun();
}

// The operation has ended. Read SMART again (off the GUI thread) so the
// verdict can include the before/after comparison, then build the report.
void QualifyTab::finishRun() {
    op_->wait();
    if (smart_before_.valid && !target_is_image_) {
        status_->setText(tr("Reading SMART again..."));
        auto job = std::make_shared<SmartJob>();
        const std::string path = target_path_.toStdString();
        job->worker = std::thread([job, path] {
            auto r = smart::query(path);
            if (r) job->snapshot = qualify::smart_snapshot(*r);
            job->done = true;
        });
        smart_job_ = job;
        return;
    }
    finishReport();
}

void QualifyTab::finishReport() {
    timer_->stop();
    if (smart_job_) {
        if (smart_job_->worker.joinable()) smart_job_->worker.join();
        op_->set_smart_after(smart_job_->snapshot);
        smart_job_.reset();
    }
    last_ = op_->result();
    refreshStages();
    report_->setPlainText(qs(qualify::result_to_text(*last_, show_serial_)));
    const auto verdict = i18n::ct(qualify::verdict_name(last_->verdict));
    status_->setText(verdict);
    progress_->setValue(last_->state == OperationState::completed ? 1000 : progress_->value());
    const bool ok = last_->verdict == qualify::Verdict::pass || last_->verdict == qualify::Verdict::pass_with_warnings;
    emit logEvent(ok ? Severity::info : Severity::warning, tr("Qualification finished: %1").arg(verdict), target_path_);
    if (history_ && record_) {
        if (auto id = history_->record_qualification(*last_); !id)
            emit logEvent(Severity::warning, tr("Could not save to history: %1").arg(i18n::err(id.error())), target_path_);
    }
    updateButtons();
}

void QualifyTab::onSaveReport() {
    if (!last_) return;
    const auto file = QFileDialog::getSaveFileName(this, tr("Save Qualification Report"), QStringLiteral("qualification-report.json"),
                                                   tr("JSON report (*.json);;Text report (*.txt)"));
    if (file.isEmpty()) return;
    const reports::Privacy priv{show_serial_};
    const bool json = file.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
    const auto content = json ? reports::qualify_to_json(*last_, priv) : qualify::result_to_text(*last_, show_serial_);
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(content.data(), static_cast<qint64>(content.size())) < 0 || !f.commit())
        QMessageBox::warning(this, tr("Save Qualification Report"), tr("Cannot write %1:\n%2").arg(file, f.errorString()));
}

}  // namespace platter::gui
