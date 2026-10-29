// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <QWidget>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "../mainwindow/device_entry.hpp"
#include "../widgets/log_model.hpp"
#include "platter/imaging/imaging.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;

namespace platter::storage {
class HistoryStore;
}

namespace platter::gui {

// CLONE / IMAGE tab (spec §11): create an image from the selected device,
// restore an image onto it, clone it onto another device, or verify an image.
// Restore and clone are destructive and always go through
// confirm_and_authorize(); creating and verifying only read.
class ImagingTab final : public QWidget {
    Q_OBJECT
public:
    explicit ImagingTab(QWidget* parent = nullptr);
    ~ImagingTab() override;

    void setDevice(const DeviceEntry* e);
    // The devices offered as clone target / comparison device.
    void setChoices(const std::vector<DeviceEntry>& devices);
    void setHistory(storage::HistoryStore* store, bool record);
    void setShowSerials(bool on) { show_serial_ = on; }
    bool isRunning() const;
    void stopAndWait();
    // 0 create, 1 restore, 2 clone, 3 verify
    void selectMode(int mode);

signals:
    void logEvent(platter::gui::Severity s, const QString& message, const QString& device);

private slots:
    void onModeChanged();
    void onBrowse();
    void onStart();
    void onStop();
    void onSaveReport();
    void poll();

private:
    enum Mode { Create = 0, Restore = 1, Clone = 2, Verify = 3 };
    struct Choice {
        QString path, label;
        bool is_image;
        std::uint32_t sector_size;
    };

    void begin(std::unique_ptr<Operation> op, std::function<imaging::ImagingResult()> result, const QString& what);
    void finish();
    void updateButtons();
    bool startCreate();
    bool startRestore();
    bool startClone();
    bool startVerify();
    Result<BlockDevicePtr> openReadOnly(const QString& path, bool is_image, std::uint32_t sector_size) const;
    std::optional<std::uint64_t> parseLba(QLineEdit* e, const QString& what, bool& ok);
    const Choice* otherChoice() const;
    void warn(const QString& text);

    QString dev_path_, dev_label_;
    bool dev_is_image_ = false;
    std::uint32_t dev_sector_ = 512;
    std::uint64_t dev_lbas_ = 0;
    bool have_dev_ = false;
    bool show_serial_ = false;
    std::vector<Choice> choices_;

    storage::HistoryStore* history_ = nullptr;
    bool record_ = true;

    std::unique_ptr<Operation> op_;
    std::function<imaging::ImagingResult()> result_fn_;
    std::optional<imaging::ImagingResult> last_;
    QString running_what_;
    QTimer* timer_;

    QComboBox* mode_;
    QLineEdit* file_;
    QPushButton* browse_;
    QComboBox* compress_;
    QSpinBox* level_;
    QCheckBox* sparse_;
    QCheckBox* force_;
    QCheckBox* recover_;
    QSpinBox* retries_;
    QCheckBox* verify_;
    QComboBox* other_;
    QCheckBox* compare_;
    QLabel* other_label_;
    QLineEdit* start_lba_;
    QLineEdit* end_lba_;
    QLineEdit* target_lba_;
    QLineEdit* sha_;

    QPushButton* start_;
    QPushButton* stop_;
    QPushButton* save_;
    QProgressBar* progress_;
    QLabel* status_;
    QPlainTextEdit* report_;
};

}  // namespace platter::gui
