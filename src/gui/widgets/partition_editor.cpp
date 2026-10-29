// SPDX-License-Identifier: TBD
#include "../i18n/i18n.hpp"
#include "partition_editor.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include "confirm_dialog.hpp"
#include "platter/mkfs/mkfs.hpp"

namespace platter::gui {

using namespace platter::partitions;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
QString qs(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

// "10G", "512M", "1.5T", "max" (-> 0). Binary multiples. Returns nullopt on junk.
std::optional<std::uint64_t> parse_size_text(const QString& text, bool allow_max) {
    const auto t = text.trimmed().toLower();
    if (allow_max && (t.isEmpty() || t == QLatin1String("max"))) return std::uint64_t{0};
    static const QRegularExpression re(QStringLiteral("^(\\d+(?:\\.\\d+)?)\\s*([kmgt]?)i?b?$"));
    const auto m = re.match(t);
    if (!m.hasMatch()) return std::nullopt;
    double v = m.captured(1).toDouble();
    const auto unit = m.captured(2);
    double mult = 1;
    if (unit == QLatin1String("k")) mult = 1024.0;
    else if (unit == QLatin1String("m")) mult = 1024.0 * 1024;
    else if (unit == QLatin1String("g")) mult = 1024.0 * 1024 * 1024;
    else if (unit == QLatin1String("t")) mult = 1024.0 * 1024 * 1024 * 1024;
    v *= mult;
    if (v < 1 || v > 9.0e18) return std::nullopt;
    return static_cast<std::uint64_t>(v);
}

QStandardItem* cell(const QString& text) {
    auto* i = new QStandardItem(text);
    i->setEditable(false);
    return i;
}

safety::Request make_request(const EditorTarget& t, safety::Kind kind, std::vector<LbaRange> ranges, const QString& summary) {
    safety::Request r;
    r.kind = kind;
    r.target.path = t.path.toStdString();
    r.target.is_image = t.is_image;
    r.target.image_sector_size = t.sector_size;
    r.ranges = std::move(ranges);
    r.summary = summary.toStdString();
    return r;
}

Result<BlockDevicePtr> open_for_read(const EditorTarget& t) {
    OpenOptions oo;
    oo.mode = OpenMode::read_only;
    return t.is_image ? open_image(t.path.toStdString(), oo, t.sector_size) : open_device(t.path.toStdString(), oo);
}

}  // namespace

PartitionEditorDialog::PartitionEditorDialog(const EditorTarget& target, QWidget* parent)
    : QDialog(parent), target_(target) {
    setWindowTitle(tr("Partition Manager - %1").arg(target.label));
    resize(760, 520);
    auto* col = new QVBoxLayout(this);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    col->addWidget(summary_);

    auto* row = new QHBoxLayout;
    new_ = new QPushButton(tr("&New Table..."), this);
    add_ = new QPushButton(tr("&Add..."), this);
    del_ = new QPushButton(tr("&Delete"), this);
    props_ = new QPushButton(tr("&Properties..."), this);
    resize_ = new QPushButton(tr("&Resize..."), this);
    format_ = new QPushButton(tr("&Format..."), this);
    for (auto* b : {new_, add_, del_, props_, resize_, format_}) row->addWidget(b);
    row->addStretch(1);
    col->addLayout(row);

    auto* split = new QSplitter(Qt::Vertical, this);
    model_ = new QStandardItemModel(0, 7, this);
    model_->setHorizontalHeaderLabels({tr("#"), tr("Start LBA"), tr("End LBA"), tr("Size"), tr("Type"), tr("Name / Flags"), tr("Aligned")});
    view_ = new QTableView(split);
    view_->setModel(model_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->verticalHeader()->setVisible(false);
    view_->horizontalHeader()->setStretchLastSection(true);
    split->addWidget(view_);

    auto* lower = new QSplitter(Qt::Horizontal, split);
    problems_ = new QListWidget(lower);
    diff_ = new QPlainTextEdit(lower);
    diff_->setReadOnly(true);
    diff_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    diff_->setLineWrapMode(QPlainTextEdit::NoWrap);
    lower->addWidget(problems_);
    lower->addWidget(diff_);
    split->addWidget(lower);
    split->setStretchFactor(0, 2);
    col->addWidget(split, 1);

    auto* note = new QLabel(tr("Changes are staged here and written only when you press Apply and type the confirmation. "
                               "Resizing or moving a partition changes the table only - file systems and data are not moved."),
                            this);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("placeholder"));
    col->addWidget(note);

    auto* bottom = new QHBoxLayout;
    apply_ = new QPushButton(tr("A&pply Changes..."), this);
    revert_ = new QPushButton(tr("Re&vert"), this);
    auto* close = new QPushButton(tr("&Close"), this);
    bottom->addStretch(1);
    bottom->addWidget(apply_);
    bottom->addWidget(revert_);
    bottom->addWidget(close);
    col->addLayout(bottom);

    connect(new_, &QPushButton::clicked, this, &PartitionEditorDialog::onNewTable);
    connect(add_, &QPushButton::clicked, this, &PartitionEditorDialog::onAdd);
    connect(del_, &QPushButton::clicked, this, &PartitionEditorDialog::onDelete);
    connect(props_, &QPushButton::clicked, this, &PartitionEditorDialog::onProperties);
    connect(resize_, &QPushButton::clicked, this, &PartitionEditorDialog::onResize);
    connect(format_, &QPushButton::clicked, this, &PartitionEditorDialog::onFormat);
    connect(apply_, &QPushButton::clicked, this, &PartitionEditorDialog::onApply);
    connect(revert_, &QPushButton::clicked, this, &PartitionEditorDialog::onRevert);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateButtons(); });

    reloadFromDisk();
    refreshView();
}

