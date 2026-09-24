#include "gs/controller/controller.hpp"

#include "gs/expr/expression.hpp"
#include "gs/gcode/interpreter.hpp"
#include "gs/gcode/parser.hpp"
#include "gs/protocol/firmware_data.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <boost/regex.hpp>

#include <algorithm>
#include <cmath>

// Port of src/server/controllers/Grbl/GrblController.js and
// src/server/controllers/Grblhal/GrblHalController.js. Where the two differ the
// code branches on isGrbl()/isGrblHal(); everything else is shared. Deliberate
// behaviour differences are listed in DEV_WALKTHROUGH.md (Step 11).

namespace gs::controller {
namespace {

using protocol::Firmware;

constexpr std::string_view kWait = "%wait";
constexpr std::string_view kPreHookComplete = "%pre_complete";
constexpr std::string_view kPostHookComplete = "%toolchange_complete";
constexpr std::string_view kPauseStart = "%pause_start";
constexpr std::string_view kGcodeStart = "%_GCODE_START";
constexpr long long kAtciSupportedVersion = 20250627;

// grblHAL only reports [AXS:] in its $I reply on newer builds; a bounded number
// of $I probes decides whether it ever will.
constexpr int kAxsQueryMaxRetries = 2;
constexpr std::int64_t kAxsQueryRetryInterval = 2000;
constexpr std::int64_t kAxsProbeTimeout = 2000;

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// GRBL_REALTIME_COMMANDS / the values of GRBLHAL_REALTIME_COMMANDS: writeln()
// sends these without a trailing newline.
bool isRealtimeCommand(Firmware firmware, std::string_view data) {
    static constexpr std::string_view kGrbl[] = {"~", "!", "?", "\x18"};
    static constexpr std::string_view kGrblHal[] = {"~",    "!",    "?",    "\x18",     "\x19",
                                                    "\x87", "\x88", "\xA3", "ErrClear", "$G\n"};
    if (firmware == Firmware::Grbl) {
        return std::find(std::begin(kGrbl), std::end(kGrbl), data) != std::end(kGrbl);
    }
    return std::find(std::begin(kGrblHal), std::end(kGrblHal), data) != std::end(kGrblHal);
}

// /\([^)]*\)/g
std::string stripParenComments(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] == '(') {
            const std::size_t close = line.find(')', i + 1);
            if (close != std::string_view::npos) {
                i = close + 1;
                continue;
            }
        }
        out.push_back(line[i]);
        ++i;
    }
    return out;
}

// /\s*;.*/g: everything from the first ';' (and the whitespace before it).
std::string stripSemicolonComment(std::string_view line) {
    const std::size_t semi = line.find(';');
    if (semi == std::string_view::npos) {
        return std::string(line);
    }
    return std::string(str::trimRight(line.substr(0, semi)));
}

// The feeder's comment string: line.match(/\s*;.*/)[0].trim().replace(";", "")
// - the text after the first ';', trimmed on the right only.
std::string feederComment(std::string_view line) {
    const std::size_t semi = line.find(';');
    if (semi == std::string_view::npos) {
        return {};
    }
    return std::string(str::trimRight(line.substr(semi + 1)));
}

// Comments out M0/M1/M6 while leaving longer codes (M600) alone. M00, M01 and
// M06 are valid too; the flattened word was normalized but the line was not.
std::string commentOut(const std::string& line, char which) {
    static const boost::regex kM0("M0+(?!\\d)", boost::regex::icase);
    static const boost::regex kM1("M0*1(?!\\d)", boost::regex::icase);
    static const boost::regex kM6("M0*6(?!\\d)", boost::regex::icase);
    const boost::regex& pattern = which == '0' ? kM0 : which == '1' ? kM1 : kM6;
    const char* replacement = which == '0' ? "(M0)" : which == '1' ? "(M1)" : "(M6)";
    return boost::regex_replace(line, pattern, replacement,
                                boost::regex_constants::format_first_only | boost::regex_constants::format_literal);
}

bool contains(const std::vector<std::string>& words, std::string_view word) {
    return std::find(words.begin(), words.end(), word) != words.end();
}

// _.intersection(words, candidates)[0]: the first word (in line order) that
// is one of the candidates.
std::optional<std::string> firstOf(const std::vector<std::string>& words,
                                   std::initializer_list<std::string_view> candidates) {
    for (const std::string& word : words) {
        for (std::string_view c : candidates) {
            if (word == c) {
                return word;
            }
        }
    }
    return std::nullopt;
}

// convertGcodeToArray(): split on '\n', drop blank lines, keep the rest as is.
std::vector<std::string> toLines(std::string_view gcode) {
    std::vector<std::string> out;
    for (std::string_view line : str::splitView(gcode, '\n')) {
        if (!str::trim(line).empty()) {
            out.emplace_back(line);
        }
    }
    return out;
}

// ensurePositiveNumber(): Math.max(Number(value) || 0, 0)
double ensurePositive(double value) {
    return std::isnan(value) ? 0.0 : std::max(value, 0.0);
}

// Number(value) || 0
double numberOr0(double value) {
    return std::isnan(value) ? 0.0 : value;
}

std::string numberText(double value) {
    return js::numberToString(value);
}

// The params entry for a [Gxx:]/[TLO:]/[PRB:] line, shaped like the JavaScript
// payload (axis values stay the firmware's text).
expr::Value parameterValue(bool grblHal, const std::string& name, const protocol::ParameterValue& value) {
    static constexpr const char* kAxes[] = {"x", "y", "z", "a", "b", "c"};
    const auto setAxes = [](const expr::Value& object, const std::string& list) {
        const std::vector<std::string> items = str::split(list, ',');
        for (std::size_t i = 0; i < items.size() && i < 6; ++i) {
            object.set(kAxes[i], expr::Value(items[i]));
        }
    };
    if (name == "TLO" && grblHal) {
        return expr::Value(value.raw);
    }
    expr::Value object = expr::Value::object();
    if (name == "PRB" || name == "TLO") {
        // "0.000,0.000,1.492:1" - result first, as the payload was built.
        const std::size_t colon = value.raw.find(':');
        const double result = colon == std::string::npos
                                  ? std::nan("")
                                  : js::stringToNumber(std::string_view(value.raw).substr(colon + 1));
        object.set("result", expr::Value(result));
        setAxes(object, value.raw.substr(0, colon));
        return object;
    }
    setAxes(object, value.raw);
    return object;
}

}  // namespace

// ---- construction ------------------------------------------------------------------

Controller::Controller(runtime::EventLoop& loop, DeviceLink& link, protocol::Firmware firmware, ControllerHooks hooks,
                       ControllerEventSink sink)
    : loop_(loop),
      timers_(loop),
      link_(link),
      firmware_(firmware),
      hooks_(std::move(hooks)),
      sink_(std::move(sink)),
      runner_(firmware),
      eventTrigger_(hooks_.findEvent,
                    [this](const std::string&, const std::string& trigger, const std::string& commands) {
                        if (trigger == "system") {
                            if (hooks_.runSystemCommands) {
                                hooks_.runSystemCommands(commands);
                            }
                        } else {
                            gcode(commands);
                        }
                    }) {
    if (!hooks_.preferences) {
        hooks_.preferences = std::make_shared<Preferences>();
    }
    expr::installGlobals(globals_);

    // Deduct from the RX buffer size to prevent an overrun.
    sender_ = std::make_unique<Sender>(loop_, Sender::Protocol::CharacterCounting, isGrbl() ? 128 - 28 : 1024 - 300,
                                       [this](std::string line, const expr::Value& context) {
                                           return senderFilter(std::move(line), context);
                                       });
    feeder_ = std::make_unique<Feeder>(
        [this](std::string line, const expr::Value& context) { return feederFilter(std::move(line), context); });
    toolChanger_ = std::make_unique<ToolChanger>(loop_, [this] { return runner_.isIdle(); }, 200);
    ymodem_ = std::make_unique<protocol::YModemSender>(
        loop_, protocol::YModemSender::Callbacks{
                   [this](std::string_view bytes) { writeImmediate(bytes); },
                   [this] { report(YModemStarted{}); },
                   [this](int percent) { report(YModemProgress{percent}); },
                   [this] {
                       report(YModemCompleted{});
                       timers_.timeout(150, [this] { sdList(); });
                   },
                   [this](const std::string& message) { report(YModemFailed{message}); },
               });
    wireJogStreamer();
    wireStreaming();
    setPollingEnabled(true);
}

Controller::~Controller() {
    sink_ = nullptr;  // nothing is reported while tearing down
    timers_.clearAll();
    if (jogStreamer_) {
        jogStreamer_->onAbort = nullptr;
        jogStreamer_->abort("destroy");
    }
    if (toolChanger_) {
        toolChanger_->clearInterval();
    }
}

void Controller::report(ControllerEvent event) const {
    if (sink_) {
        sink_(event);
    }
}

void Controller::emitState(std::optional<std::string> tool) {
    report(StateChanged{firmware_, runner_.state(), std::move(tool)});
}

Preferences Controller::preferences() const {
    return hooks_.preferences ? *hooks_.preferences : Preferences{};
}

void Controller::setPollingEnabled(bool enabled) {
    if (enabled && queryTimer_ == 0) {
        queryTimer_ = timers_.interval(250, [this] { queryTick(); });
    } else if (!enabled) {
        timers_.clear(queryTimer_);
        timers_.clear(parserQueryTrailing_);
    }
}

void Controller::debounce(runtime::TimerId& timer, std::int64_t delayMs, std::function<void()> fn) {
    timers_.clear(timer);
    timer = timers_.timeout(delayMs, [&timer, fn = std::move(fn)] {
        timer = 0;
        fn();
    });
}

