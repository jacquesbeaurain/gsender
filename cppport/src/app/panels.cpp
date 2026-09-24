#include "panels.hpp"

#include "controls.hpp"
#include "jogger.hpp"
#include "gcode_editor_dialog.hpp"
#include "machine.hpp"
#include "step_through_dialog.hpp"
#include "start_from_line_dialog.hpp"

#include "gs/controller/actions.hpp"
#include "gs/transport/port_list.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace gs::app {
namespace {

QString duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) {
        seconds = 0;
    }
    const auto total = static_cast<long long>(std::llround(seconds));
    const long long h = total / 3600;
    const long long m = (total % 3600) / 60;
    const long long s = total % 60;
    return h > 0 ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
                 : QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

}  // namespace

// ---- connection ---------------------------------------------------------------------

ConnectionBar::ConnectionBar(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    ports_ = new QComboBox;
    ports_->setEditable(true);  // also accepts an IP address
    ports_->setMinimumWidth(260);
    ports_->setToolTip(tr("Serial port, IP address of a networked board, or the built-in simulator"));
    refresh_ = new QPushButton(tr("Refresh"));
    baud_ = new QComboBox;
    for (const char* rate : {"115200", "250000", "57600", "38400", "19200", "9600"}) {
        baud_->addItem(rate);
    }
    connect_ = new QPushButton(tr("Connect"));
    connect_->setMinimumWidth(110);
    state_ = new QLabel;
    state_->setMinimumWidth(220);
    layout->addWidget(new QLabel(tr("Port")));
    layout->addWidget(ports_);
    layout->addWidget(refresh_);
    layout->addWidget(new QLabel(tr("Baud")));
    layout->addWidget(baud_);
    layout->addWidget(connect_);
    layout->addSpacing(12);
    layout->addWidget(state_, 1);

    connect(refresh_, &QPushButton::clicked, this, &ConnectionBar::refreshPorts);
    connect(connect_, &QPushButton::clicked, this, &ConnectionBar::toggleConnection);
    connect(&machine_, &Machine::connectionChanged, this, &ConnectionBar::updateState);
    connect(&machine_, &Machine::stateChanged, this, &ConnectionBar::updateState);
    refreshPorts();
    // Preselect what was used last.
    const AppSettings& settings = machine_.settings();
    if (const int index = ports_->findData(QString::fromStdString(settings.port)); index >= 0) {
        ports_->setCurrentIndex(index);
    } else if (!settings.port.empty()) {
        ports_->setEditText(QString::fromStdString(settings.port));
    }
    baud_->setCurrentText(QString::number(settings.baudRate));
    updateState();
}

void ConnectionBar::refreshPorts() {
    const QString previous = ports_->currentText();
    ports_->clear();
    ports_->addItem(tr("Simulator (no hardware)"), Machine::kSimulatorPort);
    ports_->addItem(tr("Simulator, grblHAL (no hardware)"), Machine::kSimulatorHalPort);
    std::vector<transport::SerialPortInfo> ports = transport::listSerialPorts();
    // Boards gSender recognizes first.
    std::stable_partition(ports.begin(), ports.end(), [](const transport::SerialPortInfo& port) {
        return transport::isRecognizedPort(port.vendorId, port.productId);
    });
    for (const transport::SerialPortInfo& port : ports) {
        QString label = QString::fromStdString(port.path);
        if (!port.manufacturer.empty()) {
            label += " - " + QString::fromStdString(port.manufacturer);
        }
        ports_->addItem(label, QString::fromStdString(port.path));
    }
    const int index = ports_->findText(previous);
    // The first serial port, else the Grbl simulator.
    ports_->setCurrentIndex(index >= 0 ? index : (ports.empty() ? 0 : 2));
}

void ConnectionBar::toggleConnection() {
    if (machine_.isConnected() || machine_.isConnecting()) {
        machine_.disconnectFromMachine();
        return;
    }
    // A typed entry (an IP address) has no item data.
    const int index = ports_->findText(ports_->currentText());
    const QString port = index >= 0 ? ports_->itemData(index).toString() : ports_->currentText().trimmed();
    if (port.isEmpty()) {
        return;
    }
    AppSettings settings = machine_.settings();
    settings.port = port.toStdString();
    settings.baudRate = baud_->currentText().toInt();
    machine_.setSettings(settings);
    machine_.connectTo(port, settings.baudRate, settings.networkPort);
}

