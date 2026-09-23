#pragma once

// What gSender's visualizer worker worked out for a loaded program - the
// per-line time estimates the sender's countdown uses and the statistics the
// file panel shows - computed with the port's single interpreter.
// (app: workers/Visualize.worker.ts + GCodeVirtualizer.generateFileStats,
// and the settings read in store/redux/sagas/controllerSagas.tsx.)

#include "gs/gcode/interpreter.hpp"
#include "gs/protocol/types.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace gs::job {

struct ProgramAnalysis {
    std::size_t bytes = 0;
    std::size_t totalLines = 0;       // every line, as the visualizer counts them
    // Seconds per line the sender streams (every non-blank line, in order),
    // so estimates[i] belongs to sender line i.
    std::vector<double> estimates;
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

// Interpreter limits from the firmware settings: accelerations $120-$123,
// maximum rates $110-$113 (defaults where a setting is missing or not a
// positive number) and the ATC option (a 45 s estimate per tool change).
gcode::InterpreterOptions interpreterOptionsFor(const protocol::FirmwareSettings& settings);

// Runs the whole program through the interpreter, reporting geometry to
// `sink` when given. `cancelled` is polled every few thousand lines; a
// cancelled analysis is incomplete.
ProgramAnalysis analyzeProgram(std::string_view program, const gcode::InterpreterOptions& options = {},
                               gcode::GeometrySink* sink = nullptr,
                               const std::function<bool()>& cancelled = {});

}  // namespace gs::job
