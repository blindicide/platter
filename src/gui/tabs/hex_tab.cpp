// SPDX-License-Identifier: TBD
#include "hex_tab.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace platter::gui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
constexpr std::uint32_t kBytesPerRow = 16;
constexpr std::size_t kMaxHits = 1000;
}  // namespace

struct HexTab::SearchJob {
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::atomic<std::uint64_t> scanned{0};
    std::uint64_t total = 0;
    bool all = false;
    // written by the worker before `done` is set
    std::optional<inspect::SearchOutcome> outcome;
    std::string error;
    ~SearchJob() {
        cancel = true;
        if (worker.joinable()) worker.join();
    }
};

HexTab::HexTab(QWidget* parent) : QWidget(parent), timer_(new QTimer(this)) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 4, 4, 4);

    // ---- navigation row
    auto* nav = new QHBoxLayout;
    goto_kind_ = new QComboBox(this);
    goto_kind_->addItem(tr("LBA"), 0);
    goto_kind_->addItem(tr("Byte offset"), 1);
    goto_ = new QLineEdit(this);
    goto_->setPlaceholderText(tr("number or 0x…"));
    goto_->setMaximumWidth(180);
    go_ = new QPushButton(tr("&Go"), this);
    prev_ = new QPushButton(tr("◀ &Prev"), this);
    next_ = new QPushButton(tr("&Next ▶"), this);
    sectors_ = new QSpinBox(this);
    sectors_->setRange(1, 16);
    sectors_->setValue(1);
    sectors_->setSuffix(tr(" sector(s)"));
    interpret_ = new QComboBox(this);
    interpret_->addItem(tr("Interpret: automatic"), -1);
    interpret_->addItem(tr("Interpret: none"), -2);
    for (int i = 0; i < inspect::kStructKindCount; ++i) {
        const auto k = static_cast<inspect::StructKind>(i);
        interpret_->addItem(tr("Interpret as: %1").arg(qs(std::string(inspect::struct_kind_name(k)))), i);
    }
    where_ = new QLabel(this);
    nav->addWidget(goto_kind_);
    nav->addWidget(goto_);
    nav->addWidget(go_);
    nav->addWidget(prev_);
    nav->addWidget(next_);
    nav->addWidget(sectors_);
    nav->addWidget(interpret_);
    nav->addWidget(where_, 1);
    col->addLayout(nav);

    // ---- hex view | structure + matches
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    view_ = new QPlainTextEdit(split);
    view_->setReadOnly(true);
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view_->setObjectName(QStringLiteral("hexView"));

    auto* right = new QTabWidget(split);
    auto* struct_page = new QWidget(right);
    auto* sl = new QVBoxLayout(struct_page);
    sl->setContentsMargins(2, 2, 2, 2);
    fields_ = new QTableWidget(0, 5, struct_page);
    fields_->setHorizontalHeaderLabels({tr("Offset"), tr("Size"), tr("Field"), tr("Value"), tr("Note")});
    fields_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fields_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fields_->setSelectionMode(QAbstractItemView::SingleSelection);
    fields_->verticalHeader()->setVisible(false);
    fields_->horizontalHeader()->setStretchLastSection(true);
    fields_->setAlternatingRowColors(true);
    notes_ = new QPlainTextEdit(struct_page);
    notes_->setReadOnly(true);
    notes_->setMaximumHeight(90);
    sl->addWidget(fields_, 1);
    sl->addWidget(notes_);
    right->addTab(struct_page, tr("Structure"));
    matches_ = new QListWidget(right);
    right->addTab(matches_, tr("Matches"));
    split->addWidget(view_);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    col->addWidget(split, 1);

    // ---- search row
    auto* find = new QHBoxLayout;
    find_kind_ = new QComboBox(this);
    find_kind_->addItem(tr("Hex bytes"), 0);
    find_kind_->addItem(tr("ASCII text"), 1);
    find_kind_->addItem(tr("UTF-16 text"), 2);
    find_text_ = new QLineEdit(this);
    find_text_->setPlaceholderText(tr("e.g. 55 AA ?? 0F  (?? matches any byte)"));
    find_case_ = new QCheckBox(tr("Ignore case"), this);
    find_next_ = new QPushButton(tr("Find N&ext"), this);
    find_all_ = new QPushButton(tr("Find &All"), this);
    find_stop_ = new QPushButton(tr("&Cancel"), this);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1000);
    progress_->setMaximumWidth(160);
    find->addWidget(find_kind_);
    find->addWidget(find_text_, 1);
    find->addWidget(find_case_);
    find->addWidget(find_next_);
    find->addWidget(find_all_);
    find->addWidget(find_stop_);
    find->addWidget(progress_);
    col->addLayout(find);
    status_ = new QLabel(this);
    col->addWidget(status_);

    connect(go_, &QPushButton::clicked, this, &HexTab::onGo);
    connect(goto_, &QLineEdit::returnPressed, this, &HexTab::onGo);
    connect(prev_, &QPushButton::clicked, this, &HexTab::onPrev);
    connect(next_, &QPushButton::clicked, this, &HexTab::onNext);
    connect(sectors_, qOverload<int>(&QSpinBox::valueChanged), this, &HexTab::onPageSettingsChanged);
    connect(interpret_, qOverload<int>(&QComboBox::currentIndexChanged), this, &HexTab::onPageSettingsChanged);
    connect(fields_, &QTableWidget::itemSelectionChanged, this, &HexTab::onFieldSelected);
    connect(find_text_, &QLineEdit::returnPressed, this, &HexTab::onFindNext);
    connect(find_next_, &QPushButton::clicked, this, &HexTab::onFindNext);
    connect(find_all_, &QPushButton::clicked, this, &HexTab::onFindAll);
    connect(find_stop_, &QPushButton::clicked, this, &HexTab::onCancelSearch);
    connect(matches_, &QListWidget::itemActivated, this, &HexTab::onMatchActivated);
    connect(matches_, &QListWidget::itemDoubleClicked, this, &HexTab::onMatchActivated);
    connect(find_kind_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        find_text_->setPlaceholderText(find_kind_->currentData().toInt() == 0 ? tr("e.g. 55 AA ?? 0F  (?? matches any byte)")
                                                                              : tr("text to look for"));
        find_case_->setEnabled(find_kind_->currentData().toInt() != 0);
    });
    find_case_->setEnabled(false);
    timer_->setInterval(200);
    connect(timer_, &QTimer::timeout, this, &HexTab::poll);

    setDevice(nullptr);
}

