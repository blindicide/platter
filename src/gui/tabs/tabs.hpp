// SPDX-License-Identifier: TBD
//
// Operation tabs. Each tab renders a DeviceEntry and emits requests; the main
// window owns all device access and runs it off the GUI thread.
#pragma once

#include <QWidget>

#include "../mainwindow/device_entry.hpp"

class QLabel;
class QPushButton;
class QStandardItemModel;
class QTableView;
class QListWidget;

namespace platter::gui {

class LogModel;
class PartitionBar;

class InfoTab final : public QWidget {
    Q_OBJECT
public:
    explicit InfoTab(QWidget* parent = nullptr);
    void setDevice(const DeviceEntry* e, bool show_serial);
    void saveState();

private:
    QStandardItemModel* props_;
    QStandardItemModel* caps_;
    QTableView* props_view_;
    QTableView* caps_view_;
};

class SmartTab final : public QWidget {
    Q_OBJECT
public:
    explicit SmartTab(QWidget* parent = nullptr);
    void setDevice(const DeviceEntry* e);
    void setBusy(bool busy);
    void saveState();

signals:
    void queryRequested();

private:
    QPushButton* query_;
    QLabel* status_;
    QLabel* health_;
    QStandardItemModel* attrs_;
    QTableView* attrs_view_;
    QListWidget* findings_;
};

class PartitionsTab final : public QWidget {
    Q_OBJECT
public:
    explicit PartitionsTab(QWidget* parent = nullptr);
    void setDevice(const DeviceEntry* e);
    void setBusy(bool busy);
    void saveState();

signals:
    void readRequested();

private:
    QPushButton* read_;
    QLabel* summary_;
    PartitionBar* bar_;
    QStandardItemModel* parts_;
    QTableView* parts_view_;
    QListWidget* warnings_;
};

class LogTab final : public QWidget {
    Q_OBJECT
public:
    LogTab(LogModel* model, QWidget* parent = nullptr);
    void saveState();

private:
    QTableView* view_;
};

// Tabs for features from later milestones: visible (keyboard-reachable, so the
// layout matches the spec) but clearly marked as not yet available.
class PlannedTab final : public QWidget {
    Q_OBJECT
public:
    PlannedTab(const QString& feature, const QString& milestone, const QString& description,
               QWidget* parent = nullptr);
};

}  // namespace platter::gui
