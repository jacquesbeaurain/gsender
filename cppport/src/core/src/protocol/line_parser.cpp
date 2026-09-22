#include "gs/protocol/line_parser.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <boost/regex.hpp>

#include <cmath>
#include <map>

namespace gs::protocol {
namespace {

using boost::regex;
using boost::smatch;

bool search(const std::string& text, const regex& re, smatch& m) {
    return boost::regex_search(text, m, re);
}

std::string group(const smatch& m, int i) {
    return m[i].matched ? m[i].str() : std::string();
}

int toInt(std::string_view text) {
    const double d = js::stringToNumber(text);
    return std::isfinite(d) ? static_cast<int>(d) : 0;
}

AxisValues axisValues(std::string_view csv) {
    AxisValues out;
    for (std::string_view part : str::splitView(csv, ',')) {
        if (out.count >= out.values.size()) {
            break;
        }
        out.values[out.count++] = js::stringToNumber(part);
    }
    return out;
}

// ---- status reports ------------------------------------------------------------

// Emulates /[a-zA-Z]+(:[a-zA-Z0-9.-]+(,SUB+){0,5})?/g on the report body.
std::vector<std::string_view> statusTokens(std::string_view s, bool grblHal) {
    const auto firstValueChar = [](char c) {
        return str::isAsciiAlpha(c) || str::isAsciiDigit(c) || c == '.' || c == '-';
    };
    const auto nextValueChar = [grblHal](char c) {
        if (str::isAsciiDigit(c) || c == '.' || c == '-' || c == '[') {
            return true;
        }
        return grblHal ? str::isAsciiAlpha(c) : c == 'a';
    };
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < s.size()) {
        if (!str::isAsciiAlpha(s[i])) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < s.size() && str::isAsciiAlpha(s[end])) {
            ++end;
        }
        if (end < s.size() && s[end] == ':') {
            std::size_t k = end + 1;
            while (k < s.size() && firstValueChar(s[k])) {
                ++k;
            }
            if (k > end + 1) {
                end = k;
                for (int repeat = 0; repeat < 5; ++repeat) {
                    if (end >= s.size() || s[end] != ',') {
                        break;
                    }
                    std::size_t m = end + 1;
                    while (m < s.size() && nextValueChar(s[m])) {
                        ++m;
                    }
                    if (m == end + 1) {
                        break;
                    }
                    end = m;
                }
            }
        }
        tokens.push_back(s.substr(i, end - i));
        i = end;
    }
    return tokens;
}

}  // namespace

