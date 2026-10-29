// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QDialog>
#include <memory>

#include "platter/core/sha256.hpp"

class QFile;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTimer;

namespace platter::gui {

// SHA-256 of any file, with an optional expected value to compare against.
// Reads in small slices from a timer so the window stays responsive.
class ChecksumDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ChecksumDialog(QWidget* parent = nullptr);
    ~ChecksumDialog() override;
    void setFile(const QString& path);

private slots:
    void onBrowse();
    void onStart();
    void onStop();
    void step();
    void updateVerdict();

private:
    void finish(const QString& message);

    QLineEdit* file_;
    QLineEdit* expected_;
    QLineEdit* result_;
    QLabel* verdict_;
    QLabel* status_;
    QProgressBar* progress_;
    QPushButton* start_;
    QPushButton* stop_;
    QTimer* timer_;
    std::unique_ptr<QFile> in_;
    Sha256 hash_;
    qint64 done_ = 0, total_ = 0;
};

}  // namespace platter::gui
