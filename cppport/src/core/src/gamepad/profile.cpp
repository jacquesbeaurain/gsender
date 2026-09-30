#include "gs/gamepad/profile.hpp"

#include <boost/json.hpp>
#include <boost/regex.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gs::gamepad {

namespace json = boost::json;

const ButtonMapping* Profile::button(int value) const {
    const auto it = std::find_if(buttons.begin(), buttons.end(), [value](const ButtonMapping& b) { return b.value == value; });
    return it == buttons.end() ? nullptr : &*it;
}

ButtonMapping* Profile::button(int value) {
    const auto it = std::find_if(buttons.begin(), buttons.end(), [value](const ButtonMapping& b) { return b.value == value; });
    return it == buttons.end() ? nullptr : &*it;
}

const std::vector<std::string>& standardButtonLabels() {
    // store/gamepad.js gamepadMapping.standard.buttons
    static const std::vector<std::string> labels{"A",  "B",     "X",  "Y",  "LB",   "RB",    "LT",   "RT",   "Back",
                                                 "Start", "L3", "R3", "Up", "Down", "Left", "Right", "Home"};
    return labels;
}

std::vector<ButtonMapping> defaultButtons(int buttonCount, bool standard) {
    std::vector<ButtonMapping> buttons;
    if (standard) {
        const auto& labels = standardButtonLabels();
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            buttons.push_back({labels[static_cast<std::size_t>(i)], i, {}, {}});
        }
        return buttons;
    }
    for (int i = 0; i < buttonCount; ++i) {
        buttons.push_back({std::to_string(i), i, {}, {}});
    }
    return buttons;
}

std::vector<Profile> defaultProfiles() {
    // The F710's list stops before Home; the Xbox one has it.
    std::vector<ButtonMapping> f710 = defaultButtons(0, true);
    f710.pop_back();
    Profile logitech;
    logitech.ids = {"Logitech Cordless RumblePad 2 (STANDARD GAMEPAD Vendor: 046d Product: c219)"};
    logitech.name = "Logitech F710 Gamepad";
    logitech.buttons = std::move(f710);
    Profile xbox;
    xbox.ids = {"Xbox 360 Controller (XInput STANDARD GAMEPAD)",
                "Xbox 360 Controller (STANDARD GAMEPAD Vendor: 045e Product: 028e)",
                "Wireless Gamepad (Vendor: 2563 Product: 0575)"};
    xbox.name = "Xbox Controller";
    xbox.buttons = defaultButtons(0, true);
    return {logitech, xbox};
}

