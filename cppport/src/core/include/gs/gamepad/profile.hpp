#pragma once

// gSender's gamepad profiles (workspace.gamepad.profiles): which gamepads a
// profile is for, what each button does (an action, a second action while the
// "2nd action" button is held), the lockout button, and what the sticks jog.
// Ported from src/app/src/lib/gamepad (definitions.ts, index.ts) and
// store/defaultState/gamepad.ts. Profiles are stored in upstream's JSON
// shape, so a profile exported by gSender imports here and the other way.

#include <boost/json/fwd.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::gamepad {

// A button's actions: gSender's shortcut command ids ("JOG_X_P",
// "START_JOB", ...) or a macro's id; empty when none.
struct ButtonMapping {
    std::string label;  // "A", "LB", "Up" (standard mapping) or the index
    int value = 0;      // the button's index
    std::string primaryAction;
    std::string secondaryAction;
    bool operator==(const ButtonMapping&) const = default;
};

// What one stick direction jogs: an axis ("x", "y", "z", "a", upstream's
// lower-case AXIS_* values) or none, without and with the 2nd action button.
struct StickAction {
    std::string primaryAction;
    std::string secondaryAction;
    bool isReversed = false;
    bool operator==(const StickAction&) const = default;
};

struct StickOptions {
    StickAction horizontal;
    StickAction vertical;
    // Handwheel mode: circling the stick jogs this axis (a set axis takes
    // over the stick for that action).
    StickAction mpgMode;
    bool operator==(const StickOptions&) const = default;
};

struct JoystickOptions {
    StickOptions stick1{{"x", "x", false}, {"y", "y", false}, {}};
    StickOptions stick2{{"a", "a", false}, {"z", "z", false}, {}};
    double zeroThreshold = 30;              // %: the sticks' dead zone
    double movementDistanceOverride = 100;  // %: scales the stick jog speed
    bool fixedSpeedMode = false;            // full speed however far the stick goes
    bool operator==(const JoystickOptions&) const = default;
};

struct Profile {
    std::vector<std::string> ids;  // the gamepad ids it is for
    std::string name;
    std::string mapping = "standard";
    std::vector<ButtonMapping> buttons;
    JoystickOptions joystickOptions;
    std::optional<int> lockout;   // held to allow anything; none: always allowed
    std::optional<int> modifier;  // held for the 2nd actions
    bool operator==(const Profile&) const = default;

    const ButtonMapping* button(int value) const;
    ButtonMapping* button(int value);
};

// The W3C "standard" gamepad layout's button names, by index (0-16).
const std::vector<std::string>& standardButtonLabels();
// A new profile's buttons: the standard names for a standard pad, the indices
// otherwise (ProfileModal's handleAddProfile).
std::vector<ButtonMapping> defaultButtons(int buttonCount, bool standard);

// The two profiles gSender ships (Logitech F710, Xbox), buttons unassigned.
std::vector<Profile> defaultProfiles();

// Upstream's profile.id.includes(gamepad.id), then - for the ids a browser
// makes, which differ by browser and platform - the same USB vendor and
// product ("... Vendor: 045e Product: 028e)"). Null when none.
const Profile* findProfile(const std::vector<Profile>& profiles, std::string_view padId);
// "045e:028e" from an id with "Vendor: 045e Product: 028e", lower case; empty
// when it has none.
std::string vendorProduct(std::string_view padId);

// onGamepadButtonPress(): the action a button change runs. A released jog
// button stops the continuous jog ("STOP_CONT_JOG"); a press does nothing
// without the lockout button held, runs the 2nd action with the modifier
// held, the action otherwise. `pressed` holds every button's state now.
std::optional<std::string> buttonAction(const Profile& profile, int button, bool isPressed,
                                        const std::vector<bool>& pressed);
// checkButtonHold(): whether the lockout / modifier button is held (false
// when the profile has none).
bool isHeld(std::optional<int> button, const std::vector<bool>& pressed);

// JSON in upstream's shape.
Profile profileFromJson(const boost::json::value& value);
boost::json::value profileToJson(const Profile& profile);
std::vector<Profile> profilesFromJson(const boost::json::value& value);  // an array; others skipped
boost::json::value profilesToJson(const std::vector<Profile>& profiles);

// Profile > Export's file: {version: "1.0", exportDate, profile}.
boost::json::value exportProfile(const Profile& profile, std::string_view exportDate);
// Profile > Import: the file's buttons, joystick options, lockout and
// modifier over `into` (its ids and name kept). Nullopt when the file has no
// profile.
std::optional<Profile> importProfile(const Profile& into, const boost::json::value& file);

}  // namespace gs::gamepad