std::optional<StatusReport> parseStatusReport(std::string_view line, bool grblHal) {
    if (line.size() < 3 || line.front() != '<' || line.back() != '>') {
        return std::nullopt;
    }
    const std::vector<std::string_view> tokens = statusTokens(line.substr(1, line.size() - 2), grblHal);
    if (tokens.empty()) {
        return std::nullopt;
    }

    StatusReport report;
    {
        const std::vector<std::string_view> state = str::splitView(tokens.front(), ':');
        report.activeState = std::string(state[0]);
        report.subState = state.size() > 1 ? toInt(state[1]) : 0;
    }

    std::map<std::string, std::vector<std::string>, std::less<>> fields;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        // /^(.+):(.+)/ is greedy: the name ends at the last colon.
        const std::string_view token = tokens[i];
        const std::size_t colon = token.rfind(':');
        if (colon == std::string_view::npos || colon == 0 || colon + 1 >= token.size()) {
            continue;
        }
        fields[std::string(token.substr(0, colon))] = str::split(token.substr(colon + 1), ',');
    }
    const auto field = [&fields](std::string_view name) -> const std::vector<std::string>* {
        auto it = fields.find(name);
        return it == fields.end() ? nullptr : &it->second;
    };
    const auto join = [](const std::vector<std::string>& parts) { return str::join(parts, ","); };

    if (const auto* v = field("MPos")) report.mpos = axisValues(join(*v));
    if (const auto* v = field("WPos")) report.wpos = axisValues(join(*v));
    if (const auto* v = field("WCO")) report.wco = axisValues(join(*v));
    if (const auto* v = field("Buf")) {
        report.buf = BufferState{toInt((*v)[0]), 0};
    }
    if (const auto* v = field("RX")) {
        BufferState buf = report.buf.value_or(BufferState{});
        buf.rx = toInt((*v)[0]);
        report.buf = buf;
    }
    if (const auto* v = field("Bf")) {
        report.buf = BufferState{toInt((*v)[0]), v->size() > 1 ? toInt((*v)[1]) : 0};
    }
    if (const auto* v = field("Ln")) report.lineNumber = toInt((*v)[0]);
    if (const auto* v = field("F")) report.feedrate = js::stringToNumber((*v)[0]);
    if (const auto* v = field("FS")) {
        report.feedrate = js::stringToNumber((*v)[0]);
        report.spindle = v->size() > 1 ? js::stringToNumber((*v)[1]) : 0.0;
    }
    // Input pins are always reported afresh; absent means none triggered.
    report.pinState = std::string();
    if (const auto* v = field("Pn")) report.pinState = (*v)[0];
    if (const auto* v = field("Ov")) {
        std::array<int, 3> ov{100, 100, 100};
        for (std::size_t i = 0; i < 3 && i < v->size(); ++i) {
            ov[i] = toInt((*v)[i]);
        }
        report.overrides = ov;
    }
    if (const auto* v = field("A")) report.accessoryState = (*v)[0];

    if (grblHal) {
        if (const auto* v = field("H")) report.hasHomed = toInt((*v)[0]) != 0;
        if (const auto* v = field("T")) report.currentTool = toInt((*v)[0]);
        if (const auto* v = field("ATCI")) {
            std::vector<std::string> flags;
            for (char c : (*v)[0]) {
                flags.emplace_back(1, c);
            }
            report.keepoutFlags = std::move(flags);
        }
        if (const auto* v = field("P")) {
            report.probe = ProbeInfo{toInt((*v)[0]), v->size() > 1 && (*v)[1] == "P"};
        }
        SdProgress sd;
        if (const auto* v = field("SD")) {
            sd.percentage = js::stringToNumber((*v)[0]);
            if (v->size() > 1) {
                sd.name = (*v)[1];
            }
        }
        report.sdProgress = sd;
    }
    return report;
}

std::optional<StatusReport> parseCompleteStatusReport(std::string_view line) {
    static const regex kPattern(R"(^<(Idle|Run|Hold|Jog|Alarm|Door|Check|Home|Sleep|Tool)(:\d*)?\|(.*\|FW:grblHAL)(.*)?>$)");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    StatusReport report;
    report.activeState = group(m, 1);
    std::string sub = group(m, 2);
    if (!sub.empty() && sub.front() == ':') {
        sub.erase(0, 1);
    }
    report.subState = sub.empty() ? std::optional<int>{} : std::optional<int>{toInt(sub)};

    std::map<std::string, std::vector<std::string>, std::less<>> fields;
    const std::string body = group(m, 3);
    for (std::string_view param : str::splitView(body, '|')) {
        // /^([a-zA-Z]+):?(.*)$/
        std::size_t n = 0;
        while (n < param.size() && str::isAsciiAlpha(param[n])) {
            ++n;
        }
        if (n == 0) {
            continue;
        }
        std::string_view rest = param.substr(n);
        if (!rest.empty() && rest.front() == ':') {
            rest.remove_prefix(1);
        }
        fields[std::string(param.substr(0, n))] = str::split(rest, ',');
    }
    const auto field = [&fields](std::string_view name) -> const std::vector<std::string>* {
        auto it = fields.find(name);
        return it == fields.end() ? nullptr : &it->second;
    };
    const auto join = [](const std::vector<std::string>& parts) { return str::join(parts, ","); };

    if (const auto* v = field("MPos")) report.mpos = axisValues(join(*v));
    if (const auto* v = field("WPos")) report.wpos = axisValues(join(*v));
    if (const auto* v = field("WCO")) report.wco = axisValues(join(*v));
    if (const auto* v = field("Pn")) report.pinState = (*v)[0];
    if (const auto* v = field("P")) report.probe = ProbeInfo{toInt((*v)[0]), v->size() > 1 && (*v)[1] == "P"};
    if (const auto* v = field("T")) report.currentTool = toInt((*v)[0]);
    if (const auto* v = field("H")) report.hasHomed = toInt((*v)[0]) != 0;
    const auto* sd = field("SD");
    report.sdCard = sd != nullptr && toInt((*sd)[0]) != 0;
    return report;
}