PartitionEditorDialog::~PartitionEditorDialog() = default;

bool PartitionEditorDialog::reloadFromDisk() {
    auto dev = open_for_read(target_);
    if (!dev) {
        plan_.reset();
        have_disk_ = false;
        summary_->setText(tr("Cannot open the device: %1").arg(i18n::err(dev.error())));
        return false;
    }
    auto table = read_partition_table(**dev);
    if (!table) {
        plan_.reset();
        have_disk_ = false;
        summary_->setText(tr("Cannot read the partition table: %1").arg(i18n::err(table.error())));
        return false;
    }
    on_disk_ = std::move(table).value();
    have_disk_ = true;
    dirty_ = false;
    if (on_disk_.type == TableType::none) {
        plan_.reset();  // nothing to edit until the user creates a table
    } else {
        auto plan = PartitionPlan::from_device(**dev);
        if (!plan) {
            plan_.reset();
            summary_->setText(tr("This table cannot be edited: %1").arg(i18n::err(plan.error())));
            have_disk_ = true;
            return false;
        }
        plan_ = std::move(plan).value();
    }
    return true;
}

std::optional<std::uint32_t> PartitionEditorDialog::selectedNumber() const {
    const auto rows = view_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return std::nullopt;
    return model_->item(rows.first().row(), 0)->data(Qt::UserRole).toUInt();
}

void PartitionEditorDialog::updateButtons() {
    const bool plan = plan_.has_value();
    const bool sel = plan && selectedNumber().has_value();
    const Partition* p = sel ? plan_->find(*selectedNumber()) : nullptr;
    new_->setEnabled(have_disk_);
    add_->setEnabled(plan);
    del_->setEnabled(sel);
    props_->setEnabled(sel);
    resize_->setEnabled(sel && p);
    // Formatting acts on the table that is really on disk.
    format_->setEnabled(sel && !dirty_ && p && !p->extended_container);
    const bool ok = plan && dirty_ && plan_->problems().empty();
    apply_->setEnabled(ok);
    revert_->setEnabled(dirty_);
}

