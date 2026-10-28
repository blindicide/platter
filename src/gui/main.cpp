// SPDX-License-Identifier: TBD
#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>
#include <QTimer>

#include "mainwindow/main_window.hpp"
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
    cli.addOptions({sector, shot, tab, size, select, smart_now, open_scan, start_scan});
    cli.process(app);

    platter::gui::MainWindow w;
    const auto ss = cli.value(sector).toUInt() == 4096 ? 4096u : 512u;
    for (const auto& img : cli.positionalArguments()) w.openImage(img, ss);
    if (cli.isSet(select)) w.selectIndex(cli.value(select).toInt());
    if (cli.isSet(tab)) w.selectTab(cli.value(tab));
    if (cli.isSet(smart_now)) w.querySmartNow();
    if (cli.isSet(open_scan)) w.surfaceTab()->openScanFile(cli.value(open_scan));
    if (cli.isSet(start_scan)) w.surfaceTab()->startScan();
    if (cli.isSet(size)) {
        const auto parts = cli.value(size).split(QLatin1Char('x'));
        if (parts.size() == 2) w.resize(parts[0].toInt(), parts[1].toInt());
    }
    w.show();

    if (cli.isSet(shot)) {
        const auto file = cli.value(shot);
        QTimer::singleShot(1500, &w, [&w, file] {
            w.grab().save(file);
            QApplication::quit();
        });
    }
    return QApplication::exec();
}
