#include "ui_shortcuts.hpp"

#include "backend.hpp"
#include "jogger.hpp"
#include "machine.hpp"
#include "shortcuts.hpp"
#include "spindle_model.hpp"

#include "gs/controller/actions.hpp"
#include "gs/controller/controller.hpp"
#include "gs/controller/locations.hpp"

#include <QQuickItem>
#include <QQuickWindow>

namespace gs::ui {
namespace {

// Mousetrap's rule: keys typed into an editable text field are its own.
bool typingInQuick(QQuickWindow& window) {
    QQuickItem* focus = window.activeFocusItem();
    if (!focus || !(focus->inherits("QQuickTextInput") || focus->inherits("QQuickTextEdit"))) {
        return false;
    }
    return !focus->property("readOnly").toBool();
}

}  // namespace

UiShortcuts::UiShortcuts(UiBackend& backend, QQuickWindow& window) : QObject(&window), backend_(backend) {
    // Key events reach the window first, then its items: they are taken at
    // the window.
    app::ShortcutScope scope{&window, [&window](QObject* watched) { return watched == &window; },
                             [&window] { return window.isActive(); }, [&window] { return typingInQuick(window); }};
    manager_ = new app::ShortcutManager(backend_.machine(), std::move(scope), this);
    spindle_ = new SpindleModel(this);
    install();
}

void UiShortcuts::install() {
    app::ShortcutManager& s = *manager_;
    app::Machine& machine = backend_.machine();
    app::Jogger& jogger = backend_.jogger();
    // What the screens do.
    const auto screen = [this](const QString& id) {
        manager_->setHandler(id, [this, id] { Q_EMIT backend_.shortcutTriggered(id); });
    };

    using controller::ControllerCommand;
    const auto command = [&machine](ControllerCommand which) {
        return [&machine, which] {
            if (auto* c = machine.controller()) {
                controller::runControllerCommand(*c, which);
            }
        };
    };
    s.setHandler("CONTROLLER_COMMAND_UNLOCK", command(ControllerCommand::ResetLimit));
    s.setHandler("CONTROLLER_COMMAND_RESET", command(ControllerCommand::Reset));
    s.setHandler("CONTROLLER_COMMAND_HOMING", command(ControllerCommand::Homing));
    s.setHandler("CONTROLLER_COMMAND_REALTIME_REPORT", command(ControllerCommand::RealtimeReport));
    s.setHandler("CONTROLLER_COMMAND_ERROR_CLEAR", command(ControllerCommand::ErrorClear));
    s.setHandler("CONTROLLER_COMMAND_TOOLCHANGE_ACKNOWLEDGEMENT", command(ControllerCommand::ToolChangeAcknowledge));
    s.setHandler("CONTROLLER_COMMAND_VIRTUAL_STOP_TOGGLE", command(ControllerCommand::VirtualStopToggle));

    // The DRO's canRunShortcut(): connected, no job running, idle or jogging.
    const auto dro = [&machine](std::function<void()> action) {
        return [&machine, action = std::move(action)] {
            controller::Controller* c = machine.controller();
            const std::string state = c ? c->state().status.activeState : std::string();
            if (c && !c->workflow().isRunning() && (state == "Idle" || state == "Jog")) {
                action();
            }
        };
    };
    for (const char axis : {'X', 'Y', 'Z', 'A'}) {
        s.setHandler(QString("ZERO_%1_AXIS").arg(axis), dro([&machine, axis] { machine.zeroAxis(axis); }));
        s.setHandler(QString("GO_TO_%1_AXIS_ZERO").arg(axis),
                     dro([&machine, axis] { machine.goToZero(std::string(1, axis)); }));
    }
    s.setHandler("ZERO_ALL_AXIS", dro([&machine] { machine.zeroAllAxes(); }));
    s.setHandler("GO_TO_XY_AXIS_ZERO", dro([&machine] { machine.goToZero("XY"); }));
    // The corners need homing enabled; Park needs the machine homed.
    using controller::MachineCorner;
    const std::pair<const char*, MachineCorner> corners[] = {
        {"HOMING_GO_TO_BACK_LEFT_CORNER", MachineCorner::BackLeft},
        {"HOMING_GO_TO_BACK_RIGHT_CORNER", MachineCorner::BackRight},
        {"HOMING_GO_TO_FRONT_LEFT_CORNER", MachineCorner::FrontLeft},
        {"HOMING_GO_TO_FRONT_RIGHT_CORNER", MachineCorner::FrontRight},
    };
    for (const auto& [id, corner] : corners) {
        s.setHandler(id, dro([&machine, corner] {
                         if (machine.homingEnabled()) {
                             machine.goToCorner(corner);
                         }
                     }));
    }
    s.setHandler("HOMING_PARK", dro([&machine] {
                     if (machine.controller()->hasHomed()) {
                         machine.goToPark();
                     }
                 }));

    // Job control, with the buttons' rules.
    s.setHandler("START_JOB", [&machine] {
        controller::Controller* c = machine.controller();
        if (c && machine.hasProgram() && !machine.isAnalyzing() && !machine.isRunningSdFile() &&
            controller::canRun(c->state().status.activeState, c->workflow().state())) {
            controller::runJob(*c);
        }
    });
    s.setHandler("PAUSE_JOB", [&machine] {
        controller::Controller* c = machine.controller();
        if (c && controller::canPause(c->state().status.activeState, c->workflow().state())) {
            controller::pauseJob(*c);
        }
    });
    s.setHandler("STOP_JOB", [&machine] {
        controller::Controller* c = machine.controller();
        if (!c) {
            return;
        }
        if (controller::canStop(c->workflow().state())) {
            controller::stopJob(*c);
        } else {
            controller::stopWithoutJob(*c);  // with no job it stops jogs
        }
    });
    s.setHandler("RUN_OUTLINE", [&machine] { machine.runOutline(); });
    // A macro's shortcut runs it when the machine is idle, with the file's box.
    s.setMacroHandler([&machine](const QString& id) {
        controller::Controller* c = machine.controller();
        if (c && c->state().status.activeState == "Idle") {
            c->runMacro(id.toStdString(), machine.fileContext());
        }
    });
    s.setHandler("UNLOAD_FILE", [&machine] {
        controller::Controller* c = machine.controller();
        if (!c || c->workflow().isIdle()) {
            machine.unloadProgram();
        }
    });

    // Jogging: held keys jog continuously, taps step.
    const auto jog = [&s, &jogger](const QString& id, controller::JogAxes directions) {
        s.setHandler(id, [&jogger, directions] { jogger.press(directions); }, [&jogger] { jogger.release(); });
    };
    jog("JOG_X_P", {{'X', 1}});
    jog("JOG_X_M", {{'X', -1}});
    jog("JOG_Y_P", {{'Y', 1}});
    jog("JOG_Y_M", {{'Y', -1}});
    jog("JOG_Z_P", {{'Z', 1}});
    jog("JOG_Z_M", {{'Z', -1}});
    jog("JOG_X_P_Y_M", {{'X', 1}, {'Y', -1}});
    jog("JOG_X_M_Y_P", {{'X', -1}, {'Y', 1}});
    jog("JOG_X_Y_P", {{'X', 1}, {'Y', 1}});
    jog("JOG_X_Y_M", {{'X', -1}, {'Y', -1}});
    // A: the rotary (on Y in rotary mode). Grbl has no A of its own unless
    // its A words go through as they are.
    const auto rotaryJog = [&s, &jogger, &machine](const QString& id, int direction) {
        s.setHandler(
            id,
            [&jogger, &machine, direction] {
                controller::Controller* c = machine.controller();
                if (c && c->isGrbl() && !machine.rotaryMode() && !machine.settings().preferences.useAaxisForGrbl) {
                    return;
                }
                jogger.pressRotary(direction);
            },
            [&jogger] { jogger.release(); });
    };
    rotaryJog("JOG_A_PLUS", 1);
    rotaryJog("JOG_A_MINUS", -1);
    s.setHandler("STOP_CONT_JOG", [&jogger] { jogger.release(); });
    s.setHandler("SET_R_JOG_PRESET", [&jogger] { jogger.selectPreset(controller::JogPreset::Rapid); });
    s.setHandler("SET_N_JOG_PRESET", [&jogger] { jogger.selectPreset(controller::JogPreset::Normal); });
    s.setHandler("SET_P_JOG_PRESET", [&jogger] { jogger.selectPreset(controller::JogPreset::Precise); });
    s.setHandler("CYCLE_JOG_PRESETS", [&jogger] { jogger.cyclePreset(); });

    // Overrides: one realtime byte each.
    const auto realtime = [&machine](unsigned char byte) {
        return [&machine, byte] {
            if (auto* c = machine.controller()) {
                c->write(std::string(1, static_cast<char>(byte)));
            }
        };
    };
    s.setHandler("FEEDRATE_OVERRIDE_P", realtime(0x93));
    s.setHandler("FEEDRATE_OVERRIDE_PP", realtime(0x91));
    s.setHandler("FEEDRATE_OVERRIDE_M", realtime(0x94));
    s.setHandler("FEEDRATE_OVERRIDE_MM", realtime(0x92));
    s.setHandler("FEEDRATE_OVERRIDE_RESET", realtime(0x90));
    s.setHandler("SPINDLE_OVERRIDE_P", realtime(0x9C));
    s.setHandler("SPINDLE_OVERRIDE_PP", realtime(0x9A));
    s.setHandler("SPINDLE_OVERRIDE_M", realtime(0x9D));
    s.setHandler("SPINDLE_OVERRIDE_MM", realtime(0x9B));
    s.setHandler("SPINDLE_OVERRIDE_RESET", realtime(0x99));

    s.setHandler("LIGHTWEIGHT_MODE", [&machine] {
        app::AppSettings settings = machine.settings();
        settings.liteMode = !settings.liteMode;
        machine.setSettings(settings);
    });
    s.setHandler("TOGGLE_SHORTCUTS", [this, &machine] {
        app::AppSettings settings = machine.settings();
        settings.shortcutsEnabled = !settings.shortcutsEnabled;
        machine.setSettings(settings);
        backend_.notify(settings.shortcutsEnabled ? tr("Keyboard shortcuts on") : tr("Keyboard shortcuts off"),
                        QStringLiteral("info"));
    });

    // Coolant (canRunShortcut: connected, no job running, idle) and spindle.
    const auto coolant = [&machine](const char* code) {
        return [&machine, code] {
            controller::Controller* c = machine.controller();
            if (c && !c->workflow().isRunning() && c->state().status.activeState == "Idle") {
                c->gcode(code);
            }
        };
    };
    s.setHandler("MIST_COOLANT", coolant("M7"));
    s.setHandler("FLOOD_COOLANT", coolant("M8"));
    s.setHandler("STOP_COOLANT", coolant("M9"));
    s.setHandler("TOGGLE_SPINDLE_LASER_MODE", [this] { spindle_->toggleMode(); });
    s.setHandler("CW_LASER_ON", [this] { spindle_->startClockwise(); });
    s.setHandler("CCW_LASER_TEST", [this] { spindle_->startCounterClockwise(); });
    s.setHandler("STOP_LASER_OFF", [this] { spindle_->stop(); });

    // The screens'.
    for (const char* id : {"LOAD_FILE", "DISPLAY_MACHINE_INFO", "DISPLAY_NOTIFICATIONS", "VISUALIZER_VIEW_3D",
                           "VISUALIZER_VIEW_TOP", "VISUALIZER_VIEW_FRONT", "VISUALIZER_VIEW_RIGHT",
                           "VISUALIZER_VIEW_LEFT", "VISUALIZER_VIEW_RESET", "VISUALIZER_VIEW_CYCLE",
                           "VISUALIZER_ZOOM_IN", "VISUALIZER_ZOOM_OUT", "VISUALIZER_ZOOM_FIT", "OPEN_PROBE",
                           "PROBE_ROUTINE_SCROLL_RIGHT", "PROBE_ROUTINE_SCROLL_LEFT", "SWITCH_WORKSPACE_MODE",
                           "TOGGLE_ROTARY_SURFACING", "TOGGLE_MOUNTING_SETUP"}) {
        screen(QString::fromLatin1(id));
    }
}

}  // namespace gs::ui