namespace {

// ---- parser state ------------------------------------------------------------

struct ModalGroup {
    const char* name;
    std::vector<std::string_view> modes;
};

const std::vector<ModalGroup>& modalGroups(bool grblHal) {
    static const std::vector<ModalGroup> kGrbl{
        {"motion", {"G0", "G1", "G2", "G3", "G38.2", "G38.3", "G38.4", "G38.5", "G80"}},
        {"wcs", {"G54", "G55", "G56", "G57", "G58", "G59"}},
        {"plane", {"G17", "G18", "G19"}},
        {"units", {"G20", "G21"}},
        {"distance", {"G90", "G91"}},
        {"feedrate", {"G93", "G94"}},
        {"program", {"M0", "M1", "M2", "M30"}},
        {"spindle", {"M3", "M4", "M5"}},
        {"coolant", {"M7", "M8", "M9"}},
    };
    static const std::vector<ModalGroup> kGrblHal{
        {"motion", {"G0", "G1", "G2", "G3", "G5", "G38.2", "G38.3", "G38.4", "G38.5", "G80"}},
        {"wcs", {"G54", "G55", "G56", "G57", "G58", "G59"}},
        {"lathe", {"G7", "G8"}},
        {"plane", {"G17", "G18", "G19"}},
        {"units", {"G20", "G21"}},
        {"distance", {"G90", "G91"}},
        {"feedrate", {"G93", "G94"}},
        {"program", {"M0", "M1", "M2", "M30"}},
        {"cycle", {"G98", "G99"}},
        {"spindle", {"M3", "M4", "M5"}},
        {"coolant", {"M7", "M8", "M9"}},
    };
    return grblHal ? kGrblHal : kGrbl;
}

std::string* modalField(ModalState& modal, std::string_view group) {
    if (group == "motion") return &modal.motion;
    if (group == "wcs") return &modal.wcs;
    if (group == "plane") return &modal.plane;
    if (group == "units") return &modal.units;
    if (group == "distance") return &modal.distance;
    if (group == "feedrate") return &modal.feedrate;
    if (group == "program") return &modal.program;
    if (group == "spindle") return &modal.spindle;
    if (group == "lathe") return &modal.lathe;
    if (group == "cycle") return &modal.cycle;
    return nullptr;
}

}  // namespace

std::optional<ParserStateLine> parseParserState(std::string_view line, bool grblHal) {
    static const regex kPattern(R"(^\[(?:GC:)?((?:[a-zA-Z][0-9]+(?:\.[0-9]*)?\s*)+)\]$)");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    ParserStateLine result;
    // Only the groups present in the report are set; coolant starts empty so
    // "M7 M8" collects both.
    result.modal.coolant.clear();
    bool sawCoolant = false;
    const std::string words = group(m, 1);
    for (std::string_view raw : str::splitView(words, ' ')) {
        const std::string_view word = str::trim(raw);
        if (word.empty()) {
            continue;
        }
        const char first = word.front();
        if (first == 'G' || first == 'M') {
            for (const ModalGroup& g : modalGroups(grblHal)) {
                if (std::find(g.modes.begin(), g.modes.end(), word) == g.modes.end()) {
                    continue;
                }
                if (std::string_view(g.name) == "coolant") {
                    result.modal.coolant.emplace_back(word);
                    sawCoolant = true;
                } else if (std::string* slot = modalField(result.modal, g.name)) {
                    *slot = std::string(word);
                }
                break;
            }
            continue;
        }
        if (first == 'T') {
            result.tool = std::string(word.substr(1));
        } else if (first == 'F') {
            result.feedrate = std::string(word.substr(1));
        } else if (first == 'S') {
            result.spindle = std::string(word.substr(1));
        }
    }
    if (!sawCoolant) {
        result.modal.coolant = {"M9"};
    }
    return result;
}

