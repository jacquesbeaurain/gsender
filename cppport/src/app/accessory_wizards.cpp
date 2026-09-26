#include "accessory_wizards.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace gs::app {

// ---- the commands --------------------------------------------------------------------------

std::vector<std::string> autoSpinCommands(bool grblHal, bool slbLite) {
    if (!grblHal) {
        return {"G4P0.1", "$31=1", "G4P0.1", "$30=31250", "G4P0.1", "$$"};
    }
    return {"$9 = 1",
            "G4P0.1",
            "$16 = 0",
            "G4P0.1",
            "$30 = 30000",
            "G4P0.1",
            "$31 = 10000",
            "G4P0.1",
            "$33 = 1000",
            "G4P0.1",
            "$34 = 0",
            "G4P0.1",
            std::string("$35 = ") + (slbLite ? "32" : "30"),
            "G4P0.1",
            std::string("$36 = ") + (slbLite ? "96" : "90"),
            "G4P0.1",
            "$395 = 0",
            "G4P0.1",
            ";Flash onboard LED to confirm",
            "M356 P0 Q2",
            "M356 P1 Q2",
            "G4P0.1",
            "M356 P0 Q1",
            "M356 P1 Q1",
            "M356 P0 Q2",
            "M356 P1 Q2",
            "G4P0.1",
            "M356 P0 Q1",
            "M356 P1 Q1",
            "M356 P0 Q0",
            "M356 P1 Q0",
            "(End of Macro 1)",
            "(Reset)",
            "M2",
            "G4P0.1",
            "$$"};
}

std::vector<std::string> sienciSpindleCommands(long long semver) {
    if (semver < kAtciSupportedVersion) {
        // sienciHAL
        return {"$30=24000", "$31=7500", "$340=5", "$374=3", "$375=50", "$392=11", "$395=6", "$476=2", "$$"};
    }
    return {"$30=24000", "$31=7500", "$340=5", "$374=3", "$375=50", "$394=11",
            std::string("$395=") + (semver >= kSpindle395V7Version ? "7" : "2"),
            "$539=11", "$681=0", "$$", "$REBOOT"};
}

std::vector<std::string> modbusCommands(long long semver) {
    std::vector<std::string> code{"$476=2"};
    if (semver >= kAtciSupportedVersion) {
        code.emplace_back("$REBOOT");
    }
    return code;
}

}  // namespace gs::app
