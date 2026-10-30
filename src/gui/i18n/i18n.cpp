// SPDX-License-Identifier: LicenseRef-Proprietary
#include "i18n.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>
#include <memory>
#include <vector>

#include "core_patterns.hpp"

namespace platter::gui::i18n {

namespace {
// Translators must outlive the application's use of them.
std::vector<std::unique_ptr<QTranslator>> g_translators;
double g_scale = 1.0;
}  // namespace

QList<Language> available_languages() {
    QList<Language> out;
    out.push_back({QStringLiteral("en"), QStringLiteral("English")});
#ifdef PLATTER_HAVE_TRANSLATIONS
    if (QFile::exists(QStringLiteral(":/i18n/platter_ru.qm")))
        out.push_back({QStringLiteral("ru"), QStringLiteral("Русский")});
#endif
    return out;
}

QString saved_language() {
    return QSettings().value(QStringLiteral("prefs/language"), QStringLiteral("en")).toString();
}

void save_language(const QString& code) { QSettings().setValue(QStringLiteral("prefs/language"), code); }

bool install(const QString& code) {
    if (code.isEmpty() || code == QStringLiteral("en")) return true;
    bool found = false;
    for (const auto& l : available_languages()) found = found || l.code == code;
    if (!found) return false;

    // Prefer the portable package's Qt strings, including windeployqt's merged catalogue.
    {
        auto qt = std::make_unique<QTranslator>();
        const auto deployed = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
        if (qt->load(QStringLiteral("qtbase_") + code, deployed) ||
            qt->load(QStringLiteral("qt_") + code, deployed) ||
            qt->load(QStringLiteral("qtbase_") + code, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            QCoreApplication::installTranslator(qt.get());
            g_translators.push_back(std::move(qt));
        }
    }
    auto own = std::make_unique<QTranslator>();
    if (!own->load(QStringLiteral(":/i18n/platter_") + code + QStringLiteral(".qm"))) return false;
    QCoreApplication::installTranslator(own.get());
    g_translators.push_back(std::move(own));
    QLocale::setDefault(QLocale(code));
    if (code == QStringLiteral("ru")) g_scale = 1.3;
    return true;
}

QString ct(std::string_view english) {
    if (english.empty()) return {};
    const std::string key(english);
    const QString original = QString::fromUtf8(key.c_str());
    const auto direct = QCoreApplication::translate("core", key.c_str());
    if (direct != original) return direct;
    // Phrases with variable parts: "a volume is mounted at /mnt/x".
    for (const auto& p : core_patterns()) {
        const auto m = p.regex.match(original);
        if (!m.hasMatch()) continue;
        QString t = QCoreApplication::translate("core", p.tmpl);
        // Highest placeholder first, so that %1 never eats the start of %10.
        for (int i = p.regex.captureCount(); i >= 1; --i) {
            const QString cap = m.captured(i);
            QString arg = cap;
            if (p.mode == ArgMode::phrase) {
                arg = ct(cap.toStdString());
            } else if (p.mode == ArgMode::words) {
                QStringList w;
                for (const auto& word : cap.split(QLatin1Char(' '), Qt::SkipEmptyParts)) w << ct(word.toStdString());
                arg = QLatin1Char(' ') + w.join(QLatin1Char(' '));
            }
            t.replace(QStringLiteral("%") + QString::number(i), arg);
        }
        return units(t.trimmed());
    }
    return original;
}

int scale(int pixels) { return static_cast<int>(pixels * g_scale + 0.5); }

QString units(const QString& text) {
    // Marked for lupdate; looked up with the exact token below.
    [[maybe_unused]] static const char* const kUnits[] = {
        QT_TRANSLATE_NOOP("units", "B"),   QT_TRANSLATE_NOOP("units", "KB"),  QT_TRANSLATE_NOOP("units", "MB"),
        QT_TRANSLATE_NOOP("units", "GB"),  QT_TRANSLATE_NOOP("units", "TB"),  QT_TRANSLATE_NOOP("units", "PB"),
        QT_TRANSLATE_NOOP("units", "KiB"), QT_TRANSLATE_NOOP("units", "MiB"), QT_TRANSLATE_NOOP("units", "GiB"),
        QT_TRANSLATE_NOOP("units", "TiB"), QT_TRANSLATE_NOOP("units", "PiB"), QT_TRANSLATE_NOOP("units", "/s"),
    };
    static const QRegularExpression re(QStringLiteral("(?<=\\d) ((?:B|[KMGTP]B|[KMGTP]iB))(/s)?(?![A-Za-z])"));
    if (!text.contains(QLatin1Char(' '))) return text;
    QString out;
    qsizetype last = 0;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        out += QLatin1Char(' ');
        out += QCoreApplication::translate("units", m.captured(1).toUtf8().constData());
        if (m.capturedLength(2) > 0) out += QCoreApplication::translate("units", "/s");
        last = m.capturedEnd();
    }
    return out + text.mid(last);
}

QString err(const platter::Error& e) {
    if (e.message.empty()) return QString::fromStdString(e.describe());
    const auto t = ct(e.message);
    if (t == QString::fromStdString(e.message)) return QString::fromStdString(e.describe());
    QString s = t;
    if (e.os_error != 0) s += QStringLiteral(" (%1 %2)").arg(QCoreApplication::translate("core", "os error"), QString::number(e.os_error));
    return s;
}

}  // namespace platter::gui::i18n