void ConnectionBar::updateState() {
    const bool connected = machine_.isConnected();
    const bool connecting = machine_.isConnecting();
    connect_->setText(connected ? tr("Disconnect") : connecting ? tr("Cancel") : tr("Connect"));
    if (connected || connecting) {
        // Show what is connected (e.g. after --simulator on the command line).
        const int index = ports_->findData(machine_.port());
        if (index >= 0) {
            ports_->setCurrentIndex(index);
        } else {
            ports_->setEditText(machine_.port());
        }
    }
    ports_->setEnabled(!connected && !connecting);
    baud_->setEnabled(!connected && !connecting);
    refresh_->setEnabled(!connected && !connecting);

    QString text = tr("Not connected");
    QString color = "#777";
    if (connecting) {
        text = tr("Connecting to %1...").arg(machine_.port());
    } else if (const controller::Controller* c = machine_.controller()) {
        const std::string& state = c->state().status.activeState;
        text = QString("%1 on %2  -  %3")
                   .arg(QString::fromUtf8(protocol::firmwareName(c->firmware()).data(),
                                         static_cast<qsizetype>(protocol::firmwareName(c->firmware()).size())), machine_.port(),
                        state.empty() ? tr("Waiting for status") : QString::fromStdString(state));
        color = state == "Idle"                       ? "#2e7d32"
                : state == "Run" || state == "Jog"    ? "#1565c0"
                : state == "Alarm"                    ? "#c62828"
                : state.rfind("Hold", 0) == 0 || state == "Door" ? "#ef6c00"
                                                                 : "#555";
    }
    state_->setText(QString("<span style='color:%1; font-weight:600'>%2</span>").arg(color, text));
}

// ---- jogging ------------------------------------------------------------------------------

