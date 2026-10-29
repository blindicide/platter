// SPDX-License-Identifier: TBD
#include "imaging_tab.hpp"

#include <QCheckBox>
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
#include "platter/reports/imaging_report.hpp"
#include "platter/storage/history.hpp"

namespace platter::gui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

ImagingTab::ImagingTab(QWidget* parent) : QWidget(parent), timer_(new QTimer(this)) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 4, 4, 4);

    auto* row = new QHBoxLayout;
    mode_ = new QComboBox(this);
    mode_->addItem(tr("Create image from the selected device"), Create);
    mode_->addItem(tr("Restore an image onto the selected device"), Restore);
    mode_->addItem(tr("Clone the selected device onto another device"), Clone);
    mode_->addItem(tr("Verify an image"), Verify);
    start_ = new QPushButton(tr("&Start..."), this);
    stop_ = new QPushButton(tr("S&top"), this);
    save_ = new QPushButton(tr("&Save Report..."), this);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1000);
    progress_->setMaximumWidth(200);
    status_ = new QLabel(this);
    row->addWidget(mode_);
    row->addWidget(start_);
    row->addWidget(stop_);
    row->addWidget(save_);
    row->addWidget(progress_);
    row->addWidget(status_, 1);
    col->addLayout(row);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    auto* box = new QGroupBox(tr("Settings"), split);
    auto* f = new QFormLayout(box);
    f->setContentsMargins(6, 4, 6, 4);
    f->setVerticalSpacing(3);
    f->setLabelAlignment(Qt::AlignRight);

    auto* filerow = new QHBoxLayout;
    file_ = new QLineEdit(box);
    browse_ = new QPushButton(tr("..."), box);
    browse_->setMaximumWidth(28);
    filerow->addWidget(file_, 1);
    filerow->addWidget(browse_);
    f->addRow(tr("Image file:"), filerow);

    compress_ = new QComboBox(box);
    compress_->addItem(tr("None (raw)"), static_cast<int>(imaging::Compression::none));
    compress_->addItem(tr("zstd"), static_cast<int>(imaging::Compression::zstd));
    if (!imaging::zstd_available()) {
        compress_->setItemData(1, 0, Qt::UserRole - 1);  // disabled
    }
    f->addRow(tr("Compression:"), compress_);
    level_ = new QSpinBox(box);
    level_->setRange(1, 19);
    level_->setValue(3);
    f->addRow(tr("zstd level:"), level_);
    sparse_ = new QCheckBox(tr("Raw: store empty blocks as holes"), box);
    f->addRow(QString(), sparse_);
    force_ = new QCheckBox(tr("Replace an existing file"), box);
    f->addRow(QString(), force_);
    recover_ = new QCheckBox(tr("Recovery mode (skip unreadable sectors)"), box);
    recover_->setToolTip(tr("Unreadable sectors are replaced by zeros and listed in the report."));
    f->addRow(QString(), recover_);
    retries_ = new QSpinBox(box);
    retries_->setRange(0, 16);
    retries_->setValue(2);
    f->addRow(tr("Retries:"), retries_);
    verify_ = new QCheckBox(tr("Verify afterwards (read back and compare)"), box);
    verify_->setChecked(true);
    f->addRow(QString(), verify_);

    compare_ = new QCheckBox(tr("Also compare with the selected device"), box);
    f->addRow(QString(), compare_);
    other_label_ = new QLabel(tr("Clone onto:"), box);
    other_ = new QComboBox(box);
    other_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    other_->setMinimumContentsLength(18);
    f->addRow(other_label_, other_);
    auto* digits = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,20}")), box);
    start_lba_ = new QLineEdit(box);
    start_lba_->setValidator(digits);
    start_lba_->setPlaceholderText(tr("0"));
    end_lba_ = new QLineEdit(box);
    end_lba_->setValidator(digits);
    end_lba_->setPlaceholderText(tr("end of device"));
    target_lba_ = new QLineEdit(box);
    target_lba_->setValidator(digits);
    target_lba_->setPlaceholderText(tr("0"));
    f->addRow(tr("Start LBA:"), start_lba_);
    f->addRow(tr("End LBA:"), end_lba_);
    f->addRow(tr("Target LBA:"), target_lba_);
    sha_ = new QLineEdit(box);
    sha_->setPlaceholderText(tr("optional SHA-256 (64 hex digits)"));
    sha_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{0,64}")), box));
    f->addRow(tr("Expected SHA-256:"), sha_);
    auto* note = new QLabel(tr("Reading a failing drive stresses it. A recovery copy replaces unreadable sectors with "
                               "zeros; it is not a substitute for a specialised recovery tool."),
                            box);
    note->setObjectName(QStringLiteral("placeholder"));
    note->setWordWrap(true);
    f->addRow(QString(), note);
    box->setMaximumWidth(360);

    report_ = new QPlainTextEdit(split);
    report_->setReadOnly(true);
    report_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    report_->setLineWrapMode(QPlainTextEdit::NoWrap);
    report_->setPlaceholderText(tr("The report appears here when the operation ends."));
    split->addWidget(box);
    split->addWidget(report_);
    split->setStretchFactor(1, 1);
    col->addWidget(split, 1);

    connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this, &ImagingTab::onModeChanged);
    connect(browse_, &QPushButton::clicked, this, &ImagingTab::onBrowse);
    connect(start_, &QPushButton::clicked, this, &ImagingTab::onStart);
    connect(stop_, &QPushButton::clicked, this, &ImagingTab::onStop);
    connect(save_, &QPushButton::clicked, this, &ImagingTab::onSaveReport);
    connect(timer_, &QTimer::timeout, this, &ImagingTab::poll);
    onModeChanged();
}

