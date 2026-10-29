// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

class QApplication;

namespace platter::gui {

// Applies the Win2000-era look (spec §2.1): Qt's built-in "Windows" style,
// a #D4D0C8 3D-face palette, navy selection, Tahoma / Liberation Sans 8 pt,
// no UI animations. Identical on Windows and Linux.
void apply_classic_theme(QApplication& app);

// Remembers the platform's own style, palette and font so the user can switch
// back to them from Settings. Call once, before apply_classic_theme().
void remember_system_theme(QApplication& app);

// classic = true: the Win2000 look; false: whatever the platform provides.
void apply_theme(QApplication& app, bool classic);

}  // namespace platter::gui
