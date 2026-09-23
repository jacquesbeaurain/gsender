#pragma once

// The Surfacing tool (gSender's src/app/src/features/Surfacing/utils/
// surfacingGcodeGenerator.js): flattens a rectangle of stock with a spiral
// or zig-zag pass per layer, ramping into the material. Output matches
// upstream line for line; tests/data/surfacing_golden.json comes from
// running the JavaScript.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::surfacing {

enum class Pattern { Spiral, ZigZag };
// Where X0 Y0 sits on the rectangle.
enum class StartPosition { BackLeft, BackRight, FrontLeft, FrontRight, Center };

// Upstream's names ("SPIRAL_MOVEMENT", "START_POSITION_BACK_LEFT", ...), as
// stored in the configuration.
std::string_view patternName(Pattern pattern);
std::optional<Pattern> patternFromName(std::string_view name);
std::string_view startPositionName(StartPosition position);
std::optional<StartPosition> startPositionFromName(std::string_view name);

// widgets.surfacing, in the workspace units (defaults are gSender's, mm).
struct Options {
    double bitDiameter = 22;
    double stepover = 40;  // % of the bit diameter, at most 80 used
    double feedrate = 2500;
    double length = 100;   // Y
    double width = 100;    // X
    double skimDepth = 1;  // per layer
    double maxDepth = 1;
    double spindleRPM = 17000;
    Pattern type = Pattern::Spiral;
    StartPosition startPosition = StartPosition::BackLeft;
    std::string spindle = "M3";
    bool cutDirectionFlipped = false;
    bool shouldDwell = false;  // 4 s after starting and stopping the spindle
    bool flood = false;
    bool mist = false;
    int toolNumber = 0;  // M6 T<n> before the spindle starts when set
};

// generate({ returnArray: true }). Some elements are "\n" - upstream's
// spacers, which become blank lines in the program text.
std::vector<std::string> generateLines(const Options& options, bool metric);

// generate(): the program text (the lines joined with '\n').
std::string generate(const Options& options, bool metric);

// The widget stores mm and shows inch workspaces the six lengths and feeds
// converted (units.ts convertToImperial / convertToMetric).
Options toImperial(Options mm);
Options toMetric(Options inches);

}  // namespace gs::surfacing
