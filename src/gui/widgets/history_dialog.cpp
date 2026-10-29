// SPDX-License-Identifier: TBD
#include "history_dialog.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace platter::gui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
QString qs(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
}  // namespace

HistoryDialog::HistoryDialog(storage::HistoryStore* store, const QString& device_key, bool show_serial, QWidget* parent)
    : QDialog(parent), store_(store), show_serial_(show_serial), initial_device_key_(device_key) {
    setWindowTitle(tr("Operation History"));
    resize(900, 560);
    auto* col = new QVBoxLayout(this);

    auto* filters = new QHBoxLayout;
    kind_ = new QComboBox(this);
    kind_->addItem(tr("All kinds"), -1);
    for (int i = 0; i < storage::kSessionKindCount; ++i) {
        const auto k = static_cast<storage::SessionKind>(i);
        kind_->addItem(qs(storage::session_kind_name(k)), i);
    }
    device_ = new QComboBox(this);
    device_->addItem(tr("All devices"), QString());
    filters->addWidget(new QLabel(tr("Show:"), this));
    filters->addWidget(kind_);
    filters->addWidget(device_, 1);
    auto* refresh = new QPushButton(tr("&Refresh"), this);
    filters->addWidget(refresh);
    col->addLayout(filters);

    auto* split = new QSplitter(Qt::Vertical, this);
    table_ = new QTableWidget(0, 6, split);
    table_->setHorizontalHeaderLabels({tr("Id"), tr("Kind"), tr("Started (UTC)"), tr("Device"), tr("State"), tr("Result")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    detail_ = new QPlainTextEdit(split);
    detail_->setReadOnly(true);
    detail_->setLineWrapMode(QPlainTextEdit::NoWrap);
    detail_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    detail_->setPlaceholderText(tr("Select a saved session to see its report."));
    split->addWidget(table_);
    split->addWidget(detail_);
    split->setStretchFactor(1, 1);
    col->addWidget(split, 1);

    auto* buttons = new QHBoxLayout;
    save_ = new QPushButton(tr("&Save Report..."), this);
    delete_ = new QPushButton(tr("&Delete Record..."), this);
    auto* close = new QPushButton(tr("&Close"), this);
    buttons->addWidget(save_);
    buttons->addWidget(delete_);
    buttons->addStretch(1);
    buttons->addWidget(close);
    col->addLayout(buttons);

    connect(refresh, &QPushButton::clicked, this, &HistoryDialog::reload);
    connect(kind_, qOverload<int>(&QComboBox::currentIndexChanged), this, &HistoryDialog::reload);
    connect(device_, qOverload<int>(&QComboBox::currentIndexChanged), this, &HistoryDialog::reload);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &HistoryDialog::onSelectionChanged);
    connect(save_, &QPushButton::clicked, this, &HistoryDialog::onSave);
    connect(delete_, &QPushButton::clicked, this, &HistoryDialog::onDelete);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    if (store_) {
        if (auto devs = store_->devices()) {
            for (const auto& d : *devs)
                device_->addItem(qs(d.model.empty() ? d.key : d.model) + QStringLiteral("  [") + qs(d.key.substr(0, 12)) + QLatin1Char(']'),
                                 qs(d.key));
            const int idx = device_->findData(initial_device_key_);
            if (idx >= 0) device_->setCurrentIndex(idx);
        }
    }
    reload();
}

const storage::SessionSummary* HistoryDialog::selected() const {
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return nullptr;
    const auto i = static_cast<std::size_t>(rows.first().row());
    return i < rows_.size() ? &rows_[i] : nullptr;
}

void HistoryDialog::reload() {
    rows_.clear();
    table_->setRowCount(0);
    detail_->clear();
    if (!store_) {
        detail_->setPlainText(tr("The history database is not available, so nothing was saved."));
        onSelectionChanged();
        return;
    }
    const auto key = device_->currentData().toString().toStdString();
    const int kind = kind_->currentData().toInt();
    auto rows = kind < 0 ? store_->list_all(key, 1000) : store_->list(static_cast<storage::SessionKind>(kind), key, 1000);
    if (!rows) {
        detail_->setPlainText(tr("Cannot read the history: %1").arg(qs(rows.error().describe())));
        onSelectionChanged();
        return;
    }
    rows_ = std::move(rows).value();
    table_->setRowCount(static_cast<int>(rows_.size()));
    int row = 0;
    for (const auto& r : rows_) {
        auto add = [&](int c, const QString& t) { table_->setItem(row, c, new QTableWidgetItem(t)); };
        add(0, QString::number(r.id));
        add(1, qs(storage::session_kind_name(r.kind)));
        add(2, qs(r.started_utc));
        add(3, qs(r.device_model));
        add(4, qs(r.state));
        add(5, qs(r.headline));
        ++row;
    }
    table_->resizeColumnsToContents();
    if (rows_.empty()) detail_->setPlainText(tr("Nothing saved yet for this filter. Results are saved when \"Record results in history\" is on."));
    onSelectionChanged();
}

void HistoryDialog::onSelectionChanged() {
    const auto* s = selected();
    save_->setEnabled(s != nullptr);
    delete_->setEnabled(s != nullptr);
    if (!s) return;
    auto text = store_->render_text(*s, show_serial_);
    detail_->setPlainText(text ? qs(*text) : tr("Cannot show this record: %1").arg(qs(text.error().describe())));
}

void HistoryDialog::onSave() {
    const auto* s = selected();
    if (!s) return;
    const auto name = QStringLiteral("%1-%2").arg(qs(storage::session_kind_name(s->kind))).arg(s->id);
    const auto file = QFileDialog::getSaveFileName(this, tr("Save Report"), name + QStringLiteral(".json"),
                                                   tr("JSON report (*.json);;Text report (*.txt)"));
    if (file.isEmpty()) return;
    std::string content;
    if (file.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        auto doc = store_->load_document(s->kind, s->id);
        if (!doc) {
            QMessageBox::warning(this, tr("Save Report"), qs(doc.error().describe()));
            return;
        }
        content = *doc;
    } else {
        auto text = store_->render_text(*s, show_serial_);
        if (!text) {
            QMessageBox::warning(this, tr("Save Report"), qs(text.error().describe()));
            return;
        }
        content = *text;
    }
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(content.data(), static_cast<qint64>(content.size())) < 0 || !f.commit())
        QMessageBox::warning(this, tr("Save Report"), tr("Cannot write %1:\n%2").arg(file, f.errorString()));
}

void HistoryDialog::onDelete() {
    const auto* s = selected();
    if (!s) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete Record"),
        tr("Remove the saved %1 record #%2 (%3) from the history?\n\nThis only deletes the saved record. Nothing on any device is touched.")
            .arg(qs(storage::session_kind_name(s->kind)))
            .arg(s->id)
            .arg(qs(s->headline)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    if (auto r = store_->remove(s->kind, s->id); !r) {
        QMessageBox::warning(this, tr("Delete Record"), qs(r.error().describe()));
        return;
    }
    reload();
}

}  // namespace platter::gui
