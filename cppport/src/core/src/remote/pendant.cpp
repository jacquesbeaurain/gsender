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
    return std::nullopt;
}

}  // namespace gs::remote
