// The accessibility announcer's words and sounds: the job summary, progress
// announcements and the audio cues' tones.

#include "gs/job/accessibility.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::job;

TEST(Accessibility, DurationsAreSpokenAsUpstreamSpeaksThem) {
    EXPECT_EQ(spokenDuration(42.2), "43 seconds");
    EXPECT_EQ(spokenDuration(185), "3 minutes and 5 seconds");
    EXPECT_EQ(spokenDuration(119.5), "1 minutes and 60 seconds");  // seconds rounded up
    EXPECT_EQ(spokenDuration(7630), "2 hours and 7 minutes");
}

TEST(Accessibility, TheSummarySaysWhatTheFileDoes) {
    ProgramAnalysis a;
    a.bounds = {{0, 0, -3, 0}, {50, 25, 5, 0}};
    a.estimatedTime = 185;
    a.tools = {"T1", "T2"};
    a.spindleSpeeds = {"S18000", "S12000"};
    a.feedrates = {"F1000", "F300"};
    a.usedAxes = "XYZ";
    a.invalidLines = {"G1 X"};
    // Comments about tools or stock in the first lines; lines starting M0 or
    // M1 (case matters, M03 and M100 count); M8 anywhere.
    const std::string program = "(Tool: 1/4in end mill)\r\n; Stock: 100x100x20\r\n(notes) (more)\r\nG21 G90 (units)\r\n"
                                "M03 S12000\r\nM8\r\nM0\r\nm1\r\nM100\r\n";
    EXPECT_EQ(jobSummary("part.nc", a, program, "mm"),
              "File loaded: part.nc. Dimensions: 50.00 wide, 25.00 deep, and 8.00 high mm. X ranges from 0.00 to "
              "50.00. Y ranges from 0.00 to 25.00. Minimum Z height is -3.00 mm. Estimated completion time: 3 minutes "
              "and 5 seconds. Uses 2 tools: 1, 2. Job metadata: Tool: 1/4in end mill, Stock: 100x100x20. Contains 3 "
              "program stops (M0/M1). File requests coolant (M7/M8). Spindle speed range: 12000 to 18000 RPM. "
              "Feedrate range: 300 to 1000 mm/min. Active axes: X, Y, Z. WARNING: 1 invalid G-code lines detected.");

    ProgramAnalysis b;
    b.bounds = {{-1.5, 2, 0, 0}, {1.5, 4.25, 0.5, 0}};
    b.tools = {"T3"};
    b.spindleSpeeds = {"S1000"};
    b.feedrates = {"F20"};
    b.usedAxes = "XY";
    EXPECT_EQ(jobSummary("inch.nc", b, "G20\nM1\nG1 X1 F20\n", "in"),
              "File loaded: inch.nc. Dimensions: 3.00 wide, 2.25 deep, and 0.50 high in. X ranges from -1.50 to 1.50. "
              "Y ranges from 2.00 to 4.25. Minimum Z height is 0.00 in. Uses 1 tool: 3. Contains 1 program stop "
              "(M0/M1). Spindle speed: 1000 RPM. Feedrate: 20 in/min. Active axes: X, Y.");
}

TEST(Accessibility, ProgressIsAnnouncedEverySoManyPercent) {
    ProgressAnnouncer p;
    EXPECT_FALSE(p.update(5, 10));
    EXPECT_EQ(p.update(12.7, 10), "Job progress: 12%");
    EXPECT_FALSE(p.update(19, 10));
    EXPECT_EQ(p.update(20, 10), "Job progress: 20%");
    EXPECT_EQ(p.update(47, 10), "Job progress: 47%");  // the next one at 50
    EXPECT_FALSE(p.update(49, 10));
    EXPECT_EQ(p.update(100, 10), "Job complete: 100%");
    EXPECT_FALSE(p.update(100, 10));
    EXPECT_FALSE(p.update(3, 10));  // a new job starts over
    EXPECT_EQ(p.update(10, 10), "Job progress: 10%");
    EXPECT_EQ(p.update(35, 25), "Job progress: 35%");
}

namespace {

int signChanges(const std::vector<float>& samples, std::size_t from, std::size_t to) {
    int changes = 0;
    for (std::size_t i = from + 1; i < to; ++i) {
        changes += (samples[i - 1] < 0) != (samples[i] < 0) ? 1 : 0;
    }
    return changes;
}

}  // namespace

TEST(Accessibility, TheCuesSoundAsTheirOscillators) {
    const int rate = 44100;
    const std::vector<float> success = audioCueSamples(AudioCue::Success, rate);
    ASSERT_EQ(success.size(), 8820u);  // 0.2 s
    // Past its sweep the sine sits at 1760 Hz: 176 cycles in 0.1 s.
    EXPECT_NEAR(signChanges(success, 4410, 8820), 352, 2);
    EXPECT_LE(*std::max_element(success.begin(), success.end()), 0.1f);
    EXPECT_NEAR(std::fabs(success.back()), 0.0, 0.011);  // faded to 0.01

    const std::vector<float> alarm = audioCueSamples(AudioCue::Alarm, rate);
    ASSERT_EQ(alarm.size(), 17640u);  // 0.4 s
    EXPECT_NEAR(signChanges(alarm, 0, 4410), 88, 2);      // 440 Hz
    EXPECT_NEAR(signChanges(alarm, 4410, 8820), 44, 2);   // 220 Hz
    EXPECT_FLOAT_EQ(std::fabs(alarm[100]), 0.1f);         // a square wave
    EXPECT_NEAR(std::fabs(alarm.back()), 0.0, 0.001);     // ramped down

    const std::vector<float> info = audioCueSamples(AudioCue::Info, rate);
    ASSERT_EQ(info.size(), 6615u);  // 0.15 s
    EXPECT_NEAR(signChanges(info, 0, 6615), 198, 2);      // 660 Hz

    const std::string wav = wavFile({0.0f, 0.5f, -1.0f}, 8000);
    ASSERT_EQ(wav.size(), 44u + 6u);
    EXPECT_EQ(wav.substr(0, 4), "RIFF");
    EXPECT_EQ(wav.substr(8, 8), "WAVEfmt ");
    EXPECT_EQ(wav.substr(36, 4), "data");
    EXPECT_EQ(wav.substr(44), std::string("\x00\x00\x00\x40\x01\x80", 6));
}
