#include "remote_models.hpp"

#include "config_model.hpp"
#include "macros_model.hpp"
#include "probe_model.hpp"
#include "rotary_model.hpp"
#include "spindle_model.hpp"

#include "remote_service.hpp"

namespace gs::ui {

void bindRemoteModels(app::RemoteService& remote, QObject* owner) {
    // Upstream's Tools route: Probe, Macros, Spindle, Coolant, Rotary.
    remote.bindModel("macros", {[owner] { return new MacrosModel(owner); }, {"run"}, {}});
    remote.bindModel("spindle",
                     {[owner] { return new SpindleModel(owner); },
                      {"startClockwise", "startCounterClockwise", "stop", "toggleMode", "setSpeed", "setPower",
                       "setDuration", "selectSpindle", "applyNow"},
                      {}});
    remote.bindModel("coolant", {[owner] { return new CoolantModel(owner); }, {"mist", "flood", "off"}, {}});
    remote.bindModel("probe",
                     {[owner] { return new ProbeModel(owner); },
                      {"selectCommand", "stepCommand", "setPlateType", "selectTool", "addTool", "removeTool",
                       "nextCorner", "beginRun", "confirmCircuit", "start"},
                      {}});
    remote.bindModel("rotary",
                     {[owner] { return new RotaryModel(owner); }, {"enableConfirmation", "setRotaryMode", "runProbe"}, {}});
    // The Config route. What names a file on the computer (import, export,
    // folders) means nothing from a phone and is left out.
    remote.bindModel("config",
                     {[owner] { return new ConfigModel(owner); },
                      {"sectionRow", "valueOf", "setValue", "setNumber", "setPart", "resetValue", "setEeprom",
                       "toggleEepromBit", "resetEeprom", "apply", "revert", "useCurrentPosition", "goToLocation",
                       "sendTest", "jogAxis", "restoreDefaultSettings", "restoreFirmwareDefaults", "reloadFirmware"},
                      {"search", "onlyModified", "profileId"}});
}

}  // namespace gs::ui