JogPanel::JogPanel(Machine& machine, Jogger& jogger, QWidget* parent)
    : QWidget(parent), machine_(machine), jogger_(jogger) {
    auto* box = new QGroupBox(tr("Jog"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);
    auto* layout = new QVBoxLayout(box);

    auto* grid = new QGridLayout;
    yButtons_[0] = jogButton("Y+", 'Y', +1);
    yButtons_[1] = jogButton("Y-", 'Y', -1);
    grid->addWidget(yButtons_[0], 0, 1);
    grid->addWidget(jogButton("X-", 'X', -1), 1, 0);
    grid->addWidget(jogButton("X+", 'X', +1), 1, 2);
    grid->addWidget(yButtons_[1], 2, 1);
    grid->addWidget(jogButton("Z+", 'Z', +1), 0, 4);
    grid->addWidget(jogButton("Z-", 'Z', -1), 2, 4);
    grid->setColumnMinimumWidth(3, 16);
    // A (AJog): the rotary - on Y in rotary mode.
    for (const int direction : {+1, -1}) {
        auto* button = new QPushButton(direction > 0 ? "A+" : "A-");
        button->setMinimumSize(56, 44);
        connect(button, &QPushButton::pressed, this, [this, direction] { jogger_.pressRotary(direction); });
        connect(button, &QPushButton::released, this, [this] { jogger_.release(); });
        buttons_.append(button);
        aControls_.append(button);
        grid->addWidget(button, direction > 0 ? 0 : 2, 6);
    }
    grid->setColumnMinimumWidth(5, 16);
    layout->addLayout(grid);

    auto* presetRow = new QHBoxLayout;
    presets_ = new QButtonGroup(this);
    const std::pair<controller::JogPreset, QString> presets[] = {
        {controller::JogPreset::Rapid, tr("Rapid")},
        {controller::JogPreset::Normal, tr("Normal")},
        {controller::JogPreset::Precise, tr("Precise")},
    };
    for (const auto& [preset, name] : presets) {
        auto* button = new QPushButton(name);
        button->setCheckable(true);
        presets_->addButton(button, static_cast<int>(preset));
        presetRow->addWidget(button);
    }
    connect(presets_, &QButtonGroup::idClicked, this,
            [this](int id) { jogger_.selectPreset(static_cast<controller::JogPreset>(id)); });
    layout->addLayout(presetRow);

    auto* values = new QHBoxLayout;
    const auto field = [](double max, int decimals, const QString& suffix) {
        auto* spin = new QDoubleSpinBox;
        spin->setRange(0.001, max);
        spin->setDecimals(decimals);
        spin->setSuffix(suffix);
        return spin;
    };
    xyStep_ = field(1000, 3, " mm");
    zStep_ = field(1000, 3, " mm");
    feed_ = field(20000, 0, " mm/min");
    aStep_ = field(3600, 3, tr(" deg"));
    values->addWidget(new QLabel(tr("XY")));
    values->addWidget(xyStep_);
    values->addWidget(new QLabel(tr("Z")));
    values->addWidget(zStep_);
    auto* aLabel = new QLabel(tr("A"));
    values->addWidget(aLabel);
    values->addWidget(aStep_);
    aControls_.append(aLabel);
    aControls_.append(aStep_);
    values->addWidget(new QLabel(tr("Speed")));
    values->addWidget(feed_);
    layout->addLayout(values);
    for (QDoubleSpinBox* spin : {xyStep_, zStep_, feed_, aStep_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] {
            if (showing_) {
                return;
            }
            controller::JogSpeeds speeds = jogger_.speeds();
            speeds.xyStep = xyStep_->value();
            speeds.zStep = zStep_->value();
            speeds.aStep = aStep_->value();
            speeds.feedrate = feed_->value();
            jogger_.setSpeeds(speeds);
        });
    }
    auto* hint = new QLabel(tr("Click to step, hold to jog continuously."));
    hint->setStyleSheet("color:#777");
    layout->addWidget(hint);

    connect(&jogger_, &Jogger::changed, this, &JogPanel::showSpeeds);
    connect(&machine_, &Machine::connectionChanged, this, &JogPanel::updateEnabled);
    connect(&machine_, &Machine::workflowChanged, this, &JogPanel::updateEnabled);
    connect(&machine_, &Machine::stateChanged, this, &JogPanel::updateEnabled);
    connect(&machine_, &Machine::appSettingsChanged, this, &JogPanel::updateEnabled);
    showSpeeds();
    updateEnabled();
}

QPushButton* JogPanel::jogButton(const QString& text, char axis, int direction) {
    auto* button = new QPushButton(text);
    button->setMinimumSize(56, 44);
    connect(button, &QPushButton::pressed, this, [this, axis, direction] {
        jogger_.press({{axis, static_cast<double>(direction)}});
    });
    connect(button, &QPushButton::released, this, [this] { jogger_.release(); });
    buttons_.append(button);
    return button;
}

void JogPanel::showSpeeds() {
    showing_ = true;
    const bool metric = jogger_.metric();
    for (QDoubleSpinBox* step : {xyStep_, zStep_}) {
        step->setSuffix(metric ? " mm" : " in");
    }
    feed_->setSuffix(metric ? " mm/min" : " in/min");
    feed_->setDecimals(metric ? 0 : 2);
    const controller::JogSpeeds& speeds = jogger_.speeds();
    xyStep_->setValue(speeds.xyStep);
    zStep_->setValue(speeds.zStep);
    aStep_->setValue(speeds.aStep);
    feed_->setValue(speeds.feedrate);
    presets_->button(static_cast<int>(jogger_.preset()))->setChecked(true);
    showing_ = false;
}

void JogPanel::updateEnabled() {
    controller::Controller* c = machine_.controller();
    const bool can = c && !c->workflow().isRunning() && c->state().status.activeState != "Alarm";
    for (QPushButton* button : buttons_) {
        button->setEnabled(can);
    }
    // A shows as upstream's Jogging decides: with the rotary controls on a
    // grblHAL board or in rotary mode, or with Grbl's A words passed through.
    const AppSettings& settings = machine_.settings();
    const bool rotary = settings.rotary.rotaryMode;
    const bool showA = ((c && c->isGrblHal()) || rotary) && settings.rotary.showControls;
    for (QWidget* widget : aControls_) {
        widget->setVisible(showA || settings.preferences.useAaxisForGrbl);
    }
    for (QPushButton* button : yButtons_) {
        button->setEnabled(can && !rotary);  // the rotary is on Y
    }
}