void PartitionEditorDialog::refreshView() {
    model_->removeRows(0, model_->rowCount());
    problems_->clear();
    diff_->clear();
    if (!plan_) {
        if (have_disk_)
            summary_->setText(on_disk_.type == TableType::none
                                  ? tr("The device has no partition table. Use New Table to create one.")
                                  : summary_->text());
        updateButtons();
        return;
    }
    const bool gpt = plan_->type() == TableType::gpt;
    summary_->setText(tr("%1: %2 table, %3 partition(s)%4")
                          .arg(target_.label, qs(table_type_name(plan_->type())).toUpper())
                          .arg(plan_->partitions().size())
                          .arg(dirty_ ? tr("   *** UNSAVED CHANGES ***") : QString()));
    for (const auto& p : plan_->partitions()) {
        QStringList flags;
        if (gpt) flags << qs(p.name);
        else {
            if (p.bootable) flags << tr("boot");
            if (p.extended_container) flags << tr("extended");
            if (p.logical) flags << tr("logical");
        }
        const auto cap = format_capacity(p.size_bytes);
        auto* num = cell(QString::number(p.number));
        num->setData(p.number, Qt::UserRole);
        model_->appendRow({num, cell(QString::number(p.first_lba)), cell(QString::number(p.last_lba)),
                           cell(i18n::units(qs(cap.substr(0, cap.find(" ("))))), cell(i18n::ct(p.type_name)), cell(flags.join(QStringLiteral(", "))),
                           cell(p.aligned_1mib ? tr("1 MiB") : tr("NO"))});
    }
    view_->resizeColumnsToContents();
    for (const auto& pr : plan_->problems()) problems_->addItem(tr("ERROR: %1").arg(i18n::ct(pr)));
    for (const auto& w : plan_->warnings()) problems_->addItem(QStringLiteral("! ") + i18n::ct(w));
    if (problems_->count() == 0) problems_->addItem(tr("No problems found."));
    diff_->setPlainText(dirty_ ? qs(plan_->diff(on_disk_)) : tr("No staged changes."));
    updateButtons();
}

void PartitionEditorDialog::onNewTable() {
    QDialog d(this);
    d.setWindowTitle(tr("New Partition Table"));
    auto* f = new QFormLayout(&d);
    auto* scheme = new QComboBox(&d);
    scheme->addItem(QStringLiteral("GPT"), static_cast<int>(TableType::gpt));
    scheme->addItem(QStringLiteral("MBR"), static_cast<int>(TableType::mbr));
    f->addRow(tr("Scheme:"), scheme);
    auto* warn = new QLabel(tr("The new table is empty. Applying it makes every existing partition unreachable."), &d);
    warn->setWordWrap(true);
    f->addRow(warn);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;

    auto dev = open_for_read(target_);
    if (!dev) {
        QMessageBox::warning(this, tr("New Table"), i18n::err(dev.error()));
        return;
    }
    auto plan = PartitionPlan::blank(static_cast<TableType>(scheme->currentData().toInt()), (*dev)->sector_size(),
                                     (*dev)->info().total_lbas());
    if (!plan) {
        QMessageBox::warning(this, tr("New Table"), i18n::err(plan.error()));
        return;
    }
    plan_ = std::move(plan).value();
    dirty_ = true;
    refreshView();
}

