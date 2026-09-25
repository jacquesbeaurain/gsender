// Ports of src/server/controllers/__tests__/{controllerCommands,
// controllerFileLifecycle,controllerRunnerEvents,controllerWorkflow}.test.js and
// Grblhal/__tests__/{GrblHalAxsProbe,GrblHalReplyParserState}.test.js.
//
// The JavaScript tests spy on internals (sender.next, event.trigger); here the
// real Sender/Feeder/Workflow run against a fake link and simulated time, and
// the assertions are on what reaches the wire and the reported events.

#include "gs/controller/controller.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::controller;
using protocol::Firmware;

namespace {

constexpr const char* kIdle = "<Idle|MPos:1.000,2.000,3.000|FS:0,0>";
constexpr const char* kJob = "G21\nG90\nG1 X10 Y20 Z-2 F300\nM3 S12000\nG2 X20 Y20 I5 J0\nG1 X30";

class FakeLink final : public DeviceLink {
public:
    bool open = true;
    std::vector<std::pair<std::string, SendKind>> sends;

    bool isOpen() const override { return open; }
    void send(std::string_view bytes, SendKind kind) override { sends.emplace_back(std::string(bytes), kind); }

    std::vector<std::string> of(SendKind kind) const {
        std::vector<std::string> out;
        for (const auto& [bytes, k] : sends) {
            if (k == kind) {
                out.push_back(bytes);
            }
        }
        return out;
    }
};

bool has(const std::vector<std::string>& items, std::string_view item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

std::vector<std::string> bytes(std::initializer_list<int> values) {
    std::vector<std::string> out;
    for (int v : values) {
        out.emplace_back(1, static_cast<char>(v));
    }
    return out;
}

std::vector<std::string> repeated(int value, int count) {
    return std::vector<std::string>(static_cast<std::size_t>(count), std::string(1, static_cast<char>(value)));
}

// A controller on a fake link, with recorded events and configurable hooks.
class Harness {
protected:
    void make(Firmware firmware) {
        controller_.reset();
        link.sends.clear();
        events.clear();
        ControllerHooks hooks;
        hooks.findMacro = [this](std::string_view id) -> std::optional<Macro> {
            for (const Macro& macro : macros) {
                if (macro.id == id) {
                    return macro;
                }
            }
            return std::nullopt;
        };
        hooks.findEvent = [this](std::string_view key) -> std::optional<EventConfig> {
            lookups.emplace_back(key);
            if (auto it = eventConfigs.find(std::string(key)); it != eventConfigs.end()) {
                return it->second;
            }
            return std::nullopt;
        };
        hooks.unloadFile = [this] { ++unloads; };
        hooks.preferences = prefs;
        controller_ = std::make_unique<Controller>(loop, link, firmware, hooks,
                                                   [this](const ControllerEvent& e) { events.push_back(e); });
        // Background polling is exercised by its own tests.
        controller_->setPollingEnabled(false);
    }

    Controller& c() { return *controller_; }
    void line(std::string_view text) { controller_->receiveLine(text); }
    std::vector<std::string> writes() const { return link.of(SendKind::Write); }
    std::vector<std::string> immediates() const { return link.of(SendKind::Immediate); }
    std::vector<std::string> queued() const { return controller_->feeder().queuedCommands(); }
    bool looked(std::string_view key) const { return has(lookups, key); }

    template <class T>
    std::vector<T> eventsOf() const {
        std::vector<T> out;
        for (const ControllerEvent& e : events) {
            if (const T* p = std::get_if<T>(&e)) {
                out.push_back(*p);
            }
        }
        return out;
    }

    std::vector<std::string> console() const {
        std::vector<std::string> out;
        for (const auto& e : eventsOf<ConsoleOutput>()) {
            out.push_back(e.text);
        }
        return out;
    }

    void enableEvent(const std::string& key, const std::string& commands) {
        eventConfigs[key] = EventConfig{key, "gcode", commands, true};
    }

    // Settings and status that let a jog stream start.
    void prepareJog() {
        for (const char* setting : {"$13=0", "$20=0", "$110=5000", "$111=5000", "$112=1000", "$120=500", "$121=500",
                                    "$122=100"}) {
            line(setting);
        }
        line("<Idle|MPos:0.000,0.000,0.000|Bf:15,128|FS:0,0>");
    }

    runtime::ManualEventLoop loop;
    FakeLink link;
    std::shared_ptr<Preferences> prefs = std::make_shared<Preferences>();
    std::map<std::string, EventConfig> eventConfigs;
    std::vector<std::string> lookups;
    std::vector<Macro> macros;
    int unloads = 0;
    std::vector<ControllerEvent> events;

private:
    std::unique_ptr<Controller> controller_;
};

class ControllerTest : public ::testing::TestWithParam<Firmware>, protected Harness {
protected:
    void SetUp() override { make(GetParam()); }
    bool grbl() const { return GetParam() == Firmware::Grbl; }
};

INSTANTIATE_TEST_SUITE_P(Firmwares, ControllerTest, ::testing::Values(Firmware::Grbl, Firmware::GrblHal),
                         [](const auto& test) { return test.param == Firmware::Grbl ? "Grbl" : "GrblHal"; });

// ---- command dispatch --------------------------------------------------------------

TEST_P(ControllerTest, SimpleCommandsWriteTheirProtocolBytesAndTriggerEvents) {
    c().statusReport();
    EXPECT_EQ(writes(), std::vector<std::string>{"?"});
    EXPECT_TRUE(lookups.empty());

    const auto check = [&](auto command, const std::string& wire, const char* event) {
        link.sends.clear();
        lookups.clear();
        command();
        EXPECT_EQ(writes(), std::vector<std::string>{wire});
        if (event) {
            EXPECT_TRUE(looked(event)) << event;
        } else {
            EXPECT_TRUE(lookups.empty());
        }
    };
    check([&] { c().feedHold(); }, "!", "feedhold");
    check([&] { c().cycleStart(); }, "~", "cyclestart");
    check([&] { c().sleep(); }, "$SLP\n", "sleep");
    check([&] { c().populateConfig(); }, "$$\n", nullptr);
}

TEST_P(ControllerTest, HomingWritesHomeAndReportsTheHomeState) {
    c().home('X');
    EXPECT_EQ(writes(), std::vector<std::string>{grbl() ? "$H\n" : "$HX\n"});
    EXPECT_EQ(c().state().status.activeState, "Home");
    EXPECT_TRUE(looked("homing"));
    EXPECT_EQ(eventsOf<StateChanged>().size(), 1u);

    link.sends.clear();
    c().home();
    EXPECT_EQ(writes(), std::vector<std::string>{"$H\n"});
}

TEST_P(ControllerTest, RapidOverrideSendsOnlySupportedValues) {
    const std::vector<std::pair<int, std::string>> cases{{0, "\x95"}, {100, "\x95"}, {50, "\x96"}, {25, "\x97"}, {75, ""}};
    for (const auto& [value, wire] : cases) {
        link.sends.clear();
        c().rapidOverride(value);
        EXPECT_EQ(writes(), (wire.empty() ? std::vector<std::string>{} : std::vector<std::string>{wire})) << value;
    }
}

TEST_P(ControllerTest, OverridesAreRealtimeBytesSpacedBy25ms) {
    struct Case {
        bool feed;
        int value;
        std::vector<std::string> expected;
    };
    const std::vector<Case> cases{
        {true, 100, bytes({0x90})},
        {true, 112, bytes({0x91, 0x93, 0x93})},
        {true, 89, bytes({0x92, 0x94})},
        {false, 100, bytes({0x99})},
        {false, 112, bytes({0x9A, 0x9C, 0x9C})},
        {false, 89, bytes({0x9B, 0x9D})},
        {false, 0, repeated(0x9B, 9)},     // clamped to 10%
        {false, 250, repeated(0x9A, 13)},  // clamped to 230%
    };
    for (const Case& test : cases) {
        make(GetParam());
        c().sender().setEstimatedTime(112);
        if (test.feed) {
            c().feedOverride(test.value);
        } else {
            c().spindleOverride(test.value);
        }
        EXPECT_TRUE(immediates().empty());
        loop.advance(24);
        EXPECT_TRUE(immediates().empty());
        loop.advance(1);
        EXPECT_EQ(immediates().size(), 1u);
        loop.advance(1000);
        EXPECT_EQ(immediates(), test.expected) << (test.feed ? "feed " : "spindle ") << test.value;
        if (test.feed) {
            EXPECT_EQ(c().sender().status().ovF, test.value);
            EXPECT_NEAR(c().sender().status().remainingTime, 112 / (test.value / 100.0), 1e-9);
        }
    }
}

TEST_P(ControllerTest, GcodeSplitsLinesAndQueuesBehindAPendingLine) {
    // The feeder is pending while lines wait behind the one in flight.
    c().gcode(std::vector<std::string>{"G0 X0", "G0 X1"});
    EXPECT_EQ(writes(), std::vector<std::string>{"G0 X0\n"});
    c().gcode(std::vector<std::string>{"G1 X1\r\n\nG1 X2", "   ", "G1 X3"});
    EXPECT_EQ(queued(), (std::vector<std::string>{"G0 X1", "G1 X1", "G1 X2", "G1 X3"}));
    EXPECT_EQ(writes().size(), 1u);
}

TEST_P(ControllerTest, FeederSendsOneLinePerAcknowledgment) {
    c().feederFeed({"G1 X1 F100", "G1 X2"});
    EXPECT_EQ(writes(), std::vector<std::string>{"G1 X1 F100\n"});
    EXPECT_EQ(c().feeder().outstanding(), 1);
    line("ok");
    EXPECT_EQ(writes(), (std::vector<std::string>{"G1 X1 F100\n", "G1 X2\n"}));
    EXPECT_EQ(c().feeder().outstanding(), 1);
}

TEST_P(ControllerTest, GcodeSafeRunsInThePreferredUnitsAndRestoresTheDevices) {
    line("[GC:G0 G54 G17 G20 G90 G94 M5 M9 T0 F0 S0]");
    c().feeder().hold();
    c().gcodeSafe({"G1 X1"}, "G21");
    EXPECT_EQ(queued(), (std::vector<std::string>{"G21", "G1 X1", "G20"}));

    c().feeder().clear();
    line("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]");
    c().gcodeSafe({"G1 X1"}, "G21");
    EXPECT_EQ(queued(), std::vector<std::string>{"G1 X1"});
}

TEST_P(ControllerTest, LaserAndSpindleCommandsQueueTheirOutput) {
    c().feeder().hold();
    c().laserTestOff();
    EXPECT_EQ(queued(), std::vector<std::string>{"M5S0"});
    c().feeder().clear();
    c().laserPowerChange(25, 800);
    EXPECT_EQ(queued(), std::vector<std::string>{"S200"});
    c().feeder().clear();
    c().spindleSpeedChange(12000);
    EXPECT_EQ(queued(), std::vector<std::string>{"S12000"});
}

TEST_P(ControllerTest, LaserTestUsesTheFirmwaresMaximumSpindleSetting) {
    line("$30=800");
    line("$730=1200");
    const std::string power = grbl() ? "200.00" : "300.00";
    c().feeder().hold();
    c().laserTestOn(25, 0);
    EXPECT_EQ(queued(), std::vector<std::string>{"G1F1 M3 S" + power});
    c().feeder().clear();
    c().laserTestOn(25, 2);
    EXPECT_EQ(queued(), (std::vector<std::string>{"G1F1 M3 S" + power, "G4P2", "M5 S0"}));
    EXPECT_EQ(c().state().parserState.modal.spindle, "M3");
}

TEST_P(ControllerTest, MacrosRunThroughTheFeederAndLoadIntoTheSender) {
    macros.push_back({"m", "Probe", "G1 X1"});
    c().feeder().hold();
    EXPECT_FALSE(c().runMacro("missing"));
    EXPECT_TRUE(queued().empty());
    EXPECT_FALSE(looked("macro:run"));

    EXPECT_TRUE(c().runMacro("m"));
    EXPECT_EQ(queued(), std::vector<std::string>{"G1 X1"});
    EXPECT_TRUE(looked("macro:run"));

    EXPECT_FALSE(c().loadMacro("missing").has_value());
    expr::Value context = expr::Value::object();
    context.set("offset", expr::Value(3));
    const auto loaded = c().loadMacro("m", context);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->ok);
    EXPECT_EQ(loaded->status.name, "Probe");
    EXPECT_EQ(c().sender().name(), "Probe");
    EXPECT_EQ(expr::toNumber(c().sender().context().get("offset")), 3);
    EXPECT_TRUE(looked("macro:load"));
}

TEST_P(ControllerTest, UnloadStopsTheJobAndNotifies) {
    c().sender().load("job.nc", "G1 X1");
    c().workflow().start();
    c().unloadProgram();
    EXPECT_TRUE(c().workflow().isIdle());
    EXPECT_EQ(c().sender().name(), "");
    EXPECT_EQ(c().sender().total(), 0u);
    EXPECT_EQ(unloads, 1);
    EXPECT_EQ(eventsOf<FileUnloaded>().size(), 1u);
    EXPECT_TRUE(looked("file:unload"));
}

TEST_P(ControllerTest, ToolChangeContextIsReplacedOnGrblAndKeepsMappingsOnGrblHal) {
    ToolChangeContext first;
    first.preHook = "M5";
    first.mappings = std::map<std::string, std::string>{{"1", "2"}};
    c().setToolChangeContext(first);
    ToolChangeContext second;
    second.postHook = "M3";
    c().setToolChangeContext(second);
    EXPECT_EQ(c().toolChangeContext().postHook, "M3");
    if (grbl()) {
        EXPECT_FALSE(c().toolChangeContext().mappings.has_value());
    } else {
        EXPECT_EQ(c().toolChangeContext().mappings, (std::map<std::string, std::string>{{"1", "2"}}));
    }
}

TEST_P(ControllerTest, ToolChangeHooksQueueTheirBlocksAndCompletionMarkers) {
    ToolChangeContext context;
    context.preHook = "M5";
    context.postHook = "M3";
    c().setToolChangeContext(context);
    c().feeder().hold();
    c().toolChangePre();
    EXPECT_EQ(queued(), (std::vector<std::string>{"G4 P1", "M5", "%pre_complete ;"}));
    c().feeder().clear();
    c().toolChangePost();
    EXPECT_EQ(writes(), std::vector<std::string>{"~"});
    EXPECT_EQ(queued(), (std::vector<std::string>{"G4 P1", "M3", "%toolchange_complete"}));
}

TEST_P(ControllerTest, PositionsInExpressionsAreNumbers) {
    // populateContext() gives positions as toFixed(3) strings, but
    // evaluate-expression turns numeric identifiers into numbers, so stored
    // and printed back they lose the padding: "X5", not "X5.000".
    line("<Idle|MPos:5.000,0.000,-2.000|FS:0,0|WCO:0.000,0.000,0.000>");
    c().gcode(std::vector<std::string>{"%global.t.X=posx", "G0 X[global.t.X] Z[posz]"});
    for (int i = 0; i < 3; ++i) {
        line("ok");
    }
    EXPECT_TRUE(has(writes(), "G0 X5 Z-2\n")) << ::testing::PrintToString(writes());
}

TEST_P(ControllerTest, WizardStartWaitsForIdleAndAStepCompletesOnce) {
    c().feeder().hold();
    c().wizardStart("G1 X1");
    loop.advance(1000);
    EXPECT_TRUE(queued().empty());
    line(kIdle);
    loop.advance(200);
    EXPECT_EQ(queued(), std::vector<std::string>{"G1 X1"});

    c().wizardStep(2, 3);
    c().feeder().onComplete();
    c().feeder().onComplete();
    const auto next = eventsOf<WizardNext>();
    ASSERT_EQ(next.size(), 1u);
    EXPECT_EQ(next[0].step, 2);
    EXPECT_EQ(next[0].substep, 3);
}

TEST_P(ControllerTest, EstimateDataReachesTheSender) {
    c().updateEstimateData({1, 2}, 3);
    EXPECT_EQ(c().sender().status().estimatedTime, 3);
    EXPECT_EQ(c().sender().status().remainingTime, 3);
}

TEST_P(ControllerTest, JogStopAndCancelSendTheJogCancelByte) {
    c().jogStop();
    EXPECT_EQ(writes(), std::vector<std::string>{"\x85"});
    link.sends.clear();
    c().jogCancel();
    EXPECT_EQ(writes(), std::vector<std::string>{"\x85"});
}

TEST_P(ControllerTest, AConflictingCommandEndsAnActiveJogBeforeWriting) {
    prepareJog();
    ASSERT_TRUE(c().jogStreamer().start({1, 0, 0, 0}, 1000));
    link.sends.clear();
    c().statusReport();  // stream-safe
    EXPECT_TRUE(c().jogStreamer().isActive());
    EXPECT_EQ(writes(), std::vector<std::string>{"?"});

    link.sends.clear();
    c().populateConfig();
    EXPECT_FALSE(c().jogStreamer().isActive());
    EXPECT_EQ(writes(), (std::vector<std::string>{"\x85", "$$\n"}));
}

TEST_P(ControllerTest, JogErrorsAbortOnlyTheJog) {
    prepareJog();
    c().jogStart({1, 0, 0, 0});
    ASSERT_TRUE(c().jogStreamer().isActive());
    line("error:2");
    EXPECT_FALSE(c().jogStreamer().isActive());
    EXPECT_TRUE(c().workflow().isIdle());
    const auto errors = eventsOf<ErrorReported>();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].origin, "Jog");
    EXPECT_EQ(errors[0].line, "jog");
    EXPECT_EQ(errors[0].code, "2");
    EXPECT_TRUE(eventsOf<GcodeError>().empty());
    EXPECT_TRUE(has(console(), "error:2"));
}

