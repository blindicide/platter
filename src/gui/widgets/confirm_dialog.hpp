// SPDX-License-Identifier: LicenseRef-Proprietary
//
// The one and only GUI path to a destructive operation: shows exactly what
// will be destroyed, refuses when the safety pipeline found blockers, and
// requires the confirmation text to be typed (never pre-filled, never pasted
// from a default). See docs/safety.md.
#pragma once

#include <optional>

#include <QString>

#include "platter/core/safety.hpp"

class QWidget;

namespace platter::gui {

struct AuthorizeOutcome {
    std::optional<safety::AuthorizedTarget> target;
    bool cancelled = false;  // user closed the dialog
    QString error;           // set when preview/authorize failed (not when cancelled)
};

// `localized_summary` is shown instead of request.summary (which stays English
// for the audit record) when it is not empty.
AuthorizeOutcome confirm_and_authorize(QWidget* parent, const QString& title, const safety::Request& request,
                                       bool show_serial, const QString& localized_summary = {});

}  // namespace platter::gui
