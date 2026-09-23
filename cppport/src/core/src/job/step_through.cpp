#include "gs/job/step_through.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <boost/regex.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace gs::job {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr std::size_t kLinesPerChunk = 20000;
constexpr double kMmPerInch = 25.4;

std::string signature(const StepModal& m) {
    return m.motion + '|' + m.wcs + '|' + m.plane + '|' + m.units + '|' + m.distance + '|' + m.feedMode + '|' +
           m.spindle + '|' + m.coolant + '|' + js::numberToString(m.tool);
}

// Feed and speed with long conversion tails, to 2 decimals without trailing
// zeros (roundModalNumber).
std::string modalNumber(double value) {
    return js::numberToString(js::stringToNumber(js::toFixed(value, 2)));
}

// A positive, finite number, else nothing (tools.ts' finite()).
std::optional<double> positive(double value) {
    return std::isfinite(value) && value > 0 ? std::optional<double>(value) : std::nullopt;
}

// spindleSpeedForTool(): the S nearest the tool change at or before its last
// line (the earlier one on a tie).
std::optional<double> spindleSpeedForTool(const std::map<std::size_t, gcode::SpindleToolEvent>& events,
                                          std::size_t line, std::size_t endLine) {
    std::optional<double> best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (const auto& [eventLine, event] : events) {
        if (!event.S || eventLine > endLine) {
            continue;
        }
        const double distance = std::fabs(static_cast<double>(eventLine) - static_cast<double>(line));
        if (distance < bestDistance) {
            bestDistance = distance;
            best = event.S;
        }
    }
    return best ? positive(*best) : std::nullopt;
}

double linearise(int channel) {
    const double c = channel / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

}  // namespace

// ---- the line index ------------------------------------------------------------------------

std::size_t StepIndex::clamp(std::size_t line) const noexcept {
    return std::min(std::max<std::size_t>(line, 1), lines_.size()) - 1;
}

gcode::Vec4 StepIndex::position(std::size_t line) const {
    return lines_.empty() ? gcode::Vec4{} : lines_[clamp(line)].position;
}

std::size_t StepIndex::streamedThrough(std::size_t line) const {
    return lines_.empty() || line == 0 ? 0 : lines_[clamp(line)].streamed;
}

StepModalReadout StepIndex::modals(std::size_t line) const {
    if (lines_.empty() || modalTable_.empty()) {
        return {};
    }
    const Line& at = lines_[clamp(line)];
    const StepModal& m = modalTable_[at.modal];
    return {m.motion,
            m.wcs,
            m.plane,
            m.units,
            m.distance,
            m.feedMode,
            m.spindle,
            m.coolant,
            "T" + js::numberToString(m.tool),
            "F" + (std::isfinite(at.feedRate) ? modalNumber(at.feedRate) : std::string("0")),
            "S" + (std::isfinite(at.spindleSpeed) ? modalNumber(at.spindleSpeed) : std::string("0"))};
}

StepIndex buildStepIndex(std::string_view program, const gcode::InterpreterOptions& options,
                         const std::function<bool()>& cancelled,
                         const std::function<void(std::size_t, std::size_t)>& progress) {
    StepIndex index;
    const std::vector<std::string_view> lines = str::splitLines(program);
    index.lines_.reserve(lines.size());
    std::unordered_map<std::string, std::uint32_t> modalBySignature;
    gcode::Interpreter interpreter(options);
    std::uint32_t streamed = 0;
    double feedRate = kNaN;
    double spindleSpeed = kNaN;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0 && i % kLinesPerChunk == 0) {
            if (progress) {
                progress(i, lines.size());
            }
            if (cancelled && cancelled()) {
                return {};
            }
        }
        const std::string_view line = lines[i];
        interpreter.processLine(line);
        if (!str::trim(line).empty()) {
            ++streamed;
        }
        // F and S as the file last set them (a word that is not a number
        // sets nothing).
        for (const gcode::Token& token : interpreter.lastScan().tokens) {
            if (token.letter == 'F' || token.letter == 'S') {
                const double value = js::stringToNumber(token.value);
                if (std::isfinite(value)) {
                    (token.letter == 'F' ? feedRate : spindleSpeed) = value;
                }
            }
        }
        const gcode::Modal& m = interpreter.modal();
        StepModal modal{m.motion, m.wcs, m.plane, m.units, m.distance, m.feedrate, m.spindle, m.coolant, m.tool};
        auto [entry, added] =
            modalBySignature.try_emplace(signature(modal), static_cast<std::uint32_t>(index.modalTable_.size()));
        if (added) {
            index.modalTable_.push_back(std::move(modal));
        }
        const gcode::Vec4& p = interpreter.position();
        const gcode::Vec4& g92 = interpreter.g92Offset();
        index.lines_.push_back(
            {{p.x + g92.x, p.y + g92.y, p.z + g92.z, p.a + g92.a}, streamed, entry->second, feedRate, spindleSpeed});
    }
    if (progress) {
        progress(lines.size(), lines.size());
    }
    return index;
}

// ---- tools -------------------------------------------------------------------------------