ImagingTab::~ImagingTab() { stopAndWait(); }

bool ImagingTab::isRunning() const { return op_ && !is_terminal(op_->state()); }

void ImagingTab::stopAndWait() {
    timer_->stop();
    if (op_) {
        op_->cancel();
        op_->wait();
    }
}

void ImagingTab::setHistory(storage::HistoryStore* store, bool record) {
    history_ = store;
    record_ = record;
}

void ImagingTab::setDevice(const DeviceEntry* e) {
    if (isRunning()) return;
    have_dev_ = e != nullptr;
    if (!e) {
        dev_path_.clear();
        dev_label_.clear();
        status_->setText(tr("No device selected."));
    } else {
        dev_path_ = qs(e->info.path);
        dev_label_ = e->displayName();
        dev_is_image_ = e->is_image;
        dev_sector_ = e->is_image ? e->image_sector_size : e->info.logical_sector_size;
        dev_lbas_ = e->info.total_lbas();
        end_lba_->setPlaceholderText(dev_lbas_ ? tr("end (%1)").arg(dev_lbas_ - 1) : tr("end of device"));
        status_->setText(tr("Ready. Restore and clone write nothing until you confirm."));
    }
    updateButtons();
}

void ImagingTab::selectMode(int mode) {
    if (isRunning()) return;
    const int idx = mode_->findData(mode);
    if (idx >= 0) mode_->setCurrentIndex(idx);
}

void ImagingTab::setChoices(const std::vector<DeviceEntry>& devices) {
    const auto keep = other_->currentData().toString();
    choices_.clear();
    other_->clear();
    for (const auto& d : devices) {
        Choice c{qs(d.info.path), d.displayName(), d.is_image, d.is_image ? d.image_sector_size : d.info.logical_sector_size};
        choices_.push_back(c);
        other_->addItem(c.label, c.path);
    }
    const int idx = other_->findData(keep);
    if (idx >= 0) other_->setCurrentIndex(idx);
    updateButtons();
}

const ImagingTab::Choice* ImagingTab::otherChoice() const {
    const int i = other_->currentIndex();
    return i >= 0 && i < static_cast<int>(choices_.size()) ? &choices_[static_cast<std::size_t>(i)] : nullptr;
}

void ImagingTab::onModeChanged() {
    const auto m = static_cast<Mode>(mode_->currentData().toInt());
    other_label_->setText(tr("Clone onto:"));
    start_lba_->setToolTip(m == Verify ? tr("Where the image starts on the comparison device") : tr("First source LBA"));
    start_->setText(m == Create ? tr("&Create...") : m == Restore ? tr("&Restore...") : m == Clone ? tr("&Clone...") : tr("&Verify"));
    updateButtons();
}

