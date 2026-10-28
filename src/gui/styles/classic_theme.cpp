// SPDX-License-Identifier: TBD
#include "classic_theme.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace platter::gui {

void apply_classic_theme(QApplication& app) {
    if (auto* style = QStyleFactory::create(QStringLiteral("Windows"))) app.setStyle(style);

    // Windows 2000 "Windows Classic" scheme.
    const QColor face(0xD4, 0xD0, 0xC8);
    const QColor shadow(0x80, 0x80, 0x80);
    const QColor dark_shadow(0x40, 0x40, 0x40);
    const QColor highlight(0x0A, 0x24, 0x6A);
    const QColor gray_text(0x80, 0x80, 0x80);

    QPalette p;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(group, QPalette::Window, face);
        p.setColor(group, QPalette::Button, face);
        p.setColor(group, QPalette::Base, Qt::white);
        p.setColor(group, QPalette::AlternateBase, QColor(0xF4, 0xF2, 0xEE));
        p.setColor(group, QPalette::WindowText, Qt::black);
        p.setColor(group, QPalette::ButtonText, Qt::black);
        p.setColor(group, QPalette::Text, Qt::black);
        p.setColor(group, QPalette::Light, Qt::white);
        p.setColor(group, QPalette::Midlight, face);
        p.setColor(group, QPalette::Mid, shadow);
        p.setColor(group, QPalette::Dark, shadow);
        p.setColor(group, QPalette::Shadow, dark_shadow);
        p.setColor(group, QPalette::Highlight, highlight);
        p.setColor(group, QPalette::HighlightedText, Qt::white);
        p.setColor(group, QPalette::ToolTipBase, QColor(0xFF, 0xFF, 0xE1));
        p.setColor(group, QPalette::ToolTipText, Qt::black);
        p.setColor(group, QPalette::Link, QColor(0x00, 0x00, 0xFF));
        p.setColor(group, QPalette::PlaceholderText, gray_text);
    }
    p.setColor(QPalette::Inactive, QPalette::Highlight, shadow);
    p.setColor(QPalette::Disabled, QPalette::WindowText, gray_text);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, gray_text);
    p.setColor(QPalette::Disabled, QPalette::Text, gray_text);
    p.setColor(QPalette::Disabled, QPalette::Highlight, shadow);
    app.setPalette(p);

#ifdef Q_OS_WIN
    QFont font(QStringLiteral("Tahoma"), 8);
#else
    QFont font(QStringLiteral("Liberation Sans"), 8);
    if (!QFontDatabase::families().contains(QStringLiteral("Liberation Sans")))
        font = QFont(QStringLiteral("DejaVu Sans"), 8);
#endif
    font.setStyleStrategy(QFont::PreferDefault);
    font.setHintingPreference(QFont::PreferFullHinting);
    app.setFont(font);

    for (auto effect : {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
                        Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox})
        QApplication::setEffectEnabled(effect, false);

    // Details the stock "Windows" style leaves out.
    app.setStyleSheet(QStringLiteral(R"(
QStatusBar::item { border: none; }
QStatusBar QLabel#statusPanel {
    border: 1px solid; border-color: #808080 #FFFFFF #FFFFFF #808080;
    padding: 0px 4px;
}
QProgressBar {
    border: 1px solid; border-color: #808080 #FFFFFF #FFFFFF #808080;
    background: #FFFFFF; text-align: center; height: 14px;
}
QProgressBar::chunk { background: #0A246A; width: 7px; margin: 1px; }
QLabel#healthGood     { background: #008000; color: white; font-weight: bold; padding: 1px 6px; }
QLabel#healthWarning  { background: #FFD000; color: black; font-weight: bold; padding: 1px 6px; }
QLabel#healthCritical { background: #C00000; color: white; font-weight: bold; padding: 1px 6px; }
QLabel#healthUnknown  { background: #808080; color: white; font-weight: bold; padding: 1px 6px; }
QLabel#placeholder    { color: #808080; }
QTableView { gridline-color: #D4D0C8; }
)"));
}

}  // namespace platter::gui
