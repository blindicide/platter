// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Messages produced by the core libraries that carry numbers, names or paths
// ("a volume is mounted at /mnt/x"). Each entry is a regular expression over
// the English text plus an English template with %1, %2, ... for the captured
// parts; the template is what gets translated (context "core"), so the
// translator can reorder the parts freely.
#pragma once

#include <QRegularExpression>
#include <vector>

namespace platter::gui::i18n {

enum class ArgMode {
    verbatim,  // captured parts are inserted as they are (numbers, paths, model names)
    phrase,    // captured parts are themselves translated with ct() first
    words,     // captured part is a space-separated list of known words ("spare-below-threshold temperature")
};

struct Pattern {
    QRegularExpression regex;
    const char* tmpl;  // English template, translated as a whole
    ArgMode mode;
};

const std::vector<Pattern>& core_patterns();

}  // namespace platter::gui::i18n
