// SPDX-License-Identifier: TBD
#include "preferences_dialog.hpp"

#include "../i18n/i18n.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

namespace platter::gui {

Preferences Preferences::load() {
    QSettings s;
    Preferences p;
    p.record_history = s.value(QStringLiteral("history/record"), true).toBool();
    p.classic_theme = s.value(QStringLiteral("prefs/theme"), QStringLiteral("classic")).toString() != QStringLiteral("system");
    p.show_toolbar = s.value(QStringLiteral("prefs/show_toolbar"), true).toBool();
    p.show_statusbar = s.value(QStringLiteral("prefs/show_statusbar"), true).toBool();
    p.helper_at_startup = s.value(QStringLiteral("prefs/helper_at_startup"), false).toBool();
    p.helper_command = s.value(QStringLiteral("prefs/helper_command")).toString();
    p.log_file = s.value(QStringLiteral("prefs/log_file")).toString();
    p.history_db = s.value(QStringLiteral("prefs/history_db")).toString();
    p.language = s.value(QStringLiteral("prefs/language"), QStringLiteral("en")).toString();
    return p;
}

void Preferences::save() const {
    QSettings s;
    s.setValue(QStringLiteral("history/record"), record_history);
    s.setValue(QStringLiteral("prefs/theme"), classic_theme ? QStringLiteral("classic") : QStringLiteral("system"));
    s.setValue(QStringLiteral("prefs/show_toolbar"), show_toolbar);
    s.setValue(QStringLiteral("prefs/show_statusbar"), show_statusbar);
    s.setValue(QStringLiteral("prefs/helper_at_startup"), helper_at_startup);
    s.setValue(QStringLiteral("prefs/helper_command"), helper_command);
    s.setValue(QStringLiteral("prefs/log_file"), log_file);
    s.setValue(QStringLiteral("prefs/history_db"), history_db);
    s.setValue(QStringLiteral("prefs/language"), language);
}

PreferencesDialog::PreferencesDialog(const Preferences& cur, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Preferences"));
    auto* col = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    col->addWidget(tabs);

    // ---- General
    auto* general = new QWidget;
    auto* gf = new QFormLayout(general);
    record_ = new QCheckBox(tr("Record finished benchmarks, scans, SMART queries, erases, images and qualifications in the history"), general);
    record_->setChecked(cur.record_history);
    gf->addRow(record_);
    helper_start_ = new QCheckBox(tr("Start the privileged helper when platter starts (Linux)"), general);
    helper_start_->setChecked(cur.helper_at_startup);
#ifndef PLATTER_HAVE_HELPER
    helper_start_->setEnabled(false);
    helper_start_->setToolTip(tr("The privileged helper is not available on this platform"));
#endif
    gf->addRow(helper_start_);
    language_ = new QComboBox(general);
    for (const auto& l : i18n::available_languages()) language_->addItem(l.native_name, l.code);
    const int li = language_->findData(cur.language);
    language_->setCurrentIndex(li >= 0 ? li : 0);
    gf->addRow(tr("Language:"), language_);
    auto* lang_note = new QLabel(tr("The language changes the next time platter starts."), general);
    lang_note->setWordWrap(true);
    gf->addRow(lang_note);
    auto* note = new QLabel(tr("Serial numbers are always hidden when platter starts; turn them on from Settings > Show Serial Numbers "
                               "when you need them. That choice is deliberately not remembered."), general);
    note->setWordWrap(true);
    gf->addRow(note);
    tabs->addTab(general, tr("General"));

    // ---- Interface
    auto* ui = new QWidget;
    auto* uf = new QFormLayout(ui);
    theme_ = new QComboBox(ui);
    theme_->addItem(tr("Classic (Windows 2000 style)"), true);
    theme_->addItem(tr("Platform default"), false);
    theme_->setCurrentIndex(cur.classic_theme ? 0 : 1);
    uf->addRow(tr("Look:"), theme_);
    toolbar_ = new QCheckBox(tr("Show the toolbar"), ui);
    toolbar_->setChecked(cur.show_toolbar);
    statusbar_ = new QCheckBox(tr("Show the status bar"), ui);
    statusbar_->setChecked(cur.show_statusbar);
    uf->addRow(toolbar_);
    uf->addRow(statusbar_);
    tabs->addTab(ui, tr("Interface"));

    // ---- Logging
    auto* logging = new QWidget;
    auto* lf = new QFormLayout(logging);
    auto* lrow = new QHBoxLayout;
    log_file_ = new QLineEdit(cur.log_file, logging);
    log_file_->setPlaceholderText(tr("(none) - the log is kept in memory only"));
    auto* lbrowse = new QPushButton(tr("Browse..."), logging);
    lrow->addWidget(log_file_, 1);
    lrow->addWidget(lbrowse);
    lf->addRow(tr("Append log to file:"), lrow);
    auto* lnote = new QLabel(tr("One line per event: time, level, device, message. Serial numbers are not written."), logging);
    lnote->setWordWrap(true);
    lf->addRow(lnote);
    tabs->addTab(logging, tr("Logging"));

    // ---- Advanced
    auto* adv = new QWidget;
    auto* af = new QFormLayout(adv);
    helper_cmd_ = new QLineEdit(cur.helper_command, adv);
    helper_cmd_->setPlaceholderText(tr("automatic: pkexec or sudo + platter-helper"));
    af->addRow(tr("Helper command:"), helper_cmd_);
    auto* drow = new QHBoxLayout;
    history_db_ = new QLineEdit(cur.history_db, adv);
    history_db_->setPlaceholderText(tr("default location"));
    auto* dbrowse = new QPushButton(tr("Browse..."), adv);
    drow->addWidget(history_db_, 1);
    drow->addWidget(dbrowse);
    af->addRow(tr("History database:"), drow);
    auto* anote = new QLabel(tr("The history database location applies after restarting platter. $PLATTER_HISTORY_DB, if set, wins."), adv);
    anote->setWordWrap(true);
    af->addRow(anote);
    auto* reset = new QPushButton(tr("Reset All Settings..."), adv);
    af->addRow(reset);
    tabs->addTab(adv, tr("Advanced"));

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    col->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { emit applied(result()); });
    connect(lbrowse, &QPushButton::clicked, this, &PreferencesDialog::browseLog);
    connect(dbrowse, &QPushButton::clicked, this, &PreferencesDialog::browseDb);
    connect(reset, &QPushButton::clicked, this, &PreferencesDialog::resetAll);
}

