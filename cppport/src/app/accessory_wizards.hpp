#pragma once

// The accessory wizards (features/AccessoryInstaller/Wizards): Vacuum Table
// (zero, carve the mounting holes or the alignment grid), Sienci TLS (tool
// change options, the sensor's and the tool change's positions, a
// continuity check), AutoSpin (its EEPROM settings, a test run) and Sienci
// Spindle (its settings, Modbus). The Sienci ATC's wizard belongs with the
// ATC, which is not ported.

#include "accessory_installer.hpp"

#include <string>
#include <vector>

namespace gs::app {

class Machine;

// The firmware build that brought ATCi (and grblCore's settings), and the
// one from which the Sienci spindle takes $395=7 (ATCiConstants.ts).
inline constexpr long long kAtciSupportedVersion = 20250627;
inline constexpr long long kSpindle395V7Version = 20260515;

// What the wizards send, by the board's firmware and build.
std::vector<std::string> autoSpinCommands(bool grblHal, bool slbLite);
std::vector<std::string> sienciSpindleCommands(long long semver);
std::vector<std::string> modbusCommands(long long semver);

std::vector<AccessoryWizard> accessoryWizards(Machine& machine);

}  // namespace gs::app
