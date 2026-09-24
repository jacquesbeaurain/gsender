#pragma once

// Everything a controller reports to its owner (the UI), replacing the
// socket.io events the JavaScript controllers emitted. The JS event name is
// noted on each type.

#include "gs/controller/streaming.hpp"
#include "gs/protocol/line_parser.hpp"
#include "gs/protocol/runner.hpp"

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gs::controller {

enum class WriteSource { Client, Server, Feeder, Sender };

// "serialport:read" - text for the console (firmware output, annotated).
struct ConsoleOutput {
    std::string text;
};

// "serialport:write" - something the app sent, echoed to the console.
struct ConsoleInput {
    std::string text;
    WriteSource source = WriteSource::Client;
};

// "controller:state"
struct StateChanged {
    protocol::Firmware firmware;
    protocol::RunnerState state;
    std::optional<std::string> tool;  // set when a tool change updated the tool
};

// "controller:settings"
struct SettingsChanged {
    protocol::Firmware firmware;
    protocol::FirmwareSettings settings;
};

struct FeederStatusChanged {  // "feeder:status"
    FeederStatus status;
};

struct SenderStatusChanged {  // "sender:status"
    SenderStatus status;
};

// "workflow:state"; `invalidLine` carries the line a job paused on when
// "show line warnings" is enabled.
struct WorkflowChanged {
    WorkflowState state;
    std::optional<std::string> invalidLine;
};

// "error" - an error or alarm reported by the firmware.
struct ErrorReported {
    bool isAlarm = false;
    std::string code;
    std::string description;
    std::string line;        // offending line, "N/A", "$H" or "jog"
    std::optional<std::size_t> lineNumber;
    std::string origin;      // job name, "Console", "Feeder", "Jog", "Startup"
    protocol::Firmware firmware;
    bool jobRunning = false;
};

struct GcodeError {  // "gcode_error"
    std::string message;
};

struct CheckModeFinished {  // "gcode_error_checking_file"
    SenderStatus status;
};

struct ToolChangeRequested {  // "gcode:toolChange"
    std::size_t line = 0;
    int count = 0;
    std::string block;
    std::optional<std::string> tool;  // e.g. "T2"
    std::string option;               // tool change strategy
    std::string comment;
};

struct ToolChangeStarted {};  // "toolchange:start"

struct ToolChangePreHookComplete {  // "toolchange:preHookComplete"
    std::string comment;
};

// "sender:M0M1" (job) / "feeder:pause" (macro) - a program pause.
struct ProgramPaused {
    std::string data;  // "M0", "M1" or "M0/M1"
    std::string comment;
    bool fromJob = true;
    bool ignoreEvent = true;
};

struct HomingFlagChanged {  // "homing:flag"
    bool set = false;
};

struct HasHomedChanged {  // "homing:has-homed"
    bool homed = false;
};

struct JobStarted {  // "job:start"
    bool fromLine = false;
};

struct JobStopped {};  // "job:stop"

enum class ProgramFileType { Default, Rotary, FourAxis };

struct FileTypeDetected {  // "filetype"
    ProgramFileType type = ProgramFileType::Default;
};

struct EstimateDataRequested {};  // "requestEstimateData"

struct WizardNext {  // "wizard:next"
    int step = 0;
    int substep = 0;
};

struct ControllerClosed {  // "serialport:closeController"
    std::int64_t currentLineRunning = 0;
};

struct FileUnloaded {};  // "file:unload"

// grblHAL only
struct SpindleAdded {  // "spindle:add"
    protocol::SpindleLine spindle;
};
struct SettingDescriptionsChanged {};  // "settings:description"
struct SettingAlarmsChanged {};        // "settings:alarms"
struct SettingGroupsChanged {};        // "settings:group"
struct SdCardFileListed {              // "sdcard:files"
    protocol::SdFile file;
};
struct SdCardJson {  // "sdcard:json"
    std::string json;
};
struct AtciMessage {  // "atci"
    protocol::AtciLine atci;
};
struct GrblHalInfo {  // "grblHal:info"
    protocol::InfoLine info;
};
struct GrblHalAutoconfig {  // "grblHal:autoconfig"
    protocol::AutoconfigLine autoconfig;
};
// An SD card upload (YMODEM over the serial link).
struct YModemStarted {};  // "ymodem:start"
struct YModemProgress {   // "ymodem:progress" - of the file being sent
    int percent = 0;
};
struct YModemCompleted {};  // "ymodem:complete"
struct YModemFailed {       // "ymodem:error"
    std::string message;
};

using ControllerEvent =
    std::variant<ConsoleOutput, ConsoleInput, StateChanged, SettingsChanged, FeederStatusChanged,
                 SenderStatusChanged, WorkflowChanged, ErrorReported, GcodeError, CheckModeFinished,
                 ToolChangeRequested, ToolChangeStarted, ToolChangePreHookComplete, ProgramPaused,
                 HomingFlagChanged, HasHomedChanged, JobStarted, JobStopped, FileTypeDetected,
                 EstimateDataRequested, WizardNext, ControllerClosed, FileUnloaded, SpindleAdded,
                 SettingDescriptionsChanged, SettingAlarmsChanged, SettingGroupsChanged, SdCardFileListed,
                 SdCardJson, AtciMessage, GrblHalInfo, GrblHalAutoconfig, YModemStarted, YModemProgress,
                 YModemCompleted, YModemFailed>;

using ControllerEventSink = std::function<void(const ControllerEvent&)>;

}  // namespace gs::controller
