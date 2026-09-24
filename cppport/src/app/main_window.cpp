#include "main_window.hpp"

#include "accessibility.hpp"
#include "accessory_wizards.hpp"

#include "appearance.hpp"
#include "diagnostics.hpp"
#include "calibration_dialogs.hpp"
#include "controls.hpp"
#include "dro_panel.hpp"
#include "jogger.hpp"
#include "machine.hpp"
#include "notifications.hpp"
#include "power.hpp"
#include "panels.hpp"
#include "probe_panel.hpp"
#include "rotary_panel.hpp"
#include "sd_card_dialog.hpp"
#include "settings_dialog.hpp"
#include "shortcuts.hpp"
#include "shortcuts_dialog.hpp"
#include "stats_dialog.hpp"
#include "status_area.hpp"
#include "surfacing_dialog.hpp"
#include "toolchange_dialog.hpp"
#include "toolpath_view.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "gs/controller/actions.hpp"
#include "gs/transport/asio_link.hpp"
#include "gs/transport/port_list.hpp"

namespace gs::app {

MainWindow::MainWindow(Machine& machine, QWidget* parent) : QMainWindow(parent), machine_(machine) {
    setWindowTitle(tr("gSender (C++)"));
    jogger_ = new Jogger(machine_, this);

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new ConnectionBar(machine_));

