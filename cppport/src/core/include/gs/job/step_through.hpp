#pragma once

// gSender's G-code Step Through (features/GcodeStepper): the loaded file
// walked once, keeping for every line the position the machine has reached
// once it ran and the modal state in effect, and the tools the file uses
// with the lines each one cuts (utils/linePositionIndex.ts, utils/tools.ts).
//
// Deviation: positions and modes come from the port's interpreter - the one
// that draws the toolpath - so the cutter sits on the drawn path. Upstream's
// stepper used its viewer's simpler interpreter, which also moved the marker
// on G10/G28/G38.x/G92 lines and read G91.1 as G91.

#include "gs/gcode/interpreter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::job {

// The modal groups a line's state is shared by (feed and speed change too
// often, and are kept per line).
struct StepModal {
    std::string motion;
    std::string wcs;
    std::string plane;
    std::string units;
    std::string distance;
    std::string feedMode;
    std::string spindle;
    std::string coolant;
    double tool = 0;
    bool operator==(const StepModal&) const = default;
};

// StepThroughStatus's modal cells, as Grbl's $G would report them.
inline constexpr std::array<const char*, 11> kStepModalLabels = {
    "Motion", "WCS", "Plane", "Units", "Distance", "Feed mode", "Spindle", "Coolant", "Tool", "Feed", "Speed"};
using StepModalReadout = std::array<std::string, 11>;

class StepIndex {
public:
    // The file's lines, as the rest of the app counts them.
    std::size_t lineCount() const noexcept { return lines_.size(); }
    bool empty() const noexcept { return lines_.empty(); }

    // At a 1-based line, clamped to the file: the position reached once the
    // line has run (mm, the toolpath's frame: work coordinates with G92
    // offsets), carried over lines that do not move.
    gcode::Vec4 position(std::size_t line) const;
    // The sender lines (lines with content) up to and including the line:
    // the toolpath segments of sender lines below this have run.
    std::size_t streamedThrough(std::size_t line) const;
    // The modal readout at the line: unset groups as a board reports them
    // at power-on (M5 M9 T0 F0 S0), feed and speed to 2 decimals.
    StepModalReadout modals(std::size_t line) const;

    // Distinct modal-group combinations, in first-seen order.
    const std::vector<StepModal>& modalTable() const noexcept { return modalTable_; }

private:
    friend StepIndex buildStepIndex(std::string_view, const gcode::InterpreterOptions&,
                                    const std::function<bool()>&, const std::function<void(std::size_t, std::size_t)>&);
    struct Line {
        gcode::Vec4 position;
        std::uint32_t streamed = 0;
        std::uint32_t modal = 0;
        double feedRate = 0;      // NaN until the file sets one
        double spindleSpeed = 0;  // NaN until the file sets one
    };
    std::size_t clamp(std::size_t line) const noexcept;
    std::vector<Line> lines_;
    std::vector<StepModal> modalTable_;
};

// buildLinePositionIndex(): every line through the interpreter. Polls
// `cancelled` and reports `progress(done, total)` every few thousand lines;
// a cancelled build returns an empty index.
StepIndex buildStepIndex(std::string_view program, const gcode::InterpreterOptions& options = {},
                         const std::function<bool()>& cancelled = {},
                         const std::function<void(std::size_t, std::size_t)>& progress = {});

// ---- tools ----

// TOOLPATH_COLOR_HEXES: the second tool on take these in turn (the first
// draws in the cutting colour).
extern const std::array<const char*, 12> kToolpathColors;

// A tool the file uses and the lines (1-based, inclusive) it is active for.
struct StepperTool {
    int index = 0;  // 1-based, in the file's tool change order
    double toolNumber = 0;
    std::string label;  // "T3"
    std::string color;  // "#rrggbb"
    std::size_t startLine = 1;
    std::size_t endLine = 1;
    std::string comment;                  // the tool change line's comment
    std::optional<double> diameter;       // mm, when the comment says
    std::optional<double> spindleSpeed;   // the S nearest the tool change
};

// buildStepperTools(): the tool changes (lines with both M and T events,
// ToolTimeline's buildToolArray) with the lines up to the next; with none,
// the first tool the file names runs the whole file. `tools` are the
// analysis' ("T1", ...).
std::vector<StepperTool> stepperTools(const std::map<std::size_t, gcode::SpindleToolEvent>& events,
                                      std::size_t totalLines, const std::vector<std::string>& tools,
                                      const std::string& cuttingColor);
// parseToolDiameter(): "D=6.", "6mm", 1/4" or 0.25 inch, in mm.
std::optional<double> parseToolDiameter(std::string_view comment);
// activeToolIndexForLine(): the tool active at a 1-based line, or -1.
int activeToolIndex(const std::vector<StepperTool>& tools, std::size_t line);
// readableTextColor(): "#000000" or "#ffffff", whichever contrasts more
// (WCAG) on `hex`; white for anything that is not a colour.
std::string readableTextColor(std::string_view hex);

// Playback's pace: the file's estimated run time spread over its lines
// (100 lines/s without an estimate), times the speed.
double playbackLinesPerSecond(std::size_t totalLines, double estimatedSeconds, double speed);

}  // namespace gs::job