namespace {

// ---- remaining line types ------------------------------------------------------

std::optional<ErrorLine> parseError(std::string_view line) {
    if (!line.starts_with("error:")) {
        return std::nullopt;
    }
    std::string_view rest = str::trimLeft(line.substr(6));
    if (rest.empty()) {
        return std::nullopt;
    }
    return ErrorLine{std::string(rest)};
}

std::optional<AlarmLine> parseAlarm(std::string_view line) {
    if (!line.starts_with("ALARM:")) {
        return std::nullopt;
    }
    std::string_view rest = str::trimLeft(line.substr(6));
    if (rest.empty()) {
        return std::nullopt;
    }
    return AlarmLine{std::string(rest)};
}

// "[PREFIX:body]" with a non-empty body.
std::optional<std::string> bracketed(std::string_view line, std::string_view prefix) {
    if (line.size() < prefix.size() + 3 || line.front() != '[' || line.back() != ']') {
        return std::nullopt;
    }
    std::string_view body = line.substr(1, line.size() - 2);
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }
    body.remove_prefix(prefix.size());
    if (body.empty()) {
        return std::nullopt;
    }
    return std::string(body);
}

std::optional<ParametersLine> parseParameters(std::string_view line, bool grblHal) {
    static const regex kGrbl(R"(^\[(G54|G55|G56|G57|G58|G59|G28|G30|G92|TLO|PRB):(.+)\]$)");
    static const regex kGrblHal(R"(^\[(G54|G55|G56|G57|G58|G59|G59.1|G59.2|G59.3|G28|G30|G92|TLO|PRB):(.+)\]$)");
    const std::string text(line);
    smatch m;
    if (!search(text, grblHal ? kGrblHal : kGrbl, m)) {
        return std::nullopt;
    }
    ParametersLine result;
    result.name = group(m, 1);
    const std::string value = group(m, 2);
    result.value.raw = value;
    if (result.name.front() == 'G') {
        result.value.axes = axisValues(value);
    }
    if (result.name == "PRB" || (!grblHal && result.name == "TLO")) {
        const std::size_t colon = value.find(':');
        result.value.axes = axisValues(std::string_view(value).substr(0, colon));
        if (colon != std::string::npos) {
            result.value.result = toInt(std::string_view(value).substr(colon + 1));
        }
    }
    return result;
}

std::optional<SettingLine> parseSetting(std::string_view line) {
    static const regex kPattern(R"(^(\$[^=]+)=([^(]*)(\(.*\))*)");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    SettingLine result;
    result.name = group(m, 1);
    result.value = std::string(str::trim(group(m, 2)));
    std::string message = group(m, 3);
    // lodash trim(message, "()")
    const std::size_t first = message.find_first_not_of("()");
    const std::size_t last = message.find_last_not_of("()");
    result.message = first == std::string::npos ? std::string() : message.substr(first, last - first + 1);
    return result;
}

std::optional<StartupLine> parseStartup(std::string_view line) {
    static const regex kPattern(R"(^([a-zA-Z0-9]+)\s+((?:\d+\.){1,2}\d+[a-zA-Z0-9\-.]*)([^[]*\[[^\]]+\].*)?)");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    return StartupLine{group(m, 1), group(m, 2), std::string(str::trim(group(m, 3)))};
}

std::optional<AtciLine> parseAtci(std::string_view line) {
    static const regex kPattern(R"(\[(MSG:(?:(?:Error:|Warning:)\s)?ATCI):?(\d*)?\|?(.*)]$)");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    AtciLine result;
    const std::string subtype = group(m, 2);
    const std::string valueString = group(m, 3);
    std::vector<std::string> values = str::split(valueString, '|');
    if (!subtype.empty()) {
        result.subtype = subtype;
        if (!values.empty()) {
            result.message = values.front();
            values.erase(values.begin());
        }
        if (!values.empty()) {
            result.description = values.front();
            values.erase(values.begin());
        }
    }
    for (const std::string& param : values) {
        const std::vector<std::string> parts = str::split(param, ':');
        std::optional<std::string> value;
        if (parts.size() > 1 && !parts[1].empty()) {
            value = parts[1];
        }
        result.values.emplace_back(parts[0], value);
    }
    result.values.emplace_back("macro_abort", str::contains(line, "Error:") ? "1" : "0");
    if (str::contains(m[0].str(), "inside the keepout zone")) {
        result.subtype = "10";
        result.description = std::string(str::trim(valueString));
    }
    return result;
}