void Controller::wireStreaming() {
    sender_->onData = [this](const std::string& raw) {
        if (!isOpen()) {
            return;
        }
        const std::string line(str::trim(raw));
        if (line.empty()) {
            return;
        }
        report(ConsoleInput{line + "\n", WriteSource::Feeder});
        write(line + "\n");
    };
    sender_->onStart = [this](std::int64_t) { senderFinishTime_ = 0; };
    sender_->onEnd = [this](std::int64_t finishTime) {
        senderFinishTime_ = finishTime;
        // grblHAL leaves check mode as soon as the file is through; Grbl does
        // it on the next parser state report.
        if (isGrblHal() && runner_.isCheck()) {
            workflow_.stopTesting();
            gcode("$C");
            timers_.timeout(200, [this] { gcode("[global.state.testWCS]"); });
            report(CheckModeFinished{sender_->status()});
        }
    };
    sender_->onRequestData = [this] { report(EstimateDataRequested{}); };

    feeder_->onData = [this](const std::string& raw, const expr::Value&) {
        if (!isOpen()) {
            return;
        }
        const std::string line(str::trim(raw));
        if (line.empty()) {
            return;
        }
        report(ConsoleInput{line + "\n", WriteSource::Feeder});
        // Grbl writes straight to the connection; grblHAL goes through write(),
        // which also arms the reply-echo flags.
        if (isGrbl()) {
            writeFiltered(line + "\n");
        } else {
            write(line + "\n");
        }
    };
    feeder_->onComplete = [this] { consumeFeederCallback(); };

    workflow_.onStart = [this] {
        report(WorkflowChanged{workflow_.state(), std::nullopt});
        jogStreamer_->abort("workflow");
        sender_->rewind();
        sender_->resumeCountdown();
    };
    workflow_.onStop = [this] {
        report(WorkflowChanged{workflow_.state(), std::nullopt});
        feeder_->reset();
        sender_->rewind();
        sender_->stopCountdown();
    };
    workflow_.onPause = [this](const std::optional<HoldReason>& reason) {
        report(WorkflowChanged{workflow_.state(), std::nullopt});
        jogStreamer_->abort("workflow");
        sender_->hold(reason);
        timePaused_ = loop_.nowMs();
    };
    workflow_.onResume = [this] {
        report(WorkflowChanged{workflow_.state(), std::nullopt});
        const std::int64_t pauseTime = loop_.nowMs() - timePaused_;
        // grblHAL: a job paused by an error left the feeder holding.
        if (isGrblHal() && feeder_->isHeld()) {
            feeder_->unhold();
        }
        feeder_->reset();
        sender_->unhold();
        sender_->resumeCountdown();
        sender_->next({.timePaused = pauseTime});
    };
}

void Controller::wireJogStreamer() {
    JogStreamer::Options options;
    options.write = [this](const std::string& line) { writeFiltered(line); };
    options.getSettings = [this] { return runner_.settings().settings; };
    options.getStatus = [this] {
        const auto& status = runner_.state().status;
        return JogStatus{status.activeState, status.mpos, status.buf};
    };
    options.getHomingFlag = [this] { return homingFlagSet_; };
    // The streamer consumes its own acks, so all it needs is that nobody else
    // is waiting on one. Deliberately not gated on an idle workflow: jogging
    // while a job is paused for a tool change is exactly when it is needed.
    options.canStream = [this] {
        return isOpen() && !workflow_.isRunning() && !feeder_->hasOutstanding() &&
               sender_->received() >= sender_->sent();
    };
    if (isGrbl()) {
        options.lineFilter = [this](const std::string& line) {
            return preferences().useAaxisForGrbl ? line : applyRotaryTranslation(line, false);
        };
        options.rxBufferSize = 128;
    } else {
        // $40 lets grblHAL clamp jog targets itself; a travel budget is only
        // needed when it is off. grblHAL drives A natively.
        options.softLimitsEnabled = [](const protocol::OrderedMap& s) {
            return s.get("$20") == "1" && s.get("$40") == "0";
        };
        options.rxBufferSize = 256;
    }
    jogStreamer_ = std::make_unique<JogStreamer>(loop_, options);

    // One console line for the whole jog, written as the server.
    jogStreamer_->onStart = [this](const std::string& summary) {
        jogAnnounced_ = true;
        report(ConsoleInput{summary + "\n", WriteSource::Server});
    };
    jogStreamer_->onFeedrate = [this](const std::string& summary) {
        report(ConsoleInput{summary + "\n", WriteSource::Server});
    };
    // stop() drains and abort() can still fire mid-drain: announce the end once.
    const auto announceStopped = [this](std::string_view reason) {
        if (!jogAnnounced_) {
            return;
        }
        jogAnnounced_ = false;
        const std::optional<std::string> why = describeJogStopReason(reason);
        report(ConsoleInput{"Stopped jogging" + (why ? " - " + *why : std::string()) + "\n", WriteSource::Server});
    };
    jogStreamer_->onStop = [announceStopped] { announceStopped({}); };
    jogStreamer_->onAbort = [this, announceStopped](const std::string& reason) {
        announceStopped(reason);
        // cancel/close handle the machine themselves; a reset would be undone
        // by a jog cancel arriving after it.
        static constexpr std::string_view kHandledElsewhere[] = {"cancel", "close", "destroy", "reset"};
        const bool handled = std::find(std::begin(kHandledElsewhere), std::end(kHandledElsewhere), reason) !=
                             std::end(kHandledElsewhere);
        if (!handled && isOpen()) {
            write("\x85");
        }
    };
}

// ---- lifecycle ---------------------------------------------------------------------

void Controller::open() {
    workflow_.stop();
    clearActionValues();
    if (isGrbl()) {
        // Soft-reset when the board has said nothing after half a second.
        timers_.timeout(500, [this] {
            if (!ready_) {
                write("\x18");
            }
        });
        return;
    }
    resetAxsProbe();
    // 0x87 bypasses Hold restrictions and gets a status report back (which
    // makes the controller ready); $I brings the [VER:] that initializes it.
    timers_.timeout(500, [this] {
        writeFiltered("\x87");
        write("$I\n");
    });
}

void Controller::close() {
    ready_ = false;
    // A stream must never outlive the connection it writes to.
    jogStreamer_->abort("close");
    initialized_ = false;
    if (isGrblHal()) {
        resetAxsProbe();
        if (hasHomedSet_) {
            hasHomedSet_ = false;
            report(HasHomedChanged{false});
        }
    }
    report(ControllerClosed{sender_->currentLineRunning()});
}

void Controller::receiveLine(std::string_view line) {
    if (std::optional<protocol::RunnerEvent> event = runner_.parse(line)) {
        handle(*event);
    }
}

void Controller::handle(const protocol::RunnerEvent& event) {
    const std::string& raw = event.raw;
    if (isGrbl()) {
        ready_ = true;  // any line proves Grbl is alive
    }

    std::visit(
        Overloaded{
            [&](const protocol::StatusLine& l) {
                if (event.enteredAlarm) {
                    onStartupAlarm(raw);
                }
                onStatus(l.report, raw);
            },
            [&](const protocol::CompleteStatusLine& l) { onStatus(l.report, raw); },
            [&](const protocol::OkLine&) { onOk(raw); },
            [&](const protocol::ErrorLine& l) { onError(l.message, raw); },
            [&](const protocol::AlarmLine& l) { onAlarm(l.message, raw); },
            [&](const protocol::ParserStateLine&) { onParserState(raw); },
            [&](const protocol::SettingLine& l) { onSetting(l, raw); },
            [&](const protocol::StartupLine&) { onStartup(raw, std::nullopt); },
            [&](const protocol::VersionLine&) {
                if (isGrblHal()) {
                    onStartup(raw, event.semver);  // [VER:] is grblHAL's startup
                } else {
                    report(ConsoleOutput{raw});
                }
            },
            [&](const protocol::AtciLine& l) {
                report(ConsoleOutput{raw});
                if (l.subtype) {
                    report(AtciMessage{l});
                }
            },
            [&](const protocol::AutoconfigLine& l) {
                report(ConsoleOutput{raw});
                report(GrblHalAutoconfig{l});
            },
            [&](const protocol::SpindleLine& l) {
                report(SpindleAdded{l});
                report(ConsoleOutput{raw});
            },
            [&](const protocol::JsonLine& l) { report(SdCardJson{l.code}); },
            [&](const protocol::InfoLine& l) {
                report(ConsoleOutput{raw});
                report(GrblHalInfo{l});
            },
            [&](const protocol::SettingDescriptionLine&) {
                debounce(descriptionsDebounce_, 150, [this] { report(SettingDescriptionsChanged{}); });
            },
            [&](const protocol::SettingDetailsLine&) {
                debounce(descriptionsDebounce_, 150, [this] { report(SettingDescriptionsChanged{}); });
            },
            [&](const protocol::AlarmDetailLine&) { report(SettingAlarmsChanged{}); },
            [&](const protocol::GroupDetailLine&) {
                debounce(groupsDebounce_, 150, [this] { report(SettingGroupsChanged{}); });
            },
            [&](const protocol::SdFileLine& l) {
                report(ConsoleOutput{raw});
                report(SdCardFileListed{l.file});
            },
            // grblHAL's runner takes these in without anything listening.
            [&](const protocol::ToolLine&) {},
            [&](const protocol::AxsLine&) {},
            [&](const protocol::ErrorDescriptionLine&) {},
            // Parameters, feedback, help, echo and anything unrecognised.
            [&](const auto&) { report(ConsoleOutput{raw}); },
        },
        event.line);
}

// ---- runner events -----------------------------------------------------------------

