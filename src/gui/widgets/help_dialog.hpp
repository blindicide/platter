// SPDX-License-Identifier: TBD
#pragma once

#include <QDialog>

namespace platter::gui {

// Built-in, offline documentation (no network access, nothing to install).
class HelpDialog final : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(QWidget* parent = nullptr);
};

}  // namespace platter::gui
