// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QString>

class QTableView;
class QAbstractItemModel;
class QWidget;

namespace platter::gui {

// Classic dense table setup: row selection, small rows, no focus rect noise,
// sortable (if `sortable`), persistent column widths under `settings_key`, and a
// right-click menu with Copy (TSV) / Select All / Export to CSV (spec §2.5).
void setup_table(QTableView* view, const QString& settings_key, bool sortable = true);

// Saves header state for every table set up with setup_table (call on close).
void save_table_state(QTableView* view);

QString model_to_csv(const QAbstractItemModel* model);
bool export_csv(QWidget* parent, const QAbstractItemModel* model, const QString& suggested_name);

}  // namespace platter::gui
