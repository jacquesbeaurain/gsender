#pragma once

// Classifies one firmware response line.
//
// Ports GrblLineParser / GrblHalLineParser and their LineParserResult*
// classes. The parsers are tried in the same order as in the JavaScript and
// use the same regular expressions (via Boost.Regex), so a line is classified
// exactly as gSender classifies it.

#include "gs/protocol/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gs::protocol {

struct OtherLine {};
struct OkLine {};
struct ErrorLine { std::string message; };  // text after "error:"
struct AlarmLine { std::string message; };  // text after "ALARM:"
struct StatusLine { StatusReport report; };
struct CompleteStatusLine { StatusReport report; };  // grblHAL 0x87 report
struct ParserStateLine {
    ModalState modal;
    std::optional<std::string> tool;
    std::optional<std::string> feedrate;
    std::optional<std::string> spindle;
};
struct ParametersLine {
    std::string name;
    ParameterValue value;
};
struct HelpLine { std::string message; };
struct VersionLine { std::string text; };  // [VER:...] contents
struct OptionLine { std::string message; };
struct EchoLine { std::string message; };
struct FeedbackLine { std::string message; };  // [MSG:...] or [...]
struct SettingLine {
    std::string name;     // "$110"
    std::string value;    // trimmed
    std::string message;  // "(...)" annotation without the parentheses
};
struct StartupLine {
    std::string firmware;
    std::string version;
    std::string message;
};
// grblHAL only
struct AtciLine {
    std::optional<std::string> message;
    std::optional<std::string> subtype;
    std::optional<std::string> description;
    std::vector<std::pair<std::string, std::optional<std::string>>> values;  // incl. macro_abort
};
struct AutoconfigLine { std::vector<std::pair<std::string, std::string>> values; };
struct AxsLine {
    int count = 0;
    std::string letters;
};
struct SettingDescriptionLine { SettingDescription description; };
struct SpindleLine {
    std::optional<int> order;
    std::optional<int> id;
    std::string label;
    std::string capabilities;
    bool enabled = false;
    bool laser = false;
};
struct SettingDetailsLine {
    int id = 0;
    std::string unitString;
    std::string details;
};
struct GroupDetailLine { SettingGroup group; };
struct AlarmDetailLine { CodeDescription alarm; };
struct ErrorDescriptionLine { CodeDescription error; };
struct ToolLine { ToolEntry tool; };
struct SdFileLine { SdFile file; };
struct InfoLine {
    std::string name;
    InfoValue value;
};
struct JsonLine { std::string code; };

using ResponseLine =
    std::variant<OtherLine, OkLine, ErrorLine, AlarmLine, StatusLine, CompleteStatusLine, ParserStateLine,
                 ParametersLine, HelpLine, VersionLine, OptionLine, EchoLine, FeedbackLine, SettingLine,
                 StartupLine, AtciLine, AutoconfigLine, AxsLine, SettingDescriptionLine, SpindleLine,
                 SettingDetailsLine, GroupDetailLine, AlarmDetailLine, ErrorDescriptionLine, ToolLine,
                 SdFileLine, InfoLine, JsonLine>;

ResponseLine parseGrblResponse(std::string_view line);
ResponseLine parseGrblHalResponse(std::string_view line);

// Individual parsers, exposed for tests and for code that needs one format.
std::optional<StatusReport> parseStatusReport(std::string_view line, bool grblHal);
std::optional<StatusReport> parseCompleteStatusReport(std::string_view line);
std::optional<ParserStateLine> parseParserState(std::string_view line, bool grblHal);

}  // namespace gs::protocol
