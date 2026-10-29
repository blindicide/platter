// SPDX-License-Identifier: TBD
#include "../i18n/i18n.hpp"
#include "erase_tab.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "../widgets/confirm_dialog.hpp"
#include "../widgets/partition_pick_button.hpp"
#include "platter/reports/erase_report.hpp"
#include "platter/storage/history.hpp"

namespace platter::gui {

namespace {
}  // namespace

EraseTab::EraseTab(QWidget* parent) : QWidget(parent), timer_(new QTimer(this)) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 4, 4, 4);

    auto* row = new QHBoxLayout;
    start_ = new QPushButton(tr("&Erase..."), this);
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

    auto* box = new QGroupBox(tr("Erase Settings"), split);
    auto* f = new QFormLayout(box);
    f->setContentsMargins(6, 4, 6, 4);
    f->setVerticalSpacing(3);
    f->setLabelAlignment(Qt::AlignRight);
    method_ = new QComboBox(box);
    for (int i = 0; i < erase::kMethodCount; ++i)
        method_->addItem(i18n::ct(erase::method_label(static_cast<erase::Method>(i))), i);
    f->addRow(tr("Method:"), method_);
    passes_ = new QSpinBox(box);
    passes_->setRange(1, 99);
    passes_->setValue(3);
    f->addRow(tr("Random passes:"), passes_);
    pattern_ = new QLineEdit(box);
    pattern_->setPlaceholderText(tr("hex bytes, e.g. DEADBEEF"));
    pattern_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("([0-9A-Fa-f]{2}){0,2048}")), box));
    f->addRow(tr("Pattern:"), pattern_);
    verify_ = new QComboBox(box);
    verify_->addItem(tr("Read back last pass"), static_cast<int>(erase::VerifyMode::last_pass));
    verify_->addItem(tr("Read back every pass"), static_cast<int>(erase::VerifyMode::every_pass));
    verify_->addItem(tr("Do not verify"), static_cast<int>(erase::VerifyMode::none));
    f->addRow(tr("Verification:"), verify_);
    auto* digits = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,20}")), box);
    start_lba_ = new QLineEdit(box);
    start_lba_->setValidator(digits);
    start_lba_->setPlaceholderText(tr("0 (whole device)"));
    end_lba_ = new QLineEdit(box);
    end_lba_->setValidator(digits);
    end_lba_->setPlaceholderText(tr("end of device"));
    f->addRow(tr("Start LBA:"), start_lba_);
    f->addRow(tr("End LBA:"), end_lba_);
    pick_ = new PartitionPickButton(start_lba_, end_lba_, QString(), box);
    f->addRow(QString(), pick_);
    auto* note = new QLabel(tr("Leave both LBAs blank to erase the WHOLE device. Overwriting cannot reach blocks the "
                               "drive has remapped (flash wear levelling, reallocated sectors); the report says so. "
                               "Hardware secure erase / sanitize is not offered."),
                            box);
    note->setObjectName(QStringLiteral("placeholder"));
    note->setWordWrap(true);
    f->addRow(QString(), note);
    box->setMaximumWidth(i18n::scale(270));

    report_ = new QPlainTextEdit(split);
    report_->setReadOnly(true);
    report_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    report_->setLineWrapMode(QPlainTextEdit::NoWrap);
    report_->setPlaceholderText(tr("The erase report appears here when the operation ends."));

    split->addWidget(box);
    split->addWidget(report_);
    split->setStretchFactor(1, 1);
    col->addWidget(split, 1);

    connect(start_, &QPushButton::clicked, this, &EraseTab::onStart);
    connect(stop_, &QPushButton::clicked, this, &EraseTab::onStop);
    connect(save_, &QPushButton::clicked, this, &EraseTab::onSaveReport);
    connect(method_, qOverload<int>(&QComboBox::currentIndexChanged), this, &EraseTab::onMethodChanged);
    connect(timer_, &QTimer::timeout, this, &EraseTab::poll);
    onMethodChanged();
    updateButtons();
}

EraseTab::~EraseTab() { stopAndWait(); }

bool EraseTab::isRunning() const { return op_ && !is_terminal(op_->state()); }