void Controller::onStatus(const protocol::StatusReport& reported, const std::string& raw) {
    if (isGrbl()) {
        if (!runner_.hasSettings() && reported.activeState == "Idle") {
            initialized_ = true;
            initController();
        }
        if (homingStarted_) {
            homingFlagSet_ = determineMachineZeroFlagSet(runner_.state().status.mpos, runner_.settings().settings);
            report(HomingFlagChanged{homingFlagSet_});
            homingStarted_ = false;
            if (!hasHomedSet_ && reported.activeState != "Alarm") {
                hasHomedSet_ = true;
                report(HasHomedChanged{true});
            }
        }
    } else {
        // Polling may start with any status (e.g. the 0x87 reply in Hold).
        ready_ = true;
        // This only ever starts a probe; resolveAxsProbe() judges the reply.
        if (!runner_.hasAxs() && !axsProbePending_ && axsSupport_ == AxsSupport::Unknown &&
            axsQueryCount_ < kAxsQueryMaxRetries && reported.activeState == "Idle" &&
            loop_.nowMs() - axsQueryLastTime_ >= kAxsQueryRetryInterval) {
            startAxsProbe();
        }
        // Newer grblHAL reports the runtime homed state in H:.
        if (reported.hasHomed) {
            if (*reported.hasHomed != hasHomedSet_) {
                hasHomedSet_ = *reported.hasHomed;
                report(HasHomedChanged{hasHomedSet_});
            }
            homingStarted_ = false;
        }
    }

    actionMask_.queryStatusReport = false;

    // The reported position is the truth the streamer's budget estimates.
    jogStreamer_->onStatus(JogStatus{reported.activeState, runner_.state().status.mpos, reported.buf});

    if (actionMask_.replyStatusReport) {
        actionMask_.replyStatusReport = false;
        report(ConsoleOutput{raw});
    }

    // Grow the character-counting buffer to what the firmware reports - never
    // during a job and never with bytes in flight.
    const int rx = reported.buf ? reported.buf->rx : 0;
    if (rx > 0 && workflow_.isIdle() && sender_->protocol() == Sender::Protocol::CharacterCounting &&
        sender_->dataLength() == 0) {
        const int bufferSize = rx - 8;
        if (bufferSize > sender_->bufferSize()) {
            sender_->setBufferSize(bufferSize);
        }
    }
}

void Controller::onOk(const std::string& raw) {
    // The $I reply block ends with this ok. Resolve before anything below can
    // consume it.
    if (isGrblHal() && axsProbePending_) {
        resolveAxsProbe();
    }

    if (isGrbl()) {
        if (actionMask_.queryParserStateReply) {
            if (actionMask_.replyParserState) {
                actionMask_.replyParserState = false;
                report(ConsoleOutput{raw});
            }
            actionMask_.queryParserStateReply = false;
            return;
        }
    } else if (actionMask_.queryParserStateReply && !parserStateEnabled_) {
        // $G is only polled while $10 does not push the parser state.
        if (actionMask_.replyParserState) {
            actionMask_.replyParserState = false;
            report(ConsoleOutput{raw});
        }
        actionMask_.queryParserStateReply = false;
        return;
    } else if (actionMask_.queryParserStateReply && parserStateEnabled_ && actionMask_.replyParserState) {
        // A pushed parser state has no ok: only a user-typed $G produces one.
        actionMask_.replyParserState = false;
        actionMask_.queryParserStateReply = false;
        report(ConsoleOutput{raw});
        return;
    }

    // A streamed jog owns the link while it runs: an ok it waits for is its
    // own, and letting it reach the feeder would desync its accounting.
    if (jogStreamer_->isActive() && jogStreamer_->ack()) {
        return;
    }

    const bool hold = sender_->isHeld();
    const std::size_t sent = sender_->sent();
    const std::size_t received = sender_->received();
    if (workflow_.isRunning()) {
        report(ConsoleOutput{raw});
        if (hold && received + 1 >= sent) {
            sender_->unhold();
        }
        sender_->ack();
        sender_->next({.isOk = true});
        return;
    }
    if (workflow_.isPaused() && received < sent) {
        report(ConsoleOutput{raw});
        sender_->ack();
        sender_->next({.isOk = true});
        return;
    }

    report(ConsoleOutput{raw});
    feeder_->ack();
    feeder_->next();
}

std::pair<std::string, std::string> Controller::errorOrigin(bool checkHoming) {
    if (consoleInput_) {
        std::string line = std::move(*consoleInput_);
        consoleInput_.reset();
        if (!line.empty()) {
            return {"Console", line};
        }
    }
    if (checkHoming && runner_.state().status.activeState == "Home") {
        return {"Console", "$H"};
    }
    if (feeder_->outstanding() > 0) {
        return {"Feeder", "N/A"};
    }
    if (sender_->total() != 0) {
        return {sender_->name(), std::string(sender_->line(sender_->received()))};
    }
    return {"Feeder", "N/A"};
}

std::optional<protocol::CodeInfo> Controller::errorInfo(int code) const {
    if (code == 0) {
        return std::nullopt;
    }
    const std::string key = std::to_string(code);
    if (isGrblHal()) {
        // grblHAL describes its own errors ($EE).
        if (auto it = runner_.settings().errors.find(code); it != runner_.settings().errors.end()) {
            return protocol::CodeInfo{key, it->second.description, it->second.description};
        }
    }
    if (const protocol::CodeInfo* info = protocol::FirmwareTables::get(firmware_).error(key)) {
        return *info;
    }
    return std::nullopt;
}

std::optional<protocol::CodeInfo> Controller::alarmInfo(const std::string& code) const {
    if (code.empty()) {
        return std::nullopt;
    }
    if (isGrblHal()) {
        // grblHAL describes its own alarms ($EA).
        const double number = js::stringToNumber(code);
        if (std::isfinite(number)) {
            const auto& alarms = runner_.settings().alarms;
            if (auto it = alarms.find(static_cast<int>(number)); it != alarms.end()) {
                return protocol::CodeInfo{code, it->second.description, it->second.description};
            }
        }
    }
    if (const protocol::CodeInfo* info = protocol::FirmwareTables::get(firmware_).alarm(code)) {
        return *info;
    }
    return std::nullopt;
}

void Controller::onError(const std::string& message, const std::string& raw) {
    // Number(message) || undefined
    const double parsed = js::stringToNumber(message);
    const int code = std::isfinite(parsed) ? static_cast<int>(parsed) : 0;
    const std::string codeText = code != 0 ? std::to_string(code) : std::string("undefined");
    const std::optional<protocol::CodeInfo> error = errorInfo(code);

    // An error on a streamed jog line belongs to the jog - not to the feeder,
    // the loaded file, or a workflow a jog cannot have been part of.
    if (jogStreamer_->isActive()) {
        const bool wasJogError = jogStreamer_->onError();
        jogStreamer_->abort("error");
        if (wasJogError) {
            report(ConsoleOutput{raw});
            report(ErrorReported{false, codeText, error ? error->description : std::string(), "jog", std::nullopt,
                               "Jog", firmware_, false});
            return;
        }
    }

    const bool running = workflow_.isRunning();
    if (isGrblHal()) {
        const bool alarmed = runner_.isAlarm();
        // Stop streaming at the first error: hold the sender and the machine.
        if (running) {
            workflow_.pause();
            sender_->hold();
            writeImmediate("\n");
            write("!");
        }
        // An alarmed firmware errors on everything, and 79 is always noise.
        if (alarmed || code == 79) {
            return;
        }
    }

    const bool isFileError = sender_->total() != 0;
    const std::size_t received = sender_->received();
    const auto [origin, line] = errorOrigin(false);
    report(ErrorReported{false, codeText, error ? error->description : std::string(), line,
                       isFileError ? std::optional<std::size_t>(received) : std::nullopt, origin, firmware_,
                       isGrblHal() && running});

    // Grbl annotates with the short message, grblHAL with the description.
    const std::string annotation = error ? (isGrbl() ? error->message : error->description) : std::string("undefined");

    if (workflow_.isRunning() || workflow_.isPaused()) {
        const std::string invalidLine = std::to_string(sender_->total()) + " " + std::string(sender_->line(received));
        const Preferences prefs = preferences();
        report(ConsoleOutput{"error:" + codeText + " (" + annotation + ")"});
        if (isGrbl()) {
            if (error) {
                const HoldReason reason{"", "", "error:" + codeText + " (" + error->message + ")"};
                if (!prefs.showLineWarnings) {
                    report(GcodeError{"Error " + codeText + " on line " + std::to_string(received) + " - " +
                                    error->message});
                    workflow_.pause(reason);
                } else {
                    workflow_.pause(reason);
                    report(WorkflowChanged{workflow_.state(), invalidLine});
                }
            } else {
                report(ConsoleOutput{raw});
            }
            sender_->ack();
            sender_->next();
            return;
        }
        if (!prefs.showLineWarnings) {
            report(GcodeError{"Error " + codeText + " on line " + std::to_string(received) + " - " + annotation});
        } else {
            report(WorkflowChanged{workflow_.state(), invalidLine});
        }
        sender_->ack();
        sender_->next({.isOk = true});
        return;
    }

    if (isGrbl()) {
        report(ConsoleOutput{error ? "error:" + codeText + " (" + error->message + ")" : raw});
    } else {
        report(ConsoleOutput{"error:" + codeText + " (" + annotation + ")"});
        // SD card errors: the card is gone.
        if (error && (code == 60 || code == 62 || code == 64)) {
            runner_.setSdStatus(false);
            emitState();
        }
        report(GcodeError{"Error " + codeText + " - " + annotation});
    }
    feeder_->ack();
    feeder_->next();
}

void Controller::onAlarm(const std::string& message, const std::string& raw) {
    // Number(message) || undefined (Grbl) / || status.subState (grblHAL)
    const double parsed = js::stringToNumber(message);
    std::string code;
    if (std::isfinite(parsed) && parsed != 0) {
        code = std::to_string(static_cast<int>(parsed));
    } else if (isGrblHal()) {
        code = std::to_string(runner_.state().status.subState);
    }

    if (isGrbl() && !code.empty()) {
        // Homing failures (6-9) invalidate the homed state.
        const int number = std::stoi(code);
        if (number >= 6 && number <= 9 && hasHomedSet_) {
            hasHomedSet_ = false;
            report(HasHomedChanged{false});
        }
    }

    const bool isFileError = sender_->total() != 0;
    const std::size_t received = sender_->received();
    const auto [origin, line] = errorOrigin(true);
    const std::optional<protocol::CodeInfo> alarm = alarmInfo(code);
    if (!alarm) {
        report(ConsoleOutput{raw});
        return;
    }
    const bool running = workflow_.isRunning();
    if (isGrblHal() && running) {
        workflow_.stop();
    }
    report(ConsoleOutput{"ALARM:" + code + " (" + (isGrbl() ? alarm->message : alarm->description) + ")"});
    report(ErrorReported{true, code, alarm->description, line,
                       isFileError ? std::optional<std::size_t>(received) : std::nullopt, origin, firmware_,
                       isGrblHal() && running});
    emitState();  // propagate the alarm right away
}

