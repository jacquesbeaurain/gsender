#pragma once

// What the gs_app tests share: one (offscreen) Qt application for the whole
// executable, and running its events until something happens.

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QGuiApplication>

#include <functional>

namespace gs::app::test_support {

inline QGuiApplication& application() {
    static int argc = 1;
    static char name[] = "gs_app_tests";
    static char* argv[] = {name, nullptr};
    static QGuiApplication* app = [] {
        // Another suite in this executable may have made it first.
        if (auto* existing = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
            return existing;
        }
        qputenv("QT_QPA_PLATFORM", "offscreen");
        if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && !qEnvironmentVariableIsEmpty("WINDIR")) {
            qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "\\Fonts");
        }
        return new QGuiApplication(argc, argv);
    }();
    return *app;
}

// Runs Qt events until `done` holds; false on timeout.
inline bool waitFor(const std::function<bool()>& done, int timeoutMs = 5000) {
    const QDeadlineTimer deadline(timeoutMs);
    while (!done()) {
        if (deadline.hasExpired()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

inline void runFor(int ms) {
    waitFor([] { return false; }, ms);
}

}  // namespace gs::app::test_support