const std::array<const char*, 12> kToolpathColors = {"#4A90E2", "#F08A4F", "#D74296", "#42D7BA",
                                                     "#A7D742", "#C44C36", "#A142D7", "#4296D7",
                                                     "#D7BA42", "#42D763", "#D742C4", "#D74242"};

std::vector<StepperTool> stepperTools(const std::map<std::size_t, gcode::SpindleToolEvent>& events,
                                      std::size_t totalLines, const std::vector<std::string>& tools,
                                      const std::string& cuttingColor) {
    std::vector<StepperTool> out;
    for (const auto& [line, event] : events) {
        if (!event.M || !event.T) {
            continue;
        }
        StepperTool tool;
        tool.index = static_cast<int>(out.size()) + 1;
        tool.toolNumber = *event.T;
        tool.label = "T" + js::numberToString(*event.T);
        tool.color = out.empty() ? cuttingColor : kToolpathColors[out.size() % kToolpathColors.size()];
        tool.startLine = line;
        tool.comment = event.comment;
        out.push_back(std::move(tool));
    }
    if (out.empty()) {
        // No tool change: the program runs on the first tool it names.
        if (tools.empty()) {
            return out;
        }
        std::string label = tools.front();
        std::string number = label;
        if (!number.empty() && (number[0] == 'T' || number[0] == 't')) {
            number.erase(0, 1);
        }
        std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        const double toolNumber = js::stringToNumber(number);
        StepperTool tool;
        tool.index = 1;
        tool.toolNumber = std::isfinite(toolNumber) ? toolNumber : 0;
        tool.label = std::move(label);
        tool.color = cuttingColor;
        tool.startLine = 1;
        tool.endLine = totalLines;
        tool.spindleSpeed = spindleSpeedForTool(events, 1, totalLines);
        out.push_back(std::move(tool));
        return out;
    }
    out.back().endLine = totalLines;
    for (std::size_t i = out.size() - 1; i-- > 0;) {
        out[i].endLine = out[i + 1].startLine - 1;
    }
    for (StepperTool& tool : out) {
        tool.diameter = parseToolDiameter(tool.comment);
        tool.spindleSpeed = spindleSpeedForTool(events, tool.startLine, tool.endLine);
    }
    return out;
}

std::optional<double> parseToolDiameter(std::string_view comment) {
    if (comment.empty()) {
        return std::nullopt;
    }
    static const boost::regex equals(R"(\bD\s*=\s*(\d+(?:\.\d*)?))", boost::regex::icase);
    static const boost::regex mm(R"((\d+(?:\.\d*)?)\s*mm\b)", boost::regex::icase);
    static const boost::regex fraction(R"lit((\d+)\s*/\s*(\d+)\s*(?:"|''|in\b|inch\b))lit", boost::regex::icase);
    static const boost::regex decimal(R"lit((\d+(?:\.\d*)?)\s*(?:"|''|in\b|inch\b))lit", boost::regex::icase);
    boost::match_results<std::string_view::const_iterator> match;
    const auto find = [&](const boost::regex& pattern) {
        return boost::regex_search(comment.begin(), comment.end(), match, pattern);
    };
    const auto number = [&](int group) { return js::stringToNumber(std::string(match[group].first, match[group].second)); };
    if (find(equals)) {
        return positive(number(1));
    }
    if (find(mm)) {
        return positive(number(1));
    }
    if (find(fraction)) {
        const double denominator = number(2);
        if (denominator > 0) {
            return positive(number(1) / denominator * kMmPerInch);
        }
    }
    if (find(decimal)) {
        return positive(number(1) * kMmPerInch);
    }
    return std::nullopt;
}

int activeToolIndex(const std::vector<StepperTool>& tools, std::size_t line) {
    for (std::size_t i = 0; i < tools.size(); ++i) {
        if (line >= tools[i].startLine && line <= tools[i].endLine) {
            return static_cast<int>(i);
        }
    }
    return -1;  // before the first tool change nothing cuts yet
}

std::string readableTextColor(std::string_view hex) {
    std::string digits(str::trim(hex));
    if (!digits.empty() && digits[0] == '#') {
        digits.erase(0, 1);
    }
    const bool valid = (digits.size() == 3 || digits.size() == 6) &&
                       std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isxdigit(c); });
    if (!valid) {
        return "#ffffff";
    }
    if (digits.size() == 3) {
        digits = {digits[0], digits[0], digits[1], digits[1], digits[2], digits[2]};
    }
    const auto channel = [&digits](int i) { return std::stoi(digits.substr(static_cast<std::size_t>(i) * 2, 2), nullptr, 16); };
    const double luminance =
        0.2126 * linearise(channel(0)) + 0.7152 * linearise(channel(1)) + 0.0722 * linearise(channel(2));
    // WCAG contrast against white (luminance 1) and black (0).
    const double againstWhite = 1.05 / (luminance + 0.05);
    const double againstBlack = (luminance + 0.05) / 0.05;
    return againstBlack >= againstWhite ? "#000000" : "#ffffff";
}

double playbackLinesPerSecond(std::size_t totalLines, double estimatedSeconds, double speed) {
    const double base = estimatedSeconds > 0 ? static_cast<double>(totalLines) / estimatedSeconds : 100.0;
    return base * speed;
}

}  // namespace gs::job