// ---- file loading and start-from-line --------------------------------------------------

TEST_P(ControllerTest, LoadingAppendsTheFirmwareSuffixAndStopsTheWorkflow) {
    c().workflow().start();
    expr::Value context = expr::Value::object();
    context.set("offset", expr::Value(7));
    const auto result = c().loadProgram("job.nc", kJob, context);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.status, c().sender().status());
    EXPECT_EQ(c().sender().name(), "job.nc");
    EXPECT_EQ(expr::toNumber(c().sender().context().get("offset")), 7);
    const std::size_t total = c().sender().total();
    EXPECT_EQ(total, grbl() ? 7u : 6u);
    EXPECT_EQ(c().sender().line(total - 1), grbl() ? "%wait ; Wait for the planner to empty" : "G1 X30");
    EXPECT_TRUE(c().workflow().isIdle());
    EXPECT_TRUE(writes().empty());
}

TEST_P(ControllerTest, RefreshingAFileIsSkippedWhileAJobIsActive) {
    for (const char* state : {"idle", "running", "paused"}) {
        make(GetParam());
        c().sender().load("old.nc", "G1 X1");
        if (std::string(state) != "idle") {
            c().workflow().start();
        }
        if (std::string(state) == "paused") {
            c().workflow().pause();
        }
        const WorkflowState before = c().workflow().state();
        c().loadFile("job.nc", kJob, true);
        EXPECT_EQ(c().sender().name(), std::string(state) == "idle" ? "job.nc" : "old.nc") << state;
        EXPECT_EQ(c().workflow().state(), std::string(state) == "idle" ? WorkflowState::Idle : before) << state;
    }
}