HexTab::~HexTab() { stopAndWait(); }

bool HexTab::isRunning() const { return job_ && !job_->done.load(); }

void HexTab::stopAndWait() {
    if (job_) {
        job_->cancel = true;
        if (job_->worker.joinable()) job_->worker.join();
        job_.reset();
    }
    timer_->stop();
}

void HexTab::setDevice(const DeviceEntry* e) {
    stopAndWait();
    matches_->clear();
    have_dev_ = e != nullptr;
    page_.clear();
    structure_.reset();
    read_error_.clear();
    loaded_ = false;
    if (!e) {
        dev_path_.clear();
        view_->setPlainText(tr("No device selected."));
        clearStructure(QString());
        where_->clear();
        status_->setText(tr("Select a device or open an image to inspect its sectors (read-only)."));
    } else {
        dev_path_ = qs(e->info.path);
        dev_label_ = e->displayName();
        dev_is_image_ = e->is_image;
        sector_ = e->is_image ? e->image_sector_size : e->info.logical_sector_size;
        if (sector_ == 0) sector_ = 512;
        total_lbas_ = e->info.total_lbas();
        // Selecting a device must not read from it: nothing is touched until the user asks.
        lba_ = 0;
        view_->setPlainText(tr("Nothing has been read yet.\n\nPress Go to read the sector above (empty = LBA 0). "
                               "Reads are strictly read-only; physical disks usually need elevated rights."));
        view_->setExtraSelections({});
        clearStructure(QString());
        where_->setText(tr("%1 sectors of %2 bytes").arg(total_lbas_).arg(sector_));
        status_->setText(tr("Read-only inspector for %1.").arg(dev_label_));
    }
    updateButtons();
}

void HexTab::updateButtons() {
    const bool running = isRunning();
    const bool ok = have_dev_ && !running;
    go_->setEnabled(ok);
    prev_->setEnabled(ok && loaded_ && lba_ > 0);
    next_->setEnabled(ok && loaded_ && lba_ + static_cast<std::uint64_t>(sectors_->value()) < total_lbas_);
    find_next_->setEnabled(ok);
    find_all_->setEnabled(ok);
    find_stop_->setEnabled(running);
    goto_->setEnabled(ok);
    sectors_->setEnabled(ok);
    interpret_->setEnabled(ok);
}

std::optional<std::uint64_t> HexTab::parseNumber(const QString& text) const {
    bool ok = false;
    const auto t = text.trimmed();
    if (t.isEmpty()) return std::nullopt;
    const auto v = t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) ? t.mid(2).toULongLong(&ok, 16) : t.toULongLong(&ok, 10);
    if (!ok) return std::nullopt;
    return v;
}

