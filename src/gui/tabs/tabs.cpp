// SPDX-License-Identifier: LicenseRef-Proprietary
#include "../i18n/i18n.hpp"
#include "tabs.hpp"

#include <QBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>

#include "../widgets/log_model.hpp"
#include "../widgets/partition_bar.hpp"
#include "../widgets/table_tools.hpp"
#include "platter/reports/json.hpp"

namespace platter::gui {
namespace {

QString qs(const std::string& s) { return s.empty() ? QStringLiteral("-") : QString::fromStdString(s); }
QString qs(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<int>(s.size())); }

QStandardItem* item(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* it = new QStandardItem(text);
    it->setTextAlignment(align);
    it->setEditable(false);
    it->setData(text, Qt::UserRole);  // sort key; num() overrides with a number
    return it;
}

// Numeric cell that sorts numerically but displays `text`.
QStandardItem* num(qulonglong v, const QString& text) {
    auto* it = item(text, Qt::AlignRight | Qt::AlignVCenter);
    it->setData(v, Qt::UserRole);
    return it;
}

QString health_object_name(smart::Health h) {
    switch (h) {
        case smart::Health::good: return QStringLiteral("healthGood");
        case smart::Health::warning: return QStringLiteral("healthWarning");
        case smart::Health::critical: return QStringLiteral("healthCritical");
        default: return QStringLiteral("healthUnknown");
    }
}

void restyle(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}

QHBoxLayout* compact_row() {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    return row;
}

QVBoxLayout* compact_column(QWidget* w) {
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(4);
    return col;
}

}  // namespace

QString DeviceEntry::displayName() const {
    const auto cap = format_capacity(info.capacity_bytes);
    const auto short_cap = cap.substr(0, cap.find(" ("));
    QString model = info.bus == BusType::image ? i18n::ct(info.model) : QString::fromStdString(info.model);
    return QStringLiteral("%1  %2  [%3]").arg(QString::fromStdString(info.name), model, i18n::units(QString::fromStdString(short_cap)));
}

// ======================================================================= INFO

InfoTab::InfoTab(QWidget* parent) : QWidget(parent) {
    auto* col = compact_column(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);

    auto* g1 = new QGroupBox(tr("Identification"), split);
    auto* l1 = new QVBoxLayout(g1);
    l1->setContentsMargins(4, 4, 4, 4);
    props_ = new QStandardItemModel(0, 2, this);
    props_->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    props_view_ = new QTableView(g1);
    props_view_->setModel(props_);
    setup_table(props_view_, QStringLiteral("info.properties"), false);
    l1->addWidget(props_view_);

    auto* g2 = new QGroupBox(tr("Capabilities"), split);
    auto* l2 = new QVBoxLayout(g2);
    l2->setContentsMargins(4, 4, 4, 4);
    caps_ = new QStandardItemModel(0, 3, this);
    caps_->setHorizontalHeaderLabels({tr("Capability"), tr("Supported"), tr("Source")});
    caps_view_ = new QTableView(g2);
    caps_view_->setModel(caps_);
    setup_table(caps_view_, QStringLiteral("info.capabilities"), false);
    l2->addWidget(caps_view_);

    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    col->addWidget(split);
}