TEST_P(ControllerTest, SpindleDelayRespectsTheFirmwareVersion) {
    for (const char* semver : {"20240101", "20250627"}) {
        make(GetParam());
        prefs->spindleDelay = 2;
        line(std::string("[VER:1.1f.") + semver + ":]");
        c().loadFile("job.nc", "M3 S12000\nG1 X1 F100");
        const bool delayed = grbl() || std::string(semver) < "20250627";
        EXPECT_EQ(c().sender().line(0), delayed ? "M3 S12000 G4 P2" : "M3 S12000") << semver;
    }
}

TEST_P(ControllerTest, AnUnrelatedDwellSuppressesOnlyGrblsDelayInsertion) {
    prefs->spindleDelay = 2;
    line("[VER:1.1f.20240101:]");
    c().loadFile("job.nc", "G4 P1\nM3 S12000");
    EXPECT_EQ(c().sender().line(1), grbl() ? "M3 S12000" : "M3 S12000 G4 P2");
}

TEST_P(ControllerTest, LoadingAJobClearsToolMappingsOnlyOnGrblHal) {
    ToolChangeContext context;
    context.mappings = std::map<std::string, std::string>{{"1", "2"}};
    context.postHook = "M3";
    c().setToolChangeContext(context);
    c().loadFile("job.nc", kJob);
    EXPECT_EQ(c().toolChangeContext().postHook, "M3");
    EXPECT_EQ(c().toolChangeContext().mappings,
              (grbl() ? std::map<std::string, std::string>{{"1", "2"}} : std::map<std::string, std::string>{}));
}

