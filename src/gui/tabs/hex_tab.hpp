// SPDX-License-Identifier: TBD
#pragma once

#include <QWidget>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "../mainwindow/device_entry.hpp"
#include "../widgets/log_model.hpp"
#include "platter/inspect/inspect.hpp"

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace platter::gui {

// HEX tab (spec §13): read-only sector viewer with jump-to-LBA, byte-pattern
// search and structure interpreters. Pages are read with a short-lived
// read-only handle; nothing here can write.
class HexTab final : public QWidget {
    Q_OBJECT
public:
    explicit HexTab(QWidget* parent = nullptr);
    ~HexTab() override;

    void setDevice(const DeviceEntry* e);
    bool isRunning() const;
    void stopAndWait();

    // Shows `sectors` sectors starting at `lba`. Public so other tabs can jump here.
    void showLba(std::uint64_t lba);

signals:
    void logEvent(platter::gui::Severity s, const QString& message, const QString& device);

private slots:
    void onGo();
    void onPrev();
    void onNext();
    void onPageSettingsChanged();
    void onFieldSelected();
    void onFindNext();
    void onFindAll();
    void onCancelSearch();
    void onMatchActivated(QListWidgetItem* item);
    void poll();

private:
    struct SearchJob;

    void loadPage(std::uint64_t lba, bool from_user);
    void render();
    void highlight(std::uint32_t first_byte, std::uint32_t size);
    void clearStructure(const QString& why);
    std::optional<std::uint64_t> parseNumber(const QString& text) const;
    std::optional<inspect::Pattern> buildPattern(QString& error) const;
    void startSearch(bool all);
    void finishSearch();
    void updateButtons();

    // device
    bool have_dev_ = false;
    bool loaded_ = false;   // a page has been read since the device was selected
    QString dev_path_, dev_label_;
    bool dev_is_image_ = false;
    std::uint32_t sector_ = 512;
    std::uint64_t total_lbas_ = 0;

    // page
    std::uint64_t lba_ = 0;
    std::vector<std::uint8_t> page_;
    QString read_error_;
    std::optional<inspect::Structure> structure_;
    std::uint32_t structure_base_ = 0;
    std::uint64_t search_from_ = 0;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> match_highlight_;  // within the page

    // widgets
    QLineEdit* goto_ = nullptr;
    QComboBox* goto_kind_ = nullptr;
    QPushButton *go_ = nullptr, *prev_ = nullptr, *next_ = nullptr;
    QSpinBox* sectors_ = nullptr;
    QComboBox* interpret_ = nullptr;
    QLabel* where_ = nullptr;
    QPlainTextEdit* view_ = nullptr;
    QTableWidget* fields_ = nullptr;
    QPlainTextEdit* notes_ = nullptr;
    QListWidget* matches_ = nullptr;
    QComboBox* find_kind_ = nullptr;
    QLineEdit* find_text_ = nullptr;
    QCheckBox* find_case_ = nullptr;
    QPushButton *find_next_ = nullptr, *find_all_ = nullptr, *find_stop_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* timer_ = nullptr;

    std::shared_ptr<SearchJob> job_;
    std::uint32_t last_pattern_size_ = 0;
};

}  // namespace platter::gui