std::optional<AutoconfigLine> parseAutoconfig(std::string_view line) {
    static const regex kPattern(R"(^\[MSG:Info:\s*Autoconfig:\s*(.+)\]$)");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    AutoconfigLine result;
    for (const std::string& pair : str::split(group(m, 1), ',')) {
        const std::size_t eq = pair.find('=');
        const std::string key(str::trim(std::string_view(pair).substr(0, eq)));
        if (key.empty()) {
            continue;
        }
        const std::string value = eq == std::string::npos ? std::string()
                                                           : std::string(str::trim(std::string_view(pair).substr(eq + 1)));
        result.values.emplace_back(key, value);
    }
    return result;
}

std::optional<AxsLine> parseAxs(std::string_view line) {
    std::optional<std::string> body = bracketed(line, "AXS:");
    if (!body && line == "[AXS:]") {
        body = std::string();
    }
    if (!body || body->find(']') != std::string::npos) {
        return std::nullopt;
    }
    const std::vector<std::string> info = str::split(*body, ':');
    const double count = js::parseInt(info[0], 10);
    std::string letters;
    if (info.size() > 1) {
        for (char c : info[1]) {
            if (str::isAsciiAlpha(c)) {
                letters.push_back(str::toUpperAscii(c));
            }
        }
    }
    if (letters.empty() && !std::isfinite(count)) {
        return std::nullopt;
    }
    return AxsLine{letters.empty() ? static_cast<int>(count) : static_cast<int>(letters.size()), letters};
}

std::optional<SettingDescriptionLine> parseSettingDescription(std::string_view line) {
    static const regex kPattern(R"(^\[SETTING:(\d+)(\|)(.+?)(?=]))");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    const std::vector<std::string> data = str::split(group(m, 3), '|');
    const auto at = [&data](std::size_t i) { return i < data.size() ? data[i] : std::string(); };
    SettingDescription d;
    d.id = toInt(group(m, 1));
    d.group = toInt(at(0));
    d.description = at(1);
    d.unit = at(2);
    d.dataType = toInt(at(3));
    const std::string format = at(4);
    if (format.find(',') != std::string::npos) {
        d.format = str::split(format, ',');
    } else if (!format.empty()) {
        d.format = {format};
    }
    return SettingDescriptionLine{d};
}

std::optional<SpindleLine> parseSpindle(std::string_view line) {
    static const regex kLegacy(R"(^(\d+)( - )(.+?)?$)");
    static const regex kCurrent(R"(\[SPINDLE:(.+?)])");
    const std::string text(line);
    smatch m;
    if (search(text, kCurrent, m)) {
        // [SPINDLE:0|0|0|*DIRV|PWM|0.0,1000.0]
        const std::vector<std::string> parts = str::split(group(m, 1), '|');
        if (parts.size() < 2) {
            return std::nullopt;
        }
        SpindleLine s;
        s.label = parts.size() > 4 ? parts[4] : std::string();
        s.id = toInt(parts[1]);
        std::string caps = parts.size() > 3 ? parts[3] : std::string();
        s.enabled = caps.find('*') != std::string::npos;
        s.laser = caps.find('L') != std::string::npos;
        caps.erase(std::remove(caps.begin(), caps.end(), '*'), caps.end());
        s.capabilities = caps;
        return s;
    }
    if (!search(text, kLegacy, m)) {
        return std::nullopt;
    }
    // 7 - SLB_LASER, enabled as spindle 0, DLIRV, current
    SpindleLine s;
    s.order = toInt(group(m, 1));
    const std::vector<std::string> parts = str::split(group(m, 3), ',');
    s.label = parts[0];
    if (parts.size() > 1) {
        const std::string& idText = parts[1];
        s.id = idText.empty() ? 0 : toInt(std::string(1, idText.back()));
        s.capabilities = parts.size() > 2 ? std::string(str::trim(parts[2])) : std::string();
        s.enabled = parts.size() > 3 && !parts[3].empty();
        s.laser = s.capabilities.find('L') != std::string::npos;
    }
    return s;
}