TEST_P(ControllerTest, GrblClassifiesRotaryFiles) {
    const std::vector<std::pair<const char*, std::optional<ProgramFileType>>> cases{
        {"G1 A10 Y20", ProgramFileType::FourAxis},
        {"G1 A10", ProgramFileType::Rotary},
        {"G1 X1 (A10 Y20)", std::nullopt},
    };
    for (const auto& [gcode, type] : cases) {
        make(GetParam());
        c().loadFile("job.nc", gcode);
        const auto detected = eventsOf<FileTypeDetected>();
        if (grbl() && type) {
            ASSERT_EQ(detected.size(), 1u) << gcode;
            EXPECT_EQ(detected[0].type, *type) << gcode;
        } else {
            EXPECT_TRUE(detected.empty()) << gcode;
        }
    }
}

TEST_P(ControllerTest, StartFromLineRestoresArcAndSpindleState) {
    c().sender().load("job.nc", kJob);
    c().feeder().hold();
    c().start({.lineToStartFrom = 5, .zMax = 10, .safeHeight = 3});
    std::vector<std::string> expected{"%global.state.workspace=modal.wcs", "G0 G90 G21 Z13",
                                      grbl() ? "G21 M3 F300 S12000" : "M3 S12000", "G0 G90 G21 X20.000 Y20.000",
                                      "G0 G90 G21 Z-2.000", "G21 G90 G91.1 G54 G17  "};
    if (grbl()) {
        expected.insert(expected.end(), {"G2", "G4 P0"});
    } else {
        expected.insert(expected.end(), {"F300", "G2 X20.000 J0 F300"});
    }
    expected.emplace_back("%_GCODE_START");
    EXPECT_EQ(queued(), expected);
    EXPECT_EQ(c().sender().sent(), 5u);
    EXPECT_EQ(c().sender().received(), 5u);
    EXPECT_TRUE(c().workflow().isIdle());
    EXPECT_TRUE(writes().empty());
    const auto started = eventsOf<JobStarted>();
    ASSERT_EQ(started.size(), 1u);
    EXPECT_TRUE(started[0].fromLine);
}

TEST_P(ControllerTest, StartFromLineSuppliesAFallbackFeed) {
    for (const auto& [units, feed] : std::vector<std::pair<std::string, std::string>>{{"G21", "200"}, {"G20", "8"}}) {
        make(GetParam());
        c().sender().load("job.nc", units + "\nG1 X1\nG1 X2");
        c().feeder().hold();
        c().start({.lineToStartFrom = 2, .zMax = 10});
        EXPECT_TRUE(has(queued(), units + " F" + feed)) << units;
        if (!grbl()) {
            EXPECT_TRUE(has(queued(), "F" + feed)) << units;
        }
        EXPECT_TRUE(has(queued(), "G0 G90 G21 Z20")) << units;
    }
}

TEST_P(ControllerTest, StartFromLineKeepsAnExplicitWorkspaceOrRestoresTheSelectedOne) {
    for (const std::string wcs : {"G54", "G56"}) {
        make(GetParam());
        line("[GC:G0 G55 G17 G21 G90 G94 M5 M9 T0 F0 S0]");
        c().sender().load("job.nc", wcs + "\nG1 X1 F100\nG1 X2");
        c().feeder().hold();
        c().start({.lineToStartFrom = 2, .zMax = 10});
        EXPECT_TRUE(has(queued(), "G21 G90 G91.1 " + std::string(wcs == "G54" ? "G55" : "G56") + " G17  ")) << wcs;
    }
}

TEST_P(ControllerTest, StartFromLineUsesThePerCommandSpindleDelayOnlyOnGrbl) {
    prefs->spindleDelay = 2;
    c().sender().load("job.nc", kJob);
    c().feeder().hold();
    c().start({.lineToStartFrom = 5, .zMax = 10, .safeHeight = 3, .spindleDelay = 7});
    EXPECT_TRUE(has(queued(), grbl() ? "G4 P7" : "G4 P2"));
}

TEST_P(ControllerTest, AStartLineOutOfRangeStartsAtTheTop) {
    for (std::size_t start : {std::size_t{0}, std::size_t{99}}) {
        make(GetParam());
        c().sender().load("job.nc", kJob);
        c().start({.lineToStartFrom = start, .zMax = 10});
        EXPECT_TRUE(c().workflow().isRunning()) << start;
        ASSERT_FALSE(writes().empty());
        EXPECT_EQ(writes()[0], "G21\n") << start;
        const auto started = eventsOf<JobStarted>();
        ASSERT_EQ(started.size(), 1u);
        EXPECT_FALSE(started[0].fromLine);
    }
}

TEST_P(ControllerTest, StartFromLineKeepsRotaryPositionModulo360AndBothCoolants) {
    c().sender().load("job.nc", "G21\nG1 A450 F100\nM7\nM8\nG1 X1");
    c().feeder().hold();
    c().start({.lineToStartFrom = 4, .zMax = 10});
    EXPECT_TRUE(has(queued(), "G0 G90 G21 A90.000"));
    EXPECT_TRUE(has(queued(), "G21 G90 G91.1 G54 G17 M8 M7"));
}

TEST_P(ControllerTest, StartFromLineRestoresTheToolOnlyWithAtcAndARealToolChange) {
    struct Case {
        bool atc, m6, mapping;
    };
    for (const Case test : {Case{false, true, true}, Case{true, false, true}, Case{true, true, false},
                            Case{true, true, true}}) {
        make(GetParam());
        line(std::string("[NEWOPT:ENUMS,ATC=") + (test.atc ? "1" : "0") + "]");
        ToolChangeContext context;
        context.mappings = test.mapping ? std::map<std::string, std::string>{{"2", "7"}}
                                        : std::map<std::string, std::string>{};
        c().setToolChangeContext(context);
        c().sender().load("job.nc", std::string("T2\n") + (test.m6 ? "M6" : "G90") + "\nG1 X1 F100\nG1 X2");
        c().feeder().hold();
        c().start({.lineToStartFrom = 3, .zMax = 10});
        std::vector<std::string> toolChanges;
        for (const std::string& command : queued()) {
            if (command.starts_with("M6")) {
                toolChanges.push_back(command);
            }
        }
        const bool expected = !grbl() && test.atc && test.m6;
        EXPECT_EQ(toolChanges,
                  expected ? std::vector<std::string>{test.mapping ? "M6 T7" : "M6 T2"} : std::vector<std::string>{})
            << test.atc << test.m6 << test.mapping;
    }
}