    auto* splitter = new QSplitter(Qt::Horizontal);
    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(6, 0, 0, 6);
    visualizer_ = new ToolpathView(machine_);
    statusArea_ = new StatusArea(machine_, visualizer_);
    // Accessibility: the announcer, and its job summary above the visualizer
    // ("Show summary visually").
    announcer_ = new AccessibilityAnnouncer(machine_, *this, this);
    summaryBox_ = new QLabel;
    summaryBox_->setObjectName("jobSummary");
    summaryBox_->setWordWrap(true);
    summaryBox_->setTextFormat(Qt::RichText);
    summaryBox_->setStyleSheet("QLabel#jobSummary { background: #eff6ff; color: #1d4ed8; border-left: 4px solid "
                               "#3b82f6; border-radius: 4px; padding: 8px 12px; margin: 4px 6px 4px 0; }");
    const auto showSummary = [this] {
        const AccessibilitySettings& a = machine_.settings().accessibility;
        const QString& text = announcer_->summary();
        summaryBox_->setText("<b>" + tr("Job Summary") + "</b><br>" + text.toHtmlEscaped());
        summaryBox_->setVisible(a.gcodeSummary && a.gcodeSummaryVisible && !text.isEmpty());
    };
    connect(announcer_, &AccessibilityAnnouncer::summaryChanged, this, showSummary);
    connect(&machine_, &Machine::appSettingsChanged, this, showSummary);
    showSummary();
    leftLayout->addWidget(summaryBox_);
    leftLayout->addWidget(visualizer_, 1);
    leftLayout->addWidget(new JobPanel(machine_));

    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 6, 6);
    rightLayout->addWidget(new PositionPanel(machine_));
    tabs_ = new QTabWidget;
    spindle_ = new SpindlePanel(machine_);
    probe_ = new ProbePanel(machine_);
    tabs_->addTab(new JogPanel(machine_, *jogger_), tr("Jog"));
    tabs_->addTab(spindle_, tr("Spindle/Laser"));
    coolant_ = new CoolantPanel(machine_);
    tabs_->addTab(coolant_, tr("Coolant"));
    tabs_->addTab(probe_, tr("Probe"));
    tabs_->addTab(new MacrosPanel(machine_), tr("Macros"));
    rotary_ = new RotaryPanel(machine_);
    tabs_->addTab(rotary_, tr("Rotary"));
    // Tabs that follow their settings (Tools' filteredTabs).
    const auto showTabs = [this] {
        const AppSettings& settings = machine_.settings();
        tabs_->setTabVisible(tabs_->indexOf(rotary_), settings.rotary.showControls);
        tabs_->setTabVisible(tabs_->indexOf(spindle_), settings.spindleFunctions);
        tabs_->setTabVisible(tabs_->indexOf(coolant_), settings.coolantFunctions);
    };
    connect(&machine_, &Machine::appSettingsChanged, this, showTabs);
    showTabs();
    rightLayout->addWidget(tabs_);
    console_ = new ConsolePanel(machine_);
    rightLayout->addWidget(console_, 1);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    setCentralWidget(central);

    // Notifications: every message is kept for the bell's list and pops up
    // at the bottom right for workspace.toastDuration.
    notifications_ = new NotificationCenter(this);
    toasts_ = new ToastArea(central);
    connect(notifications_, &NotificationCenter::added, this, [this](const Notification& n) {
        toasts_->showToast(n.message, n.type, machine_.settings().toastDuration);
    });
    bell_ = new NotificationButton(*notifications_, this);
    menuBar()->setCornerWidget(bell_, Qt::TopRightCorner);

    statusBar()->showMessage(tr("Ready"));
    const auto announce = [this](NotificationType type) {
        return [this, type](const QString& text) {
            statusBar()->showMessage(text, 15000);
            console_->append(text, false);
            notifications_->add(text, type);
        };
    };
    connect(&machine_, &Machine::notice, this, announce(NotificationType::Info));
    connect(&machine_, &Machine::successNotice, this, announce(NotificationType::Success));
    // "Warn if bad file": the invalid lines of a loaded file.
    connect(&machine_, &Machine::invalidLinesFound, this, [this](int count, const QStringList& sample) {
        QString detail = tr("Detected %1 invalid lines on file load. Your job may not run correctly.\n\n"
                            "Sample invalid lines found include:")
                             .arg(count);
        for (const QString& line : sample) {
            detail += "\n- " + line;
        }
        showMessage(tr("Invalid Lines Detected"), detail);
    });
    // Power saving: the display kept awake unless sleeping is allowed; and
    // the application dark or not.
    const auto applyPower = [this] {
        setDisplaySleepAllowed(machine_.settings().powerSaving);
        applyDarkMode(machine_.settings().darkMode);
    };
    connect(&machine_, &Machine::appSettingsChanged, this, applyPower);
    applyPower();
    // Focus rings, and reduced motion: the platform's UI effects (menus,
    // combo boxes, tooltips sliding and fading) off.
    focusRing_ = new FocusRing(this);
    generalEffects_ = QApplication::isEffectEnabled(Qt::UI_General);
    const auto applyAccessibility = [this] {
        const AccessibilitySettings& a = machine_.settings().accessibility;
        focusRing_->setActive(a.focusRings);
        QApplication::setEffectEnabled(Qt::UI_General, generalEffects_ && !a.reducedMotion);
    };
    connect(&machine_, &Machine::appSettingsChanged, this, applyAccessibility);
    applyAccessibility();
    // The alerts at a job's end (workspace/Alerts).
    connect(&machine_, &Machine::jobEnded, this,
            [this](bool completed, double durationMs, const QStringList& errors) {
                const AppSettings& settings = machine_.settings();
                if (!dialogsEnabled_) {
                    return;
                }
                if (settings.jobEndModal) {
                    auto* summary = new JobEndDialog(completed, durationMs, errors, this);
                    summary->setAttribute(Qt::WA_DeleteOnClose);
                    summary->show();
                }
                if (settings.maintenanceNotifications) {
                    std::vector<config::MaintenanceTask> due = machine_.dueMaintenanceTasks();
                    if (!due.empty()) {
                        auto* alert = new MaintenanceAlertDialog(machine_, std::move(due), this);
                        alert->setAttribute(Qt::WA_DeleteOnClose);
                        alert->show();
                    }
                }
            });
    connect(&machine_, &Machine::connectionFailed, this,
            [this](const QString& reason) { showError(tr("Connection"), reason); });
    connect(&machine_, &Machine::errorReported, this, &MainWindow::showError);
    // AccessoryConnectivityToast: its own pop-up, not kept in the list.
    connect(&machine_, &Machine::accessoryConnectivityChanged, this, [this](const QString& accessory, bool connected) {
        toasts_->showToast(connected ? tr("%1 connected\nReady to use - device available").arg(accessory)
                                     : tr("%1 disconnected\nConnection lost").arg(accessory),
                           connected ? NotificationType::Success : NotificationType::Error, kToastDefault);
    });
    // M6 with a wizard strategy (controllerSagas' gcode:toolChange).
    connect(&machine_, &Machine::toolChangeWizardRequested, this,
            [this](const QString& option, int count, const QString& comment) {
                bool fullWizard = true;
                const AppSettings& settings = machine_.settings();
                if (option == "Fixed Tool Sensor" && count <= 1 &&
                    settings.firstToolBehaviour == toolchange::kFirstToolBehaviours[1] && dialogsEnabled_) {
                    // showFirstToolchangePrompt()
                    fullWizard = QMessageBox::question(
                                     this, tr("First tool change"),
                                     tr("A tool change was requested for the first tool%1.\n\nRun the full tool "
                                        "change wizard? Choose No to only measure the tool that is loaded.")
                                         .arg(comment.isEmpty() ? QString() : " (" + comment + ")")) ==
                                 QMessageBox::Yes;
                }
                const auto wizard = machine_.startToolChangeWizard(option.toStdString(), count, fullWizard);
                if (!wizard) {
                    return;
                }
                auto* dialog = new ToolChangeWizardDialog(machine_, *wizard, this);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->show();
            });
    // gSender's recovery prompt: the job can resume from where it stopped.
    connect(&machine_, &Machine::jobInterrupted, this, [this](qint64 line) {
        showMessage(tr("Job interrupted"),
                  tr("The connection closed while the job was running, around line %1.\n\n"
                     "Reconnect (and home if needed), then use Start From Line to resume.")
                      .arg(line));
    });
    // A "Code" tool change: the pre-hook ran, now the operator changes the
    // tool and continues (gSender's tool change dialog).
    connect(&machine_, &Machine::toolChangeWaiting, this, [this](const QString& comment) {
        statusBar()->showMessage(tr("Change the tool, then continue."));
        if (!dialogsEnabled_) {
            return;
        }
        auto* box = new QMessageBox(QMessageBox::Information, tr("Tool change"),
                                    tr("Change the tool, then continue the job.") +
                                        (comment.isEmpty() ? QString() : "\n\n" + comment),
                                    QMessageBox::NoButton, this);
        QPushButton* next = box->addButton(tr("Continue"), QMessageBox::AcceptRole);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setModal(false);
        connect(next, &QPushButton::clicked, this, [this] {
            if (auto* c = machine_.controller()) {
                c->toolChangePost();
            }
        });
        box->show();
    });
    shortcuts_ = new ShortcutManager(machine_, *this, this);
    installShortcuts();
    createMenus();
    // Accessibility's keyboard map: what the shortcut manager runs.
    keyboardMap_ = new KeyboardMapOverlay(machine_, *shortcuts_, centralWidget());
}