void InfoTab::setDevice(const DeviceEntry* e, bool show_serial) {
    props_->removeRows(0, props_->rowCount());
    caps_->removeRows(0, caps_->rowCount());
    if (!e) return;
    const auto& d = e->info;
    auto add = [&](const QString& k, const QString& v) { props_->appendRow({item(k), item(v)}); };
    add(tr("Device path"), qs(d.path));
    add(tr("Vendor"), qs(d.vendor));
    add(tr("Model"), d.bus == BusType::image ? i18n::ct(d.model) : qs(d.model));
    add(tr("Serial number"), show_serial ? qs(d.serial) : qs(reports::mask_serial(d.serial)));
    add(tr("Firmware revision"), qs(d.firmware));
    add(tr("Stable identifier"), qs(reports::redact_stable_id(d, {show_serial})));
    add(tr("Capacity"), qs(format_capacity(d.capacity_bytes)));
    add(tr("Total LBAs"), QString::number(d.total_lbas()));
    add(tr("Logical sector size"), QString::number(d.logical_sector_size) + tr(" bytes"));
    add(tr("Physical sector size"), QString::number(d.physical_sector_size) + tr(" bytes"));
    add(tr("Interface"), i18n::ct(bus_type_name(d.bus)));
    add(tr("Media type"), i18n::ct(media_kind_name(d.media)));
    add(tr("Removable"), i18n::ct(tri_name(d.removable)));
    add(tr("Read-only"), i18n::ct(tri_name(d.read_only)));
    add(tr("TRIM / discard"), i18n::ct(tri_name(d.trim_supported)));
    QStringList mps;
    for (const auto& m : d.mountpoints) mps << QString::fromStdString(m);
    add(tr("Mounted at"), mps.isEmpty() ? QStringLiteral("-") : mps.join(QStringLiteral(", ")));
    QStringList hs;
    for (const auto& h : d.holders) hs << QString::fromStdString(h);
    add(tr("Held by"), hs.isEmpty() ? QStringLiteral("-") : hs.join(QStringLiteral(", ")));
    add(tr("System disk"), d.is_system_disk ? tr("YES") : tr("No"));
    if (e->table) add(tr("Partition table"), qs(partitions::table_type_name(e->table->type)).toUpper());
    if (e->smart) {
        add(tr("SMART protocol"), i18n::ct(smart::protocol_name(e->smart->protocol)));
        if (e->smart->rotation_rate)
            add(tr("Rotation rate"), *e->smart->rotation_rate ? QString::number(*e->smart->rotation_rate) + tr(" rpm")
                                                              : tr("Solid state"));
    }
    add(tr("Bus topology"), qs(d.topology));
    props_view_->resizeColumnToContents(0);

    for (auto c : all_capabilities()) {
        const bool smart_cap = c == Capability::smart || c == Capability::self_test;
        QString supported = d.capabilities.has(c) ? tr("Yes") : tr("No");
        QString source = e->is_image ? tr("image file") : tr("OS device query");
        if (smart_cap && !e->is_image) {
            if (!e->smart) {
                supported = tr("Not probed");
                source = tr("run SMART query");
            } else {
                source = tr("smartctl");
            }
        }
        if ((c == Capability::secure_erase || c == Capability::sanitize || c == Capability::power_management) &&
            !d.capabilities.has(c))
            source = tr("not probed (v0.4+)");
        auto* sup = item(supported);
        if (d.capabilities.has(c)) sup->setForeground(QColor(0x00, 0x60, 0x00));
        caps_->appendRow({item(i18n::ct(capability_name(c))), sup, item(source)});
    }
    caps_view_->resizeColumnToContents(0);
}

void InfoTab::saveState() {
    save_table_state(props_view_);
    save_table_state(caps_view_);
}

// ====================================================================== SMART