// ---- runner events -----------------------------------------------------------------------

TEST_P(ControllerTest, UnhandledLinesGoToTheConsole) {
    for (const char* text : {"[G54:1.000,2.000,3.000]", "[MSG:Hello]", "unrecognized output"}) {
        line(text);
        EXPECT_TRUE(has(console(), text)) << text;
    }
}

TEST_P(ControllerTest, AParserStateAwaitsItsOkAndIsEchoedOnlyOnRequest) {
    const char* state = "[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]";
    c().actionMask().queryParserStateState = true;
    line(state);
    EXPECT_FALSE(c().actionMask().queryParserStateState);
    EXPECT_TRUE(c().actionMask().queryParserStateReply);
    EXPECT_FALSE(has(console(), state));
    c().actionMask().replyParserState = true;
    line(state);
    EXPECT_TRUE(has(console(), state));
}

TEST_P(ControllerTest, TheReceiveBufferGrowsOnlyWhileIdle) {
    for (const char* state : {"idle", "running", "paused"}) {
        make(GetParam());
        if (std::string(state) != "idle") {
            c().workflow().start();
        }
        if (std::string(state) == "paused") {
            c().workflow().pause();
        }
        c().sender().setBufferSize(100);
        c().actionMask().queryStatusReport = true;
        c().actionMask().replyStatusReport = true;
        const char* status = "<Run|MPos:0,0,0|Bf:15,512|FS:0,0>";
        line(status);
        EXPECT_EQ(c().sender().bufferSize(), std::string(state) == "idle" ? 504 : 100) << state;
        EXPECT_FALSE(c().actionMask().queryStatusReport);
        EXPECT_FALSE(c().actionMask().replyStatusReport);
        EXPECT_TRUE(has(console(), status)) << state;
    }
}

TEST_P(ControllerTest, TheReceiveBufferNeitherGrowsWithBytesInFlightNorShrinks) {
    c().sender().load("job.nc", "G1 X1\nG1 X2");
    c().sender().setBufferSize(100);
    c().sender().next();  // bytes in flight, workflow idle
    line("<Run|MPos:0,0,0|Bf:15,512|FS:0,0>");
    EXPECT_EQ(c().sender().bufferSize(), 100);

    make(GetParam());
    c().sender().setBufferSize(256);
    line("<Run|MPos:0,0,0|Bf:15,128|FS:0,0>");
    EXPECT_EQ(c().sender().bufferSize(), 256);
}

TEST_P(ControllerTest, StreamedLinesAreTrimmedAndNeedAnOpenLink) {
    c().sender().onData("  G1 X1  ");
    c().feeder().onData("  G1 X2  ", expr::Value::object());
    EXPECT_EQ(writes(), (std::vector<std::string>{"G1 X1\n", "G1 X2\n"}));
    c().sender().onData("  ");
    c().feeder().onData("  ", expr::Value::object());
    EXPECT_EQ(writes().size(), 2u);
    link.open = false;
    c().sender().onData("G1 X3");
    c().feeder().onData("G1 X4", expr::Value::object());
    EXPECT_EQ(writes().size(), 2u);
    // Both are echoed to the console as feeder input.
    EXPECT_EQ(eventsOf<ConsoleInput>().size(), 2u);
}

TEST_P(ControllerTest, SenderLifecycleRecordsTheFinishTimeAndRequestsEstimates) {
    c().sender().onEnd(300);
    EXPECT_EQ(c().senderFinishTime(), 300);
    c().sender().onStart(100);
    EXPECT_EQ(c().senderFinishTime(), 0);
    c().sender().onEnd(500);
    EXPECT_EQ(c().senderFinishTime(), 500);
    c().sender().onRequestData();
    EXPECT_EQ(eventsOf<EstimateDataRequested>().size(), 1u);
}

TEST_P(ControllerTest, StreamingStopsWhilePausedAndResumesTheRemainingLines) {
    c().sender().load("job.nc", "G1 X1 F100\nG1 X2\nG1 X3");
    c().sender().setBufferSize(12);
    c().workflow().start();
    c().sender().next();
    EXPECT_EQ(writes(), std::vector<std::string>{"G1 X1 F100\n"});
    c().workflow().pause();
    line("ok");
    EXPECT_EQ(c().sender().received(), 1u);
    EXPECT_EQ(writes().size(), 1u);
    c().workflow().resume();
    EXPECT_EQ(writes(), (std::vector<std::string>{"G1 X1 F100\n", "G1 X2\n"}));
    line("ok");
    EXPECT_EQ(writes(), (std::vector<std::string>{"G1 X1 F100\n", "G1 X2\n", "G1 X3\n"}));
}

TEST_P(ControllerTest, AnErrorDuringAJobPausesIt) {
    for (bool showLineWarnings : {false, true}) {
        make(GetParam());
        prefs->showLineWarnings = showLineWarnings;
        if (!grbl()) {
            line("[ERRORCODE:2||Bad number format]");
        }
        c().sender().load("job.nc", "G1 Xbad\nG1 X2");
        c().sender().setBufferSize(10);  // only the first line fits
        c().workflow().start();
        c().sender().next();
        ASSERT_EQ(c().sender().sent(), 1u);
        link.sends.clear();

        line("error:2");
        EXPECT_TRUE(c().workflow().isPaused());
        EXPECT_TRUE(c().sender().isHeld());
        EXPECT_EQ(c().sender().received(), 1u);
        const auto errors = eventsOf<ErrorReported>();
        ASSERT_EQ(errors.size(), 1u);
        EXPECT_FALSE(errors[0].isAlarm);
        EXPECT_EQ(errors[0].code, "2");
        EXPECT_EQ(errors[0].origin, "job.nc");
        EXPECT_EQ(errors[0].line, "G1 Xbad");
        EXPECT_EQ(errors[0].lineNumber, std::optional<std::size_t>(0));
        EXPECT_EQ(errors[0].firmware, GetParam());
        EXPECT_EQ(errors[0].jobRunning, !grbl());
        // grblHAL stops the machine too.
        EXPECT_EQ(writes(), (grbl() ? std::vector<std::string>{} : std::vector<std::string>{"!"}));
        EXPECT_EQ(immediates(), (grbl() ? std::vector<std::string>{} : std::vector<std::string>{"\n"}));
        if (showLineWarnings) {
            bool reported = false;
            for (const auto& changed : eventsOf<WorkflowChanged>()) {
                reported = reported || changed.invalidLine == std::optional<std::string>("2 G1 Xbad");
            }
            EXPECT_TRUE(reported);
            EXPECT_TRUE(eventsOf<GcodeError>().empty());
        } else {
            const auto gcodeErrors = eventsOf<GcodeError>();
            ASSERT_EQ(gcodeErrors.size(), 1u);
            EXPECT_EQ(gcodeErrors[0].message, "Error 2 on line 0 - Bad number format");
        }
    }
}

