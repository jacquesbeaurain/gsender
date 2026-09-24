#pragma once

// Settings > Accessibility at work (features/Helper): what a screen reader
// hears - the machine's status, the job's progress and the loaded file in
// words - the audio cues, the keyboard shortcut map and the focus ring.

#include "gs/job/accessibility.hpp"

#include <QFrame>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <string>

class QGridLayout;
class QTimer;

namespace gs::app {

class Machine;
class ShortcutManager;

// The cue through the speakers: its tone as a WAV (PlaySound) on Windows,
// the system beep elsewhere.
void playAudioCue(job::AudioCue cue);

// AccessibilityAnnouncer: announcements go to the screen reader (as
// `window`'s), assertive for the machine's status, polite for the rest.
class AccessibilityAnnouncer final : public QObject {
    Q_OBJECT
public:
    AccessibilityAnnouncer(Machine& machine, QWidget& window, QObject* parent = nullptr);

    // Tests hear the cues here instead of the speakers.
    void setCuePlayer(std::function<void(job::AudioCue)> player) { player_ = std::move(player); }
    // What was announced (the last 50), oldest first.
    const QStringList& announcements() const noexcept { return announced_; }
    // The loaded file in words: empty with the summary off or no file.
    const QString& summary() const noexcept { return summary_; }

Q_SIGNALS:
    void summaryChanged(const QString& summary);

private:
    void announce(const QString& text, bool assertive);
    void play(job::AudioCue cue);
    void onState();
    void onProgress();
    void onProgram();

    Machine& machine_;
    QWidget& window_;
    std::function<void(job::AudioCue)> player_;
    QStringList announced_;
    QString summary_;
    std::string lastState_;
    job::ProgressAnnouncer progress_;
};

// KeyboardMapOverlay: the shortcuts that work now, by category, over the
// bottom of `host` while "Show keyboard shortcut map" is on; its close
// button turns the setting off.
class KeyboardMapOverlay final : public QFrame {
    Q_OBJECT
public:
    KeyboardMapOverlay(Machine& machine, ShortcutManager& shortcuts, QWidget* host);

    void refresh();
    // "Category: title = keys" for each shortcut listed (tests).
    const QStringList& entries() const noexcept { return entries_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void apply();  // shown with the setting
    void place();

    Machine& machine_;
    ShortcutManager& shortcuts_;
    QWidget* host_;
    QWidget* body_;
    QGridLayout* grid_;
    QStringList entries_;
};

// Focus rings: a high-contrast ring around the widget with the keyboard
// focus, in whichever of the application's windows it is - a window of its
// own laid over it, which clicks go through.
class FocusRing final : public QWidget {
    Q_OBJECT
public:
    explicit FocusRing(QWidget* owner);

    void setActive(bool on);
    bool isActive() const noexcept { return active_; }
    // Around `widget` (none: hidden).
    void follow(QWidget* widget);
    QWidget* target() const { return target_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void track();  // the focus moved, or its widget did

    bool active_ = false;
    QPointer<QWidget> target_;
    QTimer* timer_;
};

}  // namespace gs::app