void MainWindow::installShortcuts() {
    ShortcutManager& s = *shortcuts_;
    using controller::ControllerCommand;
    const auto command = [this](ControllerCommand which) {
        return [this, which] {
            if (auto* c = machine_.controller()) {
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
    const auto dro = [this](std::function<void()> action) {
        return [this, action = std::move(action)] {
            controller::Controller* c = machine_.controller();
            const std::string state = c ? c->state().status.activeState : std::string();
            if (c && !c->workflow().isRunning() && (state == "Idle" || state == "Jog")) {
                action();
            }
        };
    };
    for (const char axis : {'X', 'Y', 'Z', 'A'}) {
        s.setHandler(QString("ZERO_%1_AXIS").arg(axis), dro([this, axis] { machine_.zeroAxis(axis); }));
        s.setHandler(QString("GO_TO_%1_AXIS_ZERO").arg(axis),
                     dro([this, axis] { machine_.goToZero(std::string(1, axis)); }));
    }
    s.setHandler("ZERO_ALL_AXIS", dro([this] { machine_.zeroAllAxes(); }));
    s.setHandler("GO_TO_XY_AXIS_ZERO", dro([this] { machine_.goToZero("XY"); }));
    // The corners need homing enabled (canRunShortcut(true)) - not a homed
    // machine, unlike their buttons; Park needs the machine homed.
    using controller::MachineCorner;
    const std::pair<const char*, MachineCorner> corners[] = {
        {"HOMING_GO_TO_BACK_LEFT_CORNER", MachineCorner::BackLeft},
        {"HOMING_GO_TO_BACK_RIGHT_CORNER", MachineCorner::BackRight},
        {"HOMING_GO_TO_FRONT_LEFT_CORNER", MachineCorner::FrontLeft},
        {"HOMING_GO_TO_FRONT_RIGHT_CORNER", MachineCorner::FrontRight},
    };
    for (const auto& [id, corner] : corners) {
        s.setHandler(id, dro([this, corner] {
                         if (machine_.homingEnabled()) {
                             machine_.goToCorner(corner);
                         }
                     }));
    }
    s.setHandler("HOMING_PARK", dro([this] {
                     if (machine_.controller()->hasHomed()) {
                         machine_.goToPark();
                     }
                 }));

    // Job control, with the buttons' rules.
    s.setHandler("START_JOB", [this] {
        controller::Controller* c = machine_.controller();
        if (c && machine_.hasProgram() && !machine_.isAnalyzing() && !machine_.isRunningSdFile() &&
            controller::canRun(c->state().status.activeState, c->workflow().state())) {
            controller::runJob(*c);
        }
    });
    s.setHandler("PAUSE_JOB", [this] {
        controller::Controller* c = machine_.controller();
        if (c && controller::canPause(c->state().status.activeState, c->workflow().state())) {
            controller::pauseJob(*c);
        }
    });
    s.setHandler("STOP_JOB", [this] {
        controller::Controller* c = machine_.controller();
        if (!c) {
            return;
        }
        if (controller::canStop(c->workflow().state())) {
            controller::stopJob(*c);
        } else {
            controller::stopWithoutJob(*c);  // with no job it stops jogs
        }
    });
    s.setHandler("RUN_OUTLINE", [this] { machine_.runOutline(); });
    s.setHandler("DISPLAY_MACHINE_INFO", [this] { statusArea_->toggleMachineInfo(); });
    s.setHandler("DISPLAY_NOTIFICATIONS", [this] { bell_->togglePanel(); });
    // A macro's shortcut runs it when the machine is idle (upstream's MACRO
    // event), with the loaded file as context.
    s.setMacroHandler([this](const QString& id) {
        controller::Controller* c = machine_.controller();
        if (c && c->state().status.activeState == "Idle") {
            c->runMacro(id.toStdString(), machine_.fileContext());
        }
    });
    s.setHandler("LOAD_FILE", [this] { openFile(); });
    s.setHandler("UNLOAD_FILE", [this] {
        controller::Controller* c = machine_.controller();
        if (!c || c->workflow().isIdle()) {
            machine_.unloadProgram();
        }
    });

    // Jogging: held keys jog continuously, taps step.
    const auto jog = [this](const QString& id, controller::JogAxes directions) {
        shortcuts_->setHandler(
            id, [this, directions] { jogger_->press(directions); }, [this] { jogger_->release(); });
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
    const auto rotaryJog = [this](const QString& id, int direction) {
        shortcuts_->setHandler(
            id,
            [this, direction] {
                controller::Controller* c = machine_.controller();
                if (c && c->isGrbl() && !machine_.rotaryMode() && !machine_.settings().preferences.useAaxisForGrbl) {
                    return;
                }
                jogger_->pressRotary(direction);
            },
            [this] { jogger_->release(); });
    };
    rotaryJog("JOG_A_PLUS", 1);
    rotaryJog("JOG_A_MINUS", -1);
    s.setHandler("STOP_CONT_JOG", [this] { jogger_->release(); });
    s.setHandler("SET_R_JOG_PRESET", [this] { jogger_->selectPreset(controller::JogPreset::Rapid); });
    s.setHandler("SET_N_JOG_PRESET", [this] { jogger_->selectPreset(controller::JogPreset::Normal); });
    s.setHandler("SET_P_JOG_PRESET", [this] { jogger_->selectPreset(controller::JogPreset::Precise); });
    s.setHandler("CYCLE_JOG_PRESETS", [this] { jogger_->cyclePreset(); });

    // Overrides: one realtime byte each, as the visualizer's shortcuts send.
    const auto realtime = [this](unsigned char byte) {
        return [this, byte] {
            if (auto* c = machine_.controller()) {
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

    // Visualizer.
    s.setHandler("VISUALIZER_VIEW_3D", [this] { visualizer_->setView(ToolpathView::View::Iso); });
    s.setHandler("VISUALIZER_VIEW_TOP", [this] { visualizer_->setView(ToolpathView::View::Top); });
    s.setHandler("VISUALIZER_VIEW_FRONT", [this] { visualizer_->setView(ToolpathView::View::Front); });
    s.setHandler("VISUALIZER_VIEW_RIGHT", [this] { visualizer_->setView(ToolpathView::View::Right); });
    s.setHandler("VISUALIZER_VIEW_LEFT", [this] { visualizer_->setView(ToolpathView::View::Left); });
    s.setHandler("VISUALIZER_VIEW_RESET", [this] { visualizer_->set3dView(); });
    s.setHandler("VISUALIZER_VIEW_CYCLE", [this] { visualizer_->cycleView(); });
    s.setHandler("VISUALIZER_ZOOM_IN", [this] { visualizer_->zoom(1.25); });
    s.setHandler("VISUALIZER_ZOOM_OUT", [this] { visualizer_->zoom(0.8); });
    s.setHandler("VISUALIZER_ZOOM_FIT", [this] { visualizer_->fit(); });
    s.setHandler("LIGHTWEIGHT_MODE", [this] { visualizer_->toggleLiteMode(); });
    s.setHandler("TOGGLE_SHORTCUTS", [this] {
        AppSettings settings = machine_.settings();
        settings.shortcutsEnabled = !settings.shortcutsEnabled;
        machine_.setSettings(settings);
        statusBar()->showMessage(settings.shortcutsEnabled ? tr("Keyboard shortcuts on")
                                                           : tr("Keyboard shortcuts off"),
                                 5000);
    });

    // Coolant (canRunShortcut: connected, no job running, idle) and spindle.
    const auto coolant = [this](const char* code) {
        return [this, code] {
            controller::Controller* c = machine_.controller();
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
    s.setHandler("STOP_LASER_OFF", [this] { spindle_->stopSpindle(); });

    // Probing.
    s.setHandler("OPEN_PROBE", [this] {
        tabs_->setCurrentWidget(probe_);
        probe_->openRunDialog();
    });
    s.setHandler("PROBE_ROUTINE_SCROLL_RIGHT", [this] { probe_->stepCommand(1); });
    s.setHandler("PROBE_ROUTINE_SCROLL_LEFT", [this] { probe_->stepCommand(-1); });

    // Rotary. Deviation: upstream's shortcut only flipped the stored mode,
    // leaving the board's settings as they were; it runs the switch here.
    s.setHandler("SWITCH_WORKSPACE_MODE", [this] { rotary_->toggleRotaryMode(); });
    s.setHandler("TOGGLE_ROTARY_SURFACING", [this] { rotary_->openSurfacing(); });
    s.setHandler("TOGGLE_MOUNTING_SETUP", [this] { rotary_->openMountingSetup(); });
}

SdCardDialog& MainWindow::sdCardDialog() {
    // Kept once opened: an upload's end is heard with the dialog closed.
    if (!sdCard_) {
        sdCard_ = new SdCardDialog(machine_, *notifications_, this);
    }
    return *sdCard_;
}

bool MainWindow::rotaryTabVisible() const {
    return tabs_->isTabVisible(tabs_->indexOf(rotary_));
}

QStringList MainWindow::visibleTabs() const {
    QStringList shown;
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->isTabVisible(i)) {
            shown << tabs_->tabText(i);
        }
    }
    return shown;
}

void MainWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* open = file->addAction(tr("&Load File..."), this, &MainWindow::openFile);
    open->setShortcut(QKeySequence::Open);
    QMenu* recent = file->addMenu(tr("&Recent Files"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        for (const RecentFile& entry : machine_.settings().recentFiles) {
            const QString path = QString::fromStdString(entry.filePath);
            QAction* action = recent->addAction(QString::fromStdString(entry.fileName));
            action->setStatusTip(path);
            action->setToolTip(path);
            connect(action, &QAction::triggered, this, [this, path] { openRecent(path); });
        }
        if (recent->isEmpty()) {
            recent->addAction(tr("(none)"))->setEnabled(false);
        }
        recent->addSeparator();
        QAction* clear = recent->addAction(tr("Clear Recent Files"), &machine_, &Machine::clearRecentFiles);
        clear->setEnabled(!machine_.settings().recentFiles.empty());
    });
    file->addAction(tr("&Close File"), &machine_, &Machine::unloadProgram);
    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"), qApp, &QApplication::quit);
    quit->setShortcut(QKeySequence::Quit);

    QMenu* machine = menuBar()->addMenu(tr("&Machine"));
    QAction* settings = machine->addAction(tr("&Settings..."), this, [this] { openSettings(SettingsDialog::Page::General); });
    settings->setShortcut(QKeySequence::Preferences);
    machine->addAction(tr("&Firmware Settings..."), this, [this] { openSettings(SettingsDialog::Page::Firmware); });

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("&Surfacing..."), this, [this] {
        SurfacingDialog dialog(machine_, this);
        dialog.exec();
    });
    tools->addAction(tr("&Rotary Surfacing..."), this, [this] {
        RotarySurfacingDialog dialog(machine_, this);
        dialog.exec();
    });
    // The calibration wizards stay open beside the main window, whose jog
    // controls position the machine between their steps.
    tools->addAction(tr("&Movement Tuning..."), this, [this] {
        auto* dialog = new MovementTuningDialog(machine_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    tools->addAction(tr("&XY Squaring..."), this, [this] {
        auto* dialog = new SquaringDialog(machine_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    tools->addSeparator();
    tools->addAction(tr("SD &Card..."), this, [this] { sdCardDialog().show(); });
    tools->addAction(tr("&Accessory Installation..."), this, [this] {
        AccessoryInstallerDialog dialog(machine_, *jogger_, accessoryWizards(machine_), this);
        dialog.exec();
    });
    tools->addSeparator();
    tools->addAction(tr("S&tatistics..."), this, [this] {
        StatsDialog dialog(machine_, this);
        connect(&dialog, &StatsDialog::diagnosticsRequested, this, &MainWindow::downloadDiagnostics);
        dialog.exec();
    });
    tools->addAction(tr("&Keyboard Shortcuts..."), this, [this] {
        ShortcutsDialog dialog(machine_, this);
        dialog.exec();
    });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Download &Diagnostic File..."), this, &MainWindow::downloadDiagnostics);
    help->addAction(tr("&About"), this, [this] {
        QMessageBox::about(this, tr("About gSender (C++)"),
                           tr("<b>gSender (C++)</b> %1<p>A native C++/Qt port of gSender, the CNC control "
                              "software for Grbl and grblHAL by Sienci Labs.</p>")
                               .arg(QApplication::applicationVersion()));
    });
}

void MainWindow::downloadDiagnostics() {
    const QDateTime now = QDateTime::currentDateTime();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Download Diagnostic File"), QDir::home().filePath("diagnostics_" + diagnosticsStamp(now) + ".zip"),
        tr("ZIP archives (*.zip)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (writeDiagnostics(machine_, console_->history(), path, &error, now)) {
        notifications_->add(tr("Diagnostic file downloaded successfully!"), NotificationType::Success);
    } else {
        notifications_->add(tr("Failed to generate diagnostic file") + (error.isEmpty() ? QString() : ": " + error),
                            NotificationType::Error);
    }
}

void MainWindow::openFile() {
    controller::Controller* c = machine_.controller();
    if (c && !c->workflow().isIdle()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load G-code"), QString(), tr("G-code (*.nc *.gcode *.gc *.ngc *.tap *.cnc *.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!machine_.loadFile(path, &error)) {
        showError(tr("Load file"), tr("Cannot open %1: %2").arg(path, error));
    }
}

void MainWindow::openRecent(const QString& path) {
    controller::Controller* c = machine_.controller();
    if (c && !c->workflow().isIdle()) {
        return;
    }
    if (!QFileInfo::exists(path)) {
        // Upstream's message; the entry goes, there being nothing to load.
        showError(tr("Load file"), tr("Unable to load file - file may have been moved or deleted."));
        machine_.forgetRecentFile(path);
        return;
    }
    QString error;
    if (!machine_.loadFile(path, &error)) {
        showError(tr("Load file"), tr("Cannot open %1: %2").arg(path, error));
    }
}

void MainWindow::openSettings(SettingsDialog::Page page) {
    SettingsDialog dialog(machine_, this);
    dialog.showPage(page);
    dialog.exec();
}

bool MainWindow::reconnectAutomatically() {
    const AppSettings& settings = machine_.settings();
    if (!settings.autoReconnect || settings.port.empty() || machine_.isConnected() || machine_.isConnecting()) {
        return false;
    }
    const QString port = QString::fromStdString(settings.port);
    bool known = Machine::isSimulatorPort(port) || transport::looksLikeIpAddress(settings.port);
    for (const transport::SerialPortInfo& info : transport::listSerialPorts()) {
        known = known || info.path == settings.port;
    }
    if (!known) {
        return false;
    }
    machine_.connectTo(port, settings.baudRate, settings.networkPort);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (machine_.settings().promptExit && dialogsEnabled_ &&
        QMessageBox::question(this, tr("Exit gSender"), tr("Are you sure you want to exit?"),
                              QMessageBox::No | QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::showError(const QString& title, const QString& detail) {
    // toast.error("Error 20: ..."): the first line pops up; the console has it all.
    const QString text = title + ": " + detail.section('\n', 0, 0);
    statusBar()->showMessage(text, 15000);
    console_->append(title + ": " + detail, false);
    notifications_->add(text, NotificationType::Error);
}

void MainWindow::showMessage(const QString& title, const QString& detail) {
    statusBar()->showMessage(title + ": " + detail.section('\n', 0, 0), 15000);
    console_->append(title + ": " + detail, false);
    if (!dialogsEnabled_) {
        return;
    }
    // Non-modal: the machine keeps reporting while the message is open.
    auto* box = new QMessageBox(QMessageBox::Warning, title, detail, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();
}

}  // namespace gs::app