std::optional<SettingDetailsLine> parseSettingDetails(std::string_view line) {
    if (str::contains(line, "\"$-Code\"")) {
        return std::nullopt;
    }
    const std::size_t tab = line.find('\t');
    if (tab == std::string_view::npos) {
        return std::nullopt;
    }
    // (\d*)\t(.*) - the digits immediately before the first tab.
    std::size_t start = tab;
    while (start > 0 && str::isAsciiDigit(line[start - 1])) {
        --start;
    }
    const std::vector<std::string> data = str::split(line.substr(tab + 1), '\t');
    SettingDetailsLine result;
    result.id = toInt(line.substr(start, tab - start));
    result.unitString = data.size() > 1 ? data[1] : std::string();
    result.details = data.size() > 4 ? data[4] : std::string();
    return result;
}

std::optional<GroupDetailLine> parseGroupDetail(std::string_view line) {
    static const regex kPattern(R"(^\[SETTINGGROUP:(\d+)\|(\d+)\|(.*)]$)");
    static const std::map<std::string, std::string, std::less<>> kRelabels{
        {"Stepper", "Motors"},        {"Stepper driver", "Motors"}, {"Safety Door", "Door"},
        {"Aux ports", "Aux IO"},      {"Networking", "Network"},    {"Control signals", "General"},
        {"Homing", "Location"},       {"Limits", "Location"},       {"Jogging", "Location"},
    };
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    SettingGroup g{toInt(group(m, 1)), toInt(group(m, 2)), group(m, 3)};
    if (auto it = kRelabels.find(g.label); it != kRelabels.end()) {
        g.label = it->second;
    }
    return GroupDetailLine{g};
}

// [ALARMCODE:16||Power on selftest (POS) failed.] / [ERRORCODE:62||...]
std::optional<CodeDescription> parseCodeTable(std::string_view line, bool alarm) {
    static const regex kAlarm(R"(^\[ALARMCODE:(\d+)\|\|(.*)]$)");
    static const regex kError(R"(^\[ERRORCODE:(\d+)\|\|(.*)]$)");
    const std::string text(line);
    smatch m;
    if (!search(text, alarm ? kAlarm : kError, m)) {
        return std::nullopt;
    }
    return CodeDescription{toInt(group(m, 1)), group(m, 2)};
}

std::optional<ToolLine> parseTool(std::string_view line) {
    static const regex kPattern(R"(\[T:(\d+)\|([-\d.]+(?:,[-\d.]+){2,6})\|([-\d.]+)(.+)?])");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    ToolEntry tool;
    tool.id = toInt(group(m, 1));
    tool.offsets = axisValues(group(m, 2));
    for (std::size_t i = 0; i < tool.offsets.count; ++i) {
        // Number(Number(cur).toFixed(3))
        tool.offsets.values[i] = js::stringToNumber(js::toFixed(tool.offsets.values[i], 3));
    }
    tool.radius = js::stringToNumber(group(m, 3));
    return ToolLine{tool};
}

std::optional<SdFileLine> parseSdFile(std::string_view line) {
    static const regex kPattern(R"(\[FILE:\/([^|]+)\|SIZE:(\d+)(\|UNUSABLE)?\])");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    SdFile file;
    file.name = group(m, 1);
    file.size = static_cast<long long>(js::stringToNumber(group(m, 2)));
    file.unusable = m[3].matched;
    return SdFileLine{file};
}

std::optional<InfoLine> parseInfo(std::string_view line) {
    static const regex kPattern(R"(^\[([A-Z ]*):(.+)\]$)");
    const std::string text(line);
    smatch m;
    if (!search(text, kPattern, m)) {
        return std::nullopt;
    }
    InfoLine result;
    result.name = group(m, 1);
    result.value.text = group(m, 2);
    if (result.name == "NEWOPT") {
        result.value.isOptionList = true;
        for (const std::string& opt : str::split(result.value.text, ',')) {
            const std::size_t eq = opt.find('=');
            std::optional<std::string> value;
            if (eq != std::string::npos && eq + 1 < opt.size()) {
                value = opt.substr(eq + 1);
            }
            result.value.options.emplace_back(opt.substr(0, eq), value);
        }
    }
    return result;
}

}  // namespace

