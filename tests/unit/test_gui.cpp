// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
#include "../../src/gui/tabs/erase_tab.hpp"
#include "../../src/gui/tabs/surface_tab.hpp"
#include "../../src/gui/tabs/benchmark_tab.hpp"
using namespace platter::gui;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("switching operation targets clears the previous disk range") {
    DeviceEntry a, b;
    a.info.path = "a.img"; b.info.path = "b.img";
    a.is_image = b.is_image = true;
    EraseTab erase; SurfaceTab surface; BenchmarkTab bench;
    for (QWidget* widget : {static_cast<QWidget*>(&erase), static_cast<QWidget*>(&surface), static_cast<QWidget*>(&bench)}) {
        auto set = [&](const DeviceEntry* entry) {
            if (widget == &erase) erase.setDevice(entry);
            else if (widget == &surface) surface.setDevice(entry);
            else bench.setDevice(entry);
        };
        set(&a);
        const auto fields = widget->findChildren<QLineEdit*>();
        QLineEdit* end = nullptr;
        for (auto* field : fields) {
            if (field->placeholderText().startsWith("end")) end = field;
        }
        REQUIRE(end);
        end->setText("4095");
        set(&a);
        CHECK(end->text() == "4095");
        set(&b);
        CHECK(end->text().isEmpty());
    }
}

TEST_CASE("erase rejects overflowing input before authorization") {
    EraseTab erase;
    DeviceEntry entry; entry.info.path = "nonexistent.img"; entry.is_image = true;
    erase.setDevice(&entry);
    for (auto* field : erase.findChildren<QLineEdit*>()) {
        if (field->placeholderText().startsWith("0")) field->setText("99999999999999999999");
        if (field->placeholderText().startsWith("end")) field->setText("4095");
    }
    QString message;
    QTimer::singleShot(0, [&] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(widget)) { message = box->text(); box->accept(); }
    });
    REQUIRE(QMetaObject::invokeMethod(&erase, "onStart", Qt::DirectConnection));
    CHECK(message == "LBA values must be unsigned 64-bit numbers.");
}
