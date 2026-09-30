#pragma once

// Settings > Accessibility at work (features/Helper/AccessibilityAnnouncer):
// what a screen reader hears - the machine's status, the job's progress and
// the loaded file in words - and the audio cues. The words come from the
// core (gs/job/accessibility); this adapts the Machine's signals to them.

#include "gs/job/accessibility.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <string>

namespace gs::app {

class Machine;

// The cue through the speakers: its tone as a WAV (PlaySound) on Windows,
// the terminal bell elsewhere (Qt Gui has no beep of its own).
void playAudioCue(job::AudioCue cue);

class AccessibilityAnnouncer final : public QObject {
    Q_OBJECT
public:
    explicit AccessibilityAnnouncer(Machine& machine, QObject* parent = nullptr);

    // What the screen reader's announcements come from: the window (none:
    // they are only recorded).
    void setTarget(QObject* target) { target_ = target; }
    // Tests hear the cues here instead of the speakers.
    void setCuePlayer(std::function<void(job::AudioCue)> player) { player_ = std::move(player); }
    // What was announced (the last 50), oldest first.
    const QStringList& announcements() const noexcept { return announced_; }
    // The loaded file in words: empty with the summary off or no file.
    const QString& summary() const noexcept { return summary_; }

Q_SIGNALS:
    void summaryChanged();

private:
    void announce(const QString& text, bool assertive);
    void play(job::AudioCue cue);
    void onState();
    void onProgress();
    void onProgram();

    Machine& machine_;
    QPointer<QObject> target_;
    std::function<void(job::AudioCue)> player_;
    QStringList announced_;
    QString summary_;
    std::string lastState_;
    job::ProgressAnnouncer progress_;
};

}  // namespace gs::app
