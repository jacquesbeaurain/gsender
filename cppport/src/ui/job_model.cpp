#include "job_model.hpp"

#include "backend.hpp"
#include "machine.hpp"
#include "qt_text.hpp"

#include "gs/controller/actions.hpp"
#include "gs/controller/controller.hpp"
#include "gs/util/datetime.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <algorithm>

namespace gs::ui {

JobModel::JobModel(QObject* parent) : UiModelBase(parent) {
    for (auto signal : {&app::Machine::workflowChanged, &app::Machine::connectionChanged,
                        &app::Machine::programChanged, &app::Machine::appSettingsChanged,
                        &app::Machine::stateChanged, &app::Machine::settingsChanged}) {
        connect(&machine_, signal, this, &JobModel::changed);
    }
    for (auto signal : {&app::Machine::senderStatusChanged, &app::Machine::workflowChanged,
                        &app::Machine::stateChanged, &app::Machine::connectionChanged}) {
        connect(&machine_, signal, this, &JobModel::progressChanged);
    }
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::connectionChanged}) {
        connect(&machine_, signal, this, &JobModel::overridesChanged);
    }
}

// A file that can run: loaded, analysed, not while the card runs one.
static bool ready(const app::Machine& machine) {
    return machine.controller() && machine.isConnected() && machine.hasProgram() && !machine.isAnalyzing() &&
           !machine.isRunningSdFile();
}

bool JobModel::canStart() const {
    controller::Controller* c = machine_.controller();
    return ready(machine_) && controller::canRun(c->state().status.activeState, c->workflow().state());
}

bool JobModel::canPause() const {
    controller::Controller* c = machine_.controller();
    return c && machine_.hasProgram() && controller::canPause(c->state().status.activeState, c->workflow().state());
}

bool JobModel::canStop() const {
    controller::Controller* c = machine_.controller();
    return c && machine_.hasProgram() && controller::canStop(c->workflow().state());
}

bool JobModel::canPrepare() const {
    controller::Controller* c = machine_.controller();
    return ready(machine_) && c->workflow().isIdle() && c->state().status.activeState == "Idle";
}

bool JobModel::running() const {
    controller::Controller* c = machine_.controller();
    return c && c->workflow().isRunning();
}

bool JobModel::paused() const {
    controller::Controller* c = machine_.controller();
    return c && c->workflow().state() == controller::WorkflowState::Paused;
}

bool JobModel::showProgress() const {
    controller::Controller* c = machine_.controller();
    if (machine_.isRunningSdFile()) {
        return true;
    }
    return c && machine_.isConnected() && machine_.hasProgram() && c->sender().hasProgram() &&
           c->sender().status().sent > 0;
}

int JobModel::received() const {
    // ProgressArea's currentLineRunning: the line the machine is on - the
    // board acknowledges lines into its planner well ahead of it.
    controller::Controller* c = machine_.controller();
    if (!c || !c->sender().hasProgram()) {
        return 0;
    }
    return static_cast<int>(std::max<std::int64_t>(0, c->sender().currentLineRunning()));
}

int JobModel::total() const {
    controller::Controller* c = machine_.controller();
    return c && c->sender().hasProgram() ? static_cast<int>(c->sender().status().total) : 0;
}

double JobModel::percent() const {
    if (machine_.isRunningSdFile()) {
        return std::clamp(machine_.controller()->state().status.sdProgress.percentage, 0.0, 100.0);
    }
    return total() > 0 ? 100.0 * received() / total() : 0;
}

QString JobModel::elapsed() const {
    controller::Controller* c = machine_.controller();
    const double seconds =
        c && c->sender().hasProgram() ? static_cast<double>(c->sender().status().elapsedTime) / 1000.0 : 0.0;
    return qstr(util::clockText(seconds));
}

QString JobModel::remaining() const {
    controller::Controller* c = machine_.controller();
    return qstr(util::clockText(c && c->sender().hasProgram() ? c->sender().status().remainingTime : 0.0));
}

QString JobModel::sdFile() const {
    return machine_.isRunningSdFile()
               ? QString::fromStdString(machine_.controller()->state().status.sdProgress.name.value_or(""))
               : QString();
}

int JobModel::totalLines() const {
    return static_cast<int>(machine_.analysis().totalLines);
}

int JobModel::lastLine() const {
    return static_cast<int>(machine_.lastLine());
}

double JobModel::defaultSafeHeight() const {
    // The safe retract height when set, else 10 mm (0.4 in).
    const bool metric = machine_.settings().metric;
    const double retract = machine_.settings().safeRetractHeight;
    return retract == 0 ? (metric ? 10 : 0.4) : (metric ? retract : units::convertToImperial(retract));
}

int JobModel::feedOverride() const {
    controller::Controller* c = machine_.controller();
    return c ? c->state().status.overrides[0] : 100;
}

int JobModel::spindleOverride() const {
    controller::Controller* c = machine_.controller();
    return c ? c->state().status.overrides[2] : 100;
}

QString JobModel::feedText() const {
    controller::Controller* c = machine_.controller();
    const double feed = c ? c->state().status.feedrate : 0;
    const bool metric = machine_.settings().metric;
    // The board reports in its $13 units; show the workspace's.
    const bool boardInches = c && c->runner().setting("$13", "0") == "1";
    double value = feed;
    if (boardInches && metric) {
        value = feed * 25.4;
    } else if (!boardInches && !metric) {
        value = feed / 25.4;
    }
    return QString("%1 %2").arg(QString::fromStdString(js::toFixed(value, metric ? 0 : 1)),
                                metric ? QStringLiteral("mm/min") : QStringLiteral("in/min"));
}

QString JobModel::spindleText() const {
    controller::Controller* c = machine_.controller();
    const double speed = c ? c->state().status.spindle : 0;
    return QString("%1 %2").arg(speed).arg(machine_.laserMode() ? QStringLiteral("Power") : QStringLiteral("RPM"));
}

bool JobModel::showSpindleOverride() const {
    return machine_.settings().spindleFunctions;
}

QString JobModel::spindleLabel() const {
    return machine_.laserMode() ? tr("Laser") : tr("Spindle");
}

void JobModel::start() {
    if (canStart()) {
        controller::runJob(*machine_.controller());
    }
}

void JobModel::pause() {
    if (canPause()) {
        controller::pauseJob(*machine_.controller());
    }
}

void JobModel::stop() {
    if (canStop()) {
        controller::stopJob(*machine_.controller());
    }
}

QString JobModel::outline() {
    QString error;
    if (!canPrepare()) {
        return tr("The machine must be idle with a file loaded");
    }
    return machine_.runOutline(&error) ? QString() : error;
}

bool JobModel::startFromLine(int line, double safeHeight) {
    if (!canPrepare() || line < 1) {
        return false;
    }
    const double mm = machine_.settings().metric ? safeHeight : safeHeight * 25.4;
    if (!machine_.startFromLine(static_cast<std::size_t>(line), mm)) {
        return false;
    }
    Q_EMIT notice(tr("Running Start From Specific Line Command"));
    return true;
}

void JobModel::setFeedOverride(int percent) {
    if (controller::Controller* c = machine_.controller()) {
        c->feedOverride(std::clamp(percent, 10, 200));
    }
}

void JobModel::setSpindleOverride(int percent) {
    if (controller::Controller* c = machine_.controller()) {
        c->spindleOverride(std::clamp(percent, 10, 200));
    }
}

}  // namespace gs::ui
