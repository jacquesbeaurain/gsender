#include "gs/protocol/runner.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <algorithm>
#include <cmath>

namespace gs::protocol {
namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

std::string codeText(std::string_view message) {
    const double code = js::stringToNumber(message);
    return std::isfinite(code) ? js::numberToString(code) : std::string(message);
}

}  // namespace

std::string_view firmwareName(Firmware firmware) noexcept {
    return firmware == Firmware::Grbl ? "Grbl" : "grblHAL";
}

Runner::Runner(Firmware firmware) : firmware_(firmware) {
    // GrblRunner starts with alarmCode "Homing"; GrblHalRunner with "".
    if (firmware_ == Firmware::Grbl) {
        state_.status.alarmCode = "Homing";
    }
}

int Runner::tool() const {
    const double value = js::stringToNumber(state_.parserState.tool);
    return std::isfinite(value) && value != 0 ? static_cast<int>(value) : 0;
}

void Runner::changeState(RunnerState next) {
    if (next != state_) {
        state_ = std::move(next);
        ++stateRevision_;
    }
}

void Runner::changeSettings(FirmwareSettings next) {
    if (next != settings_) {
        settings_ = std::move(next);
        ++settingsRevision_;
    }
}

std::optional<RunnerEvent> Runner::parse(std::string_view line) {
    const std::string_view data = str::trimRight(line);
    if (data.empty()) {
        return std::nullopt;
    }
    RunnerEvent event;
    event.raw = std::string(data);
    event.line = firmware_ == Firmware::Grbl ? parseGrblResponse(data) : parseGrblHalResponse(data);

    std::visit(
        Overloaded{
            [&](StatusLine& l) { handleStatus(l.report, event); },
            [&](CompleteStatusLine& l) { handleCompleteStatus(l.report); },
            [&](AlarmLine& l) {
                RunnerState next = state_;
                next.status.activeState = "Alarm";
                next.status.alarmCode = codeText(l.message);
                changeState(std::move(next));
            },
            [&](ParserStateLine& l) { handleParserState(l); },
            [&](ParametersLine& l) { handleParameters(l); },
            [&](SettingLine& l) { setSetting(l.name, l.value); },
            [&](StartupLine& l) {
                if (firmware_ == Firmware::Grbl && settings_.version != l.version) {
                    FirmwareSettings next = settings_;
                    next.version = l.version;
                    changeSettings(std::move(next));
                }
            },
            [&](VersionLine& l) {
                if (firmware_ != Firmware::GrblHal) {
                    return;
                }
                const std::string head = str::split(l.text, ':').front();
                const std::vector<std::string> parts = str::split(head, '.');
                const double semver = js::stringToNumber(parts.back());
                FirmwareSettings next = settings_;
                next.version = l.text;
                next.semver = std::isfinite(semver) ? static_cast<long long>(semver) : -1;
                changeSettings(std::move(next));
                event.semver = settings_.semver;
            },
            [&](AtciLine& l) {
                FirmwareSettings next = settings_;
                for (const auto& [key, value] : l.values) {
                    next.atci[key] = value.value_or(std::string());
                }
                changeSettings(std::move(next));
            },
            [&](AxsLine& l) {
                RunnerState next = state_;
                if (!l.letters.empty()) {
                    next.axes = AxesInfo{l.count, l.letters, false};
                } else {
                    next.axes.count = l.count;
                }
                changeState(std::move(next));
            },
            [&](AlarmDetailLine& l) {
                FirmwareSettings next = settings_;
                next.alarms[l.alarm.code] = l.alarm;
                changeSettings(std::move(next));
            },
            [&](ErrorDescriptionLine& l) {
                FirmwareSettings next = settings_;
                next.errors[l.error.code] = l.error;
                changeSettings(std::move(next));
            },
            [&](GroupDetailLine& l) {
                FirmwareSettings next = settings_;
                next.groups[l.group.id] = l.group;
                changeSettings(std::move(next));
            },
            [&](InfoLine& l) {
                FirmwareSettings next = settings_;
                next.info[l.name] = l.value;
                changeSettings(std::move(next));
            },
            [&](SettingDescriptionLine& l) {
                FirmwareSettings next = settings_;
                next.descriptions[l.description.id] = l.description;
                changeSettings(std::move(next));
            },
            [&](SettingDetailsLine& l) {
                FirmwareSettings next = settings_;
                SettingDescription& d = next.descriptions[l.id];
                d.id = l.id;
                d.unitString = l.unitString;
                d.details = l.details;
                changeSettings(std::move(next));
            },
            [&](ToolLine& l) {
                FirmwareSettings next = settings_;
                next.toolTable[l.tool.id] = l.tool;
                changeSettings(std::move(next));
            },
            [&](SdFileLine& l) {
                RunnerState next = state_;
                auto& files = next.sdcard.files;
                files.erase(std::remove_if(files.begin(), files.end(),
                                           [&](const SdFile& f) { return f.name == l.file.name; }),
                            files.end());
                files.push_back(l.file);
                changeState(std::move(next));
            },
            [](auto&) {},
        },
        event.line);
    return event;
}