void EraseTab::stopAndWait() {
    timer_->stop();
    if (op_) {
        op_->cancel();
        op_->wait();
    }
}

void EraseTab::setHistory(storage::HistoryStore* store, bool record) {
    history_ = store;
    record_ = record;
}

void EraseTab::setDevice(const DeviceEntry* e) {
    if (isRunning()) return;
    have_target_ = e != nullptr;
    if (!e) {
        target_path_.clear();
        target_label_.clear();
        pick_->setDevice(nullptr);
        status_->setText(tr("No device selected."));
    } else {
        target_path_ = QString::fromStdString(e->info.path);
        target_label_ = e->displayName();
        target_is_image_ = e->is_image;
        target_sector_ = e->is_image ? e->image_sector_size : e->info.logical_sector_size;
        target_lbas_ = e->info.total_lbas();
        pick_->setDevice(e);
        end_lba_->setPlaceholderText(target_lbas_ ? tr("end (%1)").arg(target_lbas_ - 1) : tr("end of device"));
        status_->setText(tr("Ready. Nothing is written until you confirm."));
    }
    updateButtons();
}

void EraseTab::onMethodChanged() {
    const auto m = static_cast<erase::Method>(method_->currentData().toInt());
    const bool idle = !isRunning();
    passes_->setEnabled(idle && m == erase::Method::random_n);
    pattern_->setEnabled(idle && m == erase::Method::pattern);
}

void EraseTab::setSettingsEnabled(bool on) {
    for (QWidget* w : std::initializer_list<QWidget*>{method_, verify_, start_lba_, end_lba_, pick_}) w->setEnabled(on);
    if (on) onMethodChanged();
    else {
        passes_->setEnabled(false);
        pattern_->setEnabled(false);
    }
}

void EraseTab::updateButtons() {
    const bool running = isRunning();
    start_->setEnabled(!running && have_target_);
    stop_->setEnabled(running);
    save_->setEnabled(!running && last_.has_value());
    setSettingsEnabled(!running);
}

std::optional<erase::EraseConfig> EraseTab::readConfig(QString& error) const {
    erase::EraseConfig c;
    c.method = static_cast<erase::Method>(method_->currentData().toInt());
    c.random_passes = static_cast<std::uint32_t>(passes_->value());
    c.verify = static_cast<erase::VerifyMode>(verify_->currentData().toInt());
    if (c.method == erase::Method::pattern) {
        const auto hex = pattern_->text();
        if (hex.isEmpty() || hex.size() % 2 != 0) {
            error = tr("Enter the pattern as an even number of hex digits.");
            return std::nullopt;
        }
        for (int i = 0; i + 1 < hex.size(); i += 2) c.pattern.push_back(static_cast<std::uint8_t>(hex.mid(i, 2).toUInt(nullptr, 16)));
    }
    return c;
}

