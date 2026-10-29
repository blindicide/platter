// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Partition manager dialog (spec §10). Edits are STAGED in memory and shown as
// a diff; nothing is written until "Apply Changes" passes the typed-confirmation
// dialog. "Format..." works on the table as it is on disk and has its own
// confirmation. Neither moves or resizes file systems.
#pragma once

#include <QDialog>
#include <QString>
#include <cstdint>
#include <memory>
#include <optional>

#include "../widgets/log_model.hpp"
#include "platter/partitions/partition_edit.hpp"

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStandardItemModel;
class QTableView;

namespace platter::gui {

struct EditorTarget {
    QString path;
    QString label;  // display name
    bool is_image = false;
    std::uint32_t sector_size = 512;
    bool show_serial = false;
};

class PartitionEditorDialog final : public QDialog {
    Q_OBJECT
public:
    PartitionEditorDialog(const EditorTarget& target, QWidget* parent = nullptr);
    ~PartitionEditorDialog() override;

signals:
    void tableWritten();  // the on-disk table or a file system changed
    void logEvent(platter::gui::Severity s, const QString& message, const QString& device);

private slots:
    void onNewTable();
    void onAdd();
    void onDelete();
    void onProperties();
    void onResize();
    void onFormat();
    void onApply();
    void onRevert();
    void refreshView();

private:
    bool reloadFromDisk();
    std::optional<std::uint32_t> selectedNumber() const;
    void updateButtons();

    EditorTarget target_;
    std::optional<partitions::PartitionPlan> plan_;
    partitions::PartitionTable on_disk_;
    bool have_disk_ = false;
    bool dirty_ = false;

    QLabel* summary_;
    QTableView* view_;
    QStandardItemModel* model_;
    QListWidget* problems_;
    QPlainTextEdit* diff_;
    QPushButton* new_;
    QPushButton* add_;
    QPushButton* del_;
    QPushButton* props_;
    QPushButton* resize_;
    QPushButton* format_;
    QPushButton* apply_;
    QPushButton* revert_;
};

}  // namespace platter::gui