void HexTab::onGo() {
    if (goto_->text().trimmed().isEmpty()) {
        loadPage(0, true);
        return;
    }
    const auto v = parseNumber(goto_->text());
    if (!v) {
        status_->setText(tr("Enter a decimal number or 0x… hexadecimal."));
        return;
    }
    const std::uint64_t lba = goto_kind_->currentData().toInt() == 0 ? *v : *v / sector_;
    if (total_lbas_ && lba >= total_lbas_) {
        status_->setText(tr("LBA %1 is beyond the end of the device (%2 sectors).").arg(lba).arg(total_lbas_));
        return;
    }
    loadPage(lba, true);
}

void HexTab::onPrev() {
    const auto n = static_cast<std::uint64_t>(sectors_->value());
    loadPage(lba_ > n ? lba_ - n : 0, true);
}

void HexTab::onNext() {
    const auto n = static_cast<std::uint64_t>(sectors_->value());
    if (lba_ + n < total_lbas_) loadPage(lba_ + n, true);
}

void HexTab::onPageSettingsChanged() {
    if (have_dev_ && loaded_) loadPage(lba_, false);
}

void HexTab::showLba(std::uint64_t lba) {
    if (have_dev_) loadPage(lba, true);
}

void HexTab::loadPage(std::uint64_t lba, bool from_user) {
    if (!have_dev_) return;
    loaded_ = true;
    if (from_user) {
        match_highlight_.reset();
        search_from_ = lba * sector_;
    }
    lba_ = lba;
    page_.clear();
    read_error_.clear();
    const std::uint64_t want_sectors = std::min<std::uint64_t>(static_cast<std::uint64_t>(sectors_->value()),
                                                              total_lbas_ > lba ? total_lbas_ - lba : 0);
    if (want_sectors == 0) {
        read_error_ = tr("LBA %1 is beyond the end of the device.").arg(lba);
    } else {
        OpenOptions oo;
        oo.mode = OpenMode::read_only;
        auto dev = dev_is_image_ ? open_image(dev_path_.toStdString(), oo, sector_) : open_device(dev_path_.toStdString(), oo);
        if (!dev) {
            read_error_ = tr("Cannot open the device for reading: %1").arg(qs(dev.error().describe()));
        } else {
            page_.resize(static_cast<std::size_t>(want_sectors) * sector_);
            auto r = (*dev)->read(lba * sector_, page_.data(), page_.size());
            if (!r) {
                read_error_ = tr("Read failed at LBA %1: %2").arg(lba).arg(qs(r.error().describe()));
                page_.clear();
            }
        }
    }
    render();
    updateButtons();
}

void HexTab::clearStructure(const QString& why) {
    fields_->setRowCount(0);
    notes_->setPlainText(why);
}

void HexTab::render() {
    where_->setText(tr("LBA %1 of %2   byte offset %3 (0x%4)")
                        .arg(lba_)
                        .arg(total_lbas_)
                        .arg(lba_ * sector_)
                        .arg(lba_ * sector_, 0, 16));
    structure_.reset();
    structure_base_ = 0;
    if (page_.empty()) {
        view_->setPlainText(read_error_.isEmpty() ? tr("(nothing to show)") : read_error_);
        clearStructure(QString());
        if (!read_error_.isEmpty()) emit logEvent(Severity::warning, read_error_, dev_path_);
        view_->setExtraSelections({});
        return;
    }
    inspect::HexLayout layout;
    layout.bytes_per_row = kBytesPerRow;
    view_->setPlainText(qs(inspect::hex_dump(page_, lba_ * sector_, layout)));

    const int mode = interpret_->currentData().toInt();
    QString why;
    if (mode != -2) {
        std::optional<inspect::Detected> det;
        if (mode >= 0) det = inspect::Detected{static_cast<inspect::StructKind>(mode), 0};
        else det = inspect::detect(page_, lba_, sector_);
        if (det) {
            auto s = inspect::interpret(det->kind, std::span<const std::uint8_t>(page_).subspan(det->offset), sector_);
            if (s) {
                structure_ = std::move(s).value();
                structure_base_ = det->offset;
            } else {
                why = tr("Cannot interpret as %1: %2")
                          .arg(qs(std::string(inspect::struct_kind_name(det->kind))), qs(s.error().describe()));
            }
        } else if (mode == -1) {
            why = tr("These bytes are not a structure platter recognises. Pick \"Interpret as\" to force one.");
        }
    }
    fields_->setRowCount(0);
    if (structure_) {
        fields_->setRowCount(static_cast<int>(structure_->fields.size()));
        int row = 0;
        for (const auto& f : structure_->fields) {
            const auto abs = f.offset + structure_base_;
            auto add = [&](int c, const QString& t) { fields_->setItem(row, c, new QTableWidgetItem(t)); };
            add(0, QString::number(abs));
            add(1, QString::number(f.size));
            add(2, qs(f.name));
            add(3, qs(f.value));
            add(4, qs(f.note));
            ++row;
        }
        fields_->resizeColumnsToContents();
        QStringList lines;
        lines << qs(structure_->title);
        for (const auto& n : structure_->notes) lines << QStringLiteral("• ") + qs(n);
        notes_->setPlainText(lines.join(QLatin1Char('\n')));
    } else {
        notes_->setPlainText(why);
    }
    if (match_highlight_)
        highlight(match_highlight_->first, match_highlight_->second);
    else
        view_->setExtraSelections({});
}