void ImagingTab::updateButtons() {
    const bool running = isRunning();
    start_->setEnabled(!running && have_dev_);
    stop_->setEnabled(running);
    save_->setEnabled(!running && last_.has_value());
    mode_->setEnabled(!running);
    if (!running) {
        const auto m = static_cast<Mode>(mode_->currentData().toInt());
        const bool create = m == Create;
        file_->setEnabled(m != Clone);
        browse_->setEnabled(m != Clone);
        compress_->setEnabled(create);
        level_->setEnabled(create);
        sparse_->setEnabled(create);
        force_->setEnabled(create);
        recover_->setEnabled(create || m == Clone);
        retries_->setEnabled(create || m == Clone);
        verify_->setEnabled(m != Verify);
        other_->setEnabled(m == Clone);
        compare_->setEnabled(m == Verify);
        start_lba_->setEnabled(m != Restore);
        end_lba_->setEnabled(create || m == Clone);
        target_lba_->setEnabled(m == Restore || m == Clone);
        sha_->setEnabled(m == Restore || m == Verify);
    } else {
        for (QWidget* w : std::initializer_list<QWidget*>{file_, browse_, compress_, level_, sparse_, force_, recover_, retries_,
                                                         verify_, other_, compare_, start_lba_, end_lba_, target_lba_, sha_})
            w->setEnabled(false);
    }
}

void ImagingTab::onBrowse() {
    const auto m = static_cast<Mode>(mode_->currentData().toInt());
    const auto filter = tr("Disk images (*.img *.img.zst *.zst *.raw *.bin);;All files (*)");
    QString f = m == Create ? QFileDialog::getSaveFileName(this, tr("Image file"), file_->text(), filter)
                            : QFileDialog::getOpenFileName(this, tr("Image file"), file_->text(), filter);
    if (f.isEmpty()) return;
    file_->setText(f);
    if (m == Create && f.endsWith(QStringLiteral(".zst"), Qt::CaseInsensitive) && imaging::zstd_available())
        compress_->setCurrentIndex(1);
}

void ImagingTab::warn(const QString& text) { QMessageBox::warning(this, tr("Clone / Image"), text); }

std::optional<std::uint64_t> ImagingTab::parseLba(QLineEdit* e, const QString& what, bool& ok) {
    ok = true;
    if (e->text().isEmpty()) return std::nullopt;
    bool good = false;
    const auto v = e->text().toULongLong(&good);
    if (!good) {
        ok = false;
        warn(tr("%1 is not a valid LBA.").arg(what));
        return std::nullopt;
    }
    return v;
}

Result<BlockDevicePtr> ImagingTab::openReadOnly(const QString& path, bool is_image, std::uint32_t sector_size) const {
    OpenOptions oo;
    oo.mode = OpenMode::read_only;
    return is_image ? open_image(path.toStdString(), oo, sector_size) : open_device(path.toStdString(), oo);
}

void ImagingTab::onStart() {
    if (isRunning() || !have_dev_) return;
    bool started = false;
    switch (static_cast<Mode>(mode_->currentData().toInt())) {
        case Create: started = startCreate(); break;
        case Restore: started = startRestore(); break;
        case Clone: started = startClone(); break;
        case Verify: started = startVerify(); break;
    }
    static_cast<void>(started);
}

void ImagingTab::begin(std::unique_ptr<Operation> op, std::function<imaging::ImagingResult()> result, const QString& what) {
    if (auto s = op->start(); !s) {
        const auto msg = qs(s.error().describe());
        emit logEvent(Severity::error, tr("%1 could not start: %2").arg(what, msg), dev_path_);
        warn(msg);
        return;
    }
    emit logEvent(Severity::warning, tr("%1 started").arg(what), dev_path_);
    op_ = std::move(op);
    result_fn_ = std::move(result);
    running_what_ = what;
    last_.reset();
    report_->clear();
    timer_->start(250);
    updateButtons();
    poll();
}

bool ImagingTab::startCreate() {
    if (file_->text().trimmed().isEmpty()) {
        warn(tr("Choose the image file to create."));
        return false;
    }
    imaging::ImageConfig c;
    c.output = file_->text().trimmed().toStdString();
    c.compression = static_cast<imaging::Compression>(compress_->currentData().toInt());
    c.level = level_->value();
    c.sparse = sparse_->isChecked();
    c.overwrite = force_->isChecked();
    c.recover = recover_->isChecked();
    c.retries = static_cast<std::uint32_t>(retries_->value());
    c.verify_image = verify_->isChecked();
    bool ok = true;
    if (auto v = parseLba(start_lba_, tr("Start LBA"), ok)) c.start_lba = *v;
    if (!ok) return false;
    if (auto v = parseLba(end_lba_, tr("End LBA"), ok)) c.end_lba = *v;
    if (!ok) return false;
    auto dev = openReadOnly(dev_path_, dev_is_image_, dev_sector_);
    if (!dev) {
        warn(tr("Cannot open the device for reading: %1").arg(qs(dev.error().describe())));
        return false;
    }
    auto op = std::make_unique<imaging::CreateImage>(std::move(*dev), c);
    if (auto v = op->validate(); !v) {
        warn(qs(v.error().describe()));
        return false;
    }
    const auto* raw = op.get();
    begin(std::move(op), [raw] { return raw->result(); }, tr("Image creation"));
    return true;
}

