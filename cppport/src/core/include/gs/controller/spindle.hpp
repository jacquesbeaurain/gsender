#pragma once

// The Spindle/Laser widget's G-code: switching between spindle and laser
// mode - the work offset shift between the two tools, the power range and
// $32 - and turning the laser on to focus. Ported from
// src/app/src/features/Spindle/index.tsx (the live speed and power changes
// and the laser test are controller commands: spindleSpeedChange,
// laserPowerChange, laserTestOn).

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::controller {

// getWCS(): the P number of a work coordinate system, G54 -> 1 ... G59 -> 6,
// else 0.
int wcsNumber(std::string_view wcs);

// Where the laser is relative to the spindle (mm): the settings' Laser X/Y
// offset on Grbl, $770/$771 (or $741/$742) on grblHAL.
struct ToolOffset {
    double x = 0;
    double y = 0;
};

// getLaserOffsetCode() / getSpindleOffsetCode(): "G10 L20 P1 X.. Y.." making
// the current position read the work position plus the offset (to the
// laser) or minus it (back to the spindle), so the tool taking over works
// where the other one did. In the preferred units: mm offsets rounded to 2
// decimals, inches to 3; only the axes with an offset. Empty when there is
// none. `workX`/`workY` in mm.
//
// Deviation: upstream also divides the position by 25.4 when $13=1, though
// its stored positions are already mm - in a mm workspace that mixed units.
std::string toolOffsetCommand(const ToolOffset& offset, bool toLaser, bool metric, double workX, double workY,
                              std::string_view wcs);

struct ModeSwitch {
    bool toLaser = true;
    bool metric = true;            // the workspace units, G21 or G20 for the lines
    std::string deviceUnits = "G21";  // the modal units to restore afterwards
    bool spindleOn = false;        // M5 first
    ToolOffset offset;
    double workX = 0;              // mm
    double workY = 0;
    std::string wcs = "G54";
    // The power range written with the mode ($30 max, $31 min): the laser's
    // on Grbl, the spindle's going back; none on grblHAL, whose laser has
    // its own settings.
    std::optional<std::pair<double, double>> range;
};

// enableLaserMode() / enableSpindleMode(): [M5,] the units, the offset
// shift, $30/$31, $32=1 (or 0), the device units again.
std::vector<std::string> modeSwitchCommands(const ModeSwitch& change);

// sendLaserM3(): "G1F1 M3 S<maxPower x power/100>" - the laser fires only in
// a motion mode, so it is lit in G1 at a crawl for focusing.
std::string laserOnCommand(double powerPercent, double maxPower);

}  // namespace gs::controller
