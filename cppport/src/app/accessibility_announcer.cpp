#include "accessibility_announcer.hpp"

#include "machine.hpp"

#include "gs/controller/controller.hpp"

#include <QAccessible>

#include <array>
#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace gs::app {

void playAudioCue(job::AudioCue cue) {
#ifdef _WIN32
    // PlaySound plays from memory that must outlive it: the three files are
    // made once and kept.
    static const std::array<std::string, 3> files{job::wavFile(job::audioCueSamples(job::AudioCue::Success)),
                                                  job::wavFile(job::audioCueSamples(job::AudioCue::Alarm)),
                                                  job::wavFile(job::audioCueSamples(job::AudioCue::Info))};
    const std::string& file = files[static_cast<std::size_t>(cue)];
    PlaySoundA(file.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    (void)cue;
    std::fputc('\a', stderr);
    std::fflush(stderr);
#endif
}

AccessibilityAnnouncer::AccessibilityAnnouncer(Machine& machine, QObject* parent)
    : QObject(parent), machine_(machine), player_(&playAudioCue) {
    connect(&machine_, &Machine::stateChanged, this, &AccessibilityAnnouncer::onState);
    connect(&machine_, &Machine::senderStatusChanged, this, &AccessibilityAnnouncer::onProgress);
    connect(&machine_, &Machine::programChanged, this, &AccessibilityAnnouncer::onProgram);
    connect(&machine_, &Machine::appSettingsChanged, this, &AccessibilityAnnouncer::onProgram);
    // The cues upstream subscribes to ("probe:success", "toolchange:start").
    connect(&machine_, &Machine::probeSucceeded, this, [this] {
        const AccessibilitySettings& a = machine_.settings().accessibility;
        if (a.audioCues && a.cueProbeSuccess) {
            play(job::AudioCue::Success);
        }
    });
    connect(&machine_, &Machine::toolChangeRequired, this, [this] {
        const AccessibilitySettings& a = machine_.settings().accessibility;
        if (a.audioCues && a.cueToolChange) {
            play(job::AudioCue::Info);
        }
    });
    onProgram();
}

void AccessibilityAnnouncer::announce(const QString& text, bool assertive) {
    announced_ << text;
    while (announced_.size() > 50) {
        announced_.removeFirst();
    }
    if (target_ && QAccessible::isActive()) {
        QAccessibleAnnouncementEvent event(target_, text);
        event.setPoliteness(assertive ? QAccessible::AnnouncementPoliteness::Assertive
                                      : QAccessible::AnnouncementPoliteness::Polite);
        QAccessible::updateAccessibility(&event);
    }
}

void AccessibilityAnnouncer::play(job::AudioCue cue) {
    if (player_) {
        player_(cue);
    }
}

void AccessibilityAnnouncer::onState() {
    controller::Controller* c = machine_.controller();
    const std::string state = c ? c->state().status.activeState : std::string();
    if (state.empty() || state == lastState_) {
        return;
    }
    const AccessibilitySettings& a = machine_.settings().accessibility;
    if (a.statusAnnouncements) {
        announce(tr("Machine status changed to %1").arg(QString::fromStdString(state)), true);
    }
    if (a.audioCues) {
        if (state == "Alarm" && a.cueAlarm) {
            play(job::AudioCue::Alarm);
        } else if (lastState_ == "Run" && state == "Idle" && a.cueJobComplete) {
            play(job::AudioCue::Success);
        }
    }
    lastState_ = state;
}

void AccessibilityAnnouncer::onProgress() {
    const AccessibilitySettings& a = machine_.settings().accessibility;
    controller::Controller* c = machine_.controller();
    if (!a.jobProgressAnnouncements || !c || !c->sender().hasProgram()) {
        return;
    }
    // The job's progress bar: the lines the board has taken.
    const controller::SenderStatus status = c->sender().status();
    const double percent = status.total > 0 ? 100.0 * static_cast<double>(status.received) /
                                                  static_cast<double>(status.total)
                                            : 0.0;
    if (const std::optional<std::string> message = progress_.update(percent, a.jobProgressIncrement)) {
        announce(QString::fromStdString(*message), false);
    }
}

void AccessibilityAnnouncer::onProgram() {
    QString text;
    if (machine_.settings().accessibility.gcodeSummary && machine_.hasProgram() && !machine_.isAnalyzing()) {
        // The numbers are in the file's own units.
        const job::ProgramAnalysis& analysis = machine_.analysis();
        text = QString::fromStdString(job::jobSummary(machine_.programName().toStdString(), analysis,
                                                      machine_.programText(),
                                                      analysis.fileModal == "G20" ? "in" : "mm"));
    }
    if (text == summary_) {
        return;
    }
    summary_ = text;
    if (!text.isEmpty()) {
        announce(text, false);
    }
    Q_EMIT summaryChanged();
}

}  // namespace gs::app