bool ImagingTab::startRestore() {
    const auto file = file_->text().trimmed();
    if (file.isEmpty()) {
        warn(tr("Choose the image file to restore."));
        return false;
    }
    imaging::RestoreConfig c;
    c.input = file.toStdString();
    c.verify = verify_->isChecked();
    c.expect_sha256 = sha_->text().trimmed().toStdString();
    bool ok = true;
    std::uint64_t first = 0;
    if (auto v = parseLba(target_lba_, tr("Target LBA"), ok)) first = *v;
    if (!ok) return false;
    auto sectors = imaging::RestoreImage::sectors_needed(c.input, dev_sector_);
    if (!sectors) {
        warn(qs(sectors.error().describe()));
        return false;
    }
    safety::Request req;
    req.kind = safety::Kind::restore_image;
    req.target.path = dev_path_.toStdString();
    req.target.is_image = dev_is_image_;
    req.target.image_sector_size = dev_sector_;
    req.ranges.push_back(LbaRange{first, *sectors});
    req.summary = (tr("Restore image %1 (%2)").arg(file, qs(format_capacity(*sectors * dev_sector_)))).toStdString();
    auto out = confirm_and_authorize(this, tr("Confirm Restore"), req, show_serial_);
    if (out.cancelled) return false;
    if (!out.target) {
        emit logEvent(Severity::error, tr("Restore refused: %1").arg(out.error), dev_path_);
        warn(out.error);
        return false;
    }
    for (const auto& w : out.target->warnings()) emit logEvent(Severity::warning, qs(w), dev_path_);
    auto op = std::make_unique<imaging::RestoreImage>(std::move(*out.target), c);
    if (auto v = op->validate(); !v) {
        warn(qs(v.error().describe()));
        return false;
    }
    const auto* raw = op.get();
    begin(std::move(op), [raw] { return raw->result(); }, tr("Image restore"));
    return true;
}

bool ImagingTab::startClone() {
    const Choice* other = otherChoice();
    if (!other) {
        warn(tr("Choose the device to clone onto."));
        return false;
    }
    if (other->path == dev_path_) {
        warn(tr("The source and the target are the same device."));
        return false;
    }
    imaging::CloneConfig c;
    c.recover = recover_->isChecked();
    c.retries = static_cast<std::uint32_t>(retries_->value());
    c.verify = verify_->isChecked();
    bool ok = true;
    if (auto v = parseLba(start_lba_, tr("Start LBA"), ok)) c.source_first_lba = *v;
    if (!ok) return false;
    std::uint64_t end = dev_lbas_ ? dev_lbas_ - 1 : 0;
    if (auto v = parseLba(end_lba_, tr("End LBA"), ok)) end = *v;
    if (!ok) return false;
    std::uint64_t tfirst = 0;
    if (auto v = parseLba(target_lba_, tr("Target LBA"), ok)) tfirst = *v;
    if (!ok) return false;
    if (c.source_first_lba > end || end >= dev_lbas_) {
        warn(tr("The source range is outside the device."));
        return false;
    }
    if (other->sector_size != dev_sector_) {
        warn(tr("The devices have different sector sizes (%1 and %2); a sector-for-sector clone is not possible.")
                 .arg(dev_sector_)
                 .arg(other->sector_size));
        return false;
    }
    const std::uint64_t sectors = end - c.source_first_lba + 1;
    auto src = openReadOnly(dev_path_, dev_is_image_, dev_sector_);
    if (!src) {
        warn(tr("Cannot open the source for reading: %1").arg(qs(src.error().describe())));
        return false;
    }
    safety::Request req;
    req.kind = safety::Kind::clone_target;
    req.target.path = other->path.toStdString();
    req.target.is_image = other->is_image;
    req.target.image_sector_size = other->sector_size;
    req.ranges.push_back(LbaRange{tfirst, sectors});
    req.summary = tr("Clone %1 (%2) onto this device").arg(dev_label_, qs(format_capacity(sectors * dev_sector_))).toStdString();
    auto out = confirm_and_authorize(this, tr("Confirm Clone"), req, show_serial_);
    if (out.cancelled) return false;
    if (!out.target) {
        emit logEvent(Severity::error, tr("Clone refused: %1").arg(out.error), other->path);
        warn(out.error);
        return false;
    }
    for (const auto& w : out.target->warnings()) emit logEvent(Severity::warning, qs(w), other->path);
    auto op = std::make_unique<imaging::CloneDevice>(std::move(*src), std::move(*out.target), c);
    if (auto v = op->validate(); !v) {
        warn(qs(v.error().describe()));
        return false;
    }
    const auto* raw = op.get();
    begin(std::move(op), [raw] { return raw->result(); }, tr("Clone"));
    return true;
}

