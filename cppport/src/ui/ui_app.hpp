#pragma once

// Starting the QML UI, shared by the application (main.cpp) and the UI tests.

class QQmlApplicationEngine;
class QQuickWindow;

namespace gs::ui {

// Before the application object exists: the Basic Controls style (the UI
// draws its own look over it) and, on the offscreen platform (tests,
// screenshots), Qt Quick's software renderer.
void configureQuick(bool offscreen);

// The engine's icon provider and import paths, then Main.qml: its window, or
// nullptr (the errors are printed).
QQuickWindow* loadMainWindow(QQmlApplicationEngine& engine);

}  // namespace gs::ui