SmartTab::SmartTab(QWidget* parent) : QWidget(parent) {
    auto* col = compact_column(this);
    auto* row = compact_row();
    query_ = new QPushButton(tr("&Query SMART"), this);
    query_->setToolTip(tr("Runs 'smartctl --json -a' (read-only)"));
    status_ = new QLabel(this);
    health_ = new QLabel(this);
    health_->setObjectName(QStringLiteral("healthUnknown"));
    row->addWidget(query_);
    row->addWidget(new QLabel(tr("Health:"), this));
    row->addWidget(health_);
    row->addWidget(status_, 1);
    col->addLayout(row);

    auto* split = new QSplitter(Qt::Vertical, this);
    attrs_ = new QStandardItemModel(0, 8, this);
    attrs_->setHorizontalHeaderLabels({tr("ID"), tr("Attribute"), tr("Current"), tr("Worst"), tr("Threshold"),
                                       tr("Raw (dec)"), tr("Raw (hex)"), tr("Status")});
    attrs_->setSortRole(Qt::UserRole);
    attrs_view_ = new QTableView(split);
    attrs_view_->setModel(attrs_);
    setup_table(attrs_view_, QStringLiteral("smart.attributes"));
    auto* fbox = new QGroupBox(tr("Assessment details"), split);
    auto* fl = new QVBoxLayout(fbox);
    fl->setContentsMargins(4, 4, 4, 4);
    findings_ = new QListWidget(fbox);
    fl->addWidget(findings_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    col->addWidget(split, 1);

    connect(query_, &QPushButton::clicked, this, &SmartTab::queryRequested);
    setDevice(nullptr);
}

void SmartTab::setBusy(bool busy) {
    query_->setEnabled(!busy);
    if (busy) status_->setText(tr("Querying smartctl..."));
}

void SmartTab::setDevice(const DeviceEntry* e) {
    attrs_->removeRows(0, attrs_->rowCount());
    findings_->clear();
    health_->setObjectName(QStringLiteral("healthUnknown"));
    health_->setText(tr("UNKNOWN"));
    restyle(health_);
    query_->setEnabled(e && !e->is_image);
    if (!e) {
        status_->setText(tr("No device selected."));
        return;
    }
    if (e->is_image) {
        status_->setText(tr("Image files have no SMART data."));
        return;
    }
    if (!e->smart_error.isEmpty()) {
        status_->setText(e->smart_error);
        return;
    }
    if (!e->smart) {
        status_->setText(tr("Not queried yet."));
        return;
    }
    const auto& r = *e->smart;
    status_->setText(tr("smartctl %1 (exit status %2), %3%4")
                         .arg(QString::fromStdString(r.smartctl_version))
                         .arg(r.smartctl_exit_status)
                         .arg(i18n::ct(smart::protocol_name(r.protocol)))
                         .arg(r.temperature_c ? tr(", %1 °C").arg(*r.temperature_c) : QString()));

    auto n = [](const std::optional<int>& v) {
        return v ? num(static_cast<qulonglong>(*v), QString::number(*v)) : num(0, QStringLiteral("-"));
    };
    for (const auto& a : r.attributes) {
        QString st = a.when_failed == "now" ? tr("FAILING NOW") : a.when_failed == "past" ? tr("Failed in past") : tr("OK");
        auto* status = item(st);
        if (a.when_failed == "now") status->setForeground(QColor(0xC0, 0, 0));
        char hex[24];
        std::snprintf(hex, sizeof hex, "%012llX", static_cast<unsigned long long>(a.raw_value));
        attrs_->appendRow({num(static_cast<qulonglong>(a.id), QString::number(a.id)), item(QString::fromStdString(a.name)),
                           n(a.value), n(a.worst), n(a.threshold),
                           num(a.raw_value, QString::fromStdString(a.raw_string)), item(QString::fromLatin1(hex)),
                           status});
    }
    if (r.nvme) {
        const auto& h = *r.nvme;
        int id = 1;
        auto nv = [&](const QString& name, const auto& v, const QString& unit = {}) {
            const bool has = static_cast<bool>(v);
            const qulonglong val = has ? static_cast<qulonglong>(*v) : 0;
            attrs_->appendRow({num(static_cast<qulonglong>(id), QString::number(id)), item(name), num(0, QStringLiteral("-")),
                               num(0, QStringLiteral("-")), num(0, QStringLiteral("-")),
                               num(val, has ? QString::number(val) + unit : QStringLiteral("-")),
                               item(has ? QString::number(val, 16).toUpper() : QStringLiteral("-")), item(tr("-"))});
            ++id;
        };
        nv(tr("Critical Warning"), std::optional<std::uint32_t>(h.critical_warning));
        nv(tr("Temperature"), h.temperature_c, QStringLiteral(" °C"));
        nv(tr("Available Spare"), h.available_spare, QStringLiteral("%"));
        nv(tr("Available Spare Threshold"), h.available_spare_threshold, QStringLiteral("%"));
        nv(tr("Percentage Used"), h.percentage_used, QStringLiteral("%"));
        nv(tr("Data Units Read"), h.data_units_read);
        nv(tr("Data Units Written"), h.data_units_written);
        nv(tr("Power Cycles"), h.power_cycles);
        nv(tr("Power On Hours"), h.power_on_hours);
        nv(tr("Unsafe Shutdowns"), h.unsafe_shutdowns);
        nv(tr("Media and Data Integrity Errors"), h.media_errors);
        nv(tr("Error Information Log Entries"), h.error_log_entries);
    }
    attrs_view_->resizeColumnsToContents();

    if (e->health) {
        health_->setObjectName(health_object_name(e->health->status));
        health_->setText(i18n::ct(smart::health_name(e->health->status)));
        restyle(health_);
        for (const auto& f : e->health->findings)
            findings_->addItem(QStringLiteral("[%1] %2").arg(i18n::ct(smart::health_name(f.severity)), i18n::ct(f.text)));
    }
}

void SmartTab::saveState() { save_table_state(attrs_view_); }

// ================================================================= PARTITIONS

PartitionsTab::PartitionsTab(QWidget* parent) : QWidget(parent) {
    auto* col = compact_column(this);
    auto* row = compact_row();
    read_ = new QPushButton(tr("&Read Partition Table"), this);
    read_->setToolTip(tr("Reads LBA 0-33 and the backup GPT (read-only)"));
    edit_ = new QPushButton(tr("&Edit Partitions..."), this);
    edit_->setToolTip(tr("Create or change the partition table and format partitions. Nothing is written without a typed confirmation."));
    summary_ = new QLabel(this);
    row->addWidget(read_);
    row->addWidget(edit_);
    row->addWidget(summary_, 1);
    col->addLayout(row);

    bar_ = new PartitionBar(this);
    col->addWidget(bar_);

    auto* split = new QSplitter(Qt::Vertical, this);
    parts_ = new QStandardItemModel(0, 8, this);
    parts_->setHorizontalHeaderLabels({tr("#"), tr("Start LBA"), tr("End LBA"), tr("Sectors"), tr("Size"), tr("Type"),
                                       tr("Name / Flags"), tr("Aligned")});
    parts_->setSortRole(Qt::UserRole);
    parts_view_ = new QTableView(split);
    parts_view_->setModel(parts_);
    setup_table(parts_view_, QStringLiteral("partitions.table"));
    auto* wbox = new QGroupBox(tr("Validation"), split);
    auto* wl = new QVBoxLayout(wbox);
    wl->setContentsMargins(4, 4, 4, 4);
    warnings_ = new QListWidget(wbox);
    wl->addWidget(warnings_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    col->addWidget(split, 1);

    connect(read_, &QPushButton::clicked, this, &PartitionsTab::readRequested);
    connect(edit_, &QPushButton::clicked, this, &PartitionsTab::editRequested);
    connect(bar_, &PartitionBar::partitionClicked, this, [this](int number) {
        for (int r = 0; r < parts_->rowCount(); ++r)
            if (parts_->item(r, 0)->data(Qt::UserRole).toInt() == number) parts_view_->selectRow(r);
    });
    setDevice(nullptr);
}

void PartitionsTab::setBusy(bool busy) {
    read_->setEnabled(!busy);
    edit_->setEnabled(!busy);
    if (busy) summary_->setText(tr("Reading..."));
}

void PartitionsTab::setDevice(const DeviceEntry* e) {
    parts_->removeRows(0, parts_->rowCount());
    warnings_->clear();
    read_->setEnabled(e != nullptr);
    edit_->setEnabled(e != nullptr);
    bar_->setTable(e ? e->table : std::nullopt);
    if (!e) {
        summary_->setText(tr("No device selected."));
        return;
    }
    if (!e->table_error.isEmpty()) {
        summary_->setText(e->table_error);
        return;
    }
    if (!e->table) {
        summary_->setText(tr("Not read yet. Reading sectors from a physical disk may require elevated privileges."));
        return;
    }
    const auto& t = *e->table;
    QString s = tr("Scheme: %1").arg(qs(partitions::table_type_name(t.type)).toUpper());
    if (t.type == partitions::TableType::gpt) {
        const auto& h = t.used_backup_gpt ? t.gpt_backup : t.gpt_primary;
        if (h) s += tr("   Disk GUID: %1").arg(QString::fromStdString(h->disk_guid.to_string()));
        s += t.used_backup_gpt ? tr("   (from BACKUP GPT)") : QString();
    } else if (t.type == partitions::TableType::mbr) {
        s += tr("   Disk signature: %1").arg(QString::number(t.mbr_disk_signature, 16).rightJustified(8, QLatin1Char('0')).toUpper());
    }
    s += tr("   %1 partitions").arg(t.partitions.size());
    summary_->setText(s);

    const bool gpt = t.type == partitions::TableType::gpt;
    for (const auto& p : t.partitions) {
        QString extra;
        if (gpt) extra = QString::fromStdString(p.name);
        else {
            QStringList f;
            if (p.bootable) f << tr("boot");
            if (p.extended_container) f << tr("extended");
            if (p.logical) f << tr("logical");
            extra = f.join(QStringLiteral(", "));
        }
        const auto cap = format_capacity(p.size_bytes);
        parts_->appendRow({num(p.number, QString::number(p.number)), num(p.first_lba, QString::number(p.first_lba)),
                           num(p.last_lba, QString::number(p.last_lba)),
                           num(p.lba_count(), QString::number(p.lba_count())),
                           num(p.size_bytes, i18n::units(QString::fromStdString(cap.substr(0, cap.find(" ("))))),
                           item(i18n::ct(p.type_name)), item(extra),
                           item(p.aligned_1mib ? tr("1 MiB") : tr("NO"))});
    }
    parts_view_->resizeColumnsToContents();
    if (t.warnings.empty()) warnings_->addItem(tr("No problems found."));
    for (const auto& w : t.warnings) warnings_->addItem(QStringLiteral("! ") + i18n::ct(w));
}

void PartitionsTab::saveState() { save_table_state(parts_view_); }

// ======================================================================== LOG

LogTab::LogTab(LogModel* model, QWidget* parent) : QWidget(parent) {
    auto* col = compact_column(this);
    view_ = new QTableView(this);
    view_->setModel(model);
    setup_table(view_, QStringLiteral("log.full"), false);
    view_->resizeColumnToContents(LogModel::Time);
    col->addWidget(view_);
    connect(model, &QAbstractItemModel::rowsInserted, view_, &QTableView::scrollToBottom);
}

void LogTab::saveState() { save_table_state(view_); }

// ==================================================================== PLANNED

PlannedTab::PlannedTab(const QString& feature, const QString& milestone, const QString& description, QWidget* parent)
    : QWidget(parent) {
    auto* col = compact_column(this);
    auto* box = new QGroupBox(feature, this);
    auto* l = new QVBoxLayout(box);
    auto* text = new QLabel(tr("%1\n\nPlanned for %2. This tab is a placeholder so the layout matches the "
                               "specification; no operation can be started from it.")
                                .arg(description, milestone),
                            box);
    text->setObjectName(QStringLiteral("placeholder"));
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    l->addWidget(text);
    l->addStretch(1);
    col->addWidget(box);
}

}  // namespace platter::gui
