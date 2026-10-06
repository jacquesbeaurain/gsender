#include "gs/job/program_analysis.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <cmath>
#include <optional>

namespace gs::job {
namespace {

constexpr double kAtcToolChangeSeconds = 45;

// num(): Number(value) when finite.
std::optional<double> settingNumber(const protocol::OrderedMap& settings, std::string_view key) {
    const std::string* text = settings.find(key);
    if (!text) {
        return std::nullopt;
    }
    const double value = js::stringToNumber(*text);
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

// num(value) || 0, as the integer JavaScript's & takes.
std::int64_t settingBits(const protocol::OrderedMap& settings, std::string_view key) {
    return static_cast<std::int64_t>(settingNumber(settings, key).value_or(0));
}

// Feeds the estimator the interpreter's lines, keeping the planner's line
// numbering on the sender's (it streams only the lines with content).
template <typename OnLine>
bool runLines(std::string_view program, gcode::Interpreter& interpreter, const std::function<bool()>& cancelled,
              OnLine&& onLine) {
    std::size_t index = 0;
    for (std::string_view line : str::splitLines(program)) {
        if (cancelled && (++index % 4096) == 0 && cancelled()) {
            return false;
        }
        onLine(line);
        interpreter.processLine(line);
    }
    return true;
}

// The estimate on the sender's lines: one slot per streamed line.
void fitToSenderLines(gcode::EstimateResult& estimate, std::size_t senderLines) {
    estimate.lineTime.resize(senderLines, 0.0F);
    estimate.lineKind.resize(senderLines, gcode::kLineKindNone);
}

}  // namespace

gcode::EstimatorSettings estimatorSettingsFor(const protocol::FirmwareSettings& settings,
                                              const EstimatorInputs& inputs) {
    const protocol::OrderedMap& s = settings.settings;
    gcode::EstimatorSettings e;
    e.firmware = inputs.grblHal ? gcode::Firmware::GrblHal : gcode::Firmware::Grbl;
    for (std::size_t i = 0; i < 4; ++i) {
        e.maxRate[i] = settingNumber(s, "$11" + std::to_string(i));
        e.accel[i] = settingNumber(s, "$12" + std::to_string(i));
    }
    e.junctionDeviation = settingNumber(s, "$11");
    e.arcTolerance = settingNumber(s, "$12");
    // Bf reports one block less than the configured buffer
    const double plannerBlocks = settingNumber(s, "$398").value_or(0);
    e.plannerBlocks = inputs.grblHal ? (plannerBlocks != 0 ? plannerBlocks - 1 : 34) : 15;
    e.aUsesYLimits = !inputs.grblHal && !inputs.useAaxisForGrbl;
    const std::int64_t rotaryAxes = settingBits(s, "$376");
    const std::int64_t rotaryOptions = settingBits(s, "$701");
    e.rotaryFix = inputs.grblHal && (rotaryOptions & 1) == 1 && (rotaryAxes & 1) == 1;
    e.rotaryRevertMetric = (rotaryOptions & 2) == 2;
    e.laserMode = inputs.laserMode;
    if (const auto newopt = settings.info.find("NEWOPT"); newopt != settings.info.end()) {
        e.toolChangeTime = newopt->second.option("ATC") == std::optional<std::string>("1") ? kAtcToolChangeSeconds : 0;
    }
    e.spindleDelay = inputs.grblHal ? settingNumber(s, "$394").value_or(0) : 0;
    // grblHAL is typically native USB where serial speed isn't the bottleneck
    const double baud = std::isfinite(inputs.baudRate) && inputs.baudRate != 0 ? inputs.baudRate : 115200;
    e.serialBytesPerSecond = inputs.grblHal ? 0 : baud / 10;
    return e;
}

std::vector<std::size_t> senderLineNumbers(std::string_view program) {
    std::vector<std::size_t> numbers;
    std::size_t line = 0;
    for (std::string_view text : str::splitLines(program)) {
        ++line;
        if (!str::trim(text).empty()) {
            numbers.push_back(line);
        }
    }
    return numbers;
}

ProgramAnalysis analyzeProgram(std::string_view program, const gcode::InterpreterOptions& options,
                               const gcode::EstimatorSettings& estimator, gcode::GeometrySink* sink,
                               const std::function<bool()>& cancelled) {
    ProgramAnalysis result;
    result.bytes = program.size();
    gcode::Interpreter interpreter(options);
    interpreter.setSink(sink);
    gcode::MotionPlanner planner(estimator, program.size() / 16);
    interpreter.setEstimator(&planner);

    std::size_t senderLines = 0;
    const bool complete = runLines(program, interpreter, cancelled, [&](std::string_view line) {
        // The geometry's line numbers follow the sender's too.
        if (!str::trim(line).empty()) {
            if (sink) {
                sink->atLine(senderLines);
            }
            ++senderLines;
        }
    });
    if (!complete) {
        result.cancelled = true;
        return result;
    }

    gcode::EstimateResult estimate = planner.finish();
    fitToSenderLines(estimate, senderLines);
    result.lineTime = std::move(estimate.lineTime);
    result.lineKind = std::move(estimate.lineKind);
    result.estimatedTime = estimate.totalTime;
    result.totalLines = interpreter.totalLines();
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

std::optional<gcode::EstimateResult> estimateProgram(std::string_view program,
                                                     const gcode::EstimatorSettings& estimator,
                                                     const std::function<bool()>& cancelled) {
    gcode::Interpreter interpreter;
    gcode::MotionPlanner planner(estimator, program.size() / 16);
    interpreter.setEstimator(&planner);
    std::size_t senderLines = 0;
    if (!runLines(program, interpreter, cancelled, [&](std::string_view line) {
            senderLines += str::trim(line).empty() ? 0 : 1;
        })) {
        return std::nullopt;
    }
    gcode::EstimateResult estimate = planner.finish();
    fitToSenderLines(estimate, senderLines);
    return estimate;
}

}  // namespace gs::job