TEST_P(ControllerTest, ConsoleErrorsKeepTheirOriginOnce) {
    c().writeConsoleLine("G1 Xbad");
    line("error:2");
    line("error:2");
    const auto errors = eventsOf<ErrorReported>();
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0].origin, "Console");
    EXPECT_EQ(errors[0].line, "G1 Xbad");
    EXPECT_EQ(errors[1].origin, "Feeder");
    EXPECT_EQ(errors[1].line, "N/A");
}

TEST_P(ControllerTest, AlarmReportsIdentifyTheJobAndStopGrblHalJobs) {
    c().sender().load("job.nc", "G1 X1\nG1 X2");
    c().workflow().start();
    if (!grbl()) {
        line("[ALARMCODE:1||Hard limit]");
    }
    line("ALARM:1");
    const auto errors = eventsOf<ErrorReported>();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(errors[0].isAlarm);
    EXPECT_EQ(errors[0].code, "1");
    EXPECT_EQ(errors[0].origin, "job.nc");
    EXPECT_EQ(errors[0].line, "G1 X1");
    EXPECT_EQ(errors[0].lineNumber, std::optional<std::size_t>(0));
    if (!grbl()) {
        EXPECT_EQ(errors[0].description, "Hard limit");
    }
    EXPECT_EQ(c().workflow().state(), grbl() ? WorkflowState::Running : WorkflowState::Idle);
}

// ---- workflow ----------------------------------------------------------------------------

TEST_P(ControllerTest, WorkflowStartRewindsTheSender) {
    c().sender().load("job.nc", "G1 X1\nG1 X2");
    c().sender().setStartLine(1);
    c().sender().hold();
    c().workflow().start();
    EXPECT_EQ(c().sender().sent(), 0u);
    EXPECT_FALSE(c().sender().isHeld());
    EXPECT_TRUE(c().workflow().isRunning());
    EXPECT_EQ(eventsOf<WorkflowChanged>().size(), 1u);
    c().workflow().start();
    EXPECT_EQ(eventsOf<WorkflowChanged>().size(), 1u);
}

TEST_P(ControllerTest, PauseHoldsTheSenderWithItsReason) {
    c().workflow().start();
    c().workflow().pause();
    EXPECT_TRUE(c().workflow().isPaused());
    EXPECT_TRUE(c().sender().isHeld());
    EXPECT_FALSE(c().sender().holdReason().has_value());
    EXPECT_EQ(c().timePaused(), loop.nowMs());

    make(GetParam());
    c().workflow().start();
    const HoldReason reason{"", "", "probe failure"};
    c().workflow().pause(reason);
    EXPECT_EQ(c().sender().holdReason(), std::optional<HoldReason>(reason));
}

TEST_P(ControllerTest, ResumeClearsTheFeederAndStreamsOn) {
    c().sender().load("job.nc", "G1 X1\nG1 X2");
    c().workflow().start();
    c().workflow().pause();
    c().feeder().feed({"G1 X9"});
    c().feeder().hold();
    loop.advance(250);
    c().workflow().resume();
    EXPECT_TRUE(queued().empty());
    EXPECT_FALSE(c().feeder().isHeld());
    EXPECT_FALSE(c().sender().isHeld());
    EXPECT_TRUE(c().workflow().isRunning());
    EXPECT_EQ(writes(), (std::vector<std::string>{"G1 X1\n", "G1 X2\n"}));
}

TEST_P(ControllerTest, StopResetsTheFeederAndRewindsTheSender) {
    c().sender().load("job.nc", "G1 X1\nG1 X2");
    c().workflow().start();
    c().sender().setStartLine(1);
    c().gcode(std::vector<std::string>{"G1 X9", "G1 X10"});
    ASSERT_EQ(c().feeder().outstanding(), 1);
    ASSERT_EQ(queued().size(), 1u);
    c().workflow().stop();
    EXPECT_EQ(c().sender().sent(), 0u);
    EXPECT_EQ(c().sender().received(), 0u);
    EXPECT_TRUE(queued().empty());
    EXPECT_EQ(c().feeder().outstanding(), 0);
    EXPECT_TRUE(c().workflow().isIdle());
}

TEST_P(ControllerTest, StartBeginsAtTheTopWithoutAHook) {
    c().sender().load("job.nc", "G1 X1 F100\nG1 X2");
    c().sender().setStartLine(1);
    c().start();
    EXPECT_TRUE(c().workflow().isRunning());
    EXPECT_EQ(writes(), (std::vector<std::string>{"G1 X1 F100\n", "G1 X2\n"}));
    const auto started = eventsOf<JobStarted>();
    ASSERT_EQ(started.size(), 1u);
    EXPECT_FALSE(started[0].fromLine);
    // "%global.state.workspace=modal.wcs" ran through the feeder.
    EXPECT_EQ(expr::toString(c().sharedContext().get("state").get("workspace")), "G54");
}

TEST_P(ControllerTest, AStartHookRunsBeforeTheJob) {
    enableEvent("gcode:start", "M3 S1000");
    c().sender().load("job.nc", "G1 X1 F100\nG1 X2");
    c().start();
    // The feeder completes as soon as the hook's last line is written (as
    // upstream), so the job follows right after it.
    EXPECT_EQ(writes(), (std::vector<std::string>{"M3 S1000\n", "G1 X1 F100\n", "G1 X2\n"}));
    EXPECT_TRUE(c().workflow().isRunning());
}

TEST_P(ControllerTest, PauseSendsFeedHoldAfter100ms) {
    c().workflow().start();
    c().pause();
    EXPECT_TRUE(c().sender().isHeld());
    EXPECT_TRUE(c().workflow().isPaused());
    EXPECT_TRUE(writes().empty());
    loop.advance(99);
    EXPECT_TRUE(writes().empty());
    loop.advance(1);
    EXPECT_EQ(writes(), std::vector<std::string>{"!"});
}

TEST_P(ControllerTest, APauseHookReplacesTheFeedHold) {
    enableEvent("gcode:pause", "M5");
    c().workflow().start();
    c().pause();
    loop.advance(100);
    EXPECT_TRUE(c().workflow().isPaused());
    EXPECT_EQ(writes(), std::vector<std::string>{"M5\n"});
}

TEST_P(ControllerTest, ResumeWaitsASecondAfterCycleStart) {
    c().workflow().start();
    c().workflow().pause();
    c().resume();
    EXPECT_EQ(writes(), std::vector<std::string>{"~"});
    loop.advance(999);
    EXPECT_TRUE(c().workflow().isPaused());
    loop.advance(1);
    EXPECT_TRUE(c().workflow().isRunning());
}

