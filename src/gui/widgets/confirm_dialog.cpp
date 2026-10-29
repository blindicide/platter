// SPDX-License-Identifier: TBD
#include "confirm_dialog.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace platter::gui {

namespace {

class ConfirmDialog final : public QDialog {
public:
    ConfirmDialog(QWidget* parent, const QString& title, const safety::Preview& preview, bool show_serial)
        : QDialog(parent), preview_(preview) {
        setWindowTitle(title);
        setModal(true);
        auto* v = new QVBoxLayout(this);

        auto* head = new QLabel(preview.allowed() ? tr("<b>This operation will DESTROY data and cannot be undone.</b>")
                                                  : tr("<b>This operation is REFUSED.</b>"),
                                this);
        head->setWordWrap(true);
        v->addWidget(head);

        auto* details = new QPlainTextEdit(this);
        details->setReadOnly(true);
        details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        details->setLineWrapMode(QPlainTextEdit::NoWrap);
        details->setPlainText(QString::fromStdString(safety::preview_text(preview, show_serial)));
        details->setMinimumSize(520, 170);
        v->addWidget(details, 1);

        if (preview.allowed()) {
            auto* prompt = new QLabel(QString::fromStdString(preview.prompt), this);
            prompt->setWordWrap(true);
            v->addWidget(prompt);
            edit_ = new QLineEdit(this);
            edit_->setPlaceholderText(tr("type the confirmation here"));
            v->addWidget(edit_);
        }

        buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        ok_ = buttons_->addButton(tr("&Destroy Data and Start"), QDialogButtonBox::AcceptRole);
        ok_->setEnabled(false);
        ok_->setAutoDefault(false);
        ok_->setDefault(false);
        buttons_->button(QDialogButtonBox::Cancel)->setDefault(true);
        v->addWidget(buttons_);
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
        if (edit_) {
            connect(edit_, &QLineEdit::textChanged, this, [this](const QString& t) {
                ok_->setEnabled(preview_.confirmation_ok(t.toStdString()));
            });
            edit_->setFocus();
        }
    }

    QString typed() const { return edit_ ? edit_->text() : QString(); }

private:
    safety::Preview preview_;
    QLineEdit* edit_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* ok_ = nullptr;
};

}  // namespace

AuthorizeOutcome confirm_and_authorize(QWidget* parent, const QString& title, const safety::Request& request,
                                       bool show_serial) {
    AuthorizeOutcome out;
    auto preview = safety::make_preview(request);
    if (!preview) {
        out.error = QString::fromStdString(preview.error().describe());
        return out;
    }
    ConfirmDialog dlg(parent, title, *preview, show_serial);
    if (dlg.exec() != QDialog::Accepted || !preview->allowed()) {
        out.cancelled = true;
        return out;
    }
    auto target = safety::authorize(*preview, dlg.typed().toStdString());
    if (!target) {
        out.error = QString::fromStdString(target.error().describe());
        return out;
    }
    out.target.emplace(std::move(target).value());
    return out;
}

}  // namespace platter::gui