bool ImagingTab::startVerify() {
    const auto file = file_->text().trimmed();
    if (file.isEmpty()) {
        warn(tr("Choose the image file to verify."));
        return false;
    }
    imaging::VerifyConfig c;
    c.input = file.toStdString();
    c.expect_sha256 = sha_->text().trimmed().toStdString();
    if (c.expect_sha256.empty()) c.expect_sha256 = imaging::read_sha256_sidecar(c.input + ".sha256");
    bool ok = true;
    if (compare_->isChecked()) {
        auto dev = openReadOnly(dev_path_, dev_is_image_, dev_sector_);
        if (!dev) {
            warn(tr("Cannot open the device for reading: %1").arg(qs(dev.error().describe())));
            return false;
        }
        c.compare_to = std::move(*dev);
        if (auto v = parseLba(start_lba_, tr("Start LBA"), ok)) c.compare_first_lba = *v;
        if (!ok) return false;
    }
    auto op = std::make_unique<imaging::VerifyImage>(std::move(c));
    if (auto v = op->validate(); !v) {
        warn(qs(v.error().describe()));
        return false;
    }
    const auto* raw = op.get();
    begin(std::move(op), [raw] { return raw->result(); }, tr("Image verification"));
    return true;
}

void ImagingTab::onStop() {
    if (!isRunning()) return;
    op_->cancel();
    status_->setText(tr("Stopping after the current block..."));
}

void ImagingTab::poll() {
    if (!op_) return;
    const auto p = op_->progress();
    progress_->setValue(static_cast<int>(p.percent() * 10));
    status_->setText(tr("%1 %  %2 MB/s  errors %3  %4")
                         .arg(p.percent(), 0, 'f', 1)
                         .arg(p.current_bytes_per_sec / 1e6, 0, 'f', 1)
                         .arg(p.errors)
                         .arg(qs(p.message)));
    if (is_terminal(p.state)) finish();
}

void ImagingTab::finish() {
    timer_->stop();
    op_->wait();
    last_ = result_fn_();
    report_->setPlainText(qs(imaging::result_to_text(*last_, show_serial_)));
    status_->setText(qs(std::string(imaging::verdict_name(last_->verdict))));
    if (last_->state == OperationState::completed) progress_->setValue(1000);
    const bool good = last_->verdict == imaging::Verdict::ok;
    emit logEvent(good ? Severity::info : Severity::warning,
                  tr("%1 finished: %2").arg(running_what_, qs(std::string(imaging::verdict_name(last_->verdict)))), dev_path_);
    if (history_ && record_) {
        if (auto id = history_->record_imaging(*last_); !id)
            emit logEvent(Severity::warning, tr("Could not save to history: %1").arg(qs(id.error().describe())), dev_path_);
    }
    op_.reset();  // releases a write lease right away
    result_fn_ = nullptr;
    updateButtons();
}

void ImagingTab::onSaveReport() {
    if (!last_) return;
    const auto file = QFileDialog::getSaveFileName(this, tr("Save Report"), QStringLiteral("imaging-report.json"),
                                                   tr("JSON report (*.json);;Text report (*.txt)"));
    if (file.isEmpty()) return;
    const bool json = file.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
    const auto content = json ? reports::imaging_to_json(*last_, reports::Privacy{show_serial_})
                              : imaging::result_to_text(*last_, show_serial_);
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(content.data(), static_cast<qint64>(content.size())) < 0 || !f.commit())
        QMessageBox::warning(this, tr("Save Report"), tr("Cannot write %1:\n%2").arg(file, f.errorString()));
}

}  // namespace platter::gui