Preferences PreferencesDialog::result() const {
    Preferences p;
    p.record_history = record_->isChecked();
    p.classic_theme = theme_->currentData().toBool();
    p.show_toolbar = toolbar_->isChecked();
    p.show_statusbar = statusbar_->isChecked();
    p.helper_at_startup = helper_start_->isChecked();
    p.helper_command = helper_cmd_->text().trimmed();
    p.log_file = log_file_->text().trimmed();
    p.history_db = history_db_->text().trimmed();
    p.language = language_->currentData().toString();
    return p;
}

void PreferencesDialog::browseLog() {
    const auto f = QFileDialog::getSaveFileName(this, tr("Log file"), log_file_->text(), tr("Log files (*.log *.txt);;All files (*)"),
                                                nullptr, QFileDialog::DontConfirmOverwrite);
    if (!f.isEmpty()) log_file_->setText(f);
}

void PreferencesDialog::browseDb() {
    const auto f = QFileDialog::getSaveFileName(this, tr("History database"), history_db_->text(), tr("SQLite database (*.db);;All files (*)"),
                                                nullptr, QFileDialog::DontConfirmOverwrite);
    if (!f.isEmpty()) history_db_->setText(f);
}

void PreferencesDialog::resetAll() {
    const auto a = QMessageBox::question(this, tr("Reset All Settings"),
                                         tr("Forget every saved setting (window layout, tab options, preferences)? platter returns to its "
                                            "defaults the next time it starts. Saved history is not touched."),
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (a != QMessageBox::Yes) return;
    QSettings s;
    s.clear();
    s.sync();
    QMessageBox::information(this, tr("Reset All Settings"), tr("Done. Restart platter to start from the defaults."));
    reject();  // do not write the old values back on OK
}

}  // namespace platter::gui
