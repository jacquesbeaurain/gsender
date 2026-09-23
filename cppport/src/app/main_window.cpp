#include "main_window.hpp"

#include "controls.hpp"
#include "jogger.hpp"
#include "machine.hpp"
#include "panels.hpp"
#include "probe_panel.hpp"
#include "settings_dialog.hpp"
#include "shortcuts.hpp"
#include "shortcuts_dialog.hpp"
#include "surfacing_dialog.hpp"
#include "toolpath_view.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "gs/controller/actions.hpp"

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
    tabs_->addTab(spindle_, tr("Spindle && Coolant"));
    tabs_->addTab(probe_, tr("Probe"));
    tabs_->addTab(new MacrosPanel(machine_), tr("Macros"));
    rightLayout->addWidget(tabs_);
    console_ = new ConsolePanel(machine_);
    rightLayout->addWidget(console_, 1);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    setCentralWidget(central);

    statusBar()->showMessage(tr("Ready"));
    connect(&machine_, &Machine::notice, this, [this](const QString& text) {
        statusBar()->showMessage(text, 15000);
        console_->append(text, false);
    });
    connect(&machine_, &Machine::connectionFailed, this,
            [this](const QString& reason) { showError(tr("Connection"), reason); });
    connect(&machine_, &Machine::errorReported, this, &MainWindow::showError);
    // gSender's recovery prompt: the job can resume from where it stopped.
    connect(&machine_, &Machine::jobInterrupted, this, [this](qint64 line) {
        showError(tr("Job interrupted"),
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

    // Job control, with the buttons' rules.
    s.setHandler("START_JOB", [this] {
        controller::Controller* c = machine_.controller();
        if (c && machine_.hasProgram() && !machine_.isAnalyzing() &&
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
    jog("JOG_A_PLUS", {{'A', 1}});
    jog("JOG_A_MINUS", {{'A', -1}});
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
}

void MainWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* open = file->addAction(tr("&Load File..."), this, &MainWindow::openFile);
    open->setShortcut(QKeySequence::Open);
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
    tools->addAction(tr("&Keyboard Shortcuts..."), this, [this] {
        ShortcutsDialog dialog(machine_, this);
        dialog.exec();
    });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, [this] {
        QMessageBox::about(this, tr("About gSender (C++)"),
                           tr("<b>gSender (C++)</b> %1<p>A native C++/Qt port of gSender, the CNC control "
                              "software for Grbl and grblHAL by Sienci Labs.</p>")
                               .arg(QApplication::applicationVersion()));
    });
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

void MainWindow::openSettings(SettingsDialog::Page page) {
    SettingsDialog dialog(machine_, this);
    dialog.showPage(page);
    dialog.exec();
}

void MainWindow::showError(const QString& title, const QString& detail) {
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