void PartitionEditorDialog::onAdd() {
    if (!plan_) return;
    const bool gpt = plan_->type() == TableType::gpt;
    QDialog d(this);
    d.setWindowTitle(tr("Add Partition"));
    auto* f = new QFormLayout(&d);
    auto* type = new QComboBox(&d);
    for (const auto& a : type_aliases()) {
        if (!gpt && a.mbr_type == 0) continue;
        type->addItem(QStringLiteral("%1 - %2").arg(QString::fromUtf8(a.name), i18n::ct(a.description)),
                      QString::fromUtf8(a.name));
    }
    const int def = type->findData(QStringLiteral("linux"));
    if (def >= 0) type->setCurrentIndex(def);
    f->addRow(tr("Type:"), type);
    auto* size = new QLineEdit(QStringLiteral("max"), &d);
    size->setToolTip(tr("e.g. 512M, 20G, 1.5T or max (fill the free space)"));
    f->addRow(tr("Size:"), size);
    auto* name = new QLineEdit(&d);
    if (gpt) f->addRow(tr("Name:"), name);
    QCheckBox* boot = nullptr;
    QCheckBox* logical = nullptr;
    QCheckBox* extended = nullptr;
    if (!gpt) {
        boot = new QCheckBox(tr("Bootable (active)"), &d);
        logical = new QCheckBox(tr("Logical partition (inside the extended partition)"), &d);
        extended = new QCheckBox(tr("This is the extended partition (container)"), &d);
        f->addRow(boot);
        f->addRow(logical);
        f->addRow(extended);
        connect(extended, &QCheckBox::toggled, &d, [=](bool on) { if (on) logical->setChecked(false); });
        connect(logical, &QCheckBox::toggled, &d, [=](bool on) { if (on) extended->setChecked(false); });
    }
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;

    PartitionSpec s;
    s.logical = logical && logical->isChecked();
    s.extended = extended && extended->isChecked();
    s.bootable = boot && boot->isChecked();
    auto fit = plan_->first_fit_start(kMiB1, s.logical);
    if (!fit) {
        QMessageBox::warning(this, tr("Add Partition"), tr("There is no free space left."));
        return;
    }
    s.first_lba = *fit;
    auto bytes = parse_size_text(size->text(), true);
    if (!bytes) {
        QMessageBox::warning(this, tr("Add Partition"), tr("Enter a size such as 512M, 20G or max."));
        return;
    }
    if (*bytes % plan_->sector_size() != 0) {
        QMessageBox::warning(this, tr("Add Partition"), tr("The size must be a multiple of %1 bytes.").arg(plan_->sector_size()));
        return;
    }
    s.lba_count = *bytes / plan_->sector_size();
    if (auto a = find_type_alias(type->currentData().toString().toStdString())) {
        if (gpt) s.type_guid = *Guid::parse(a->gpt_guid);
        else s.mbr_type = a->mbr_type;
    }
    if (extended && extended->isChecked()) s.mbr_type = 0x0F;
    s.name = name->text().toStdString();
    auto n = plan_->add(s);
    if (!n) {
        QMessageBox::warning(this, tr("Add Partition"), i18n::err(n.error()));
        return;
    }
    dirty_ = true;
    refreshView();
}

void PartitionEditorDialog::onDelete() {
    auto n = selectedNumber();
    if (!plan_ || !n) return;
    if (QMessageBox::question(this, tr("Delete Partition"),
                              tr("Remove partition %1 from the staged table? The data stays on disk until overwritten, "
                                 "but becomes unreachable once the change is applied.")
                                  .arg(*n)) != QMessageBox::Yes)
        return;
    if (auto s = plan_->remove(*n); !s) {
        QMessageBox::warning(this, tr("Delete Partition"), i18n::err(s.error()));
        return;
    }
    dirty_ = true;
    refreshView();
}

void PartitionEditorDialog::onProperties() {
    auto n = selectedNumber();
    if (!plan_ || !n) return;
    const Partition* p = plan_->find(*n);
    if (!p) return;
    const bool gpt = plan_->type() == TableType::gpt;
    QDialog d(this);
    d.setWindowTitle(tr("Partition %1 Properties").arg(*n));
    auto* f = new QFormLayout(&d);
    auto* type = new QComboBox(&d);
    for (const auto& a : type_aliases()) {
        if (!gpt && a.mbr_type == 0) continue;
        type->addItem(QStringLiteral("%1 - %2").arg(QString::fromUtf8(a.name), i18n::ct(a.description)),
                      QString::fromUtf8(a.name));
    }
    type->insertItem(0, tr("(unchanged: %1)").arg(i18n::ct(p->type_name)), QString());
    type->setCurrentIndex(0);
    f->addRow(tr("Type:"), type);
    auto* name = new QLineEdit(qs(p->name), &d);
    if (gpt) f->addRow(tr("Name:"), name);
    QCheckBox* boot = nullptr;
    if (!gpt) {
        boot = new QCheckBox(tr("Bootable (active)"), &d);
        boot->setChecked(p->bootable);
        f->addRow(boot);
    }
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;

    auto check = [this](const Status& s) {
        if (!s) QMessageBox::warning(this, tr("Properties"), i18n::err(s.error()));
        return static_cast<bool>(s);
    };
    if (const auto key = type->currentData().toString(); !key.isEmpty()) {
        if (auto a = find_type_alias(key.toStdString())) {
            if (gpt) {
                if (!check(plan_->set_type_guid(*n, *Guid::parse(a->gpt_guid)))) return;
            } else if (!check(plan_->set_mbr_type(*n, a->mbr_type))) {
                return;
            }
        }
    }
    if (gpt && name->text().toStdString() != p->name && !check(plan_->set_name(*n, name->text().toStdString()))) return;
    if (boot && boot->isChecked() != p->bootable && !check(plan_->set_bootable(*n, boot->isChecked()))) return;
    dirty_ = true;
    refreshView();
}