void Runner::deriveMissingPosition(StatusReport& report) const {
    // WPos = MPos - WCO; the WCO is sticky because firmware only sends it
    // every few reports. Results keep the decimal places of the source value.
    const std::optional<AxisValues>& wco = report.wco ? report.wco : state_.status.wco;
    const auto offset = [&wco](std::size_t i) { return wco && i < wco->count ? wco->values[i] : 0.0; };
    const auto derive = [&](const AxisValues& from, double sign) {
        AxisValues out;
        out.count = from.count;
        for (std::size_t i = 0; i < from.count; ++i) {
            const int digits = from.decimals[i];
            out.decimals[i] = digits;
            out.values[i] = js::stringToNumber(js::toFixed(from.values[i] + sign * offset(i), digits));
        }
        return out;
    };
    if (report.mpos && !report.wpos) {
        report.wpos = derive(*report.mpos, -1);
    } else if (report.wpos && !report.mpos) {
        report.mpos = derive(*report.wpos, +1);
    }
}

void Runner::handleStatus(StatusReport& report, RunnerEvent& event) {
    deriveMissingPosition(report);

    RunnerState next = state_;
    MachineStatus& s = next.status;
    if (firmware_ == Firmware::Grbl && report.activeState == "Alarm" && state_.status.activeState != "Alarm") {
        event.enteredAlarm = true;
    }
    if (firmware_ == Firmware::GrblHal && report.activeState != "Alarm") {
        s.alarmCode.clear();
    }
    s.activeState = report.activeState;
    s.subState = report.subState.value_or(0);
    if (report.mpos) s.mpos = *report.mpos;
    if (report.wpos) s.wpos = *report.wpos;
    if (report.wco) s.wco = report.wco;
    if (report.buf) s.buf = report.buf;
    if (report.lineNumber) s.lineNumber = report.lineNumber;
    if (report.feedrate) s.feedrate = *report.feedrate;
    if (report.spindle) s.spindle = *report.spindle;
    s.pinState = report.pinState.value_or(std::string());
    s.probeActive = s.pinState.find('P') != std::string::npos;
    if (report.overrides) {
        s.overrides = *report.overrides;
        s.hasOverrides = true;
        // Grbl sends A: only alongside Ov: and only while an accessory is on.
        if (firmware_ == Firmware::Grbl && !report.accessoryState) {
            s.accessoryState.clear();
        }
    }
    if (report.accessoryState) s.accessoryState = *report.accessoryState;
    if (report.hasHomed) {
        s.hasHomed = *report.hasHomed;
        s.hasHomedReported = true;
    }
    if (report.currentTool) s.currentTool = *report.currentTool;
    if (report.keepoutFlags) s.keepoutFlags = *report.keepoutFlags;
    if (report.probe) s.probe = report.probe;
    if (report.sdProgress) s.sdProgress = *report.sdProgress;
    changeState(std::move(next));
}