TEST_P(ControllerTest, AResumeHookRunsFirstUnlessIgnored) {
    enableEvent("gcode:resume", "M3 S1000");
    c().workflow().start();
    c().workflow().pause();
    c().resume();
    EXPECT_EQ(writes(), (std::vector<std::string>{"M3 S1000\n", "~"}));
    EXPECT_TRUE(c().workflow().isRunning());

    make(GetParam());
    lookups.clear();
    c().workflow().start();
    c().workflow().pause();
    c().resume(true);
    EXPECT_FALSE(looked("gcode:resume"));
    EXPECT_EQ(writes(), std::vector<std::string>{"~"});
    loop.advance(1000);
    EXPECT_TRUE(c().workflow().isRunning());
}

TEST_P(ControllerTest, StopWithoutForceSendsNothing) {
    c().workflow().start();
    c().stop();
    EXPECT_TRUE(c().workflow().isIdle());
    EXPECT_EQ(eventsOf<JobStopped>().size(), 1u);
    EXPECT_TRUE(looked("gcode:stop"));
    EXPECT_TRUE(writes().empty());
}

TEST_P(ControllerTest, AForcedStopResetsAndRestoresTheWorkspace) {
    for (const std::string wcs : {"G54", "G55"}) {
        make(GetParam());
        line("<Run|MPos:0.000,0.000,0.000|FS:0,0>");
        line("[GC:G1 " + wcs + " G17 G21 G90 G94 M5 M9 T0 F0 S0]");
        c().workflow().start();
        link.sends.clear();
        lookups.clear();
        c().stop(true);
        const std::vector<std::string> prefix = grbl() ? std::vector<std::string>{"!"}
                                                       : std::vector<std::string>{"\x19", "!"};
        EXPECT_EQ(writes(), prefix) << wcs;
        EXPECT_FALSE(looked("gcode:stop"));
        loop.advance(699);
        EXPECT_EQ(writes(), prefix);
        loop.advance(1);
        std::vector<std::string> expected = prefix;
        expected.emplace_back("\x18");
        EXPECT_EQ(writes(), expected) << wcs;
        if (wcs != "G54") {
            EXPECT_FALSE(looked("gcode:stop"));
            loop.advance(200);
            expected.emplace_back("G55\n");
            EXPECT_EQ(writes(), expected);
        }
        EXPECT_TRUE(looked("gcode:stop")) << wcs;
    }
}

TEST_P(ControllerTest, FeederStartResumesTheFeederOutsideAJob) {
    c().feeder().hold();
    c().feeder().feed({"G1 X1 F100"});
    c().feederStart();
    EXPECT_EQ(writes(), std::vector<std::string>{"~"});
    loop.advance(999);
    EXPECT_TRUE(c().feeder().isHeld());
    loop.advance(1);
    EXPECT_EQ(writes(), (std::vector<std::string>{"~", "G1 X1 F100\n"}));
    EXPECT_FALSE(c().feeder().isHeld());

    make(GetParam());
    c().workflow().start();
    c().feeder().hold();
    c().feeder().feed({"G1 X1 F100"});
    c().feederStart();
    loop.advance(1000);
    EXPECT_TRUE(writes().empty());
    EXPECT_TRUE(c().feeder().isHeld());
}

TEST_P(ControllerTest, FeederStopSoftStopsAndStopsTheJobOnGrblHal) {
    c().workflow().start();
    c().feeder().feed({"G1 X1"});
    c().feederStop();
    EXPECT_TRUE(queued().empty());
    EXPECT_EQ(c().workflow().state(), grbl() ? WorkflowState::Running : WorkflowState::Idle);
    std::vector<std::string> expected = grbl() ? std::vector<std::string>{} : std::vector<std::string>{"\x19"};
    EXPECT_EQ(writes(), expected);
    loop.advance(100);
    expected.emplace_back("~");
    EXPECT_EQ(writes(), expected);
}

TEST_P(ControllerTest, ResetStopsTheWorkflowClearsTheQueueAndSendsCtrlX) {
    c().workflow().start();
    c().feeder().feed({"G1 X1"});
    c().reset();
    EXPECT_TRUE(c().workflow().isIdle());
    EXPECT_TRUE(queued().empty());
    EXPECT_EQ(writes(), std::vector<std::string>{"\x18"});
}

TEST_P(ControllerTest, UnlockAddsASoftStopOnGrblHal) {
    c().feeder().feed({"G1 X1"});
    c().unlock();
    EXPECT_TRUE(queued().empty());
    EXPECT_EQ(writes(), (grbl() ? std::vector<std::string>{"$X\n"} : std::vector<std::string>{"$X\n", "\x19"}));
}

TEST_P(ControllerTest, ResetLimitUnlocksAfterItsDelayOnGrblHal) {
    c().resetLimit();
    EXPECT_EQ(writes(), (grbl() ? std::vector<std::string>{"\x18", "$X\n"} : std::vector<std::string>{"\x18"}));
    loop.advance(349);
    if (!grbl()) {
        EXPECT_EQ(writes(), std::vector<std::string>{"\x18"});
    }
    loop.advance(1);
    EXPECT_EQ(writes(), (std::vector<std::string>{"\x18", "$X\n"}));
    EXPECT_TRUE(immediates().empty());
    loop.advance(500);
    EXPECT_EQ(immediates(), (grbl() ? std::vector<std::string>{} : std::vector<std::string>{"\x87"}));
}

TEST_P(ControllerTest, OkGoesToTheSenderDuringAJobAndToTheFeederOtherwise) {
    for (const std::string state : {"idle", "running", "paused"}) {
        make(GetParam());
        c().sender().load("job.nc", "G1 X1\nG1 X2");
        if (state != "idle") {
            c().workflow().start();
        }
        c().sender().next();
        ASSERT_EQ(c().sender().sent(), 2u);
        if (state == "paused") {
            c().workflow().pause();
        }
        c().gcode("G0 X0");
        ASSERT_EQ(c().feeder().outstanding(), 1);
        line("ok");
        EXPECT_EQ(c().sender().received(), state == "idle" ? 0u : 1u) << state;
        EXPECT_EQ(c().feeder().outstanding(), state == "idle" ? 0 : 1) << state;
    }
}

TEST_P(ControllerTest, AParserQueryOkAdvancesNeitherQueue) {
    c().gcode("G0 X0");
    c().actionMask().queryParserStateReply = true;
    c().actionMask().replyParserState = true;
    line("ok");
    EXPECT_EQ(c().feeder().outstanding(), 1);
    EXPECT_FALSE(c().actionMask().queryParserStateReply);
    EXPECT_TRUE(has(console(), "ok"));
}

TEST_P(ControllerTest, TheJogStreamerConsumesItsOwnAcknowledgments) {
    prepareJog();
    c().jogStart({1, 0, 0, 0});
    ASSERT_TRUE(c().jogStreamer().isActive());
    const int consumed = c().jogStreamer().acksConsumed();
    line("ok");
    EXPECT_EQ(c().jogStreamer().acksConsumed(), consumed + 1);
    EXPECT_FALSE(has(console(), "ok"));
}

// ---- job scenarios ------------------------------------------------------------------------