void HexTab::highlight(std::uint32_t first_byte, std::uint32_t size) {
    QList<QTextEdit::ExtraSelection> sels;
    if (size == 0 || first_byte >= page_.size()) {
        view_->setExtraSelections(sels);
        return;
    }
    inspect::HexLayout layout;
    layout.bytes_per_row = kBytesPerRow;
    const std::uint32_t end = static_cast<std::uint32_t>(std::min<std::size_t>(page_.size(), std::size_t{first_byte} + size));
    const QColor hex_bg = palette().color(QPalette::Highlight);
    const QColor hex_fg = palette().color(QPalette::HighlightedText);
    QTextCursor first_cursor;
    for (std::uint32_t row = first_byte / kBytesPerRow; row <= (end - 1) / kBytesPerRow; ++row) {
        const auto b = view_->document()->findBlockByNumber(static_cast<int>(row));
        if (!b.isValid()) break;
        const std::uint32_t lo = std::max(first_byte, row * kBytesPerRow) - row * kBytesPerRow;
        const std::uint32_t hi = std::min(end, (row + 1) * kBytesPerRow) - row * kBytesPerRow;  // exclusive
        auto select = [&](int from, int to, const QColor& bg, const QColor& fg) {
            QTextEdit::ExtraSelection s;
            s.cursor = QTextCursor(view_->document());
            s.cursor.setPosition(b.position() + from);
            s.cursor.setPosition(b.position() + to, QTextCursor::KeepAnchor);
            s.format.setBackground(bg);
            s.format.setForeground(fg);
            sels.append(s);
            return s.cursor;
        };
        const auto c = select(static_cast<int>(layout.hex_column(lo)), static_cast<int>(layout.hex_column(hi - 1) + 2), hex_bg, hex_fg);
        select(static_cast<int>(layout.ascii_column(lo)), static_cast<int>(layout.ascii_column(hi - 1) + 1), hex_bg, hex_fg);
        if (first_cursor.isNull()) first_cursor = c;
    }
    view_->setExtraSelections(sels);
    if (!first_cursor.isNull()) {
        QTextCursor vis = first_cursor;
        vis.clearSelection();
        view_->setTextCursor(vis);
        view_->ensureCursorVisible();
    }
}

void HexTab::onFieldSelected() {
    if (!structure_) return;
    const auto rows = fields_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    const auto idx = static_cast<std::size_t>(rows.first().row());
    if (idx >= structure_->fields.size()) return;
    const auto& f = structure_->fields[idx];
    match_highlight_.reset();
    highlight(f.offset + structure_base_, f.size);
}

// ------------------------------------------------------------------- search

std::optional<inspect::Pattern> HexTab::buildPattern(QString& error) const {
    const auto text = find_text_->text();
    if (text.isEmpty()) {
        error = tr("Type something to look for.");
        return std::nullopt;
    }
    const bool ci = find_case_->isChecked();
    switch (find_kind_->currentData().toInt()) {
        case 0: {
            auto p = inspect::parse_hex_pattern(text.toStdString());
            if (!p) {
                error = qs(p.error().describe());
                return std::nullopt;
            }
            return std::move(p).value();
        }
        case 1:
            return inspect::ascii_pattern(text.toUtf8().toStdString(), ci);
        default: {
            auto p = inspect::utf16le_pattern(text.toUtf8().toStdString(), ci);
            if (!p) {
                error = qs(p.error().describe());
                return std::nullopt;
            }
            return std::move(p).value();
        }
    }
}

