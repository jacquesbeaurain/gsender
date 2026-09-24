#pragma once

// Typed firmware state shared by the Grbl and grblHAL protocol layers.
// Mirrors the `state` and `settings` objects of GrblRunner / GrblHalRunner.

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::protocol {

inline constexpr std::array<char, 6> kAxisNames{'x', 'y', 'z', 'a', 'b', 'c'};

// Up to six axis values in firmware order (x, y, z, a, b, c).
struct AxisValues {
    std::array<double, 6> values{};
    // Decimal places the firmware reported per axis; derived positions are
    // rounded to match (gSender kept positions as strings).
    std::array<int, 6> decimals{};
    std::size_t count = 0;

    double operator[](std::size_t i) const noexcept { return values[i]; }
    double x() const noexcept { return values[0]; }
    double y() const noexcept { return values[1]; }
    double z() const noexcept { return values[2]; }
    double a() const noexcept { return values[3]; }
    // Value for a lower-case axis letter; 0 when not reported.
    double axis(char name) const noexcept;
    bool has(char name) const noexcept;
    bool operator==(const AxisValues&) const = default;
};

struct BufferState {
    int planner = 0;  // free planner blocks
    int rx = 0;       // free RX buffer bytes
    bool operator==(const BufferState&) const = default;
};

struct ProbeInfo {
    int type = 0;
    bool isProtected = false;
    bool operator==(const ProbeInfo&) const = default;
};

struct SdProgress {
    std::optional<std::string> name;
    double percentage = 0;
    bool operator==(const SdProgress&) const = default;
};

// A parsed "<...>" status report. Optional fields were absent from the report.
struct StatusReport {
    std::string activeState;  // Idle, Run, Hold, Jog, Alarm, Door, Check, Home, Sleep, Tool
    std::optional<int> subState;
    std::optional<AxisValues> mpos;
    std::optional<AxisValues> wpos;
    std::optional<AxisValues> wco;
    std::optional<BufferState> buf;
    std::optional<int> lineNumber;
    std::optional<double> feedrate;
    std::optional<double> spindle;
    std::optional<std::string> pinState;  // triggered pin letters, e.g. "PZ"
    std::optional<std::array<int, 3>> overrides;  // feed, rapid, spindle (%)
    std::optional<std::string> accessoryState;    // e.g. "SFM"
    // grblHAL extensions
    std::optional<bool> hasHomed;
    std::optional<int> currentTool;
    std::optional<std::vector<std::string>> keepoutFlags;
    std::optional<ProbeInfo> probe;
    std::optional<SdProgress> sdProgress;
    std::optional<bool> sdCard;  // complete report only
    bool operator==(const StatusReport&) const = default;
};

// Accumulated status: every report is merged into this.
struct MachineStatus {
    std::string activeState;
    int subState = 0;
    AxisValues mpos{{}, 3};
    AxisValues wpos{{}, 3};
    std::optional<AxisValues> wco;
    std::optional<BufferState> buf;
    std::optional<int> lineNumber;
    double feedrate = 0;
    double spindle = 0;
    std::string pinState;
    std::array<int, 3> overrides{100, 100, 100};
    bool hasOverrides = false;
    std::string accessoryState;
    std::string alarmCode;  // numeric code as text, "Homing", or empty
    bool probeActive = false;
    // grblHAL extensions
    bool hasHomed = false;
    bool hasHomedReported = false;
    int currentTool = -1;
    std::vector<std::string> keepoutFlags;
    std::optional<ProbeInfo> probe;
    SdProgress sdProgress;
    bool sdCard = false;
    bool operator==(const MachineStatus&) const = default;
};

// G-code parser state from "$G" ([GC:...]).
struct ModalState {
    std::string motion = "G0";
    std::string wcs = "G54";
    std::string plane = "G17";
    std::string units = "G21";
    std::string distance = "G90";
    std::string feedrate = "G94";
    std::string program = "M0";
    std::string spindle = "M5";
    std::vector<std::string> coolant{"M9"};  // M7 and M8 may both be active
    std::string lathe;                       // grblHAL G7/G8
    std::string cycle;                       // grblHAL G98/G99
    std::string tool = "0";                  // last non-zero tool reported
    bool operator==(const ModalState&) const = default;
};

struct ParserState {
    ModalState modal;
    std::string tool;
    std::string feedrate;
    std::string spindle;
    bool operator==(const ParserState&) const = default;
};

// "[G54:...]", "[TLO:...]" and "[PRB:...]" values.
struct ParameterValue {
    AxisValues axes;
    std::optional<int> result;  // PRB success flag
    std::string raw;            // text after the colon
    bool operator==(const ParameterValue&) const = default;
};

// Insertion-ordered string map (JavaScript object semantics for "$n" keys).
class OrderedMap {
public:
    const std::string* find(std::string_view key) const;
    std::string get(std::string_view key, std::string_view fallback = {}) const;
    bool has(std::string_view key) const { return find(key) != nullptr; }
    // Returns true when the stored value changed.
    bool set(std::string_view key, std::string value);
    bool empty() const noexcept { return items_.empty(); }
    std::size_t size() const noexcept { return items_.size(); }
    void clear() noexcept { items_.clear(); }
    const std::vector<std::pair<std::string, std::string>>& items() const noexcept { return items_; }
    bool operator==(const OrderedMap&) const = default;

private:
    std::vector<std::pair<std::string, std::string>> items_;
};

// ---- grblHAL setting metadata reported by the firmware ----

struct SettingGroup {
    int id = 0;
    int parent = 0;
    std::string label;
    bool operator==(const SettingGroup&) const = default;
};

struct SettingDescription {
    int id = 0;
    int group = 0;
    std::string description;
    std::string unit;
    int dataType = 0;
    std::vector<std::string> format;  // comma-separated format becomes a list
    std::string unitString;           // from the $ESH detail table
    std::string details;
    bool operator==(const SettingDescription&) const = default;
};

struct CodeDescription {
    int code = 0;
    std::string description;
    bool operator==(const CodeDescription&) const = default;
};

struct ToolEntry {
    int id = 0;
    AxisValues offsets;
    double radius = 0;
    bool operator==(const ToolEntry&) const = default;
};

struct SdFile {
    std::string name;
    long long size = 0;
    bool unusable = false;
    bool operator==(const SdFile&) const = default;
};

// [NEWOPT:...] expands into key/value options; other info lines stay text.
struct InfoValue {
    std::string text;
    std::vector<std::pair<std::string, std::optional<std::string>>> options;
    bool isOptionList = false;
    std::optional<std::string> option(std::string_view key) const;
    bool hasOption(std::string_view key) const;  // with or without a value
    bool operator==(const InfoValue&) const = default;
};

struct AxesInfo {
    int count = 0;
    std::string letters;  // e.g. "XYZA"
    bool inferred = false;
    bool operator==(const AxesInfo&) const = default;
};

struct FirmwareSettings {
    std::string version;  // Grbl startup version or grblHAL [VER:] text
    long long semver = -1;  // grblHAL build date, e.g. 20250627
    std::map<std::string, ParameterValue> parameters;
    OrderedMap settings;  // "$0" -> "10"
    // grblHAL
    std::map<int, SettingGroup> groups;
    std::map<std::string, InfoValue> info;
    std::map<int, SettingDescription> descriptions;
    std::map<int, CodeDescription> alarms;
    std::map<int, CodeDescription> errors;
    std::map<int, ToolEntry> toolTable;
    std::map<std::string, std::string> atci;
    bool operator==(const FirmwareSettings&) const = default;
};

}  // namespace gs::protocol