void PartitionEditorDialog::onResize() {
    auto n = selectedNumber();
    if (!plan_ || !n) return;
    const Partition* p = plan_->find(*n);
    if (!p) return;
    QDialog d(this);
    d.setWindowTitle(tr("Resize Partition %1").arg(*n));
    auto* f = new QFormLayout(&d);
    auto* first = new QLineEdit(QString::number(p->first_lba), &d);
    auto* last = new QLineEdit(QString::number(p->last_lba), &d);
    f->addRow(tr("Start LBA:"), first);
    f->addRow(tr("End LBA:"), last);
    auto* warn = new QLabel(tr("Only the table changes. The file system and its data are NOT moved or resized; "
                               "moving the start makes existing data unreadable through this partition."),
                            &d);
    warn->setWordWrap(true);
    f->addRow(warn);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;
    bool ok1 = false, ok2 = false;
    const auto a = first->text().toULongLong(&ok1);
    const auto b = last->text().toULongLong(&ok2);
    if (!ok1 || !ok2) {
        QMessageBox::warning(this, tr("Resize"), tr("Enter both LBAs as numbers."));
        return;
    }
    if (auto s = plan_->resize(*n, a, b); !s) {
        QMessageBox::warning(this, tr("Resize"), i18n::err(s.error()));
        return;
    }
    dirty_ = true;
    refreshView();
}

void PartitionEditorDialog::onRevert() {
    if (!dirty_) return;
    reloadFromDisk();
    refreshView();
}

void PartitionEditorDialog::onApply() {
    if (!plan_ || !dirty_) return;
    if (!plan_->problems().empty()) return;
    auto ranges = plan_->metadata_ranges();
    if (!ranges) {
        QMessageBox::warning(this, tr("Apply"), i18n::err(ranges.error()));
        return;
    }
    const auto summary = tr("Rewrite the %1 partition table:\n%2").arg(qs(table_type_name(plan_->type())).toUpper(), qs(plan_->diff(on_disk_)));
    auto out = confirm_and_authorize(this, tr("Confirm Partition Table Change"),
                                     make_request(target_, safety::Kind::partition_edit, *ranges, summary), target_.show_serial);
    if (out.cancelled) return;
    if (!out.target) {
        emit logEvent(Severity::error, tr("Partition edit refused: %1").arg(out.error), target_.path);
        QMessageBox::warning(this, tr("Apply"), out.error);
        return;
    }
    std::vector<std::string> notes;
    if (auto s = apply_plan(*out.target, *plan_, &notes); !s) {
        const auto msg = i18n::err(s.error());
        emit logEvent(Severity::error, tr("Writing the partition table failed: %1").arg(msg), target_.path);
        QMessageBox::critical(this, tr("Apply"), tr("Writing the partition table failed:\n%1\n\nThe table on disk may be "
                                                    "inconsistent. Re-read it before doing anything else.").arg(msg));
        out.target.reset();
        emit tableWritten();
        reloadFromDisk();
        refreshView();
        return;
    }
    out.target.reset();  // release the exclusive lease before re-reading
    for (const auto& n : notes) emit logEvent(Severity::warning, i18n::ct(n), target_.path);
    emit logEvent(Severity::warning, tr("Partition table written and verified (%1 region(s))").arg(ranges->size()), target_.path);
    emit tableWritten();
    reloadFromDisk();
    refreshView();
}