// ---- job ----------------------------------------------------------------------------------

JobPanel::JobPanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    info_ = new QLabel;
    info_->setWordWrap(true);
    info_->setTextFormat(Qt::RichText);
    // The file's own tools beside its information, as upstream's file panel.
    auto* infoRow = new QHBoxLayout;
    infoRow->addWidget(info_, 1);
    layout->addLayout(infoRow);

    auto* row = new QHBoxLayout;
    open_ = new QPushButton(tr("Load File..."));
    unload_ = new QPushButton(tr("Close File"));
    start_ = new QPushButton(tr("Start"));
    outline_ = new QPushButton(tr("Outline"));
    outline_->setToolTip(tr("Trace the job's outline above the stock"));
    fromLine_ = new QPushButton(tr("From Line..."));
    fromLine_->setToolTip(tr("Start From Line: resume a stopped or interrupted job"));
    edit_ = new QPushButton(tr("Edit..."));
    edit_->setToolTip(tr("G-code Editor: change the loaded file's lines"));
    stepThrough_ = new QPushButton(tr("Step Through..."));
    stepThrough_->setToolTip(tr("G-code Step Through: follow the file line by line"));
    pause_ = new QPushButton(tr("Pause"));
    resume_ = new QPushButton(tr("Resume"));
    stop_ = new QPushButton(tr("Stop"));
    start_->setStyleSheet("font-weight:600");
    for (QPushButton* button : {edit_, stepThrough_}) {
        infoRow->addWidget(button, 0, Qt::AlignTop);
    }
    for (QPushButton* button : {open_, unload_, start_, outline_, fromLine_, pause_, resume_, stop_}) {
        button->setMinimumHeight(34);
        row->addWidget(button);
    }
    layout->addLayout(row);

    auto* progressRow = new QHBoxLayout;
    progress_ = new QProgressBar;
    progress_->setFormat("%v / %m lines");
    timing_ = new QLabel;
    progressRow->addWidget(progress_, 1);
    progressRow->addWidget(timing_);
    layout->addLayout(progressRow);
    layout->addWidget(new OverridesBar(machine_));

    connect(open_, &QPushButton::clicked, this, &JobPanel::openFile);
    connect(unload_, &QPushButton::clicked, &machine_, &Machine::unloadProgram);
    // The rules of gSender's job control buttons (gs/controller/actions).
    for (QPushButton* button : {start_, resume_}) {
        connect(button, &QPushButton::clicked, this, [this] {
            if (auto* c = machine_.controller()) {
                controller::runJob(*c);
            }
        });
    }
    connect(outline_, &QPushButton::clicked, this, [this] {
        QString error;
        if (!machine_.runOutline(&error)) {
            Q_EMIT machine_.notice(error);
        }
    });
    connect(fromLine_, &QPushButton::clicked, this, [this] {
        StartFromLineDialog dialog(machine_, this);
        dialog.exec();
    });
    connect(edit_, &QPushButton::clicked, this, [this] {
        auto* dialog = new GcodeEditorDialog(machine_, window());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    connect(stepThrough_, &QPushButton::clicked, this, [this] {
        auto* dialog = new StepThroughDialog(machine_, window());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    });
    connect(pause_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            controller::pauseJob(*c);
        }
    });
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            controller::stopJob(*c);
        }
    });
    for (auto signal : {&Machine::programChanged, &Machine::workflowChanged, &Machine::connectionChanged,
                        &Machine::stateChanged}) {
        connect(&machine_, signal, this, &JobPanel::refresh);
    }
    connect(&machine_, &Machine::senderStatusChanged, this, &JobPanel::updateProgress);
    refresh();
}

void JobPanel::openFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load G-code"), QString(), tr("G-code (*.nc *.gcode *.gc *.ngc *.tap *.cnc *.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!machine_.loadFile(path, &error)) {
        info_->setText(tr("<span style='color:#c62828'>Cannot open %1: %2</span>").arg(path, error));
    }
}