TEST_P(ControllerTest, AToolChangeInAJobPausesAndAsksOnceTheMachineIsIdle) {
    ToolChangeContext context;
    context.option = "Pause";
    if (!grbl()) {
        context.mappings = std::map<std::string, std::string>{{"2", "5"}};
    }
    c().setToolChangeContext(context);
    c().sender().load("job.nc", "G1 X1\nM6 T2\nG1 X2");
    c().start();
    EXPECT_TRUE(c().workflow().isPaused());
    const std::string block = grbl() ? "(M6) T2" : "(M6) T5";
    // Grbl's write filter drops the parenthesized comment.
    EXPECT_EQ(writes(), (std::vector<std::string>{"G1 X1\n", grbl() ? " T2\n" : "(M6) T5\n"}));
    EXPECT_TRUE(eventsOf<ToolChangeRequested>().empty());

    line(kIdle);
    loop.advance(200);
    const auto requests = eventsOf<ToolChangeRequested>();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].line, 2u);
    EXPECT_EQ(requests[0].count, 1);
    EXPECT_EQ(requests[0].block, block);
    EXPECT_EQ(requests[0].tool, std::optional<std::string>(grbl() ? "T2" : "T5"));
    EXPECT_EQ(requests[0].option, "Pause");
    EXPECT_EQ(c().state().parserState.modal.tool, grbl() ? "2" : "5");
}

TEST_P(ControllerTest, AJobEndsOnceTheMachineIsIdleAndStill) {
    c().setPollingEnabled(true);
    c().sender().load("job.nc", "G1 X1");
    c().start();
    ASSERT_TRUE(c().workflow().isRunning());
    line("ok");
    EXPECT_GT(c().senderFinishTime(), 0);
    line(kIdle);
    loop.advance(500);
    EXPECT_TRUE(c().workflow().isRunning());  // not yet still for half a second
    loop.advance(1000);
    EXPECT_TRUE(c().workflow().isIdle());
    EXPECT_EQ(eventsOf<JobStopped>().size(), 1u);
}

// ---- grblHAL [AXS:] probing and reply echo -------------------------------------------------

class GrblHalControllerTest : public ::testing::Test, protected Harness {
protected:
    void SetUp() override { make(Firmware::GrblHal); }

    int probes() const {
        const auto all = writes();
        return static_cast<int>(std::count(all.begin(), all.end(), "$I\n"));
    }

    // The reply block a real board sends for $I, terminated by ok.
    void replyToI(const char* axs = nullptr) {
        line("[VER:1.1f.20230131:]");
        line("[OPT:VNMSL,35,1024,3,0]");
        if (axs) {
            line(axs);
        }
        line("ok");
    }
};

TEST_F(GrblHalControllerTest, FirmwareWithoutAxsIsProbedTwiceThenNeverAgain) {
    for (int tick = 0; tick < 300; ++tick) {
        loop.advance(2000);
        line(kIdle);
        replyToI();
    }
    EXPECT_EQ(probes(), 2);
}

TEST_F(GrblHalControllerTest, StartupMessagesDoNotRefillTheProbeBudget) {
    line(kIdle);
    EXPECT_EQ(probes(), 1);
    replyToI();  // its [VER:] clears the action values
    loop.advance(2000);
    line(kIdle);
    EXPECT_EQ(probes(), 2);
    replyToI();
    loop.advance(2000);
    line(kIdle);
    EXPECT_EQ(probes(), 2);
}

TEST_F(GrblHalControllerTest, AxesAreInferredOnlyAfterTwoCompleteReplies) {
    line(kIdle);
    EXPECT_FALSE(c().runner().hasAxs());
    replyToI();
    EXPECT_FALSE(c().runner().hasAxs());
    loop.advance(2000);
    line(kIdle);
    EXPECT_FALSE(c().runner().hasAxs());
    replyToI();
    EXPECT_EQ(c().state().axes, (protocol::AxesInfo{3, "XYZ", true}));
}

TEST_F(GrblHalControllerTest, AFourthAxisIsInferredAndARealAxsLineOverridesIt) {
    for (int probe = 0; probe < 2; ++probe) {
        loop.advance(2000);
        line("<Idle|MPos:1.000,2.000,3.000,4.000|FS:0,0>");
        replyToI();
    }
    EXPECT_EQ(c().state().axes, (protocol::AxesInfo{4, "XYZA", true}));
    line("[AXS:4:XYZC]");
    EXPECT_EQ(c().state().axes, (protocol::AxesInfo{4, "XYZC", false}));
}

TEST_F(GrblHalControllerTest, NoProbeWhileTheMachineIsBusy) {
    for (int tick = 0; tick < 50; ++tick) {
        loop.advance(2000);
        line("<Run|MPos:1.000,2.000,3.000|FS:500,0>");
    }
    EXPECT_EQ(probes(), 0);
}

TEST_F(GrblHalControllerTest, FirmwareWithAxsIsProbedOnce) {
    line(kIdle);
    replyToI("[AXS:4:XYZC]");
    for (int tick = 0; tick < 300; ++tick) {
        loop.advance(2000);
        line(kIdle);
    }
    EXPECT_EQ(probes(), 1);
    EXPECT_EQ(c().state().axes, (protocol::AxesInfo{4, "XYZC", false}));
}

TEST_F(GrblHalControllerTest, AProbeWithoutItsOkStillResolves) {
    line(kIdle);
    loop.advance(2000);  // safety net: probe 1 resolved, a retry remains
    EXPECT_FALSE(c().runner().hasAxs());
    line(kIdle);
    EXPECT_EQ(probes(), 2);
    loop.advance(2000);
    EXPECT_EQ(c().state().axes, (protocol::AxesInfo{3, "XYZ", true}));
}

TEST_F(GrblHalControllerTest, AUserTypedParserStateQueryIsEchoedWithItsOk) {
    EXPECT_FALSE(c().actionMask().replyParserState);
    c().writeln("$G");
    EXPECT_TRUE(c().actionMask().replyParserState);
    const char* state = "[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]";
    line(state);
    EXPECT_TRUE(has(console(), state));
    line("ok");
    EXPECT_TRUE(has(console(), "ok"));
    EXPECT_FALSE(c().actionMask().replyParserState);
}

TEST_F(GrblHalControllerTest, ThePollingParserStateQueryIsNotEchoed) {
    line(kIdle);
    c().setPollingEnabled(true);
    loop.advance(250);
    EXPECT_TRUE(has(immediates(), "$G\n"));
    EXPECT_TRUE(has(immediates(), "?"));
    EXPECT_FALSE(c().actionMask().replyParserState);
}

TEST_F(GrblHalControllerTest, StatusQueriesTypedByTheUserAreEchoed) {
    c().write("?");
    EXPECT_TRUE(c().actionMask().replyStatusReport);
    make(Firmware::GrblHal);
    c().write("\x87");
    EXPECT_TRUE(c().actionMask().replyStatusReport);
}

}  // namespace