ResponseLine parseGrblResponse(std::string_view line) {
    if (auto status = parseStatusReport(line, false)) {
        return StatusLine{std::move(*status)};
    }
    // /^o*k*$/ tolerates mangled "ok"s.
    if (line.find_first_not_of('o') == std::string_view::npos ||
        line.substr(line.find_first_not_of('o')).find_first_not_of('k') == std::string_view::npos) {
        return OkLine{};
    }
    if (auto error = parseError(line)) {
        return *error;
    }
    if (auto alarm = parseAlarm(line)) {
        return *alarm;
    }
    if (auto parserState = parseParserState(line, false)) {
        return *parserState;
    }
    if (auto parameters = parseParameters(line, false)) {
        return *parameters;
    }
    if (auto help = bracketed(line, "HLP:")) {
        return HelpLine{*help};
    }
    if (auto version = bracketed(line, "VER:")) {
        return VersionLine{*version};
    }
    if (auto option = bracketed(line, "OPT:")) {
        return OptionLine{*option};
    }
    if (auto echo = bracketed(line, "echo:")) {
        return EchoLine{*echo};
    }
    if (auto msg = bracketed(line, "MSG:")) {
        return FeedbackLine{*msg};
    }
    if (auto feedback = bracketed(line, "")) {
        return FeedbackLine{*feedback};
    }
    if (auto setting = parseSetting(line)) {
        return *setting;
    }
    if (auto startup = parseStartup(line)) {
        return *startup;
    }
    return OtherLine{};
}

ResponseLine parseGrblHalResponse(std::string_view line) {
    if (auto complete = parseCompleteStatusReport(line)) {
        return CompleteStatusLine{std::move(*complete)};
    }
    if (auto status = parseStatusReport(line, true)) {
        return StatusLine{std::move(*status)};
    }
    if (line == "ok") {
        return OkLine{};
    }
    if (auto error = parseError(line)) {
        return *error;
    }
    if (auto alarm = parseAlarm(line)) {
        return *alarm;
    }
    if (auto atci = parseAtci(line)) {
        return *atci;
    }
    if (auto autoconfig = parseAutoconfig(line)) {
        return *autoconfig;
    }
    if (auto parserState = parseParserState(line, true)) {
        return *parserState;
    }
    if (auto parameters = parseParameters(line, true)) {
        return *parameters;
    }
    if (auto help = bracketed(line, "HLP:")) {
        return HelpLine{*help};
    }
    if (auto version = bracketed(line, "VER:")) {
        return VersionLine{*version};
    }
    if (auto axs = parseAxs(line)) {
        return *axs;
    }
    if (auto description = parseSettingDescription(line)) {
        return *description;
    }
    if (auto spindle = parseSpindle(line)) {
        return *spindle;
    }
    if (auto details = parseSettingDetails(line)) {
        return *details;
    }
    if (auto groupDetail = parseGroupDetail(line)) {
        return *groupDetail;
    }
    if (auto alarmDetail = parseCodeTable(line, true)) {
        return AlarmDetailLine{*alarmDetail};
    }
    if (auto errorDetail = parseCodeTable(line, false)) {
        return ErrorDescriptionLine{*errorDetail};
    }
    if (auto tool = parseTool(line)) {
        return *tool;
    }
    if (auto file = parseSdFile(line)) {
        if (file->file.name.starts_with("._")) {
            return OtherLine{};  // macOS metadata files are hidden
        }
        return *file;
    }
    if (auto info = parseInfo(line)) {
        return *info;
    }
    if (auto echo = bracketed(line, "echo:")) {
        return EchoLine{*echo};
    }
    if (auto msg = bracketed(line, "MSG:")) {
        return FeedbackLine{*msg};
    }
    if (auto feedback = bracketed(line, "")) {
        return FeedbackLine{*feedback};
    }
    if (auto setting = parseSetting(line)) {
        return *setting;
    }
    if (auto startup = parseStartup(line)) {
        return *startup;
    }
    if (line.size() >= 2 && line.front() == '{' && line.back() == '}') {
        return JsonLine{std::string(line)};
    }
    return OtherLine{};
}

}  // namespace gs::protocol
