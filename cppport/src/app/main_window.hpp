#pragma once

#include "settings_dialog.hpp"
#include "stats_dialog.hpp"

#include <QMainWindow>

namespace gs::app {

class AccessibilityAnnouncer;
class ConsolePanel;
class FocusRing;
class KeyboardMapOverlay;
class Jogger;
class Machine;
class NotificationButton;
class NotificationCenter;
class ToastArea;
class ProbePanel;
class RotaryPanel;
class SdCardDialog;
class ShortcutManager;
class SpindlePanel;
class StatusArea;
class ToolpathView;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Machine& machine, QWidget* parent = nullptr);

    // Errors and alarms open dialogs unless suppressed (screenshots, tests).
    void setDialogsEnabled(bool enabled) noexcept { dialogsEnabled_ = enabled; }
    ToolpathView& toolpathView() noexcept { return *visualizer_; }
    ShortcutManager& shortcuts() noexcept { return *shortcuts_; }
    Jogger& jogger() noexcept { return *jogger_; }
    RotaryPanel& rotaryPanel() noexcept { return *rotary_; }
    NotificationCenter& notifications() noexcept { return *notifications_; }
    ToastArea& toasts() noexcept { return *toasts_; }
    NotificationButton& notificationButton() noexcept { return *bell_; }
    // "Reconnect automatically": the last machine used, when the setting is
    // on and its port is there (a serial port listed, an address, or the
    // simulator). False when nothing was tried.
    bool reconnectAutomatically();
    // The Rotary tab shows while the Rotary controls are on.
    bool rotaryTabVisible() const;
    // The side tabs shown (Rotary, Spindle/Laser and Coolant follow their
    // settings).
    QStringList visibleTabs() const;
    // Tools > SD Card (made on first use).
    SdCardDialog& sdCardDialog();
    // Settings > Accessibility at work.
    AccessibilityAnnouncer& announcer() noexcept { return *announcer_; }
    KeyboardMapOverlay& keyboardMap() noexcept { return *keyboardMap_; }
    FocusRing& focusRing() noexcept { return *focusRing_; }
    // The job summary shown above the visualizer ("Show summary visually").
    class QLabel& jobSummary() noexcept { return *summaryBox_; }

protected:
    void closeEvent(QCloseEvent* event) override;  // workspace.promptExit

private:
    // An error: upstream's error pop-up (and the bell's list).
    void showError(const QString& title, const QString& detail);
    // A message that needs reading (a message box, unless dialogs are off).
    void showMessage(const QString& title, const QString& detail);
    void createMenus();
    void openFile();
    // A recent file: loaded again, or forgotten when it has gone.
    void openRecent(const QString& path);
    void openSettings(SettingsDialog::Page page);
    void openStats(StatsDialog::Page page);
    void openSquaring();
    // The diagnostics support file, saved where the user picks.
    void downloadDiagnostics();
    void installShortcuts();

    Machine& machine_;
    ConsolePanel* console_;
    ToolpathView* visualizer_;
    StatusArea* statusArea_;
    Jogger* jogger_;
    ShortcutManager* shortcuts_;
    SpindlePanel* spindle_;
    class CoolantPanel* coolant_;
    ProbePanel* probe_;
    RotaryPanel* rotary_;
    NotificationCenter* notifications_;
    ToastArea* toasts_;
    NotificationButton* bell_;
    SdCardDialog* sdCard_ = nullptr;
    AccessibilityAnnouncer* announcer_;
    class QLabel* summaryBox_;
    KeyboardMapOverlay* keyboardMap_ = nullptr;
    FocusRing* focusRing_;
    bool generalEffects_ = true;  // the platform's UI effects, before reduced motion
    class QTabWidget* tabs_;
    bool dialogsEnabled_ = true;
};

}  // namespace gs::app
