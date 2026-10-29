// SPDX-License-Identifier: TBD
#include "checksum_dialog.hpp"

#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace platter::gui {

ChecksumDialog::ChecksumDialog(QWidget* parent) : QDialog(parent), timer_(new QTimer(this)) {
    setWindowTitle(tr("Checksum Calculator"));
    resize(640, 220);
    auto* col = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* row = new QHBoxLayout;
    file_ = new QLineEdit(this);
    auto* browse = new QPushButton(tr("&Browse..."), this);
    row->addWidget(file_, 1);
    row->addWidget(browse);
    form->addRow(tr("File:"), row);
    expected_ = new QLineEdit(this);
    expected_->setPlaceholderText(tr("optional: paste the SHA-256 you expect"));
    form->addRow(tr("Expected:"), expected_);
    result_ = new QLineEdit(this);
    result_->setReadOnly(true);
    result_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    form->addRow(tr("SHA-256:"), result_);
    col->addLayout(form);
    verdict_ = new QLabel(this);
    status_ = new QLabel(tr("Choose a file and press Calculate."), this);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1000);
    col->addWidget(verdict_);
    col->addWidget(progress_);
    col->addWidget(status_);
    auto* buttons = new QHBoxLayout;
    start_ = new QPushButton(tr("&Calculate"), this);
    stop_ = new QPushButton(tr("&Stop"), this);
    auto* copy = new QPushButton(tr("Co&py"), this);
    auto* close = new QPushButton(tr("C&lose"), this);
    buttons->addWidget(start_);
    buttons->addWidget(stop_);
    buttons->addWidget(copy);
    buttons->addStretch(1);
    buttons->addWidget(close);
    col->addLayout(buttons);
    stop_->setEnabled(false);

    connect(browse, &QPushButton::clicked, this, &ChecksumDialog::onBrowse);
    connect(start_, &QPushButton::clicked, this, &ChecksumDialog::onStart);
    connect(stop_, &QPushButton::clicked, this, &ChecksumDialog::onStop);
    connect(copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(result_->text()); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(expected_, &QLineEdit::textChanged, this, &ChecksumDialog::updateVerdict);
    connect(timer_, &QTimer::timeout, this, &ChecksumDialog::step);
}

ChecksumDialog::~ChecksumDialog() = default;

void ChecksumDialog::setFile(const QString& path) { file_->setText(path); }

void ChecksumDialog::onBrowse() {
    const auto f = QFileDialog::getOpenFileName(this, tr("Choose a file"));
    if (!f.isEmpty()) file_->setText(f);
}

void ChecksumDialog::onStart() {
    in_ = std::make_unique<QFile>(file_->text().trimmed());
    if (!in_->open(QIODevice::ReadOnly)) {
        status_->setText(tr("Cannot open the file: %1").arg(in_->errorString()));
        in_.reset();
        return;
    }
    hash_.reset();
    done_ = 0;
    total_ = in_->size();
    result_->clear();
    verdict_->clear();
    progress_->setValue(0);
    start_->setEnabled(false);
    stop_->setEnabled(true);
    status_->setText(tr("Reading %1 bytes...").arg(total_));
    timer_->start(0);
}

void ChecksumDialog::onStop() { finish(tr("Stopped.")); }

void ChecksumDialog::step() {
    if (!in_) return;
    QByteArray buf = in_->read(4 << 20);
    if (buf.isEmpty()) {
        if (in_->error() != QFileDevice::NoError || done_ != total_) {
            finish(tr("Read error after %1 bytes: %2").arg(done_).arg(in_->errorString()));
            return;
        }
        result_->setText(QString::fromStdString(Sha256::hex(hash_.finish())));
        progress_->setValue(1000);
        finish(tr("Done: %1 bytes.").arg(done_));
        updateVerdict();
        return;
    }
    hash_.update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(buf.constData()), static_cast<std::size_t>(buf.size())));
    done_ += buf.size();
    progress_->setValue(total_ ? static_cast<int>(1000.0 * static_cast<double>(done_) / static_cast<double>(total_)) : 1000);
}

void ChecksumDialog::finish(const QString& message) {
    timer_->stop();
    in_.reset();
    start_->setEnabled(true);
    stop_->setEnabled(false);
    status_->setText(message);
}

void ChecksumDialog::updateVerdict() {
    const auto want = expected_->text().trimmed().toLower();
    const auto got = result_->text();
    if (want.isEmpty() || got.isEmpty()) {
        verdict_->clear();
        return;
    }
    // Accept "hash  filename" lines as found in .sha256 files.
    const auto token = want.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
    verdict_->setText(token == got ? tr("<b>MATCH</b>: the file has the expected checksum.")
                                   : tr("<b>MISMATCH</b>: the file does NOT have the expected checksum."));
}

}  // namespace platter::gui
