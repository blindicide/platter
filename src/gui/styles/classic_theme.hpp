// SPDX-License-Identifier: TBD
#pragma once

class QApplication;

namespace platter::gui {

// Applies the Win2000-era look (spec §2.1): Qt's built-in "Windows" style,
// a #D4D0C8 3D-face palette, navy selection, Tahoma / Liberation Sans 8 pt,
// no UI animations. Identical on Windows and Linux.
void apply_classic_theme(QApplication& app);

}  // namespace platter::gui
