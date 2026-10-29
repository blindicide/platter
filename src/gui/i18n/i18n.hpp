// SPDX-License-Identifier: TBD
//
// Translation support. English is the built-in language; Russian (and any
// future language) is chosen in Settings > Preferences and applied at start-up.
#pragma once

#include <QString>
#include <QStringList>
#include <string>
#include <string_view>

#include "platter/core/error.hpp"

class QCoreApplication;

namespace platter::gui::i18n {

struct Language {
    QString code;         // "en", "ru"
    QString native_name;  // shown in the language list, in that language
};

// Languages this build can offer (English plus every embedded .qm).
QList<Language> available_languages();

// The language stored in the settings ("en" by default).
QString saved_language();
void save_language(const QString& code);

// Installs the translators for `code` (no-op for "en"). Safe to call once at
// start-up, before any widget exists. Returns false if the language is not
// available in this build (the program then stays in English).
bool install(const QString& code);

// Translates a fixed phrase produced by the core libraries (device kinds,
// verdicts, safety warnings, ...). Phrases with variable parts are matched
// against patterns; anything unknown is returned unchanged (English).
QString ct(std::string_view english);
inline QString ct(const std::string& english) { return ct(std::string_view(english)); }
inline QString ct(const char* english) { return ct(std::string_view(english)); }

// A layout width adjusted for the active language: Russian needs about a third more room than English.
int scale(int pixels);

// Localizes unit suffixes after a number: "64.00 MiB" -> "64.00 МиБ", "118.4 MB/s" -> "118.4 МБ/с".
// Identity in English.
QString units(const QString& text);

// An error for display: the message is translated when a translation exists,
// otherwise the plain English Error::describe() is shown.
QString err(const platter::Error& e);

}  // namespace platter::gui::i18n
