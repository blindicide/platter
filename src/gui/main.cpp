// SPDX-License-Identifier: TBD
#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>
#include <QTimer>
#include <algorithm>

#include "mainwindow/main_window.hpp"
#include "widgets/partition_editor.hpp"
#include "tabs/benchmark_tab.hpp"
#include "tabs/hex_tab.hpp"
#include "tabs/surface_tab.hpp"
#include "platter/version.hpp"
#include "styles/classic_theme.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("platter"));
    QApplication::setApplicationName(QStringLiteral("platter"));
    QApplication::setApplicationVersion(QString::fromLatin1(platter::kVersion));
    platter::gui::apply_classic_theme(app);

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
    QCommandLineOption hex_lba({QStringLiteral("hex-lba")}, QStringLiteral("Read this LBA in the HEX tab (testing)."),
                               QStringLiteral("lba"));
    cli.addOptions({helper_opt, hex_lba, editor_shot, sector, shot, tab, size, select, smart_now, open_scan, start_scan, bench_kind, start_bench, shot_delay,
                    compare});
    cli.process(app);

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
