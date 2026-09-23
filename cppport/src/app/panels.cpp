#include "panels.hpp"

#include "machine.hpp"

#include "gs/protocol/runner.hpp"
#include "gs/transport/port_list.hpp"

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
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace gs::app {
namespace {

constexpr const char* kAxisNames[4] = {"X", "Y", "Z", "A"};

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

// Positions are reported in inches when $13=1; the panels show millimetres.
double toMillimetres(const controller::Controller& c, double value) {
    return c.settings().settings.get("$13") == "1" ? value * 25.4 : value;
}

bool workflowIdle(const Machine& machine) {
    controller::Controller* c = machine.controller();
    return c && c->workflow().isIdle();
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
    updateState();
}

void ConnectionBar::refreshPorts() {
    const QString previous = ports_->currentText();
    ports_->clear();
    ports_->addItem(tr("Simulator (no hardware)"), Machine::kSimulatorPort);
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
    ports_->setCurrentIndex(index >= 0 ? index : (ports.empty() ? 0 : 1));
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
    machine_.connectTo(port, baud_->currentText().toInt());
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

// ---- positions ------------------------------------------------------------------------

PositionPanel::PositionPanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* box = new QGroupBox(tr("Position"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);
    auto* layout = new QVBoxLayout(box);
    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Work")), 0, 1, Qt::AlignRight);
    grid->addWidget(new QLabel(tr("Machine")), 0, 2, Qt::AlignRight);
    QFont big = font();
    big.setPointSizeF(big.pointSizeF() * 1.8);
    big.setBold(true);
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (int i = 0; i < 4; ++i) {
        auto* name = new QLabel(kAxisNames[i]);
        name->setFont(big);
        work_[i] = new QLabel("0.000");
        work_[i]->setFont(big);
        work_[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        work_[i]->setMinimumWidth(120);
        machinePos_[i] = new QLabel("0.000");
        machinePos_[i]->setFont(mono);
        machinePos_[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        machinePos_[i]->setStyleSheet("color:#777");
        zero_[i] = new QPushButton(tr("Zero %1").arg(kAxisNames[i]));
        grid->addWidget(name, i + 1, 0);
        if (i == 3) {
            rowA_[3] = name;
        }
        grid->addWidget(work_[i], i + 1, 1);
        grid->addWidget(machinePos_[i], i + 1, 2);
        grid->addWidget(zero_[i], i + 1, 3);
        connect(zero_[i], &QPushButton::clicked, this,
                [this, i] { command(QString("G10 L20 P0 %1").arg(kAxisNames[i]) + "0"); });
    }
    rowA_[0] = work_[3];
    rowA_[1] = machinePos_[3];
    rowA_[2] = zero_[3];
    layout->addLayout(grid);

    auto* buttons = new QGridLayout;
    const auto add = [&](const QString& text, int row, int column, auto action) {
        auto* button = new QPushButton(text);
        connect(button, &QPushButton::clicked, this, action);
        buttons->addWidget(button, row, column);
        actions_.append(button);
        return button;
    };
    add(tr("Zero All"), 0, 0, [this] { command("G10 L20 P0 X0 Y0 Z0"); });
    add(tr("Go to XY Zero"), 0, 1, [this] { command("G90 G0 X0 Y0"); });
    add(tr("Home"), 1, 0, [this] {
        if (auto* c = machine_.controller()) {
            c->home();
        }
    });
    add(tr("Unlock"), 1, 1, [this] {
        if (auto* c = machine_.controller()) {
            c->unlock();
        }
    });
    auto* reset = add(tr("Reset"), 1, 2, [this] {
        if (auto* c = machine_.controller()) {
            c->reset();
        }
    });
    reset->setToolTip(tr("Soft reset (Ctrl-X)"));
    layout->addLayout(buttons);

    connect(&machine_, &Machine::stateChanged, this, &PositionPanel::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &PositionPanel::refresh);
    connect(&machine_, &Machine::workflowChanged, this, &PositionPanel::refresh);
    refresh();
}

void PositionPanel::command(const QString& gcode) {
    if (auto* c = machine_.controller()) {
        c->gcode(gcode.toStdString());
    }
}

void PositionPanel::refresh() {
    const controller::Controller* c = machine_.controller();
    const bool idle = workflowIdle(machine_);
    for (QPushButton* button : actions_) {
        button->setEnabled(c != nullptr && (idle || button->text() == tr("Reset") || button->text() == tr("Unlock")));
    }
    bool showA = false;
    for (int i = 0; i < 4; ++i) {
        zero_[i]->setEnabled(c != nullptr && idle);
        if (!c) {
            work_[i]->setText("-");
            machinePos_[i]->setText("-");
            continue;
        }
        const auto& status = c->state().status;
        const char axis = "xyza"[i];
        work_[i]->setText(QString::number(toMillimetres(*c, status.wpos.axis(axis)), 'f', 3));
        machinePos_[i]->setText(QString::number(toMillimetres(*c, status.mpos.axis(axis)), 'f', 3));
        if (i == 3) {
            showA = status.mpos.count >= 4 || c->state().axes.letters.find('A') != std::string::npos;
        }
    }
    for (QWidget* widget : rowA_) {
        widget->setVisible(showA);
    }
}

// ---- jogging ------------------------------------------------------------------------------

JogPanel::JogPanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* box = new QGroupBox(tr("Jog"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);
    auto* layout = new QVBoxLayout(box);

    auto* grid = new QGridLayout;
    grid->addWidget(jogButton("Y+", 1, +1), 0, 1);
    grid->addWidget(jogButton("X-", 0, -1), 1, 0);
    grid->addWidget(jogButton("X+", 0, +1), 1, 2);
    grid->addWidget(jogButton("Y-", 1, -1), 2, 1);
    grid->addWidget(jogButton("Z+", 2, +1), 0, 4);
    grid->addWidget(jogButton("Z-", 2, -1), 2, 4);
    grid->setColumnMinimumWidth(3, 16);
    layout->addLayout(grid);

    auto* settings = new QHBoxLayout;
    step_ = new QComboBox;
    for (const char* step : {"0.1", "1", "10", "100"}) {
        step_->addItem(step);
    }
    step_->setCurrentText("10");
    feed_ = new QDoubleSpinBox;
    feed_->setRange(10, 20000);
    feed_->setDecimals(0);
    feed_->setSingleStep(100);
    feed_->setValue(3000);
    feed_->setSuffix(" mm/min");
    settings->addWidget(new QLabel(tr("Step (mm)")));
    settings->addWidget(step_);
    settings->addWidget(new QLabel(tr("Speed")));
    settings->addWidget(feed_);
    settings->addStretch();
    layout->addLayout(settings);
    auto* hint = new QLabel(tr("Click to step, hold to jog continuously."));
    hint->setStyleSheet("color:#777");
    layout->addWidget(hint);

    holdTimer_ = new QTimer(this);
    holdTimer_->setSingleShot(true);
    holdTimer_->setInterval(300);
    connect(holdTimer_, &QTimer::timeout, this, [this] {
        controller::Controller* c = machine_.controller();
        if (!c || heldAxis_ < 0) {
            return;
        }
        controller::Axes4 direction;
        direction[static_cast<std::size_t>(heldAxis_)] = heldDirection_;
        continuous_ = true;
        c->jogStart(direction, feed_->value());
    });
    connect(&machine_, &Machine::connectionChanged, this, &JogPanel::updateEnabled);
    connect(&machine_, &Machine::workflowChanged, this, &JogPanel::updateEnabled);
    connect(&machine_, &Machine::stateChanged, this, &JogPanel::updateEnabled);
    updateEnabled();
}

QPushButton* JogPanel::jogButton(const QString& text, int axis, int direction) {
    auto* button = new QPushButton(text);
    button->setMinimumSize(56, 44);
    connect(button, &QPushButton::pressed, this, [this, axis, direction] { pressed(axis, direction); });
    connect(button, &QPushButton::released, this, &JogPanel::released);
    buttons_.append(button);
    return button;
}

void JogPanel::pressed(int axis, int direction) {
    heldAxis_ = axis;
    heldDirection_ = direction;
    continuous_ = false;
    holdTimer_->start();
}

void JogPanel::released() {
    holdTimer_->stop();
    controller::Controller* c = machine_.controller();
    if (c && continuous_) {
        c->jogStop();
    } else if (c && heldAxis_ >= 0) {
        // A click moves one step, as gSender's jog buttons do.
        const double distance = step_->currentText().toDouble() * heldDirection_;
        c->gcode(QString("$J=G21G91%1%2F%3")
                     .arg(kAxisNames[heldAxis_])
                     .arg(distance)
                     .arg(feed_->value(), 0, 'f', 0)
                     .toStdString());
    }
    heldAxis_ = -1;
    continuous_ = false;
}

void JogPanel::updateEnabled() {
    controller::Controller* c = machine_.controller();
    const bool can = c && !c->workflow().isRunning() &&
                     c->state().status.activeState != "Alarm";
    for (QPushButton* button : buttons_) {
        button->setEnabled(can);
    }
}

// ---- console ------------------------------------------------------------------------------

ConsolePanel::ConsolePanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* box = new QGroupBox(tr("Console"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);
    auto* layout = new QVBoxLayout(box);
    output_ = new QPlainTextEdit;
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(5000);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    output_->setMinimumHeight(140);
    input_ = new QLineEdit;
    input_->setPlaceholderText(tr("Command, e.g. $$ or G0 X10"));
    input_->installEventFilter(this);
    auto* send = new QPushButton(tr("Send"));
    auto* row = new QHBoxLayout;
    row->addWidget(input_, 1);
    row->addWidget(send);
    layout->addWidget(output_, 1);
    layout->addLayout(row);

    connect(send, &QPushButton::clicked, this, &ConsolePanel::submit);
    connect(input_, &QLineEdit::returnPressed, this, &ConsolePanel::submit);
    connect(&machine_, &Machine::consoleLine, this, &ConsolePanel::append);
    connect(&machine_, &Machine::connectionChanged, this, [this] {
        input_->setEnabled(machine_.isConnected());
    });
    input_->setEnabled(false);
}

void ConsolePanel::append(const QString& text, bool fromHost) {
    QString line = text;
    while (line.endsWith('\n') || line.endsWith('\r')) {
        line.chop(1);
    }
    if (line.isEmpty()) {
        return;
    }
    QScrollBar* bar = output_->verticalScrollBar();
    const bool atEnd = bar->value() == bar->maximum();
    output_->appendPlainText(fromHost ? "> " + line : line);
    if (atEnd) {
        bar->setValue(bar->maximum());
    }
}

void ConsolePanel::submit() {
    const QString text = input_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    machine_.sendConsoleLine(text);
    history_.append(text);
    historyIndex_ = static_cast<int>(history_.size());
    input_->clear();
}

bool ConsolePanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == input_ && event->type() == QEvent::KeyPress && !history_.isEmpty()) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            historyIndex_ = std::clamp(historyIndex_ + (key == Qt::Key_Up ? -1 : 1), 0,
                                       static_cast<int>(history_.size()));
            input_->setText(historyIndex_ < history_.size() ? history_[historyIndex_] : QString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---- job ----------------------------------------------------------------------------------

JobPanel::JobPanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    info_ = new QLabel;
    info_->setWordWrap(true);
    info_->setTextFormat(Qt::RichText);
    layout->addWidget(info_);

    auto* row = new QHBoxLayout;
    open_ = new QPushButton(tr("Load File..."));
    unload_ = new QPushButton(tr("Close File"));
    start_ = new QPushButton(tr("Start"));
    pause_ = new QPushButton(tr("Pause"));
    resume_ = new QPushButton(tr("Resume"));
    stop_ = new QPushButton(tr("Stop"));
    start_->setStyleSheet("font-weight:600");
    for (QPushButton* button : {open_, unload_, start_, pause_, resume_, stop_}) {
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

    connect(open_, &QPushButton::clicked, this, &JobPanel::openFile);
    connect(unload_, &QPushButton::clicked, &machine_, &Machine::unloadProgram);
    connect(start_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            c->start();
        }
    });
    connect(pause_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            c->pause();
        }
    });
    connect(resume_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            c->resume();
        }
    });
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            c->stop(/*force=*/true);
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
    const bool running = c && c->workflow().isRunning();
    const bool paused = c && c->workflow().isPaused();
    const bool idle = !running && !paused;
    const bool machineIdle = c && c->state().status.activeState == "Idle";
    open_->setEnabled(idle);
    unload_->setEnabled(idle && machine_.hasProgram());
    start_->setEnabled(c && idle && machineIdle && machine_.hasProgram() && !machine_.isAnalyzing());
    pause_->setEnabled(running);
    resume_->setEnabled(paused);
    stop_->setEnabled(running || paused);

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
