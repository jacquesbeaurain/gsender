#pragma once

// The words and sounds of gSender's AccessibilityAnnouncer
// (features/Helper/AccessibilityAnnouncer.tsx): the loaded file summed up
// for screen readers, the job's progress every so many percent, and the
// tones of the audio cues.

#include "gs/job/program_analysis.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::job {

// formatTime(): "42 seconds", "3 minutes and 5 seconds", "2 hours and 7
// minutes" (seconds rounded up, so 59.5 s past a minute reads 60 seconds).
std::string spokenDuration(double seconds);

// The G-code summary, a sentence each: the file's name; its size and
// ranges in `units` ("mm" or "in"); the estimated time; the tools; the CAM
// comments of its first 100 lines that mention a tool or the stock; its
// program stops (lines starting M0 or M1); coolant (M7/M8 anywhere); the
// spindle speeds and feed rates (one, or their range); the axes it moves;
// its invalid lines.
std::string jobSummary(std::string_view name, const ProgramAnalysis& analysis, std::string_view program,
                       std::string_view units);

// Job progress announcements: "Job progress: 30%" each time the progress
// reaches the next multiple of `increment` past the last one announced, "Job
// complete: 100%" once at the end; a lower progress (a new job) starts over.
class ProgressAnnouncer {
public:
    std::optional<std::string> update(double percent, int increment);

private:
    int last_ = 0;
};

// The audio cues' tones, as the Web Audio oscillators play them: Success a
// sine rising from 880 to 1760 Hz, fading over 0.2 s; Alarm a square wave at
// 440, 220 and 440 Hz for 0.4 s; Info a triangle at 660 Hz fading over
// 0.15 s. Samples in [-1, 1].
enum class AudioCue { Success, Alarm, Info };
std::vector<float> audioCueSamples(AudioCue cue, int sampleRate = 44100);
// A 16-bit mono PCM WAV file of the samples.
std::string wavFile(const std::vector<float>& samples, int sampleRate = 44100);

}  // namespace gs::job
