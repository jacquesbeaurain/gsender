#include "gs/remote/pendant.hpp"

#include <boost/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gs::remote {
namespace {

namespace json = boost::json;

// Positions are sent as numbers; non-finite values (a report not yet
// received) as 0 so the page never sees NaN, which JSON cannot carry.
double finite(double v) {
    return std::isfinite(v) ? v : 0.0;
}

json::array axes(const std::array<double, 4>& values) {
    return json::array{finite(values[0]), finite(values[1]), finite(values[2]), finite(values[3])};
}

std::string upper(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

const json::string* stringField(const json::object& object, std::string_view key) {
    const json::value* value = object.if_contains(key);
    return value ? value->if_string() : nullptr;
}

int direction(const json::object& object, std::string_view key) {
    const json::value* value = object.if_contains(key);
    if (!value) {
        return 0;
    }
    double d = 0;
    if (value->is_int64()) {
        d = static_cast<double>(value->get_int64());
    } else if (value->is_uint64()) {
        d = static_cast<double>(value->get_uint64());
    } else if (value->is_double()) {
        d = value->get_double();
    }
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

constexpr std::size_t kMaxCallArguments = 8;

bool isModelName(std::string_view name) {
    return !name.empty() && name.size() <= 32 &&
           std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_'; });
}

bool isIdentifier(std::string_view name) {
    return !name.empty() && name.size() <= 64 && !std::isdigit(static_cast<unsigned char>(name[0])) &&
           std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

// What a phone calls a file, as a plain file name: no directories, no control
// characters, at most 255 bytes; "program.nc" when nothing is left.
std::string programName(std::string_view given) {
    std::string name;
    const std::size_t slash = given.find_last_of("/\\");
    for (const char c : slash == std::string_view::npos ? given : given.substr(slash + 1)) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) {
            name.push_back(c);
        }
    }
    if (name.size() > 255) {
        name.resize(255);
    }
    return name.empty() || name == "." || name == ".." ? "program.nc" : name;
}

}  // namespace

std::string stateMessage(const PendantState& s) {
    json::object o{
        {"type", "state"},
        {"connected", s.connected},
        {"port", s.port},
        {"activeState", s.activeState},
        {"statusLabel", s.statusLabel},
        {"alarmCode", s.alarmCode},
        {"workflow", s.workflow},
        {"wpos", axes(s.wpos)},
        {"mpos", axes(s.mpos)},
        {"hasA", s.hasA},
        {"metric", s.metric},
        {"decimals", s.decimals},
        {"wcs", s.wcs},
        {"fileName", s.fileName},
        {"sent", s.sent},
        {"received", s.received},
        {"total", s.total},
        {"remainingMs", s.remainingMs},
        {"feedrate", finite(s.feedrate)},
        {"spindle", finite(s.spindle)},
        {"canJog", s.canJog},
        {"canRun", s.canRun},
        {"canPause", s.canPause},
        {"canStop", s.canStop},
        {"homingEnabled", s.homingEnabled},
        {"jogPreset", s.jogPreset},
        {"xyStep", finite(s.xyStep)},
        {"zStep", finite(s.zStep)},
        {"aStep", finite(s.aStep)},
        {"jogFeed", finite(s.jogFeed)},
    };
    return json::serialize(o);
}

std::optional<PendantCommand> parseCommand(std::string_view text) {
    boost::system::error_code ec;
    const json::value value = json::parse(text, ec);
    if (ec || !value.is_object()) {
        return std::nullopt;
    }
    const json::object& object = value.get_object();
    const json::string* type = stringField(object, "type");
    if (!type) {
        return std::nullopt;
    }
    using Kind = PendantCommand::Kind;
    PendantCommand command;
    const std::string_view t = *type;
    if (t == "jogPress") {
        command.kind = Kind::JogPress;
        command.directions = {direction(object, "x"), direction(object, "y"), direction(object, "z"),
                              direction(object, "a")};
        if (command.directions == std::array<int, 4>{}) {
            return std::nullopt;
        }
        return command;
    }
    static constexpr std::pair<std::string_view, Kind> kSimple[] = {
        {"jogRelease", Kind::JogRelease}, {"jogStop", Kind::JogStop}, {"start", Kind::Start},
        {"pause", Kind::Pause},           {"stop", Kind::Stop},       {"unlock", Kind::Unlock},
        {"home", Kind::Home},             {"reset", Kind::Reset},     {"zeroAll", Kind::ZeroAll},
        {"ping", Kind::Ping},
    };
    for (const auto& [name, kind] : kSimple) {
        if (t == name) {
            command.kind = kind;
            return command;
        }
    }
    if (t == "zeroAxis") {
        const json::string* axis = stringField(object, "axis");
        if (!axis) {
            return std::nullopt;
        }
        command.kind = Kind::ZeroAxis;
        command.text = upper(*axis);
        if (command.text.size() != 1 || std::string_view("XYZA").find(command.text[0]) == std::string_view::npos) {
            return std::nullopt;
        }
        return command;
    }
    if (t == "goToZero") {
        const json::string* axes = stringField(object, "axes");
        if (!axes) {
            return std::nullopt;
        }
        command.kind = Kind::GoToZero;
        command.text = upper(*axes);
        if (command.text != "X" && command.text != "Y" && command.text != "Z" && command.text != "A" &&
            command.text != "XY") {
            return std::nullopt;
        }
        return command;
    }
    if (t == "workspace") {
        const json::string* wcs = stringField(object, "wcs");
        if (!wcs) {
            return std::nullopt;
        }
        command.kind = Kind::Workspace;
        command.text = upper(*wcs);
        if (command.text.size() != 3 || !command.text.starts_with("G5") || command.text[2] < '4' ||
            command.text[2] > '9') {
            return std::nullopt;
        }
        return command;
    }
    if (t == "preset") {
        const json::string* preset = stringField(object, "preset");
        if (!preset || (*preset != "Rapid" && *preset != "Normal" && *preset != "Precise")) {
            return std::nullopt;
        }
        command.kind = Kind::Preset;
        command.text = std::string(*preset);
        return command;
    }
    if (t == "loadProgram") {
        const json::string* name = stringField(object, "name");
        const json::string* content = stringField(object, "content");
        if (!name || !content || content->empty()) {
            return std::nullopt;
        }
        command.kind = Kind::LoadProgram;
        command.text = programName(*name);
        command.content = std::string(*content);
        return command;
    }
    if (t == "subscribe" || t == "unsubscribe" || t == "call" || t == "set") {
        const json::string* model = stringField(object, "model");
        if (!model || !isModelName(*model)) {
            return std::nullopt;
        }
        command.model = std::string(*model);
        if (t == "subscribe" || t == "unsubscribe") {
            command.kind = t == "subscribe" ? Kind::Subscribe : Kind::Unsubscribe;
            return command;
        }
        const json::string* member = stringField(object, t == "call" ? "method" : "property");
        if (!member || !isIdentifier(*member)) {
            return std::nullopt;
        }
        command.member = std::string(*member);
        if (t == "set") {
            const json::value* scalar = object.if_contains("value");
            if (!scalar || scalar->is_object() || scalar->is_array()) {
                return std::nullopt;  // a property takes a scalar
            }
            command.kind = Kind::SetProperty;
            command.argsJson = json::serialize(*scalar);
            return command;
        }
        command.kind = Kind::Call;
        json::array args;
        if (const json::value* given = object.if_contains("args")) {
            if (!given->is_array() || given->get_array().size() > kMaxCallArguments) {
                return std::nullopt;
            }
            args = given->get_array();
        }
        command.argsJson = json::serialize(args);
        if (const json::value* id = object.if_contains("id"); id && id->is_int64()) {
            command.callId = id->get_int64();
        }
        return command;
    }
    return std::nullopt;
}

std::string modelMessage(std::string_view model, std::string_view propertiesJson) {
    // The properties come from the application (already JSON); one that does
    // not parse is sent empty rather than breaking the message.
    boost::system::error_code ec;
    json::value properties = json::parse(propertiesJson, ec);
    if (ec || !properties.is_object()) {
        properties = json::object{};
    }
    return json::serialize(json::object{{"type", "model"}, {"model", model}, {"properties", std::move(properties)}});
}

std::string resultMessage(std::int64_t id, std::string_view valueJson, std::string_view error) {
    json::object o{{"type", "result"}, {"id", id}};
    if (!error.empty()) {
        o["ok"] = false;
        o["error"] = error;
        return json::serialize(o);
    }
    boost::system::error_code ec;
    json::value parsed = json::parse(valueJson, ec);
    o["ok"] = true;
    o["value"] = ec ? json::value(nullptr) : std::move(parsed);
    return json::serialize(o);
}

}  // namespace gs::remote
