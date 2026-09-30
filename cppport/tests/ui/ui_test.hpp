#pragma once

// The UiTest fixture the UI test files share: the QML touch UI, loaded
// headless (the offscreen platform, Qt Quick's software renderer) against a
// real Machine and the simulated board. Items are found by objectName and
// driven with taps and touch gestures. GS_TEST_SCREENSHOTS=<dir> saves what
// the tests show.

#include "backend.hpp"
#include "machine.hpp"
#include "qt_event_loop.hpp"
#include "ui_app.hpp"

#include "gs/controller/controller.hpp"
#include "gs/sim/grbl_simulator.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPoint>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <ostream>

// GoogleTest prints QStrings as text.
QT_BEGIN_NAMESPACE
inline void PrintTo(const QString& text, std::ostream* os) {
    *os << '"' << text.toStdString() << '"';
}
QT_END_NAMESPACE

// The tests name app:: and ui:: types unqualified.
using namespace gs;

// QML's own warnings (a TypeError, a binding loop, an unknown property),
// kept so the test that caused them fails: they are otherwise only printed.
QStringList& qmlWarnings();

// The one QGuiApplication, created on first use.
QGuiApplication& application();

// Processes events until done() or the timeout; false on the timeout.
bool waitFor(const std::function<bool()>& done, int timeoutMs = 5000);

// The item named `name` under `root` (Repeater delegates included, which
// QObject::findChild misses).
QQuickItem* findItem(QQuickItem* root, const QString& name);

// The UI over its own configuration and Machine.
class UiTest : public ::testing::Test {
protected:
    void SetUp() override {
        application();
        qmlWarnings().clear();
        loop_ = std::make_unique<app::QtEventLoop>();
        machine_ = std::make_unique<app::Machine>(*loop_, (dir_.path() + "/rc").toStdWString());
        backend_ = std::make_unique<ui::UiBackend>(*machine_);
        ui::UiBackend::setInstance(backend_.get());
        engine_ = std::make_unique<QQmlApplicationEngine>();
        window_ = ui::loadMainWindow(*engine_);
        ASSERT_NE(window_, nullptr);
        window_->resize(1400, 900);
        window_->show();
        ASSERT_TRUE(waitFor([&] { return window_->isExposed(); }));
    }

    void TearDown() override {
        engine_.reset();
        // Delegates a Repeater dropped are deleted later; not at exit.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        backend_.reset();
        machine_.reset();
        loop_.reset();
        // A QML warning fails the test, as an exception in C++ would.
        for (const QString& warning : qmlWarnings()) {
            ADD_FAILURE() << "QML: " << warning.toStdString();
        }
    }

    QQuickItem* item(const QString& name) { return findItem(window_->contentItem(), name); }
    QString text(const QString& name) {
        QQuickItem* found = item(name);
        return found ? found->property("text").toString() : QString("<no %1>").arg(name);
    }
    QPoint centreOf(QQuickItem* target) {
        return target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    }
    void tap(const QString& name) {
        QQuickItem* target = item(name);
        ASSERT_NE(target, nullptr) << name.toStdString();
        // A tap outside the window still reaches the scene; a person's cannot.
        ASSERT_TRUE(QRect(QPoint(0, 0), window_->size()).contains(centreOf(target)))
            << name.toStdString() << " is off the window";
        QTest::mouseClick(window_, Qt::LeftButton, {}, centreOf(target));
        QCoreApplication::processEvents();
    }
    void type(const QString& text) {
        for (const QChar c : text) {
            QTest::keyClick(window_, c.toLatin1());
        }
    }
    // A tool tab, scrolled to with the arrows as a person would.
    void selectTool(const QString& key) {
        // A rebuilt strip (the settings showing a tab) settles first.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(50);
        const QString name = "toolsTab_" + key;
        QQuickItem* strip = item(name) ? item(name)->parentItem() : nullptr;
        ASSERT_NE(strip, nullptr) << name.toStdString();
        const auto inView = [&] {
            QQuickItem* tab = item(name);
            const QRectF box = tab->mapRectToItem(window_->contentItem(), QRectF(0, 0, tab->width(), tab->height()));
            QQuickItem* flick = strip->parentItem()->parentItem();
            const QRectF view = flick->mapRectToItem(window_->contentItem(), QRectF(0, 0, flick->width(), flick->height()));
            return view.contains(box.center());
        };
        for (const char* arrow : {"toolsScrollLeft", "toolsScrollRight"}) {
            for (int i = 0; i < 10 && !inView() && item(arrow)->property("can").toBool(); ++i) {
                tap(arrow);
                QTest::qWait(200);
            }
        }
        tap(name);
    }
    void connectSimulator() {
        backend_->connectSimulator();
        ASSERT_TRUE(waitFor([&] {
            return machine_->isConnected() && machine_->controller()->state().status.activeState == "Idle";
        }));
    }
    void screenshot(const QString& name) {
        if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
            window_->grabWindow().save(QString::fromLocal8Bit(out) + "/" + name + ".png");
        }
    }

    QTemporaryDir dir_;
    std::unique_ptr<app::QtEventLoop> loop_;
    std::unique_ptr<app::Machine> machine_;
    std::unique_ptr<ui::UiBackend> backend_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QQuickWindow* window_ = nullptr;
};
