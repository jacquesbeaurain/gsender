#include "gs/toolchange/wizards.hpp"

#include "gs/util/jsnumber.hpp"

#include <cmath>
#include <limits>

namespace gs::toolchange {
namespace {

std::string num(double value) {
    return js::numberToString(value);
}

// Number(setting), undefined (missing) being NaN.
double settingNumber(const std::string& value) {
    return value.empty() ? std::numeric_limits<double>::quiet_NaN() : js::stringToNumber(value);
}

// getToolString()
std::string toolString(const MachineFacts& m) {
    return "T" + m.tool;
}

const char* const kIntro =
    "Tool Change detected, stay clear of the machine! Wait until initial movements are complete!";

// The lines every re-zero start stores the job's state with.
std::vector<std::string> stateStore(const ProbeSettings& p, const std::string& probeDistance) {
    return {
        "%wait",
        "%global.toolchange.PROBE_THICKNESS=" + num(p.zProbeThickness),
        "%global.toolchange.PROBE_DISTANCE=" + probeDistance,
        "%global.toolchange.PROBE_FEEDRATE=" + num(p.fastSpeed),
        "%global.toolchange.PROBE_SLOW_FEEDRATE=" + num(p.slowSpeed),
        "%global.toolchange.RETRACT=" + num(p.retract),
        "%global.toolchange.XPOS=posx",
        "%global.toolchange.YPOS=posy",
        "%global.toolchange.ZPOS=posz",
        "%global.toolchange.UNITS=modal.units",
        "%global.toolchange.SPINDLE=modal.spindle",
        "%global.toolchange.DISTANCE=modal.distance",
        "%global.toolchange.FEEDRATE=programFeedrate",
        "M5",
        "G91 G21",
    };
}

// The Fixed Tool Sensor start (sent with gcode).
std::vector<std::string> sensorStart(const ProbeSettings& p, const std::string& probeDistance,
                                     const MachinePosition& sensor, const std::optional<MachinePosition>& manual,
                                     const std::string& zSafe, const char* initiated) {
    std::vector<std::string> lines{
        "%wait",
        "%global.toolchange.PROBE_THICKNESS_MM=" + num(p.zProbeThickness),
        "%global.toolchange.PROBE_DISTANCE=" + probeDistance,
        "%global.toolchange.PROBE_FEEDRATE=" + num(p.fastSpeed),
        "%global.toolchange.PROBE_SLOW_FEEDRATE=" + num(p.slowSpeed),
        "%global.toolchange.RETRACT=" + num(p.retract),
        "%global.toolchange.PROBE_POS_X=" + num(sensor.x),
        "%global.toolchange.PROBE_POS_Y=" + num(sensor.y),
        "%global.toolchange.PROBE_POS_Z=" + num(sensor.z),
    };
    if (manual) {
        lines.push_back("%global.toolchange.MANUAL_POS_X=" + num(manual->x));
        lines.push_back("%global.toolchange.MANUAL_POS_Y=" + num(manual->y));
        lines.push_back("%global.toolchange.MANUAL_POS_Z=" + num(manual->z));
    }
    const std::vector<std::string> rest{
        "%global.toolchange.Z_SAFE_HEIGHT=" + zSafe,
        "%global.toolchange.UNITS=modal.units",
        "%global.toolchange.SPINDLE=modal.spindle",
        "%global.toolchange.DISTANCE=modal.distance",
        "%global.toolchange.FEEDRATE=programFeedrate",
        "%global.toolchange.SPINDLE_RATE=spindleRate",
        "(STORED: [global.toolchange.SPINDLE] S[global.toolchange.SPINDLE_RATE])",
        "M5 S0",
        "%wait",
        "%global.toolchange.XPOS=posx",
        "%global.toolchange.YPOS=posy",
        "%global.toolchange.ZPOS=posz",
        "G91 G21",
        "G53 G0 Z[global.toolchange.Z_SAFE_HEIGHT]",
        initiated,
    };
    lines.insert(lines.end(), rest.begin(), rest.end());
    return lines;
}

// The sensor wizards' calculateMaxZProbeDistance(): down to 2 mm above the
// bottom of the travel from the sensor position.
std::string sensorProbeDistance(const MachineFacts& m, const MachinePosition& sensor) {
    return js::toFixed(settingNumber(m.zMaxTravel) - std::fabs(sensor.z) - 2, 3);
}

WizardAction sensorResume(const char* label) {
    return {label,
            {"(Restart Spindle)", "([global.toolchange.SPINDLE] S[global.toolchange.SPINDLE_RATE])",
             "[global.toolchange.SPINDLE] S[global.toolchange.SPINDLE_RATE]", "(Returning to initial position)",
             "G53 G0 Z[global.toolchange.Z_SAFE_HEIGHT]",
             "G90 G0 X[global.toolchange.XPOS] Y[global.toolchange.YPOS]", "G90 G0 Z[global.toolchange.ZPOS]",
             "(Restore initial modals)",
             "[global.toolchange.UNITS] [global.toolchange.DISTANCE] [global.toolchange.FEEDRATE]", "%wait",
             "%toolchange_complete"}};
}

const std::vector<std::string> kMeasureInitialTool{
    "G91 G21",
    "G53 G0 Z[global.toolchange.Z_SAFE_HEIGHT]",
    "G53 G0 X[global.toolchange.PROBE_POS_X] Y[global.toolchange.PROBE_POS_Y]",
    "G53 G0 Z[global.toolchange.PROBE_POS_Z]",
    "G38.2 Z-[global.toolchange.PROBE_DISTANCE] F[global.toolchange.PROBE_FEEDRATE]",
    "G0 Z[global.toolchange.RETRACT]",
    "G38.2 Z-10 F[global.toolchange.PROBE_SLOW_FEEDRATE]",
    "G4 P0.3",
    "%global.toolchange.TOOL_OFFSET=posz",
    "(TLO set: [global.toolchange.TOOL_OFFSET])",
    "G0 Z[global.toolchange.RETRACT]",
    "G90 G21",
    "G53 G0 Z[global.toolchange.Z_SAFE_HEIGHT]",
};

}  // namespace

ProbeSettings toolChangeProbeSettings(const probe::ProbeSettings& s) {
    double thickness = s.zThickness.standardBlock;
    switch (s.plateType) {
        case probe::PlateType::AutoZero: thickness = s.zThickness.autoZero; break;
        case probe::PlateType::ZProbe: thickness = s.zThickness.zProbe; break;
        case probe::PlateType::Probe3D: thickness = s.zThickness.probe3D; break;
        case probe::PlateType::BitZero: thickness = s.zThickness.bitZeroZOnly; break;  // flat on the surface
        case probe::PlateType::StandardBlock: break;
    }
    return {thickness, s.zProbeDistance, s.probeFastFeedrate, s.probeFeedrate, s.retractionDistance};
}

Wizard standardRezero(const ProbeSettings& p, const MachineFacts& m) {
    Wizard w;
    w.title = "Standard Re-zero Tool Change";
    w.intro = kIntro;
    w.start = stateStore(p, num(p.zProbeDistance));
    w.start.emplace_back("(Toolchange initiated)");
    w.steps = {
        {"Starting Off",
         {{"Safety First",
           "Jog your machine to a place you can reach using the jog controls and ensure that your router/spindle is "
           "turned off and has fully stopped spinning.",
           false,
           {}},
          {"Change Bit", "Change over to the next tool (" + toolString(m) + ")", true, {}}}},
        {"Probe New Tool",
         {{"Reset the Z",
           "Use jogging or X and Y gotos to bring your CNC back over to where you initially set the project zero. If "
           "you used a touch plate be sure to place the tool over the plate and attach the magnet.",
           false,
           {{"Probe Z (touch plate)",
             {"(Probing Z 0 with probe thickness of [global.toolchange.PROBE_THICKNESS]mm)", "G91",
              "G38.2 Z-[global.toolchange.PROBE_DISTANCE] F[global.toolchange.PROBE_FEEDRATE]",
              "G0 Z[global.toolchange.RETRACT]", "G38.2 Z-10 F[global.toolchange.PROBE_SLOW_FEEDRATE]", "%wait",
              "G10 L20 P0 Z[global.toolchange.PROBE_THICKNESS]", "G0 G21 Z10"}},
            {"Set Z0 (paper method)", {"(Setting Z 0)", "G10 L20 P0 Z0", "G21"}}}}}},
        {"Resume Job",
         {{"Resume Job",
           "If everything looks good, prepare for your machine to move back to the cutting area and continue as "
           "expected. Remove the touch plate magnet and turn on your router if you have them.",
           false,
           {{"Resume Job",
             {"(Returning to initial position)", "G21",
              "G90 [global.toolchange.UNITS] G0 X[global.toolchange.XPOS] Y[global.toolchange.YPOS]",
              "G90 [global.toolchange.UNITS] G0 Z[global.toolchange.ZPOS]", "(Restore initial modals)",
              "[global.toolchange.SPINDLE] [global.toolchange.UNITS] [global.toolchange.DISTANCE] "
              "[global.toolchange.FEEDRATE]",
              "%toolchange_complete"}}}}}},
    };
    return w;
}

Wizard flexibleRezero(int count, const ProbeSettings& p, const MachineFacts& m) {
    // getMoveAmount(): upstream tests the $13 string itself, so any value
    // (even "0") reads as inches.
    const std::string moveAmount = m.reportInches.empty() ? "10mm" : "0.4in";

    // calculateMaxZProbeDistance(): with soft limits, no deeper than the
    // travel left below the machine position.
    std::string probeDistance = num(p.zProbeDistance);
    const std::string softLimits = m.softLimits.empty() ? "0" : m.softLimits;
    if (js::stringToNumber(softLimits) != 0) {
        const double maxZTravel = settingNumber(m.zMaxTravel);
        const double curZPos = std::fabs(m.machineZ);
        double distance = p.zProbeDistance;
        if (curZPos + distance >= maxZTravel) {
            distance = maxZTravel - curZPos - 1;
        }
        probeDistance = js::toFixed(distance, 3);
    }

    Wizard w;
    w.title = "Flexible Re-zero Tool Change";
    w.intro = kIntro;
    w.start = stateStore(p, probeDistance);
    w.start.emplace_back("(Toolchange Initiated)");
    const std::string location = count == 1 ? "probe location" : "tool change location";
    w.steps.push_back({"Starting Off",
                       {{"Safety First",
                         "Ensure that your router/spindle is turned off and has fully stopped spinning then jog your "
                         "machine to your " +
                             location + " using the jog controls.",
                         false,
                         {}}}});
    if (count == 1) {
        w.steps.push_back(
            {"Setup Probe",
             {{"Check Offset",
               "Position the current cutting tool about " + moveAmount +
                   " above the probe location, attach the magnet, and prepare to probe.",
               false,
               {{"Probe Initial Tool",
                 {"G91 G21", "G38.2 Z-[global.toolchange.PROBE_DISTANCE] F[global.toolchange.PROBE_FEEDRATE]",
                  "G0 Z[global.toolchange.RETRACT]", "G38.2 Z-15 F[global.toolchange.PROBE_SLOW_FEEDRATE]",
                  "G4 P0.3", "%global.toolchange.TOOL_OFFSET=posz", "(TLO set: [global.toolchange.TOOL_OFFSET])",
                  "G91 G21 G0 Z10", "G90"}}}}}});
    }
    const std::string preamble = count == 1 ? "Jog your machine to a place you can reach using the jog controls "
                                              "and change over to the next tool"
                                            : "Change over to the next tool";
    w.steps.push_back(
        {"Probe New Tool",
         {{"Change Tool",
           preamble + " (" + toolString(m) + "). Once ready, jog to " + moveAmount +
               " above the probe location, attach the magnet, and prepare to probe",
           true,
           {{"Probe Changed Tool",
             {"G91 G21", "G38.2 Z-[global.toolchange.PROBE_DISTANCE] F[global.toolchange.PROBE_FEEDRATE]",
              "G0 Z[global.toolchange.RETRACT]", "G38.2 Z-15 F[global.toolchange.PROBE_SLOW_FEEDRATE]",
              "(Set Z to Tool offset and wait)", "G4 P0.3",
              "[global.toolchange.UNITS] G10 L20 P0 Z[global.toolchange.TOOL_OFFSET]",
              "G0 Z[global.toolchange.RETRACT]"}}}}}});
    w.steps.push_back(
        {"Resume Job",
         {{"Resume Job",
           "If everything looks good, prepare for your machine to move back to the cutting area and continue as "
           "expected. Remove the touch plate magnet and turn on your router if you have them.",
           false,
           {{"Resume Cutting",
             {"(Returning to initial position)", "G21 G91 G0 Z10",
              "G90 [global.toolchange.UNITS] G0 X[global.toolchange.XPOS] Y[global.toolchange.YPOS]",
              "G90 [global.toolchange.UNITS] G0 Z[global.toolchange.ZPOS]", "(Restore initial modals)",
              "[global.toolchange.SPINDLE] [global.toolchange.UNITS] [global.toolchange.DISTANCE] "
              "[global.toolchange.FEEDRATE]",
              "%toolchange_complete"}}}}}});
    return w;
}

Wizard fixedToolSensor(int count, const ProbeSettings& p, const MachineFacts& m, const MachinePosition& sensor,
                       const MachinePosition& manualPosition, bool moveToManual) {
    const bool manual = moveToManual;
    Wizard w;
    w.title = "Fixed Tool Sensor Tool Change";
    w.intro = kIntro;
    w.startDirect = true;
    w.start = sensorStart(p, sensorProbeDistance(m, sensor), sensor, manualPosition,
                          m.reportInches == "1" ? "-0.2" : "-5", "(Toolchange initiated)");
    if (count == 1) {
        w.steps.push_back({"Starting Off",
                           {{"Safety First",
                             "Ensure that your router/spindle is turned off and has fully stopped spinning and prepare "
                             "to check the length of the current, initial cutting tool.",
                             false,
                             {{"Probe Initial Tool", kMeasureInitialTool}}}}});
    }
    WizardStep prepare{"Prepare New Tool", {}};
    if (manual) {
        prepare.substeps.push_back(
            {"Change Tool",
             "Ensure that your router/spindle is turned off and has fully stopped spinning, and prepare to change "
             "over to the next tool (" +
                 toolString(m) + ").",
             true,
             {{"Move to Tool Change Location",
               {"(Moving to manual toolchange location)", "G90 G53 G0 Z[global.toolchange.Z_SAFE_HEIGHT]",
                "G90 G53 G0 X[global.toolchange.MANUAL_POS_X] Y[global.toolchange.MANUAL_POS_Y]",
                "G90 G53 G0 Z[global.toolchange.MANUAL_POS_Z]"}}}});
    }
    prepare.substeps.push_back(
        {"Measure New Tool",
         std::string(manual ? "" : "Ensure that your router/spindle is turned off and has fully stopped spinning. ") +
             "After you've switched to the new tool (" + toolString(m) +
             "), click the button below to automatically probe and set the new tool's length offset.",
         !manual,
         {{"Probe Changed Tool",
           {"(Moving back to configured location)", "G90 G53 G0 Z[global.toolchange.Z_SAFE_HEIGHT]",
            "G90 G53 G0 X[global.toolchange.PROBE_POS_X] Y[global.toolchange.PROBE_POS_Y]",
            "G53 G0 Z[global.toolchange.PROBE_POS_Z]", "G91 G21",
            "G38.2 Z-[global.toolchange.PROBE_DISTANCE] F[global.toolchange.PROBE_FEEDRATE]",
            "G0 Z[global.toolchange.RETRACT]", "G38.2 Z-15 F[global.toolchange.PROBE_SLOW_FEEDRATE]",
            "(Set Z to Tool offset and wait)", "G4 P0.3", "G10 L20 P0 Z[global.toolchange.TOOL_OFFSET]",
            "G0 Z[global.toolchange.RETRACT]", "G53 G21 G0 Z[global.toolchange.Z_SAFE_HEIGHT]", "G21 G91"}}}});
    w.steps.push_back(std::move(prepare));
    w.steps.push_back({"Resume Job",
                       {{"Resume Job",
                         "If everything looks good, prepare for your machine to move back to the cutting area and "
                         "continue as expected. Turn on your router if you have one.",
                         false,
                         {sensorResume("Resume Cutting")}}}});
    return w;
}

Wizard probeToolLength(const ProbeSettings& p, const MachineFacts& m, const MachinePosition& sensor) {
    Wizard w;
    w.title = "Fixed Tool Sensor Tool Change";
    w.intro =
        "Prepare to probe the initial tool length. Ensure your router/spindle is turned off and has fully stopped "
        "spinning.";
    w.startDirect = true;
    w.start = sensorStart(p, sensorProbeDistance(m, sensor), sensor, std::nullopt,
                          m.reportInches == "1" ? "-0.5" : "-10", "(Tool probe initiated)");
    w.steps = {
        {"Probe Initial Tool",
         {{"Measure Tool Length",
           "Ensure that your router/spindle is turned off and has fully stopped spinning. Click the button below to "
           "probe the length of the current tool.",
           false,
           {{"Probe Tool Length", kMeasureInitialTool}}}}},
        {"Resume",
         {{"Resume Operation",
           "Tool length has been probed and set. Click the button below to continue with your operation. Turn on your "
           "router if you have one.",
           false,
           {sensorResume("Resume")}}}},
    };
    return w;
}

}  // namespace gs::toolchange
