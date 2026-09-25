#pragma once

// gSender's keyboard shortcuts in the QML UI: the shared ShortcutManager
// scoped to the Quick window (not while typing into a text field). What
// acts on the machine runs here, as the widget window's shortcuts do; what
// acts on the screens (load a file, the visualizer's views, machine
// information, the probe and rotary dialogs) is handed to QML through
// Backend.shortcutTriggered.

#include <QObject>

class QQuickWindow;

namespace gs::app {
class ShortcutManager;
}

namespace gs::ui {

class SpindleModel;
class UiBackend;

class UiShortcuts final : public QObject {
    Q_OBJECT
public:
    UiShortcuts(UiBackend& backend, QQuickWindow& window);

    app::ShortcutManager& manager() noexcept { return *manager_; }

private:
    void install();

    UiBackend& backend_;
    app::ShortcutManager* manager_;
    SpindleModel* spindle_;  // the Spindle/Laser tab's rules, whether it is shown or not
};

}  // namespace gs::ui