void EraseTab::onStart() {
    if (isRunning() || !have_target_) return;
    QString error;
    auto cfg = readConfig(error);
    if (!cfg) {
        QMessageBox::warning(this, tr("Erase"), error);
        return;
    }
    safety::Request req;
    req.kind = safety::Kind::erase;
    req.target.path = target_path_.toStdString();
    req.target.is_image = target_is_image_;
    req.target.image_sector_size = target_sector_;
    if (!start_lba_->text().isEmpty() || !end_lba_->text().isEmpty()) {
        if (end_lba_->text().isEmpty()) {
            QMessageBox::warning(this, tr("Erase"), tr("Enter an End LBA, or clear Start LBA to erase the whole device."));
            return;
        }
        const auto first = start_lba_->text().isEmpty() ? 0ull : start_lba_->text().toULongLong();
        const auto last = end_lba_->text().toULongLong();
        if (last < first) {
            QMessageBox::warning(this, tr("Erase"), tr("End LBA must not be smaller than Start LBA."));
            return;
        }
        req.ranges.push_back(LbaRange{first, last - first + 1});
    }
    std::string summary = std::string(erase::method_label(cfg->method));
    if (cfg->method == erase::Method::random_n) summary = std::to_string(cfg->random_passes) + " passes of random data";
    summary += "; verification: " + std::string(erase::verify_mode_name(cfg->verify));
    req.summary = summary;
    const QString method_text = cfg->method == erase::Method::random_n
                                    ? tr("%n pass(es) of random data", nullptr, static_cast<int>(cfg->random_passes))
                                    : i18n::ct(erase::method_label(cfg->method));
    const QString local_summary =
        tr("%1; verification: %2").arg(method_text, i18n::ct(erase::verify_mode_name(cfg->verify)));

    auto out = confirm_and_authorize(this, tr("Confirm Erase"), req, show_serial_, local_summary);
    if (out.cancelled) return;
    if (!out.target) {
        emit logEvent(Severity::error, tr("Erase refused: %1").arg(out.error), target_path_);
        QMessageBox::warning(this, tr("Erase"), out.error);
        return;
    }
    for (const auto& w : out.target->warnings()) emit logEvent(Severity::warning, i18n::ct(w), target_path_);

    auto op = std::make_unique<erase::Erase>(std::move(*out.target), *cfg);
    if (auto v = op->validate(); !v) {
        const auto msg = i18n::err(v.error());
        emit logEvent(Severity::error, tr("Invalid erase settings: %1").arg(msg), target_path_);
        QMessageBox::warning(this, tr("Erase"), msg);
        return;
    }
    if (auto s = op->start(); !s) {
        emit logEvent(Severity::error, i18n::err(s.error()), target_path_);
        return;
    }
    emit logEvent(Severity::warning, tr("Erase started: %1").arg(local_summary), target_path_);
    op_ = std::move(op);
    last_.reset();
    report_->clear();
    timer_->start(250);
    updateButtons();
    poll();
}

void EraseTab::onStop() {
    if (!isRunning()) return;
    op_->cancel();
    status_->setText(tr("Stopping after the current request..."));
}

void EraseTab::poll() {
    if (!op_) return;
    const auto p = op_->progress();
    const auto [cur, total] = op_->pass_position();
    progress_->setValue(static_cast<int>(p.percent() * 10));
    status_->setText(tr("Pass %1/%2  %3 %  %4 MB/s  errors %5")
                         .arg(cur)
                         .arg(total)
                         .arg(p.percent(), 0, 'f', 1)
                         .arg(p.current_bytes_per_sec / 1e6, 0, 'f', 1)
                         .arg(p.errors));
    if (is_terminal(p.state)) finish();
}

void EraseTab::finish() {
    timer_->stop();
    op_->wait();
    last_ = op_->result();
    const reports::Privacy priv{show_serial_};
    report_->setPlainText(QString::fromStdString(reports::erase_to_text(*last_, priv)));
    const auto verdict = i18n::ct(erase::verdict_name(last_->verdict));
    status_->setText(verdict);
    progress_->setValue(last_->state == OperationState::completed ? 1000 : progress_->value());
    const bool ok = last_->verdict == erase::Verdict::erased_verified || last_->verdict == erase::Verdict::erased_unverified;
    emit logEvent(ok ? Severity::info : Severity::warning, tr("Erase finished: %1").arg(verdict), target_path_);
    if (history_ && record_) {
        if (auto id = history_->record_erase(*last_); !id)
            emit logEvent(Severity::warning, tr("Could not save to history: %1").arg(i18n::err(id.error())),
                          target_path_);
    }
    updateButtons();
}

void EraseTab::onSaveReport() {
    if (!last_) return;
    const auto file = QFileDialog::getSaveFileName(this, tr("Save Erase Report"), QStringLiteral("erase-report.json"),
                                                   tr("JSON report (*.json);;Text report (*.txt)"));
    if (file.isEmpty()) return;
    const reports::Privacy priv{show_serial_};
    const bool json = file.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
    const auto content = json ? reports::erase_to_json(*last_, priv) : reports::erase_to_text(*last_, priv);
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(content.data(), static_cast<qint64>(content.size())) < 0 || !f.commit())
        QMessageBox::warning(this, tr("Save Erase Report"), tr("Cannot write %1:\n%2").arg(file, f.errorString()));
}

}  // namespace platter::gui