void Controller::onStartupAlarm(const std::string& raw) {
    const std::optional<protocol::CodeInfo> alarm = alarmInfo("Homing");
    if (!alarm) {
        report(ConsoleOutput{raw});
        return;
    }
    report(ConsoleOutput{"ALARM:Homing (" + alarm->message + ")"});
    report(ErrorReported{true, "Homing", alarm->description, "N/A", std::nullopt, "Startup", firmware_, false});
    emitState();
}

void Controller::onParserState(const std::string& raw) {
    // Check mode ($C) has run through the whole file.
    if (sender_->finishTime() > 0 && sender_->sent() > 0 && runner_.isCheck()) {
        if (isGrbl()) {
            gcode(std::vector<std::string>{"$C", "[global.state.testWCS]"});
            workflow_.stopTesting();
        } else {
            workflow_.stopTesting();
            gcode("$C");
            timers_.timeout(200, [this] { gcode("[global.state.testWCS]"); });
        }
        report(CheckModeFinished{sender_->status()});
        return;
    }
    actionMask_.queryParserStateState = false;
    actionMask_.queryParserStateReply = true;
    if (actionMask_.replyParserState) {
        report(ConsoleOutput{raw});
    }
}

void Controller::onSetting(const protocol::SettingLine& line, const std::string& raw) {
    const protocol::SettingInfo* info = protocol::FirmwareTables::get(firmware_).setting(line.name);
    if (isGrbl()) {
        if (line.message.empty() && info) {
            report(ConsoleOutput{line.name + "=" + line.value + " (" + info->message + ", " + info->units + ")"});
        } else {
            report(ConsoleOutput{raw});
        }
        return;
    }
    if (line.message.empty()) {
        if (info && !info->message.empty()) {
            report(ConsoleOutput{line.name + "=" + line.value + " (" + info->message + ", " + info->units + ")"});
        } else {
            report(ConsoleOutput{line.name + "=" + line.value});
        }
    }
    // $10 bit 9: the firmware pushes the parser state by itself.
    if (line.name == "$10") {
        const double value = js::stringToNumber(line.value);
        parserStateEnabled_ = std::isfinite(value) && (static_cast<long long>(value) & 512) != 0;
    }
}

void Controller::onStartup(const std::string& raw, std::optional<long long> semver) {
    report(ConsoleOutput{raw});
    // The banner prints on power-up, after a reset and at program end: start
    // over from a clean slate.
    clearActionValues();
    ready_ = true;
    if (isGrbl()) {
        if (!initialized_ || !runner_.hasSettings()) {
            initialized_ = true;
            initController();
        }
        return;
    }
    workflow_.stop();
    // Only a [VER:] line (the $I reply) initializes grblHAL; the bare greeting
    // does not.
    if (!initialized_ && semver) {
        initialized_ = true;
        initController();
    }
}

void Controller::initController() {
    if (isGrbl()) {
        writeln("$$");
        timers_.timeout(50, [this] { eventTrigger_.trigger(kControllerReady); });
        return;
    }
    timers_.timeout(500, [this] {
        // Each enabled step is written, then 25 ms pass before the next.
        auto steps = std::make_shared<std::vector<StartupStep>>();
        steps->push_back({"$$"});
        steps->push_back({"$ES\n$ESH\n$EG\n$EA\n$EE\n$#"});
        if (runner_.state().status.sdCard && !actionMask_.sdAccessory) {
            steps->push_back({"$FM\n$F", true});
        }
        steps->push_back({spindleListCommand()});
        runStartupStep(std::move(steps), 0);
    });
}

void Controller::runStartupStep(std::shared_ptr<const std::vector<StartupStep>> steps, std::size_t index) {
    if (index >= steps->size()) {
        eventTrigger_.trigger(kControllerReady);
        return;
    }
    const StartupStep& step = (*steps)[index];
    writeFiltered(step.commands + "\n");
    if (step.marksSdQueried) {
        actionMask_.sdAccessory = true;
    }
    timers_.timeout(25, [this, steps, index] { runStartupStep(steps, index + 1); });
}

void Controller::clearActionValues() {
    actionMask_.queryParserStateState = false;
    actionMask_.queryParserStateReply = false;
    actionMask_.queryStatusReport = false;
    actionMask_.replyParserState = false;
    actionMask_.replyStatusReport = false;
    if (isGrblHal()) {
        actionMask_.sdAccessory = false;
    }
    queryParserStateTime_ = 0;
    queryStatusReportTime_ = 0;
    senderFinishTime_ = 0;
}

// ---- polling -----------------------------------------------------------------------

void Controller::queryTick() {
    if (!isOpen()) {
        return;
    }
    if (feeder_->peek()) {
        report(FeederStatusChanged{feeder_->status()});
    }
    if (sender_->peek()) {
        report(SenderStatusChanged{sender_->status()});
    }

    // Has the work position stayed put since the last broadcast?
    const bool zeroOffset = reportedWorkPosition_ == runner_.workPosition();

    if (runner_.settingsRevision() != reportedSettingsRevision_) {
        reportedSettingsRevision_ = runner_.settingsRevision();
        report(SettingsChanged{firmware_, runner_.settings()});
        if (isGrblHal()) {
            // $22 bit 3 decides whether machine zero is known after homing.
            const bool flag = determineHalMachineZeroFlag(runner_.settings().settings);
            if (flag != homingFlagSet_) {
                homingFlagSet_ = flag;
                report(HomingFlagChanged{flag});
            }
        }
    }

    if (runner_.stateRevision() != reportedStateRevision_) {
        const std::string current = reportedActiveState_;
        const std::string& now = runner_.state().status.activeState;
        // Pause the countdown once the machine settles; restart it on motion.
        if (workflow_.isPaused() && (current == "Idle" || current == "Hold") && sender_->isCountdownRunning()) {
            sender_->pauseCountdown();
        } else if (current == "Run" && !sender_->isCountdownRunning()) {
            sender_->resumeCountdown();
        }
        // grblHAL: a hold released with a physical/macro button resumes the job.
        if (isGrblHal() && workflow_.isPaused() && current == "Hold" && (now == "Idle" || now == "Run")) {
            timers_.clear(programResumeTimer_);
            programResumeTimer_ = timers_.timeout(1000, [this] {
                programResumeTimer_ = 0;
                if (workflow_.isIdle()) {
                    return;
                }
                const std::string& state = reportedActiveState_;
                if ((state == "Idle" || state == "Run") && sender_->isHeld()) {
                    resume();
                }
            });
        }
        reportedStateRevision_ = runner_.stateRevision();
        reportedActiveState_ = now;
        reportedWorkPosition_ = runner_.workPosition();
        emitState();
    }

    if (isGrblHal()) {
        // Poll with 0x87 until an alarm's code is known.
        const auto& status = runner_.state().status;
        if (status.activeState == "Alarm") {
            if (status.alarmCode.empty()) {
                if (!alarmActive_) {
                    alarmActive_ = true;
                    actionMask_.alarmCompleteReport = true;
                }
            } else {
                alarmActive_ = true;
                actionMask_.alarmCompleteReport = false;
            }
        } else if (alarmActive_) {
            alarmActive_ = false;
        }
    }

    if (!ready_) {
        return;
    }

    queryStatusReport();

    // $G, throttled to once per 500 ms (lodash throttle: leading + trailing).
    const std::int64_t now = loop_.nowMs();
    if (lastParserQuery_ == 0 || now - lastParserQuery_ >= 500) {
        lastParserQuery_ = now;
        queryParserState();
    } else if (parserQueryTrailing_ == 0) {
        parserQueryTrailing_ = timers_.timeout(lastParserQuery_ + 500 - now, [this] {
            parserQueryTrailing_ = 0;
            lastParserQuery_ = loop_.nowMs();
            queryParserState();
        });
    }

    // The last line was acknowledged: the job ends once the machine has been
    // idle and still for half a second.
    if (senderFinishTime_ > 0) {
        const bool machineIdle = zeroOffset && runner_.isIdle();
        const std::int64_t timespan = std::llabs(now - senderFinishTime_);
        if (!machineIdle) {
            senderFinishTime_ = now;
        } else if (timespan > 500) {
            senderFinishTime_ = 0;
            stop();
        }
    }
}

void Controller::queryStatusReport() {
    // Nothing may interrupt an upload.
    if (!ready_ || ymodem_->active()) {
        return;
    }
    const std::int64_t now = loop_.nowMs();
    // "?" is realtime and costs no buffer space; ask again after 5 s of silence.
    if (queryStatusReportTime_ > 0 && std::llabs(now - queryStatusReportTime_) >= 5000) {
        actionMask_.queryStatusReport = false;
    }
    if (actionMask_.queryStatusReport || !isOpen()) {
        return;
    }
    actionMask_.queryStatusReport = true;
    queryStatusReportTime_ = now;
    if (isGrblHal() && runner_.isAlarm() && actionMask_.alarmCompleteReport) {
        writeImmediate("\x87");
    } else {
        writeImmediate("?");
    }
}

void Controller::queryParserState() {
    if (!ready_ || (isGrblHal() && parserStateEnabled_) || ymodem_->active()) {
        return;
    }
    const std::int64_t now = loop_.nowMs();
    // Never force $G during a job: it costs 3 RX buffer bytes each time.
    if (workflow_.isIdle() && runner_.isIdle() && queryParserStateTime_ > 0 &&
        std::llabs(now - queryParserStateTime_) >= 10000) {
        actionMask_.queryParserStateState = false;
        actionMask_.queryParserStateReply = false;
    }
    if (actionMask_.queryParserStateState || actionMask_.queryParserStateReply || !isOpen()) {
        return;
    }
    actionMask_.queryParserStateState = true;
    actionMask_.queryParserStateReply = false;
    queryParserStateTime_ = now;
    // Straight to the link: the poll must not arm the reply-echo flag.
    writeImmediate("$G\n");
}

