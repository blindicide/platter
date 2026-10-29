// SPDX-License-Identifier: TBD
#include "../i18n/i18n.hpp"
#include "confirm_dialog.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

#include "../i18n/i18n.hpp"

namespace platter::gui {

namespace {

// The same layout as safety::preview_text(), with translated labels and text.
QString localized_preview_text(const safety::Preview& p, bool show_serial, const QString& summary) {
    const auto& d = p.device;
    const auto qs = [](const std::string& s) { return QString::fromStdString(s); };
    // Fixed-width labels so the values line up whatever the language.
    const QStringList labels{QCoreApplication::translate("ConfirmDialog", "Target:"),
                             QCoreApplication::translate("ConfirmDialog", "Device:"),
                             QCoreApplication::translate("ConfirmDialog", "Serial:"),
                             QCoreApplication::translate("ConfirmDialog", "Sectors:"),
                             QCoreApplication::translate("ConfirmDialog", "Operation:"),
                             QCoreApplication::translate("ConfirmDialog", "Range:"),
                             QCoreApplication::translate("ConfirmDialog", "Affected:"),
                             QCoreApplication::translate("ConfirmDialog", "Warning:"),
                             QCoreApplication::translate("ConfirmDialog", "REFUSED:"),
                             QCoreApplication::translate("ConfirmDialog", "DISMOUNT:")};
    int width = 0;
    for (const auto& l : labels) width = std::max(width, static_cast<int>(l.size()));
    const auto line = [&](int i, const QString& value) {
        return labels[i].leftJustified(width + 2) + value + QLatin1Char('\n');
    };
    QString o;
    o += line(0, qs(d.path));
    o += line(1, QCoreApplication::translate("ConfirmDialog", "%1, %2, %3")
                     .arg(d.model.empty() ? QCoreApplication::translate("ConfirmDialog", "(unknown model)") : (d.bus == BusType::image ? i18n::ct(d.model) : qs(d.model)),
                          i18n::units(qs(format_capacity(d.capacity_bytes))), i18n::ct(bus_type_name(d.bus))));
    if (show_serial && !d.serial.empty()) o += line(2, qs(d.serial));
    o += line(3, QCoreApplication::translate("ConfirmDialog", "%1 B logical / %2 B physical")
                     .arg(d.logical_sector_size)
                     .arg(d.physical_sector_size));
    o += line(4, summary.isEmpty() ? i18n::ct(p.request.summary) : summary);
    if (p.whole_device) {
        o += line(5, QCoreApplication::translate("ConfirmDialog", "WHOLE DEVICE (%1 sectors, %2)")
                         .arg(d.total_lbas())
                         .arg(i18n::units(qs(format_capacity(p.affected_bytes)))));
    } else {
        for (const auto& r : p.ranges)
            o += line(5, QCoreApplication::translate("ConfirmDialog", "LBA %1 - %2 (%3 sectors)").arg(r.first).arg(r.last()).arg(r.count));
        o += line(6, i18n::units(qs(format_capacity(p.affected_bytes))));
    }
    for (const auto& w : p.warnings) o += line(7, i18n::ct(w));
    for (const auto& m : p.mounted)
        o += line(9, QCoreApplication::translate("ConfirmDialog", "the volume mounted at %1 will be locked and dismounted").arg(qs(m)));
    for (const auto& b : p.blockers) o += line(8, i18n::ct(b));
    return o;
}

class ConfirmDialog final : public QDialog {
public:
    ConfirmDialog(QWidget* parent, const QString& title, const safety::Preview& preview, bool show_serial,
                  const QString& localized_summary)
        : QDialog(parent), preview_(preview) {
        setWindowTitle(title);
        setModal(true);
        auto* v = new QVBoxLayout(this);

        auto* head = new QLabel(preview.allowed() ? QCoreApplication::translate("ConfirmDialog", "<b>This operation will DESTROY data and cannot be undone.</b>")
                                                  : QCoreApplication::translate("ConfirmDialog", "<b>This operation is REFUSED.</b>"),
                                this);
        head->setWordWrap(true);
        v->addWidget(head);

        auto* details = new QPlainTextEdit(this);
        details->setReadOnly(true);
        details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        details->setLineWrapMode(QPlainTextEdit::NoWrap);
        details->setPlainText(localized_preview_text(preview, show_serial, localized_summary));
        details->setMinimumSize(520, 170);
        v->addWidget(details, 1);

        if (preview.allowed()) {
            auto* prompt = new QLabel(i18n::ct(preview.prompt), this);
            prompt->setWordWrap(true);
            v->addWidget(prompt);
            edit_ = new QLineEdit(this);
            edit_->setPlaceholderText(QCoreApplication::translate("ConfirmDialog", "type the confirmation here"));
            v->addWidget(edit_);
            if (preview.needs_dismount_ack()) {
                QStringList vols;
                for (const auto& m : preview.mounted) vols << QString::fromStdString(m);
                ack_ = new QCheckBox(QCoreApplication::translate("ConfirmDialog",
                                         "I understand that the mounted volumes (%1) will be locked and dismounted, "
                                         "and programs using them will lose unsaved data.")
                                         .arg(vols.join(QStringLiteral(", "))),
                                     this);
                v->addWidget(ack_);
            }
        }

        buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        ok_ = buttons_->addButton(QCoreApplication::translate("ConfirmDialog", "&Destroy Data and Start"), QDialogButtonBox::AcceptRole);
        ok_->setEnabled(false);
        ok_->setAutoDefault(false);
        ok_->setDefault(false);
        buttons_->button(QDialogButtonBox::Cancel)->setDefault(true);
        v->addWidget(buttons_);
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
        if (edit_) {
            const auto refresh = [this] {
                ok_->setEnabled(preview_.confirmation_ok(edit_->text().toStdString()) && (!ack_ || ack_->isChecked()));
            };
            connect(edit_, &QLineEdit::textChanged, this, refresh);
            if (ack_) connect(ack_, &QCheckBox::toggled, this, refresh);
            edit_->setFocus();
        }
    }

    QString typed() const { return edit_ ? edit_->text() : QString(); }
    bool acknowledged() const { return ack_ && ack_->isChecked(); }

private:
    safety::Preview preview_;
    QLineEdit* edit_ = nullptr;
    QCheckBox* ack_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* ok_ = nullptr;
};

}  // namespace

AuthorizeOutcome confirm_and_authorize(QWidget* parent, const QString& title, const safety::Request& request,
                                       bool show_serial, const QString& localized_summary) {
    AuthorizeOutcome out;
    auto preview = safety::make_preview(request);
    if (!preview) {
        out.error = i18n::err(preview.error());
        return out;
    }
    ConfirmDialog dlg(parent, title, *preview, show_serial, localized_summary);
    if (dlg.exec() != QDialog::Accepted || !preview->allowed()) {
        out.cancelled = true;
        return out;
    }
    safety::AuthorizeOptions ao;
    ao.acknowledge_dismount = dlg.acknowledged();
    auto target = safety::authorize(*preview, dlg.typed().toStdString(), ao);
    if (!target) {
        out.error = i18n::err(target.error());
        return out;
    }
    out.target.emplace(std::move(target).value());
    return out;
}

}  // namespace platter::gui
