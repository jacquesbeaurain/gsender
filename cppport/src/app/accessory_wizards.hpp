#pragma once

// The accessory wizards (features/AccessoryInstaller/Wizards): firmware command
// sequences for AutoSpin and Sienci Spindle / Modbus configuration.

#include <string>
#include <vector>

namespace gs::app {

// The firmware build that brought ATCi (and grblCore's settings), and the
// one from which the Sienci spindle takes $395=7 (ATCiConstants.ts).
inline constexpr long long kAtciSupportedVersion = 20250627;
inline constexpr long long kSpindle395V7Version = 20260515;

// What the wizards send, by the board's firmware and build.
std::vector<std::string> autoSpinCommands(bool grblHal, bool slbLite);
std::vector<std::string> sienciSpindleCommands(long long semver);
std::vector<std::string> modbusCommands(long long semver);

}  // namespace gs::app
