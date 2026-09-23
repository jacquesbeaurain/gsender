#include "gs/job/program_analysis.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <cmath>
#include <optional>

namespace gs::job {
namespace {

// A positive number from setting `key`, else `fallback`.
double positiveSetting(const protocol::OrderedMap& settings, std::string_view key, double fallback) {
    const std::string* text = settings.find(key);
    if (!text) {
        return fallback;
    }
    const double value = js::stringToNumber(*text);
    return std::isfinite(value) && value > 0 ? value : fallback;
}

}  // namespace

gcode::InterpreterOptions interpreterOptionsFor(const protocol::FirmwareSettings& settings) {
    gcode::InterpreterOptions options;
    const protocol::OrderedMap& s = settings.settings;
    options.x = {positiveSetting(s, "$120", options.x.acceleration), positiveSetting(s, "$110", options.x.maxFeed)};
    options.y = {positiveSetting(s, "$121", options.y.acceleration), positiveSetting(s, "$111", options.y.maxFeed)};
    options.z = {positiveSetting(s, "$122", options.z.acceleration), positiveSetting(s, "$112", options.z.maxFeed)};
    options.a = {positiveSetting(s, "$123", options.a.acceleration), positiveSetting(s, "$113", options.a.maxFeed)};
    if (const auto newopt = settings.info.find("NEWOPT"); newopt != settings.info.end()) {
        options.atcEnabled = newopt->second.option("ATC") == std::optional<std::string>("1");
    }
    return options;
}

ProgramAnalysis analyzeProgram(std::string_view program, const gcode::InterpreterOptions& options,
                               gcode::GeometrySink* sink, const std::function<bool()>& cancelled) {
    ProgramAnalysis result;
    result.bytes = program.size();
    gcode::Interpreter interpreter(options);
    interpreter.setSink(sink);

    std::size_t index = 0;
    for (std::string_view line : str::splitLines(program)) {
        if (cancelled && (++index % 4096) == 0 && cancelled()) {
            result.cancelled = true;
            return result;
        }
        interpreter.processLine(line);
        // The sender streams only the lines with content; keep the
        // estimates on its numbering.
        if (!str::trim(line).empty()) {
            result.estimates.push_back(interpreter.lastLineTime());
        }
    }

    result.totalLines = interpreter.totalLines();
    result.estimatedTime = interpreter.totalTime();
    result.bounds = interpreter.bounds();
    result.fileModal = interpreter.modal().units;
    result.tools = interpreter.tools().values();
    result.spindleSpeeds = interpreter.spindleSpeeds().values();
    result.feedrates = interpreter.feedrates().values();
    result.usedAxes = interpreter.usedAxes();
    result.invalidLines = interpreter.invalidLines();
    result.toolChangeLines = interpreter.toolChangeLines();
    result.spindleToolEvents = interpreter.spindleToolEvents();
    result.fileType = interpreter.fileType();
    result.rotaryDiameter = interpreter.rotaryDiameter();
    return result;
}

}  // namespace gs::job