void JobPanel::refresh() {
    controller::Controller* c = machine_.controller();
    const controller::WorkflowState workflow = c ? c->workflow().state() : controller::WorkflowState::Idle;
    const std::string activeState = c ? c->state().status.activeState : std::string();
    const bool idle = workflow == controller::WorkflowState::Idle;
    // Nothing starts while the board runs a file from its SD card.
    const bool runnable = c && machine_.hasProgram() && !machine_.isAnalyzing() && !machine_.isRunningSdFile() &&
                          controller::canRun(activeState, workflow);
    open_->setEnabled(idle);
    unload_->setEnabled(idle && machine_.hasProgram());
    stepThrough_->setEnabled(machine_.hasProgram() && !machine_.isAnalyzing());
    edit_->setEnabled(machine_.hasProgram());
    start_->setEnabled(runnable && idle && activeState != "Hold");
    fromLine_->setEnabled(runnable && idle && activeState == "Idle");
    outline_->setEnabled(runnable && idle && activeState == "Idle");
    pause_->setEnabled(c && controller::canPause(activeState, workflow));
    resume_->setEnabled(runnable && (workflow == controller::WorkflowState::Paused || activeState == "Hold"));
    stop_->setEnabled(c && controller::canStop(workflow));

    if (!machine_.hasProgram()) {
        info_->setText(tr("<b>No file loaded</b>"));
    } else if (machine_.isAnalyzing()) {
        info_->setText(tr("<b>%1</b> - analysing...").arg(machine_.programName().toHtmlEscaped()));
    } else {
        const job::ProgramAnalysis& a = machine_.analysis();
        QString text = tr("<b>%1</b> - %2 lines - estimated %3")
                           .arg(machine_.programName().toHtmlEscaped())
                           .arg(a.totalLines)
                           .arg(duration(a.estimatedTime));
        if (a.totalLines > 0) {
            text += tr("<br>X %1 to %2 - Y %3 to %4 - Z %5 to %6 mm")
                        .arg(a.bounds.min.x, 0, 'f', 2)
                        .arg(a.bounds.max.x, 0, 'f', 2)
                        .arg(a.bounds.min.y, 0, 'f', 2)
                        .arg(a.bounds.max.y, 0, 'f', 2)
                        .arg(a.bounds.min.z, 0, 'f', 2)
                        .arg(a.bounds.max.z, 0, 'f', 2);
        }
        if (!a.tools.empty()) {
            QStringList tools;
            for (const std::string& tool : a.tools) {
                tools << QString::fromStdString(tool);
            }
            text += tr("<br>Tools: %1").arg(tools.join(", "));
        }
        if (!a.invalidLines.empty()) {
            text += tr("<br><span style='color:#ef6c00'>%1 invalid line(s)</span>").arg(a.invalidLines.size());
        }
        info_->setText(text);
    }
    updateProgress();
}

void JobPanel::updateProgress() {
    controller::Controller* c = machine_.controller();
    if (machine_.isRunningSdFile()) {
        // SDCardProgress: the file the board runs and how far it is.
        const protocol::SdProgress& sd = c->state().status.sdProgress;
        progress_->setRange(0, 100);
        progress_->setValue(static_cast<int>(std::clamp(std::floor(sd.percentage), 0.0, 100.0)));
        progress_->setFormat(tr("%1 - %p%").arg(QString::fromStdString(sd.name.value_or(""))));
        timing_->setText(tr("Running from the SD card"));
        return;
    }
    progress_->setFormat("%v / %m lines");
    if (!c || !c->sender().hasProgram()) {
        progress_->setRange(0, 1);
        progress_->setValue(0);
        timing_->clear();
        return;
    }
    const controller::SenderStatus status = c->sender().status();
    progress_->setRange(0, std::max<int>(1, static_cast<int>(status.total)));
    progress_->setValue(static_cast<int>(status.received));
    if (c->workflow().isIdle()) {
        timing_->setText(tr("Estimated %1").arg(duration(machine_.analysis().estimatedTime)));
    } else {
        timing_->setText(tr("Elapsed %1 - Remaining %2")
                             .arg(duration(static_cast<double>(status.elapsedTime) / 1000.0))
                             .arg(duration(status.remainingTime)));
    }
}

}  // namespace gs::app
