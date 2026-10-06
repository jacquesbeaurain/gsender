#pragma once

// What gSender's visualizer worker worked out for a loaded program - the
// per-line time estimates the sender tracks remaining time with and the
// statistics the file panel shows - computed with the port's single
// interpreter and its MotionPlanner.
// (app: workers/Visualize.worker.ts, workers/Estimate.worker.ts,
// GCodeVirtualizer.generateFileStats and lib/timeEstimator/config.ts.)

#include "gs/gcode/interpreter.hpp"
#include "gs/protocol/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::job {

struct ProgramAnalysis {
    std::size_t bytes = 0;
    std::size_t totalLines = 0;       // every line, as the visualizer counts them
    // Seconds per line the sender streams (every non-blank line, in order),
    // so lineTime[i] belongs to sender line i, and how overrides scale it
    // (gcode::LineKind).
    std::vector<float> lineTime;
    std::vector<std::uint8_t> lineKind;
    double estimatedTime = 0;  // seconds
    gcode::BoundingBox bounds;
    std::string fileModal = "G21";  // units in effect at the end of the file
    std::vector<std::string> tools;
    std::vector<std::string> spindleSpeeds;
    std::vector<std::string> feedrates;
    std::string usedAxes;
    std::vector<std::string> invalidLines;
    std::vector<std::size_t> toolChangeLines;  // 1-based
    std::map<std::size_t, gcode::SpindleToolEvent> spindleToolEvents;
    gcode::FileType fileType = gcode::FileType::Default;
    double rotaryDiameter = 0;
    bool cancelled = false;
};

// What else feeds the estimate beside the firmware settings.
struct EstimatorInputs {
    bool grblHal = false;
    bool laserMode = false;        // the spindle widget's mode
    bool useAaxisForGrbl = false;  // otherwise Grbl's A runs on Y's limits
    double baudRate = 115200;
};

// buildEstimatorConfig(): the time estimator's machine from the firmware
// settings ($11, $12, $110-$113, $120-$123, $376, $394, $398, $701), the
// ATC option (45 s per tool change) and `inputs`. Two results compare equal
// when a re-estimate would change nothing (getEstimatorSignature).
gcode::EstimatorSettings estimatorSettingsFor(const protocol::FirmwareSettings& settings,
                                              const EstimatorInputs& inputs);

// Runs the whole program through the interpreter, reporting geometry to
// `sink` when given. `cancelled` is polled every few thousand lines; a
// cancelled analysis is incomplete.
// The file's line (1-based) of each line the sender streams - the lines
// with content - so a sender line (currentLineRunning) finds its text.
std::vector<std::size_t> senderLineNumbers(std::string_view program);

ProgramAnalysis analyzeProgram(std::string_view program, const gcode::InterpreterOptions& options = {},
                               const gcode::EstimatorSettings& estimator = {}, gcode::GeometrySink* sink = nullptr,
                               const std::function<bool()>& cancelled = {});

// The time estimate alone, for a re-estimate when the machine settings
// change (Estimate.worker). Empty when cancelled.
std::optional<gcode::EstimateResult> estimateProgram(std::string_view program,
                                                     const gcode::EstimatorSettings& estimator,
                                                     const std::function<bool()>& cancelled = {});

}  // namespace gs::job
