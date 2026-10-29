// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace platter::gui {

// Persistent preferences (QSettings). Things that are decided per operation
// (block sizes, patterns, ...) live on their tabs and are remembered there.
struct Preferences {
    bool record_history = true;
    bool classic_theme = true;
    bool show_toolbar = true;
    bool show_statusbar = true;
    bool helper_at_startup = false;
    QString helper_command;   // empty = automatic (pkexec / sudo)
    QString log_file;         // empty = do not write one
    QString history_db;       // empty = default location (needs a restart)
    QString language = QStringLiteral("en");   // needs a restart

    static Preferences load();
    void save() const;
};

class PreferencesDialog final : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const Preferences& current, QWidget* parent = nullptr);
    Preferences result() const;

signals:
    void applied(const platter::gui::Preferences& p);

private slots:
    void browseLog();
    void browseDb();
    void resetAll();

private:
    QCheckBox* record_;
    QComboBox* theme_;
    QComboBox* language_;
    QCheckBox* toolbar_;
    QCheckBox* statusbar_;
    QCheckBox* helper_start_;
    QLineEdit* helper_cmd_;
    QLineEdit* log_file_;
    QLineEdit* history_db_;
};

}  // namespace platter::gui