void Runner::handleCompleteStatus(StatusReport& report) {
    deriveMissingPosition(report);

    RunnerState next = state_;
    MachineStatus& s = next.status;
    s.activeState = report.activeState;
    s.subState = report.subState.value_or(0);
    if (report.activeState == "Alarm" && report.subState && *report.subState != 0) {
        s.alarmCode = std::to_string(*report.subState);
    }
    if (report.mpos) s.mpos = *report.mpos;
    if (report.wpos) s.wpos = *report.wpos;
    if (report.wco) s.wco = report.wco;
    if (report.pinState) {
        s.pinState = *report.pinState;
        s.probeActive = s.pinState.find('P') != std::string::npos;
    }
    if (report.probe) s.probe = report.probe;
    if (report.currentTool) s.currentTool = *report.currentTool;
    if (report.hasHomed) {
        s.hasHomed = *report.hasHomed;
        s.hasHomedReported = true;
    }
    s.sdCard = report.sdCard.value_or(false);
    changeState(std::move(next));
}

void Runner::handleParserState(const ParserStateLine& line) {
    RunnerState next = state_;
    ModalState modal = line.modal;
    // modal.tool remembers the last non-zero tool.
    const std::string& reported = line.tool.value_or(std::string());
    const double toolNumber = js::stringToNumber(reported);
    if (line.tool && !(std::isfinite(toolNumber) && toolNumber == 0)) {
        modal.tool = reported;
    } else {
        modal.tool = state_.parserState.modal.tool;
    }
    next.parserState.modal = modal;
    // gSender's Grbl runner exposes the remembered tool; grblHAL's the raw one.
    next.parserState.tool = firmware_ == Firmware::Grbl ? modal.tool : line.tool.value_or(std::string());
    next.parserState.feedrate = line.feedrate.value_or(std::string());
    next.parserState.spindle = line.spindle.value_or(std::string());
    changeState(std::move(next));
}

void Runner::handleParameters(const ParametersLine& line) {
    FirmwareSettings next = settings_;
    // grblHAL: a successful probe updates the current tool's offsets so a tool
    // change mid-job sees fresh values.
    if (firmware_ == Firmware::GrblHal && line.name == "PRB" && line.value.result == 1) {
        const int currentTool = state_.status.currentTool;
        auto entry = next.toolTable.find(currentTool);
        if (currentTool > 0 && entry != next.toolTable.end()) {
            AxisValues& offsets = entry->second.offsets;
            offsets.count = std::max<std::size_t>(offsets.count, 3);
            offsets.values[0] = line.value.axes.axis('x');
            offsets.values[1] = line.value.axes.axis('y');
            offsets.values[2] = line.value.axes.axis('z');
        }
    }
    next.parameters[line.name] = line.value;
    changeSettings(std::move(next));
}

void Runner::setActiveState(std::string state) {
    RunnerState next = state_;
    next.status.activeState = std::move(state);
    changeState(std::move(next));
}

void Runner::setSpindleModal(std::string spindle) {
    RunnerState next = state_;
    next.parserState.modal.spindle = std::move(spindle);
    changeState(std::move(next));
}

void Runner::setTool(std::string tool) {
    RunnerState next = state_;
    next.parserState.modal.tool = std::move(tool);
    changeState(std::move(next));
}

void Runner::setSetting(std::string_view key, std::string value) {
    if (settings_.settings.set(key, std::move(value))) {
        ++settingsRevision_;
    }
}

void Runner::deleteSettings() {
    if (!settings_.settings.empty()) {
        settings_.settings.clear();
        ++settingsRevision_;
    }
}

void Runner::clearSdFiles() {
    RunnerState next = state_;
    next.sdcard.files.clear();
    changeState(std::move(next));
}

void Runner::setSdStatus(bool mounted) {
    RunnerState next = state_;
    next.status.sdCard = mounted;
    changeState(std::move(next));
}

std::optional<std::string> Runner::setInferredAxesFromStatus() {
    if (hasAxs()) {
        return std::nullopt;
    }
    const int reported = state_.axes.count;
    const int count = reported > 0 ? reported : static_cast<int>(state_.status.mpos.count);
    if (count <= 0) {
        return std::nullopt;
    }
    const std::string letters = std::string("XYZABC").substr(0, static_cast<std::size_t>(std::min(count, 6)));
    RunnerState next = state_;
    next.axes = AxesInfo{static_cast<int>(letters.size()), letters, true};
    changeState(std::move(next));
    return letters;
}

}  // namespace gs::protocol
