// SPDX-License-Identifier: LicenseRef-Proprietary
#include "table_tools.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QTableView>
#include <QTextStream>
#include <map>

namespace platter::gui {
namespace {

const char* kKeyProp = "platterSettingsKey";

QString csv_escape(QString s) {
    if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))) {
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        s = QLatin1Char('"') + s + QLatin1Char('"');
    }
    return s;
}

void copy_selection(QTableView* view) {
    auto* sel = view->selectionModel();
    if (!sel || !sel->hasSelection()) return;
    auto idx = sel->selectedIndexes();
    std::sort(idx.begin(), idx.end(), [](const QModelIndex& a, const QModelIndex& b) {
        return a.row() != b.row() ? a.row() < b.row() : a.column() < b.column();
    });
    QString out;
    int row = idx.front().row();
    bool first = true;
    for (const auto& i : idx) {
        if (view->isColumnHidden(i.column())) continue;
        if (i.row() != row) {
            out += QLatin1Char('\n');
            row = i.row();
            first = true;
        }
        if (!first) out += QLatin1Char('\t');
        out += i.data().toString();
        first = false;
    }
    QApplication::clipboard()->setText(out);
}

}  // namespace

QString model_to_csv(const QAbstractItemModel* m) {
    QString out;
    QTextStream ts(&out);
    for (int c = 0; c < m->columnCount(); ++c)
        ts << (c ? "," : "") << csv_escape(m->headerData(c, Qt::Horizontal).toString());
    ts << "\n";
    for (int r = 0; r < m->rowCount(); ++r) {
        for (int c = 0; c < m->columnCount(); ++c)
            ts << (c ? "," : "") << csv_escape(m->index(r, c).data().toString());
        ts << "\n";
    }
    return out;
}

bool export_csv(QWidget* parent, const QAbstractItemModel* model, const QString& suggested_name) {
    const auto path = QFileDialog::getSaveFileName(parent, QObject::tr("Export to CSV"), suggested_name,
                                                   QObject::tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(parent, QObject::tr("Export to CSV"),
                             QObject::tr("Cannot write %1:\n%2").arg(path, f.errorString()));
        return false;
    }
    f.write(model_to_csv(model).toUtf8());
    return true;
}

void setup_table(QTableView* view, const QString& settings_key, bool sortable) {
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setAlternatingRowColors(true);
    view->setWordWrap(false);
    view->setSortingEnabled(sortable);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->verticalHeader()->setVisible(false);
    view->verticalHeader()->setMinimumSectionSize(view->fontMetrics().height());
    view->verticalHeader()->setDefaultSectionSize(view->fontMetrics().height() + 3);
    view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view->horizontalHeader()->setHighlightSections(false);
    view->horizontalHeader()->setStretchLastSection(true);
    view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    view->horizontalHeader()->setSectionsMovable(true);
    view->setContextMenuPolicy(Qt::ActionsContextMenu);
    view->setProperty(kKeyProp, settings_key);

    auto* copy = new QAction(QObject::tr("&Copy"), view);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetShortcut);
    QObject::connect(copy, &QAction::triggered, view, [view] { copy_selection(view); });
    auto* all = new QAction(QObject::tr("Select &All"), view);
    all->setShortcut(QKeySequence::SelectAll);
    all->setShortcutContext(Qt::WidgetShortcut);
    QObject::connect(all, &QAction::triggered, view, &QTableView::selectAll);
    auto* sep = new QAction(view);
    sep->setSeparator(true);
    auto* csv = new QAction(QObject::tr("&Export to CSV..."), view);
    QObject::connect(csv, &QAction::triggered, view, [view, settings_key] {
        if (view->model()) export_csv(view, view->model(), settings_key + QStringLiteral(".csv"));
    });
    view->addActions({copy, all, sep, csv});

    // Restore persisted column layout once a model with columns is attached.
    QSettings s;
    const auto state = s.value(QStringLiteral("tables/") + settings_key).toByteArray();
    if (!state.isEmpty()) view->horizontalHeader()->restoreState(state);
}

void save_table_state(QTableView* view) {
    const auto key = view->property(kKeyProp).toString();
    if (key.isEmpty()) return;
    QSettings s;
    s.setValue(QStringLiteral("tables/") + key, view->horizontalHeader()->saveState());
}

}  // namespace platter::gui
