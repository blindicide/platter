// SPDX-License-Identifier: LicenseRef-Proprietary
#include <QApplication>
#include <QCommandLineParser>
#include <QDialog>
#include <QIcon>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <algorithm>
#include <memory>

#include "mainwindow/main_window.hpp"
#include "widgets/checksum_dialog.hpp"
#include "widgets/confirm_dialog.hpp"
#include "widgets/help_dialog.hpp"
#include "widgets/history_dialog.hpp"
#include "widgets/partition_editor.hpp"
#include "widgets/preferences_dialog.hpp"
#include "platter/storage/history.hpp"
#include "tabs/benchmark_tab.hpp"
#include "tabs/hex_tab.hpp"
#include "tabs/surface_tab.hpp"
#include "platter/version.hpp"
#include "i18n/i18n.hpp"
#include "styles/classic_theme.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("platter"));
    QApplication::setApplicationName(QStringLiteral("platter"));
    QApplication::setApplicationVersion(QString::fromLatin1(platter::kVersion));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/platter.png")));
    platter::gui::remember_system_theme(app);
    platter::gui::apply_theme(app, QSettings().value(QStringLiteral("prefs/theme"), QStringLiteral("classic")).toString() != QStringLiteral("system"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("platter storage operations suite (GUI)"));
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument(QStringLiteral("images"), QStringLiteral("Raw image files to open read-only."));
    QCommandLineOption sector({QStringLiteral("sector-size")}, QStringLiteral("Sector size for images (512|4096)."),
                              QStringLiteral("bytes"), QStringLiteral("512"));
    // Development aids: render the window offscreen and save it as a PNG.
    QCommandLineOption shot({QStringLiteral("screenshot")}, QStringLiteral("Save a screenshot to FILE and exit."),
                            QStringLiteral("file"));
    QCommandLineOption tab({QStringLiteral("tab")}, QStringLiteral("Tab to show first."), QStringLiteral("name"));
    QCommandLineOption size({QStringLiteral("size")}, QStringLiteral("Window size WxH."), QStringLiteral("WxH"));
    QCommandLineOption select({QStringLiteral("select")}, QStringLiteral("Select device by index."), QStringLiteral("n"));
    QCommandLineOption smart_now({QStringLiteral("query-smart")}, QStringLiteral("Query SMART for the selection."));
    QCommandLineOption open_scan({QStringLiteral("open-scan")}, QStringLiteral("Open a saved surface scan (JSON)."),
                                 QStringLiteral("file"));
    QCommandLineOption start_scan({QStringLiteral("start-scan")}, QStringLiteral("Start a surface scan on the selection."));
    QCommandLineOption bench_kind({QStringLiteral("bench-kind")}, QStringLiteral("Benchmark kind index (0-5)."),
                                  QStringLiteral("n"));
    QCommandLineOption start_bench({QStringLiteral("start-bench")}, QStringLiteral("Start a benchmark on the selection."));
    QCommandLineOption shot_delay({QStringLiteral("shot-delay")}, QStringLiteral("Screenshot delay in ms."),
                                  QStringLiteral("ms"), QStringLiteral("1500"));
    QCommandLineOption compare({QStringLiteral("compare")}, QStringLiteral("Compare with saved benchmark #id."),
                               QStringLiteral("id"));
    QCommandLineOption editor_shot({QStringLiteral("editor-screenshot")},
                                   QStringLiteral("Render the partition manager for the first image to FILE and exit."),
                                   QStringLiteral("file"));
    QCommandLineOption helper_opt({QStringLiteral("helper")},
                                  QStringLiteral("Open devices through the privileged helper: auto, none, or a command."),
                                  QStringLiteral("spec"));
    QCommandLineOption lang_opt({QStringLiteral("lang")}, QStringLiteral("Interface language for this run: en or ru (default: the saved setting)."),
                                QStringLiteral("code"));
    QCommandLineOption dialog_shot({QStringLiteral("dialog-screenshot")},
                                   QStringLiteral("Render a dialog (preferences|help|history|checksum|about) as NAME=FILE and exit (testing)."),
                                   QStringLiteral("name=file"));
    QCommandLineOption hex_lba({QStringLiteral("hex-lba")}, QStringLiteral("Read this LBA in the HEX tab (testing)."),
                               QStringLiteral("lba"));
    cli.addOptions({lang_opt, dialog_shot, helper_opt, hex_lba, editor_shot, sector, shot, tab, size, select, smart_now, open_scan, start_scan, bench_kind, start_bench, shot_delay,
                    compare});
    cli.process(app);
    platter::gui::i18n::install(cli.isSet(lang_opt) ? cli.value(lang_opt) : platter::gui::i18n::saved_language());

    if (cli.isSet(editor_shot) && !cli.positionalArguments().isEmpty()) {
        platter::gui::EditorTarget t;
        t.path = cli.positionalArguments().first();
        t.label = t.path;
        t.is_image = true;
        t.sector_size = cli.value(sector).toUInt() == 4096 ? 4096u : 512u;
        platter::gui::PartitionEditorDialog dlg(t);
        dlg.show();
        QTimer::singleShot(300, &dlg, [&dlg, &cli, &editor_shot] {
            dlg.grab().save(cli.value(editor_shot));
            QApplication::quit();
        });
        return QApplication::exec();
    }

    platter::gui::MainWindow w;
    if (cli.isSet(dialog_shot)) {
        const auto spec = cli.value(dialog_shot);
        const auto name = spec.section(QLatin1Char('='), 0, 0);
        const auto file = spec.section(QLatin1Char('='), 1);
        if (name == QStringLiteral("about")) {
            QTimer::singleShot(300, &w, [file] {
                for (auto* widget : QApplication::topLevelWidgets()) {
                    if (auto* dialog = qobject_cast<QDialog*>(widget)) {
                        dialog->grab().save(file);
                        dialog->accept();
                    }
                }
            });
            QMetaObject::invokeMethod(&w, "about", Qt::DirectConnection);
            return 0;
        }
        std::unique_ptr<QDialog> dlg;
        if (name == QStringLiteral("confirm") && !cli.positionalArguments().isEmpty()) {
            // Testing aid: shows the real confirmation dialog for an erase of the given IMAGE file,
            // grabs it and cancels. Never touches a device.
            platter::safety::Request req;
            req.kind = platter::safety::Kind::erase;
            req.target.path = cli.positionalArguments().first().toStdString();
            req.target.is_image = true;
            req.summary = "Overwrite with zeros (1 pass), verify";
            QTimer::singleShot(500, &w, [file] {
                if (auto* d = QApplication::activeModalWidget()) {
                    d->grab().save(file);
                    if (auto* modal = qobject_cast<QDialog*>(d)) modal->reject();
                }
            });
            platter::gui::confirm_and_authorize(&w, QStringLiteral("Confirm Erase"), req, false,
                                                QCoreApplication::translate("platter::gui::EraseTab", "%1; verification: %2")
                                                    .arg(platter::gui::i18n::ct("Zero fill (1 pass)"), platter::gui::i18n::ct("READ")));
            return 0;
        }
        if (name == QStringLiteral("preferences")) dlg = std::make_unique<platter::gui::PreferencesDialog>(platter::gui::Preferences::load(), &w);
        else if (name == QStringLiteral("help")) dlg = std::make_unique<platter::gui::HelpDialog>(&w);
        else if (name == QStringLiteral("checksum")) dlg = std::make_unique<platter::gui::ChecksumDialog>(&w);
        else if (name == QStringLiteral("history")) {
            auto store = platter::storage::HistoryStore::open(platter::storage::HistoryStore::default_path());
            if (!store) return 3;
            dlg = std::make_unique<platter::gui::HistoryDialog>(store->get(), QString(), false, &w);
            dlg->show();
            QTimer::singleShot(300, dlg.get(), [&dlg, file] {
                dlg->grab().save(file);
                QApplication::quit();
            });
            return QApplication::exec();
        } else return 2;
        dlg->show();
        QTimer::singleShot(300, dlg.get(), [&dlg, file] {
            dlg->grab().save(file);
            QApplication::quit();
        });
        return QApplication::exec();
    }
    const auto ss = cli.value(sector).toUInt() == 4096 ? 4096u : 512u;
    for (const auto& img : cli.positionalArguments()) w.openImage(img, ss);
    if (cli.isSet(helper_opt)) w.useHelper(cli.value(helper_opt));
    if (cli.isSet(select)) w.selectIndex(cli.value(select).toInt());
    if (cli.isSet(tab)) w.selectTab(cli.value(tab));
    if (cli.isSet(hex_lba)) w.hexTab()->showLba(cli.value(hex_lba).toULongLong());
    if (cli.isSet(smart_now)) w.querySmartNow();
    if (cli.isSet(open_scan)) w.surfaceTab()->openScanFile(cli.value(open_scan));
    if (cli.isSet(start_scan)) w.surfaceTab()->startScan();
    if (cli.isSet(bench_kind)) w.benchmarkTab()->selectKind(cli.value(bench_kind).toInt());
    if (cli.isSet(start_bench)) w.benchmarkTab()->startBenchmark();
    if (cli.isSet(size)) {
        const auto parts = cli.value(size).split(QLatin1Char('x'));
        if (parts.size() == 2) w.resize(parts[0].toInt(), parts[1].toInt());
    }
    w.show();

    if (cli.isSet(shot)) {
        const auto file = cli.value(shot);
        if (cli.isSet(compare)) {
            const auto id = cli.value(compare).toLongLong();
            QTimer::singleShot(std::max(0, cli.value(shot_delay).toInt() - 500), &w,
                               [&w, id] { w.benchmarkTab()->selectCompare(id); });
        }
        QTimer::singleShot(cli.value(shot_delay).toInt(), &w, [&w, file] {
            w.grab().save(file);
            QApplication::quit();
        });
    }
    return QApplication::exec();
}