// ---- grblHAL [AXS:] probing -------------------------------------------------------------
//
// The probe budget lives outside actionMask: clearActionValues() runs on every
// [VER:] line - including the one in the reply to our own $I - and would reset
// a budget kept there by the very response it counts.

void Controller::resetAxsProbe() {
    timers_.clear(axsProbeTimer_);
    axsProbePending_ = false;
    axsQueryCount_ = 0;
    axsQueryLastTime_ = 0;
    axsSupport_ = AxsSupport::Unknown;
}

void Controller::startAxsProbe() {
    ++axsQueryCount_;
    axsQueryLastTime_ = loop_.nowMs();
    axsProbePending_ = true;
    // Safety net: the terminating ok can be swallowed (an error instead, or a
    // job starting in between).
    timers_.clear(axsProbeTimer_);
    axsProbeTimer_ = timers_.timeout(kAxsProbeTimeout, [this] {
        axsProbeTimer_ = 0;
        if (axsProbePending_) {
            resolveAxsProbe();
        }
    });
    writeln("$I");
}

void Controller::resolveAxsProbe() {
    axsProbePending_ = false;
    timers_.clear(axsProbeTimer_);
    if (runner_.hasAxs()) {
        axsSupport_ = AxsSupport::Supported;  // never probe again
        return;
    }
    if (axsQueryCount_ < kAxsQueryMaxRetries) {
        return;  // the status handler probes again after the retry interval
    }
    // Two complete $I replies without [AXS:]: guess from the status report.
    axsSupport_ = AxsSupport::Unsupported;
    runner_.setInferredAxesFromStatus();
}

// ---- stream filters ----------------------------------------------------------------------