std::string vendorProduct(std::string_view padId) {
    static const boost::regex pattern(R"(Vendor:\s*([0-9a-fA-F]{4})\s+Product:\s*([0-9a-fA-F]{4}))");
    boost::match_results<std::string_view::const_iterator> match;
    if (!boost::regex_search(padId.begin(), padId.end(), match, pattern)) {
        return {};
    }
    std::string out = match[1].str() + ':' + match[2].str();
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

const Profile* findProfile(const std::vector<Profile>& profiles, std::string_view padId) {
    for (const Profile& profile : profiles) {
        if (std::find(profile.ids.begin(), profile.ids.end(), padId) != profile.ids.end()) {
            return &profile;
        }
    }
    const std::string usb = vendorProduct(padId);
    if (usb.empty()) {
        return nullptr;
    }
    for (const Profile& profile : profiles) {
        for (const std::string& id : profile.ids) {
            if (vendorProduct(id) == usb) {
                return &profile;
            }
        }
    }
    return nullptr;
}

bool isHeld(std::optional<int> button, const std::vector<bool>& pressed) {
    return button && *button >= 0 && *button < static_cast<int>(pressed.size()) &&
           pressed[static_cast<std::size_t>(*button)];
}

std::optional<std::string> buttonAction(const Profile& profile, int button, bool isPressed,
                                        const std::vector<bool>& pressed) {
    const ButtonMapping* found = profile.button(button);
    const auto isJog = [](const std::string& action) { return action.find("JOG") != std::string::npos; };
    if (!isPressed && found && (isJog(found->primaryAction) || isJog(found->secondaryAction))) {
        return std::string("STOP_CONT_JOG");
    }
    if (!isPressed) {
        return std::nullopt;
    }
    // Upstream reads gamepad.buttons[index]: a lockout index past the pad's
    // buttons counts as no lockout.
    if (profile.lockout && *profile.lockout >= 0 && *profile.lockout < static_cast<int>(pressed.size()) &&
        !isHeld(profile.lockout, pressed)) {
        return std::nullopt;
    }
    if (!found) {
        return std::nullopt;
    }
    const std::string& action = isHeld(profile.modifier, pressed) ? found->secondaryAction : found->primaryAction;
    if (action.empty()) {
        return std::nullopt;
    }
    return action;
}

namespace {

std::string text(const json::object& o, std::string_view key, std::string fallback = {}) {
    const json::value* v = o.if_contains(key);
    if (!v) {
        return fallback;
    }
    if (v->is_string()) {
        return std::string(v->as_string());
    }
    if (v->is_int64()) {
        return std::to_string(v->as_int64());
    }
    if (v->is_uint64()) {
        return std::to_string(v->as_uint64());
    }
    if (v->is_double()) {
        const double d = v->as_double();
        return d == std::floor(d) ? std::to_string(static_cast<long long>(d)) : std::to_string(d);
    }
    return fallback;
}

std::optional<double> number(const json::object& o, std::string_view key) {
    const json::value* v = o.if_contains(key);
    if (!v) {
        return std::nullopt;
    }
    if (v->is_int64()) {
        return static_cast<double>(v->as_int64());
    }
    if (v->is_uint64()) {
        return static_cast<double>(v->as_uint64());
    }
    if (v->is_double()) {
        return v->as_double();
    }
    return std::nullopt;
}

// null, false (store/gamepad.js's modifier) or anything not a number: none.
std::optional<int> buttonIndex(const json::object* o) {
    if (!o) {
        return std::nullopt;
    }
    const std::optional<double> n = number(*o, "button");
    if (!n || *n < 0) {
        return std::nullopt;
    }
    return static_cast<int>(*n);
}

bool flag(const json::object& o, std::string_view key, bool fallback) {
    const json::value* v = o.if_contains(key);
    return v && v->is_bool() ? v->as_bool() : fallback;
}

const json::object* child(const json::object& o, std::string_view key) {
    const json::value* v = o.if_contains(key);
    return v && v->is_object() ? &v->as_object() : nullptr;
}

StickAction stickActionFromJson(const json::object* o, const StickAction& fallback) {
    if (!o) {
        return fallback;
    }
    return {text(*o, "primaryAction"), text(*o, "secondaryAction"), flag(*o, "isReversed", false)};
}

StickOptions stickFromJson(const json::object* o, const StickOptions& fallback) {
    if (!o) {
        return fallback;
    }
    return {stickActionFromJson(child(*o, "horizontal"), fallback.horizontal),
            stickActionFromJson(child(*o, "vertical"), fallback.vertical),
            stickActionFromJson(child(*o, "mpgMode"), fallback.mpgMode)};
}

json::value actionValue(const std::string& action) {
    return action.empty() ? json::value(nullptr) : json::value(action);
}

json::object stickActionToJson(const StickAction& a) {
    return {{"primaryAction", actionValue(a.primaryAction)},
            {"secondaryAction", actionValue(a.secondaryAction)},
            {"isReversed", a.isReversed}};
}

json::object stickToJson(const StickOptions& s) {
    return {{"horizontal", stickActionToJson(s.horizontal)},
            {"vertical", stickActionToJson(s.vertical)},
            {"mpgMode", stickActionToJson(s.mpgMode)}};
}

std::vector<ButtonMapping> buttonsFromJson(const json::value& value) {
    std::vector<ButtonMapping> buttons;
    if (!value.is_array()) {
        return buttons;
    }
    for (const json::value& item : value.as_array()) {
        if (!item.is_object()) {
            continue;
        }
        const json::object& o = item.as_object();
        const std::optional<double> index = number(o, "value");
        if (!index) {
            continue;
        }
        buttons.push_back({text(o, "label", std::to_string(static_cast<int>(*index))), static_cast<int>(*index),
                           text(o, "primaryAction"), text(o, "secondaryAction")});
    }
    return buttons;
}

JoystickOptions joystickFromJson(const json::object* o) {
    JoystickOptions options;
    if (!o) {
        return options;
    }
    options.stick1 = stickFromJson(child(*o, "stick1"), options.stick1);
    options.stick2 = stickFromJson(child(*o, "stick2"), options.stick2);
    options.zeroThreshold = number(*o, "zeroThreshold").value_or(options.zeroThreshold);
    options.movementDistanceOverride = number(*o, "movementDistanceOverride").value_or(options.movementDistanceOverride);
    options.fixedSpeedMode = flag(*o, "fixedSpeedMode", false);
    return options;
}

json::value indexValue(std::optional<int> index) {
    return index ? json::value(*index) : json::value(nullptr);
}

}  // namespace

Profile profileFromJson(const json::value& value) {
    Profile profile;
    if (!value.is_object()) {
        return profile;
    }
    const json::object& o = value.as_object();
    if (const json::value* ids = o.if_contains("id")) {
        if (ids->is_array()) {
            for (const json::value& id : ids->as_array()) {
                if (id.is_string()) {
                    profile.ids.emplace_back(id.as_string());
                }
            }
        } else if (ids->is_string()) {
            profile.ids.emplace_back(ids->as_string());
        }
    }
    profile.name = text(o, "name", text(o, "profileName"));
    profile.mapping = text(o, "mapping");
    if (const json::value* buttons = o.if_contains("buttons")) {
        profile.buttons = buttonsFromJson(*buttons);
    }
    profile.joystickOptions = joystickFromJson(child(o, "joystickOptions"));
    profile.lockout = buttonIndex(child(o, "lockout"));
    profile.modifier = buttonIndex(child(o, "modifier"));
    return profile;
}

json::value profileToJson(const Profile& profile) {
    json::array ids;
    for (const std::string& id : profile.ids) {
        ids.emplace_back(id);
    }
    json::array buttons;
    for (const ButtonMapping& b : profile.buttons) {
        buttons.push_back(json::object{{"label", b.label},
                                       {"value", b.value},
                                       {"primaryAction", actionValue(b.primaryAction)},
                                       {"secondaryAction", actionValue(b.secondaryAction)}});
    }
    const JoystickOptions& j = profile.joystickOptions;
    return json::object{
        {"id", std::move(ids)},
        {"icon", "fas fa-gamepad"},
        {"active", true},
        {"profileName", profile.name},
        {"shortcuts", json::object{}},
        {"name", profile.name},
        {"mapping", profile.mapping.empty() ? json::value(nullptr) : json::value(profile.mapping)},
        {"buttons", std::move(buttons)},
        {"axes", json::array{0, 0, 0, 0}},
        {"joystickOptions", json::object{{"stick1", stickToJson(j.stick1)},
                                         {"stick2", stickToJson(j.stick2)},
                                         {"zeroThreshold", j.zeroThreshold},
                                         {"movementDistanceOverride", j.movementDistanceOverride},
                                         {"fixedSpeedMode", j.fixedSpeedMode}}},
        {"lockout", json::object{{"button", indexValue(profile.lockout)}, {"active", false}}},
        {"modifier", json::object{{"button", indexValue(profile.modifier)}}},
    };
}

std::vector<Profile> profilesFromJson(const json::value& value) {
    std::vector<Profile> profiles;
    if (!value.is_array()) {
        return profiles;
    }
    for (const json::value& item : value.as_array()) {
        if (item.is_object()) {
            Profile profile = profileFromJson(item);
            if (!profile.ids.empty()) {
                profiles.push_back(std::move(profile));
            }
        }
    }
    return profiles;
}

json::value profilesToJson(const std::vector<Profile>& profiles) {
    json::array out;
    for (const Profile& profile : profiles) {
        out.push_back(profileToJson(profile));
    }
    return out;
}

json::value exportProfile(const Profile& profile, std::string_view exportDate) {
    return json::object{{"version", "1.0"}, {"exportDate", exportDate}, {"profile", profileToJson(profile)}};
}

std::optional<Profile> importProfile(const Profile& into, const json::value& file) {
    if (!file.is_object()) {
        return std::nullopt;
    }
    const json::value* data = file.as_object().if_contains("profile");
    if (!data || !data->is_object()) {
        return std::nullopt;
    }
    const json::object& o = data->as_object();
    const Profile imported = profileFromJson(*data);
    Profile out = into;
    if (o.contains("buttons")) {
        out.buttons = imported.buttons;
    }
    if (child(o, "joystickOptions")) {
        out.joystickOptions = imported.joystickOptions;
    }
    if (child(o, "lockout")) {
        out.lockout = imported.lockout;
    }
    if (child(o, "modifier")) {
        out.modifier = imported.modifier;
    }
    return out;
}

}  // namespace gs::gamepad