void PartitionEditorDialog::onFormat() {
    auto n = selectedNumber();
    if (!plan_ || !n || dirty_) return;
    const Partition* p = plan_->find(*n);
    if (!p || p->extended_container) return;

    QDialog d(this);
    d.setWindowTitle(tr("Format Partition %1").arg(*n));
    auto* f = new QFormLayout(&d);
    auto* fs = new QComboBox(&d);
    fs->addItem(QStringLiteral("FAT32"), static_cast<int>(mkfs::FsType::fat32));
    fs->addItem(QStringLiteral("exFAT"), static_cast<int>(mkfs::FsType::exfat));
    f->addRow(tr("File system:"), fs);
    auto* label = new QLineEdit(&d);
    label->setMaxLength(11);
    f->addRow(tr("Label:"), label);
    auto* cluster = new QComboBox(&d);
    cluster->addItem(tr("Automatic"), 0);
    for (int kb : {4, 8, 16, 32, 64, 128}) cluster->addItem(i18n::units(QStringLiteral("%1 KiB").arg(kb)), kb * 1024);
    f->addRow(tr("Cluster size:"), cluster);
    auto* note = new QLabel(tr("Quick format: only file system structures are written. Existing data stays recoverable "
                               "until overwritten - use the ERASE tab first if that matters. NTFS, ext4 and others are not "
                               "available here."),
                            &d);
    note->setWordWrap(true);
    f->addRow(note);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;

    mkfs::FormatConfig cfg;
    cfg.type = static_cast<mkfs::FsType>(fs->currentData().toInt());
    cfg.label = label->text().toStdString();
    cfg.cluster_bytes = static_cast<std::uint32_t>(cluster->currentData().toInt());

    const auto summary = tr("Format partition %1 as %2%3").arg(*n).arg(fs->currentText(),
                                                                       label->text().isEmpty() ? QString() : tr(" (label %1)").arg(label->text()));
    auto out = confirm_and_authorize(this, tr("Confirm Format"),
                                     make_request(target_, safety::Kind::filesystem_create, {LbaRange{p->first_lba, p->lba_count()}}, summary),
                                     target_.show_serial);
    if (out.cancelled) return;
    if (!out.target) {
        emit logEvent(Severity::error, tr("Format refused: %1").arg(out.error), target_.path);
        QMessageBox::warning(this, tr("Format"), out.error);
        return;
    }
    auto op = std::make_unique<mkfs::Format>(std::move(*out.target), cfg);
    if (auto v = op->validate(); !v) {
        QMessageBox::warning(this, tr("Format"), i18n::err(v.error()));
        return;
    }
    if (auto s = op->start(); !s) {
        QMessageBox::warning(this, tr("Format"), i18n::err(s.error()));
        return;
    }
    emit logEvent(Severity::warning, tr("Format started: %1").arg(summary), target_.path);

    QProgressDialog prog(tr("Writing file system structures..."), tr("Cancel"), 0, 1000, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(0);
    QTimer t;
    connect(&t, &QTimer::timeout, &prog, [&] {
        const auto pr = op->progress();
        prog.setValue(static_cast<int>(pr.percent() * 10));
        if (prog.wasCanceled()) op->cancel();
    });
    t.start(100);
    while (!is_terminal(op->state())) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    t.stop();
    op->wait();
    prog.close();
    const auto r = op->result();
    op.reset();  // releases the lease
    const bool ok = r.state == OperationState::completed;
    emit logEvent(ok ? Severity::info : Severity::error,
                  ok ? tr("Format finished (%1)").arg(r.verified ? tr("verified") : tr("not verified")) : tr("Format %1: %2").arg(i18n::ct(operation_state_name(r.state)), i18n::ct(r.failure)),
                  target_.path);
    QMessageBox box(ok ? QMessageBox::Information : QMessageBox::Warning, tr("Format"),
                    ok ? tr("Format complete.") : tr("Format did not complete: %1").arg(r.failure.empty() ? i18n::ct(operation_state_name(r.state)) : i18n::ct(r.failure)),
                    QMessageBox::Ok, this);
    box.setDetailedText(qs(mkfs::format_to_text(r)));
    box.exec();
    emit tableWritten();
    reloadFromDisk();
    refreshView();
}

}  // namespace platter::gui