void Controller::populateContext(const expr::Value& context) const {
    if (!context.isObject()) {
        return;
    }
    const auto& status = runner_.state().status;
    // Positions are raw firmware values; macros always see millimetres.
    const bool inches = runner_.setting("$13") == "1";
    const auto machineUnits = [inches](const protocol::AxisValues& values, char axis) {
        const double number = values.has(axis) ? numberOr0(values.axis(axis)) : 0.0;
        return expr::Value(js::toFixed(inches ? number * 25.4 : number, 3));
    };

    context.set("global", sharedContext_);
    for (const char* key : {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"}) {
        context.set(key, expr::Value(numberOr0(expr::toNumber(context.get(key)))));
    }
    for (char axis : protocol::kAxisNames) {
        context.set(std::string("mpos") + axis, machineUnits(status.mpos, axis));
    }
    for (char axis : protocol::kAxisNames) {
        context.set(std::string("pos") + axis, machineUnits(status.wpos, axis));
    }

    const protocol::ModalState& modal = runner_.modal();
    expr::Value modalObject = expr::Value::object();
    modalObject.set("motion", expr::Value(modal.motion));
    modalObject.set("wcs", expr::Value(modal.wcs));
    modalObject.set("plane", expr::Value(modal.plane));
    modalObject.set("units", expr::Value(modal.units));
    modalObject.set("distance", expr::Value(modal.distance));
    modalObject.set("feedrate", expr::Value(modal.feedrate));
    modalObject.set("program", expr::Value(modal.program));
    modalObject.set("spindle", expr::Value(modal.spindle));
    // M7 and M8 on separate lines: together on one they are a modal group
    // violation.
    modalObject.set("coolant", expr::Value(str::join(modal.coolant, "\n")));
    context.set("modal", modalObject);

    context.set("tool", expr::Value(runner_.tool()));

    expr::Value params = expr::Value::object();
    for (const auto& [name, value] : runner_.settings().parameters) {
        params.set(name, parameterValue(isGrblHal(), name, value));
    }
    context.set("params", params);
    context.set("programFeedrate", expr::Value(runner_.currentFeedrate()));
    context.set("spindleRate", expr::Value(runner_.currentSpindleRate()));

    for (const auto& [key, value] : globals_.objectData().properties) {
        context.set(key, value);
    }
}

std::string Controller::feederFilter(std::string line, const expr::Value& context) {
    const std::string comment = feederComment(line);
    line = std::string(str::trim(stripSemicolonComment(line)));
    // context.ignoreEvent (undefined is falsy)
    const bool ignoreEvent = context.isObject() ? expr::toBoolean(context.get("ignoreEvent")) : true;
    // grblHAL: an EEPROM macro update ($...) must not trigger pauses.
    const bool looksLikeEeprom = !line.empty() && line.front() == '$';
    bool populated = false;
    const auto ensureContext = [&] {
        if (!populated) {
            populateContext(context);
            populated = true;
        }
    };

    if (!line.empty() && line.front() == '%') {
        if (line == kWait) {
            return "G4 P0.5";  // wait for the planner to empty
        }
        if (line == kPreHookComplete) {
            feeder_->hold(HoldReason{"%toolchange", "", ""});
            report(ToolChangePreHookComplete{comment});
            return isGrbl() ? "G4 P0.5" : "G4 P1";
        }
        if (line == kPostHookComplete) {
            timers_.timeout(isGrbl() ? 500 : 1000, [this] { workflow_.resume(); });
            return isGrbl() ? "G4 P0.5" : "G4 P1";
        }
        if (line == kPauseStart) {
            report(ProgramPaused{"M0/M1", comment, true, ignoreEvent});
            return "G4 P0.5";
        }
        if (line == kGcodeStart) {
            // Start-from-line preamble done: stream from the line the sender
            // was fast-forwarded to (the workflow start rewinds it).
            const std::size_t sent = sender_->sent();
            workflow_.start();
            feeder_->reset();
            sender_->setStartLine(sent);
            sender_->next({.startFromLine = true});
            return {};
        }
        // %_x=posx,_y=posy
        ensureContext();
        expr::evaluateAssignments(std::string_view(line).substr(1), context);
        return {};
    }

    // [\xNN] tokens are realtime bytes, written right away.
    {
        RealtimeExtraction extraction = extractRealtimeCommands(line);
        for (std::uint8_t byte : extraction.realtime) {
            writeImmediate(std::string(1, static_cast<char>(byte)));
        }
        line = std::move(extraction.line);
        if (line.empty()) {
            return {};
        }
    }

    // "G0 X[posx - 8]" -> "G0 X2"
    if (line.find('[') != std::string::npos) {
        ensureContext();
        line = expr::translateExpressions(line, context);
    }
    const std::vector<std::string> words = gcode::parseLine(line).flatWords();

    if (const auto mode = firstOf(words, {"M0", "M1"}); mode && !(isGrblHal() && looksLikeEeprom)) {
        feeder_->hold(HoldReason{*mode, comment, ""});
        report(ProgramPaused{*mode, comment, false, true});
    }
    // Update the spindle modal eagerly, for safety.
    if (const auto spindle = firstOf(words, {"M3", "M4"})) {
        updateSpindleModal(*spindle);
    }
    if (contains(words, "M6")) {
        const bool passthrough = toolChangeContext_.passthrough;
        if (isGrbl()) {
            feeder_->hold(HoldReason{"M6", comment, ""});
            if (!passthrough) {
                line = commentOut(line, '6');
            }
        } else if (!passthrough) {
            feeder_->hold(HoldReason{"M6", comment, ""});
            line = commentOut(line, '6');
        }
    }

    const bool imperial = runner_.modal().units == "G20";
    if (isGrbl()) {
        return preferences().useAaxisForGrbl ? line : applyRotaryTranslation(line, imperial);
    }
    return isInRotaryMode_ ? applyRotaryTranslation(line, imperial) : line;
}

std::string Controller::senderFilter(std::string line, const expr::Value& context) {
    const std::string comment = gcode::extractComments(line);
    // "%" lines keep their comments (so "%wait ; ..." is not a wait).
    if (line.empty() || line.front() != '%') {
        line = std::string(str::trim(stripSemicolonComment(str::trim(stripParenComments(line)))));
    }
    const std::size_t sent = sender_->sent();

    if (!line.empty() && line.front() == '%') {
        if (line == kWait) {
            sender_->hold(HoldReason{std::string(kWait), "", ""});
            return "G4 P0.5";  // wait for the planner to empty
        }
        populateContext(context);
        expr::evaluateAssignments(std::string_view(line).substr(1), context);
        return {};
    }

    if (line.find('[') != std::string::npos) {
        populateContext(context);
        line = expr::translateExpressions(line, context);
    }
    const std::vector<std::string> words = gcode::parseLine(line).flatWords();

    if (const auto mode = firstOf(words, {"M0", "M1"})) {
        expr::Value options = expr::Value::object();
        options.set("ignoreEvent", expr::Value(false));
        const std::string pauseBlock = std::string(kWait) + "\n" + std::string(kPauseStart) + " ;" + comment;
        if (*mode == "M0") {
            // Carbide files open with an M0; don't pause that early.
            if (sent > 10) {
                workflow_.pause(HoldReason{"M0", comment, ""});
                gcode(pauseBlock, options);
            }
            line = commentOut(line, '0');
        } else {
            workflow_.pause(HoldReason{"M1", comment, ""});
            gcode(pauseBlock, options);
            line = commentOut(line, '1');
        }
    }

    if (const auto spindle = firstOf(words, {"M3", "M4"})) {
        updateSpindleModal(*spindle);
    }

    const bool imperial = runner_.modal().units == "G20";
    const auto rotary = [&](std::string text) {
        if (isGrbl()) {
            return preferences().useAaxisForGrbl ? text : applyRotaryTranslation(text, imperial);
        }
        return isInRotaryMode_ ? applyRotaryTranslation(text, imperial) : text;
    };

    if (!contains(words, "M6")) {
        return rotary(std::move(line));
    }

    // ---- tool change ----
    if (runner_.state().status.activeState == "Check") {
        return commentOut(line, '6');  // no tool changes in check mode
    }
    const std::string option = toolChangeContext_.option;
    static const boost::regex kTool(R"((T)(-?\d*\.?\d+\.?))");
    std::optional<std::string> toolLabel;
    std::optional<std::string> toolNumber;
    {
        boost::smatch match;
        if (boost::regex_search(line, match, kTool)) {
            toolLabel = match[0].str();
            toolNumber = match[2].str();
        }
    }
    if (isGrblHal() && toolNumber && toolChangeContext_.mappings) {
        const auto it = toolChangeContext_.mappings->find(*toolNumber);
        if (it != toolChangeContext_.mappings->end() && !it->second.empty()) {
            const std::size_t at = line.find(*toolLabel);
            if (at != std::string::npos) {
                line.replace(at, toolLabel->size(), "T" + it->second);
            }
            toolLabel = "T" + it->second;
            toolNumber = it->second;
        }
    }

    // The dialog shows the line as finally sent, filled in below.
    auto block = std::make_shared<std::string>();
    if (option != "Ignore") {
        workflow_.pause(HoldReason{"M6", comment, ""});
        if (option == "Code") {
            const auto startHook = [this, comment] {
                report(ToolChangeStarted{});
                runPreChangeHook(comment);
            };
            if (isGrbl()) {
                startHook();
            } else {
                timers_.timeout(500, startHook);
            }
        } else {
            const int count = sender_->incrementToolChanges();
            toolChanger_->addInterval([this, sent, count, block, toolLabel, toolNumber, option, comment] {
                // Report the new tool before the dialog asks for it.
                runner_.setTool(toolNumber.value_or(std::string()));
                emitState(toolNumber);
                report(ToolChangeRequested{sent + 1, count, *block, toolLabel, option, comment});
            });
        }
    }
    const bool passthrough = toolChangeContext_.passthrough;
    if (isGrbl() ? !passthrough : (!passthrough || option == "Code")) {
        line = commentOut(line, '6');
    }
    line = rotary(std::move(line));
    *block = line;
    return line;
}

// ---- commands ------------------------------------------------------------------------------

bool Controller::beginCommand(std::string_view name) {
    // A jog stream consumes its own acks, which is only safe while it is the
    // sole writer of ack-producing lines: anything else ends the stream first.
    static constexpr std::string_view kJogCommands[] = {"jog:start", "jog:update", "jog:feed", "jog:stop",
                                                        "jog:cancel"};
    static constexpr std::string_view kStreamSafe[] = {"statusreport", "reset", "reset:soft", "reset:quit"};
    const bool safe = std::find(std::begin(kJogCommands), std::end(kJogCommands), name) != std::end(kJogCommands) ||
                      std::find(std::begin(kStreamSafe), std::end(kStreamSafe), name) != std::end(kStreamSafe);
    if (jogStreamer_->isActive() && !safe) {
        jogStreamer_->abort("command:" + std::string(name));
    }
    return true;
}

Controller::LoadResult Controller::loadProgram(const std::string& name, std::string gcodeText, expr::Value context) {
    beginCommand("gcode:load");
    const Preferences prefs = preferences();
    const double spindleDelay = numberOr0(prefs.spindleDelay);
    if (isGrbl()) {
        // A dwell after each spindle start - unless the program dwells already.
        static const boost::regex kHasDwell(R"((G4 ?P?[0-9]+))");
        static const boost::regex kSpindleStart(R"(\b(?:S\d* ?M[34]|M[34] ?S\d*)\b)");
        if (spindleDelay != 0 && !boost::regex_search(gcodeText, kHasDwell)) {
            gcodeText = boost::regex_replace(gcodeText, kSpindleStart, "$& G4 P" + numberText(spindleDelay));
        }
        const std::string withoutComments = stripParenComments(gcodeText);
        const bool hasA = hasAxisWord(withoutComments, 'A');
        const bool hasY = hasAxisWord(withoutComments, 'Y');
        if (hasA && hasY) {
            report(FileTypeDetected{ProgramFileType::FourAxis});
        } else if (hasA) {
            report(FileTypeDetected{ProgramFileType::Rotary});
        }
        gcodeText += "\n%wait ; Wait for the planner to empty";
    } else {
        // Firmware older than ATCI support does not act on $392 itself.
        if (runner_.settings().semver < kAtciSupportedVersion && spindleDelay > 0) {
            static const boost::regex kSpindleStart(R"(\b(?:S\d* ?M[34]|M[34] ?S\d*)\b(?! ?G4 ?P?\b))");
            gcodeText = boost::regex_replace(gcodeText, kSpindleStart, "$& G4 P" + numberText(spindleDelay));
        }
        toolChangeContext_.mappings = std::map<std::string, std::string>{};
        gcodeText += "\n";
    }

    LoadResult result;
    if (!sender_->load(name, std::move(gcodeText), std::move(context))) {
        result.error = "Invalid G-code: name=" + name;
        return result;
    }
    workflow_.stop();
    result.ok = true;
    result.status = sender_->status();
    return result;
}

void Controller::loadFile(const std::string& name, std::string gcodeText, bool refresh) {
    if (refresh && !workflow_.isIdle()) {
        return;  // never reload under a running job
    }
    loadProgram(name, std::move(gcodeText));
}

void Controller::unloadProgram() {
    beginCommand("gcode:unload");
    workflow_.stop();
    if (hooks_.unloadFile) {
        hooks_.unloadFile();
    }
    sender_->unload();
    report(FileUnloaded{});
    eventTrigger_.trigger(kFileUnload);
}

void Controller::start(const StartOptions& options) {
    beginCommand("gcode:start");
    const Preferences prefs = preferences();
    const double defaultSpindleDelay = numberOr0(prefs.spindleDelay);
    const std::size_t totalLines = sender_->total();
    const bool fromLine = options.lineToStartFrom > 0 && options.lineToStartFrom <= totalLines;
    const bool startEventEnabled = eventTrigger_.hasEnabledEvent(kProgramStart);
    report(JobStarted{fromLine});

    gcode("%global.state.workspace=modal.wcs");

    if (fromLine) {
        // Recover modal state, position, feed and speed from the skipped lines.
        gcode::Interpreter toolpath;
        bool feedFound = false;
        double feedRate = 200;
        double spindleRate = 0;
        for (std::size_t i = 0; i < options.lineToStartFrom; ++i) {
            const std::string_view line = sender_->line(i);
            toolpath.processLine(line);
            const gcode::ParsedLine parsed = gcode::parseLine(line);
            const auto wordValue = [&parsed](char letter) -> std::optional<double> {
                for (const gcode::Word& word : parsed.words) {
                    if (word.letter == letter) {
                        return word.value;
                    }
                }
                return std::nullopt;
            };
            // Tested against the whole line, like the JavaScript.
            if (line.find('F') != std::string_view::npos) {
                feedRate = wordValue('F').value_or(0);
                feedFound = true;
            }
            if (line.find('S') != std::string_view::npos) {
                if (const auto s = wordValue('S')) {
                    spindleRate = *s;
                }
            }
        }
        const gcode::Modal& modal = toolpath.modal();
        const gcode::Vec4 position = toolpath.position();
        const std::string mist = modal.coolant.find("M7") != std::string::npos ? "M7" : "";
        const std::string flood = modal.coolant.find("M8") != std::string::npos ? "M8" : "";
        const bool hasSpindle = modal.spindle != "M5";
        if (!feedFound) {
            feedRate = modal.units == "G21" ? 200 : 8;
        }
        // A file that never selects a workspace runs in the selected one.
        const std::string wcs = runner_.modal().wcs;
        std::string modalWcs = modal.wcs;
        if (modalWcs != wcs && modalWcs == "G54") {
            modalWcs = wcs;
        }

        std::vector<std::string> preamble;
        preamble.push_back(eventTrigger_.eventCode(kProgramStart));
        // Up, then over to the start position.
        preamble.push_back("G0 G90 G21 Z" + numberText(options.zMax + options.safeHeight));
        if (isGrblHal()) {
            // ATC: load the right tool before the spindle starts - only when the
            // skipped lines really changed tools (a bare T word does not).
            const auto& info = runner_.settings().info;
            const auto newopt = info.find("NEWOPT");
            const bool atc = newopt != info.end() && newopt->second.option("ATC") == std::optional<std::string>("1");
            if (atc && modal.tool != 0 && toolpath.hasSeenM6()) {
                std::string tool = numberText(modal.tool);
                if (toolChangeContext_.mappings) {
                    const auto it = toolChangeContext_.mappings->find(tool);
                    if (it != toolChangeContext_.mappings->end() && !it->second.empty()) {
                        tool = it->second;
                    }
                }
                preamble.push_back("M6 T" + tool);
            }
            if (hasSpindle) {
                preamble.push_back(modal.spindle + " S" + numberText(spindleRate));
            } else {
                preamble.push_back(modal.units + " F" + numberText(feedRate));
            }
        } else if (hasSpindle) {
            preamble.push_back(modal.units + " " + modal.spindle + " F" + numberText(feedRate) + " S" +
                               numberText(spindleRate));
        } else {
            preamble.push_back(modal.units + " F" + numberText(feedRate));  // always an F, just in case
        }
        preamble.push_back("G0 G90 G21 X" + js::toFixed(position.x, 3) + " Y" + js::toFixed(position.y, 3));
        if (position.a != 0 && !std::isnan(position.a)) {
            preamble.push_back("G0 G90 G21 A" + js::toFixed(std::fmod(position.a, 360.0), 3));
        }
        preamble.push_back("G0 G90 G21 Z" + js::toFixed(position.z, 3));
        // The modals in effect at that point of the file.
        preamble.push_back(modal.units + " " + modal.distance + " " + modal.arc + " " + modalWcs + " " + modal.plane +
                           " " + flood + " " + mist);
        if (isGrbl()) {
            preamble.push_back(modal.motion);
            preamble.push_back("G4 P" + numberText(options.spindleDelay.value_or(defaultSpindleDelay)));
        } else {
            preamble.push_back("F" + numberText(feedRate));
            // An arc motion mode needs a (null) arc to take effect.
            preamble.push_back(modal.motion == "G2" || modal.motion == "G3"
                                   ? modal.motion + " X" + js::toFixed(position.x, 3) + " J0 F" + numberText(feedRate)
                                   : modal.motion);
            if (defaultSpindleDelay > 0) {
                preamble.push_back("G4 P" + numberText(defaultSpindleDelay));
            }
        }
        preamble.emplace_back(kGcodeStart);

        // Fast-forward the sender; %_GCODE_START starts streaming from here.
        sender_->setStartLine(options.lineToStartFrom);
        gcode(preamble);
        return;
    }

    if (startEventEnabled) {
        // The program start event's code runs first; the job follows once the
        // feeder has drained.
        feederCallback_ = [this] {
            feeder_->reset();
            workflow_.start();
            sender_->next();
            feederCallback_ = nullptr;
        };
        eventTrigger_.trigger(kProgramStart);
        return;
    }

    workflow_.start();
    feeder_->reset();
    sender_->setStartLine(0);
    sender_->next({.startFromLine = true});
}

void Controller::stop(bool force) {
    beginCommand("gcode:stop");
    workflow_.stop();
    if (isGrblHal()) {
        timers_.clear(programResumeTimer_);
    }
    report(JobStopped{});
    const std::string wcs = runner_.modal().wcs;
    // The program end event fires last - after the reset on a forced stop.
    const auto finish = [this] {
        eventTrigger_.trigger(kProgramEnd);
        sender_->stopCountdown();
    };
    if (!force) {
        finish();
        return;
    }
    if (isGrblHal()) {
        write("\x19");
    }
    if (runner_.state().status.activeState == "Run") {
        write("!");
    }
    timers_.timeout(700, [this, wcs, finish] {
        write("\x18");
        if (wcs != "G54") {
            // A reset selects G54: restore the workspace that was in use.
            timers_.timeout(200, [this, wcs, finish] {
                writeln(wcs);
                finish();
            });
            return;
        }
        finish();
    });
}

void Controller::pause() {
    beginCommand("gcode:pause");
    if (eventTrigger_.hasEnabledEvent(kProgramPause)) {
        workflow_.pause();
        eventTrigger_.trigger(kProgramPause);
        return;
    }
    workflow_.pause();
    timers_.timeout(100, [this] { write("!"); });
}

void Controller::resume(bool ignoreEvents) {
    beginCommand("gcode:resume");
    if (!ignoreEvents && eventTrigger_.hasEnabledEvent(kProgramResume)) {
        feederCallback_ = [this] {
            write("~");
            workflow_.resume();
            feederCallback_ = nullptr;
        };
        eventTrigger_.trigger(kProgramResume);
        return;
    }
    write("~");
    timers_.timeout(1000, [this] { workflow_.resume(); });
}

void Controller::testProgram() {
    beginCommand("gcode:test");
    feederCallback_ = [this] {
        if (isGrbl()) {
            workflow_.start();
            feeder_->reset();
        } else {
            feeder_->reset();
            workflow_.start();
        }
        sender_->next();
        feederCallback_ = nullptr;
    };
    gcode(std::vector<std::string>{"%global.state.testWCS=modal.wcs", "$C"});
}

void Controller::updateEstimateData(std::vector<double> estimates, double estimatedTime) {
    beginCommand("updateEstimateData");
    sender_->setEstimateData(std::move(estimates));
    sender_->setEstimatedTime(estimatedTime);
}

void Controller::gcode(const std::vector<std::string>& commands, expr::Value context) {
    beginCommand("gcode");
    // join("\n").split(/\r?\n/), dropping blank lines; lines are not trimmed.
    std::vector<std::string> lines;
    for (const std::string& command : commands) {
        for (std::string_view piece : str::splitView(command, '\n')) {
            if (piece.ends_with('\r')) {
                piece.remove_suffix(1);
            }
            if (!str::trim(piece).empty()) {
                lines.emplace_back(piece);
            }
        }
    }
    feeder_->feed(lines, std::move(context));
    if (!feeder_->isPending()) {
        feeder_->next();
    }
}

void Controller::gcode(const std::string& commands, expr::Value context) {
    gcode(std::vector<std::string>{commands}, std::move(context));
}

void Controller::feederFeed(const std::vector<std::string>& commands, expr::Value context) {
    beginCommand("feeder:feed");
    gcode(commands, std::move(context));
}

void Controller::feederStart() {
    beginCommand("feeder:start");
    if (workflow_.isRunning()) {
        return;
    }
    write("~");
    timers_.timeout(1000, [this] {
        feeder_->unhold();
        feeder_->next();
    });
}

void Controller::feederStop() {
    beginCommand("feeder:stop");
    feeder_->reset();
    if (isGrblHal()) {
        workflow_.stop();
        write("\x19");
    }
    timers_.timeout(100, [this] { write("~"); });
}

void Controller::gcodeSafe(const std::vector<std::string>& commands, const std::string& preferredUnits) {
    beginCommand("gcode:safe");
    const std::string deviceUnits = runner_.modal().units;
    if (deviceUnits.empty()) {
        return;  // the device units are unknown
    }
    // Run in the preferred units, then restore the device's.
    std::vector<std::string> code;
    if (preferredUnits != deviceUnits) {
        code.push_back(preferredUnits);
    }
    code.insert(code.end(), commands.begin(), commands.end());
    if (preferredUnits != deviceUnits) {
        code.push_back(deviceUnits);
    }
    gcode(code);
}

void Controller::feedHold() {
    beginCommand("feedhold");
    eventTrigger_.trigger(kFeedHold);
    write("!");
}

void Controller::cycleStart() {
    beginCommand("cyclestart");
    eventTrigger_.trigger(kCycleStart);
    write("~");
}

void Controller::feedHoldAlt() {
    beginCommand("feedhold_alt");
    eventTrigger_.trigger(kFeedHold);
    write("!");
}

void Controller::cycleStartAlt() {
    beginCommand("cyclestart_alt");
    eventTrigger_.trigger(kCycleStart);
    write("~");
}

void Controller::statusReport() {
    beginCommand("statusreport");
    write("?");
}

void Controller::home(std::optional<char> axis) {
    beginCommand("homing");
    eventTrigger_.trigger(kHoming);
    homingStarted_ = true;
    if (isGrblHal() && axis) {
        writeln(std::string("$H") + *axis);
    } else {
        writeln("$H");
    }
    runner_.setActiveState("Home");
    emitState();
}

void Controller::sleep() {
    beginCommand("sleep");
    eventTrigger_.trigger(kSleep);
    writeln("$SLP");
}

void Controller::unlock() {
    beginCommand("unlock");
    feeder_->reset();
    writeln("$X");
    if (isGrblHal()) {
        write("\x19");
    }
}

void Controller::populateConfig() {
    beginCommand("populateConfig");
    writeln("$$");
}

void Controller::reset() {
    beginCommand("reset");
    workflow_.stop();
    feeder_->reset();
    write("\x18");
}

void Controller::resetSoft() {
    beginCommand("reset:soft");
    workflow_.stop();
    feeder_->reset();
    write("\x19");
}

void Controller::resetLimit() {
    beginCommand("reset:limit");
    workflow_.stop();
    feeder_->reset();
    write("\x18");
    if (isGrbl()) {
        writeln("$X");
        return;
    }
    timers_.timeout(350, [this] {
        writeln("$X");
        timers_.timeout(500, [this] { writeImmediate("\x87"); });
    });
}

void Controller::restart() {
    beginCommand("restart");
    gcode("$REBOOT");
}

void Controller::checkStateUpdate() {
    beginCommand("checkStateUpdate");
    emitState();
}

void Controller::writeImmediateBytes(const std::vector<std::uint8_t>& bytes) {
    // One realtime byte every 25 ms.
    std::int64_t index = 0;
    for (std::uint8_t byte : bytes) {
        ++index;
        timers_.timeout(25 * index, [this, byte] { writeImmediate(std::string(1, static_cast<char>(byte))); });
    }
}

void Controller::feedOverride(int value) {
    beginCommand("feedOverride");
    if (value == 100) {
        writeImmediateBytes({0x90});
    } else {
        writeImmediateBytes(overrideBytes(value - runner_.state().status.overrides[0], OverrideKind::Feed));
    }
    sender_->setOvF(value);
}

void Controller::spindleOverride(int value) {
    beginCommand("spindleOverride");
    const int current = runner_.state().status.overrides[2];
    int difference = value - current;
    // Limits for keyboard/gamepad shortcuts.
    if (value < 10) {
        difference = 10 - current;
    } else if (value > 230) {
        difference = 230 - current;
    }
    if (value == 100) {
        writeImmediateBytes({0x99});
    } else {
        writeImmediateBytes(overrideBytes(difference, OverrideKind::Spindle));
    }
}

void Controller::rapidOverride(int value) {
    beginCommand("rapidOverride");
    if (value == 0 || value == 100) {
        write("\x95");
    } else if (value == 50) {
        write("\x96");
    } else if (value == 25) {
        write("\x97");
    }
}

void Controller::laserTestOn(double power, double duration) {
    beginCommand("lasertest:on");
    const double maxS = ensurePositive(js::stringToNumber(runner_.setting(isGrbl() ? "$30" : "$730", "255")));
    const std::string laserPower = js::toFixed(ensurePositive(maxS * (power / 100)), 2);
    // The laser only fires in a G1/G2/G3 motion mode.
    std::vector<std::string> commands{"G1F1 M3 S" + laserPower};
    if (duration > 0) {
        commands.push_back("G4P" + numberText(ensurePositive(duration)));
        commands.push_back("M5 S0");
    }
    runner_.setSpindleModal("M3");
    emitState();
    gcode(commands);
}

void Controller::laserTestOff() {
    beginCommand("lasertest:off");
    gcode(std::vector<std::string>{"M5S0"});
}

void Controller::laserPowerChange(double power, double maxS) {
    beginCommand("laserpower:change");
    const double value = js::mathRound(ensurePositive(maxS * (power / 100)) * 100) / 100;
    gcode(std::vector<std::string>{"S" + numberText(value)});
}

void Controller::spindleSpeedChange(double speed) {
    beginCommand("spindlespeed:change");
    gcode(std::vector<std::string>{"S" + numberText(speed)});
}

void Controller::realtimeReport() {
    beginCommand("realtime_report");
    write("\x87\n");
}

void Controller::errorClear() {
    beginCommand("error_clear");
    write("$");
}

void Controller::toolChangeAcknowledge() {
    beginCommand("toolchange:acknowledge");
    report(ConsoleInput{"Toolchange Ack sent", WriteSource::Feeder});
    write("\xA3");
}

void Controller::virtualStopToggle() {
    beginCommand("virtual_stop_toggle");
    write("\x88");
}

void Controller::setRotaryMode(bool enabled) {
    beginCommand("updateRotaryMode");
    isInRotaryMode_ = enabled;
}

void Controller::resetRunnerSettings() {
    beginCommand("runner:resetSettings");
    runner_.deleteSettings();
}

void Controller::jogStart(const Axes4& direction, double feedrate, JogUnits units) {
    beginCommand("jog:start");
    if (isGrblHal()) {
        // The streamer reads it to work out how much travel is left.
        homingFlagSet_ = determineHalMachineZeroFlag(runner_.settings().settings);
    }
    jogStreamer_->start(direction, feedrate, units);
}

void Controller::jogUpdate(const Axes4& direction, std::optional<double> feedrate) {
    beginCommand("jog:update");
    jogStreamer_->update(direction, feedrate);
}

void Controller::jogFeed(const Axes4& distances, std::optional<double> feedrate, JogUnits units) {
    beginCommand("jog:feed");
    if (isGrblHal()) {
        homingFlagSet_ = determineHalMachineZeroFlag(runner_.settings().settings);
    }
    jogStreamer_->feed(distances, feedrate, units);
}

void Controller::jogStop() {
    beginCommand("jog:stop");
    jogStreamer_->stop();
    write("\x85");
}

void Controller::jogCancel() {
    beginCommand("jog:cancel");
    jogStreamer_->abort("cancel");
    write("\x85");
}

bool Controller::runMacro(std::string_view id, expr::Value context) {
    beginCommand("macro:run");
    const std::optional<Macro> macro = hooks_.findMacro ? hooks_.findMacro(id) : std::nullopt;
    if (!macro) {
        return false;
    }
    eventTrigger_.trigger(kMacroRun);
    gcode(macro->content, std::move(context));
    return true;
}

std::optional<Controller::LoadResult> Controller::loadMacro(std::string_view id, expr::Value context) {
    beginCommand("macro:load");
    const std::optional<Macro> macro = hooks_.findMacro ? hooks_.findMacro(id) : std::nullopt;
    if (!macro) {
        return std::nullopt;
    }
    eventTrigger_.trigger(kMacroLoad);
    return loadProgram(macro->name, macro->content, std::move(context));
}

void Controller::setToolChangeContext(const ToolChangeContext& context) {
    beginCommand("toolchange:context");
    if (isGrbl()) {
        toolChangeContext_ = context;  // Grbl replaces the context
        return;
    }
    // grblHAL merges: tool mappings survive a context that carries none.
    std::optional<std::map<std::string, std::string>> mappings = std::move(toolChangeContext_.mappings);
    toolChangeContext_ = context;
    if (!toolChangeContext_.mappings) {
        toolChangeContext_.mappings = std::move(mappings);
    }
}

void Controller::runPreChangeHook(const std::string& comment) {
    std::vector<std::string> block = toLines("G4 P1\n" + toolChangeContext_.preHook);
    if (toolChangeContext_.skipDialog) {
        // No dialog: run both hooks and finish the tool change right away.
        block.emplace_back("G4 P1");
        for (std::string& line : toLines(toolChangeContext_.postHook)) {
            block.push_back(std::move(line));
        }
        block.emplace_back(kPostHookComplete);
    } else {
        // Holds the feeder and shows the dialog to continue the tool change.
        block.push_back(std::string(kPreHookComplete) + " ;" + comment);
    }
    gcode(block);
}

void Controller::runPostChangeHook() {
    std::vector<std::string> block = toLines("G4 P1\n" + toolChangeContext_.postHook);
    block.emplace_back(kPostHookComplete);
    gcode(block);
}

void Controller::toolChangePre() {
    beginCommand("toolchange:pre");
    runPreChangeHook();
}

void Controller::toolChangePost() {
    beginCommand("toolchange:post");
    feederStart();
    runPostChangeHook();
}

void Controller::wizardStart(const std::string& gcodeText, std::function<void()> started) {
    beginCommand("wizard:start");
    toolChanger_->addInterval([this, gcodeText, started = std::move(started)] {
        gcode(gcodeText);
        if (started) {
            started();
        }
    });
}

void Controller::wizardStep(int step, int substep) {
    beginCommand("wizard:step");
    feederCallback_ = [this, step, substep] { report(WizardNext{step, substep}); };
}

void Controller::consumeFeederCallback() {
    if (!feederCallback_) {
        return;
    }
    // this.feederCB(); this.feederCB = null; - anything the callback set is
    // dropped too.
    const std::function<void()> callback = std::move(feederCallback_);
    feederCallback_ = nullptr;
    callback();
    feederCallback_ = nullptr;
}

void Controller::sdMount() {
    beginCommand("sdcard:mount");
    write("$FM\n");
    runner_.setSdStatus(true);
    emitState();
}

void Controller::sdList(bool all) {
    beginCommand("sdcard:list");
    runner_.clearSdFiles();
    runner_.setSdStatus(true);
    emitState();
    if (all) {
        write("$FM\n$F+\n");
        return;
    }
    write("$FM\n");
    timers_.timeout(100, [this] { write("$F\n"); });
}

void Controller::sdRead(const std::string& fileName) {
    beginCommand("sdcard:read");
    // The ATCI macro is always requested.
    if (fileName != "ATCI.macro") {
        const auto& files = runner_.state().sdcard.files;
        const bool present =
            std::any_of(files.begin(), files.end(), [&](const protocol::SdFile& f) { return f.name == fileName; });
        if (!present) {
            return;
        }
    }
    gcode("$F<=" + fileName);
}

void Controller::sdRun(const std::string& path) {
    beginCommand("sdcard:run");
    writeln("$F=" + path);
}

void Controller::sdDelete(const std::string& path) {
    beginCommand("sdcard:delete");
    writeln("$FD=" + path);
}

void Controller::sdUpload(std::vector<protocol::YModemFile> files) {
    beginCommand("ymodem:uploadFiles");
    if (ymodem_->active()) {
        return;  // one upload at a time
    }
    sdMount();
    timers_.timeout(1500, [this, files = std::move(files)]() mutable {
        if (!runner_.isSdMounted()) {
            report(YModemFailed{
                "SD Card not detected, please insert an SD Card in FAT32 format, 32 GB or under, and try again"});
            return;
        }
        if (link_.isNetwork()) {
            if (!hooks_.ftpUpload) {
                report(YModemFailed{"Uploading over a network connection needs FTP, which is not available."});
                return;
            }
            const double port = js::stringToNumber(runner_.setting("$308", "21"));
            FtpUploadRequest request;
            request.host = link_.networkHost();
            request.port = std::isfinite(port) && port > 0 ? static_cast<int>(port) : 21;
            request.files = std::move(files);
            const std::weak_ptr<int> alive = alive_;
            hooks_.ftpUpload(std::move(request),
                             UploadCallbacks{
                                 [this, alive] {
                                     if (alive.lock()) {
                                         report(YModemStarted{});
                                     }
                                 },
                                 [this, alive](int percent) {
                                     if (alive.lock()) {
                                         report(YModemProgress{percent});
                                     }
                                 },
                                 [this, alive] {
                                     if (alive.lock()) {
                                         report(YModemCompleted{});
                                         timers_.timeout(150, [this] { sdList(); });
                                     }
                                 },
                                 [this, alive](const std::string& message) {
                                     if (alive.lock()) {
                                         report(YModemFailed{message});
                                     }
                                 },
                             });
            return;
        }
        ymodem_->start(std::move(files));
    });
}

// ---- writing -------------------------------------------------------------------------------

// connection.write(): through the controller's write filter.
void Controller::writeFiltered(std::string_view data) {
    if (!isOpen()) {
        return;
    }
    // "$13=n" (report inches) takes effect before the firmware confirms it.
    const std::string line(str::trim(data));
    if (!line.empty()) {
        static const boost::regex kSetting(R"(^(\$\d{1,3})=([\d.]+)$)");
        boost::smatch match;
        if (boost::regex_match(line, match, kSetting) && match[1] == "$13") {
            const double value = js::stringToNumber(match[2].str());
            if (value >= 0 && value <= 65535) {
                runner_.setSetting("$13", value != 0 ? "1" : "0");
            }
        }
    }
    // Grbl never sees parenthesized comments.
    if (isGrbl()) {
        link_.send(stripParenComments(data), SendKind::Write);
    } else {
        link_.send(data, SendKind::Write);
    }
}

// connection.writeImmediate(): straight to the port.
void Controller::writeImmediate(std::string_view data) {
    if (!isOpen()) {
        return;
    }
    link_.send(data, SendKind::Immediate);
}

void Controller::write(std::string_view data) {
    if (!isOpen()) {
        return;
    }
    // A user-requested report is echoed to the console.
    const std::string_view command = str::trim(data);
    actionMask_.replyStatusReport =
        command == "?" || (isGrblHal() && command == "\x87") || actionMask_.replyStatusReport;
    actionMask_.replyParserState = command == "$G" || actionMask_.replyParserState;
    writeFiltered(data);
}

void Controller::writeln(std::string_view data, bool emitWrite) {
    if (isRealtimeCommand(firmware_, data)) {
        write(data);
    } else {
        write(std::string(data) + "\n");
    }
    // Only grblHAL acts on the emit flag.
    if (emitWrite && isGrblHal()) {
        report(ConsoleInput{std::string(data) + "\n", WriteSource::Feeder});
    }
}

void Controller::writeConsoleLine(std::string_view data) {
    consoleInput_ = std::string(data);
    // Console input produces an ok, and a jog stream consumes its own acks:
    // it has to give up the link first.
    jogStreamer_->abort("writeln");
    writeln(data);
}

void Controller::updateSpindleModal(const std::string& modal) {
    runner_.setSpindleModal(modal);
    emitState();
}

}  // namespace gs::controller