void HexTab::onFindNext() { startSearch(false); }
void HexTab::onFindAll() { startSearch(true); }

void HexTab::startSearch(bool all) {
    if (!have_dev_ || isRunning()) return;
    QString err;
    auto pat = buildPattern(err);
    if (!pat) {
        status_->setText(err);
        return;
    }
    last_pattern_size_ = static_cast<std::uint32_t>(pat->size());
    const std::uint64_t capacity = total_lbas_ * sector_;
    const std::uint64_t from = all ? 0 : std::min<std::uint64_t>(search_from_, capacity);
    if (from >= capacity) {
        status_->setText(tr("Nothing left to search after this position."));
        return;
    }
    if (all) matches_->clear();

    auto job = std::make_shared<SearchJob>();
    job->all = all;
    job->total = capacity - from;
    const QString path = dev_path_;
    const bool image = dev_is_image_;
    const std::uint32_t ss = sector_;
    job->worker = std::thread([job, pattern = std::move(*pat), path, image, ss, from, capacity, all]() {
        OpenOptions oo;
        oo.mode = OpenMode::read_only;
        auto dev = image ? open_image(path.toStdString(), oo, ss) : open_device(path.toStdString(), oo);
        if (!dev) {
            job->error = dev.error().describe();
            job->done = true;
            return;
        }
        auto r = inspect::search_device(**dev, pattern, from, capacity, all ? kMaxHits : 1,
                                        [&](std::uint64_t scanned) {
                                            job->scanned = scanned;
                                            return !job->cancel.load();
                                        });
        if (r) job->outcome = std::move(r).value();
        else job->error = r.error().describe();
        job->done = true;
    });
    job_ = job;
    progress_->setValue(0);
    status_->setText(tr("Searching…"));
    timer_->start();
    updateButtons();
}

void HexTab::onCancelSearch() {
    if (job_) job_->cancel = true;
}

void HexTab::poll() {
    if (!job_) {
        timer_->stop();
        return;
    }
    const auto total = std::max<std::uint64_t>(1, job_->total);
    progress_->setValue(static_cast<int>(1000.0 * static_cast<double>(job_->scanned.load()) / static_cast<double>(total)));
    if (job_->done.load()) finishSearch();
}

void HexTab::finishSearch() {
    timer_->stop();
    auto job = std::move(job_);
    job_.reset();
    if (job->worker.joinable()) job->worker.join();
    progress_->setValue(0);
    if (!job->error.empty()) {
        status_->setText(tr("Search failed: %1").arg(qs(job->error)));
        emit logEvent(Severity::warning, status_->text(), dev_path_);
        updateButtons();
        return;
    }
    const auto& out = *job->outcome;
    const QString unreadable =
        out.unreadable_sectors ? tr(" (%1 unreadable sectors skipped)").arg(out.unreadable_sectors) : QString();
    if (job->all) {
        for (auto h : out.hits) {
            auto* item = new QListWidgetItem(tr("LBA %1 + %2   (byte %3)").arg(h / sector_).arg(h % sector_).arg(h), matches_);
            item->setData(Qt::UserRole, static_cast<qulonglong>(h));
        }
        status_->setText(out.hits.empty() ? tr("No matches%1.").arg(unreadable)
                                          : tr("%1 match(es)%2%3 — double-click one in the Matches tab to jump to it.")
                                                .arg(out.hits.size())
                                                .arg(out.truncated ? tr(" (stopped at the first %1)").arg(kMaxHits) : QString(), unreadable));
        emit logEvent(Severity::info, tr("Search found %1 match(es)").arg(out.hits.size()), dev_path_);
    } else if (out.hits.empty()) {
        status_->setText(out.complete ? tr("No further matches%1.").arg(unreadable) : tr("Search cancelled."));
    } else {
        const auto h = out.hits.front();
        loadPage(h / sector_, true);
        match_highlight_ = {static_cast<std::uint32_t>(h % sector_), last_pattern_size_};
        render();
        search_from_ = h + 1;
        status_->setText(tr("Match at LBA %1 + %2 (byte %3). Find Next continues after it.").arg(h / sector_).arg(h % sector_).arg(h));
    }
    updateButtons();
}

void HexTab::onMatchActivated(QListWidgetItem* item) {
    if (!item || !have_dev_) return;
    const auto h = item->data(Qt::UserRole).toULongLong();
    loadPage(h / sector_, true);
    match_highlight_ = {static_cast<std::uint32_t>(h % sector_), last_pattern_size_};
    render();
    search_from_ = h + 1;
}

}  // namespace platter::gui
