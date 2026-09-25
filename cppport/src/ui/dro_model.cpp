#include "dro_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"
#include "gs/controller/locations.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <QVariantMap>

#include <cmath>

namespace gs::ui {
namespace {

// A position as the DRO shows it: the workspace units, A in degrees.
QString positionText(const app::Machine& machine, char axis, double mm) {
    if (axis == 'A') {
        return QString::number(mm, 'f', 3);
    }
    const app::AppSettings& s = machine.settings();
    return QString::fromStdString(units::positionText(mm, s.metric, s.customDecimalPlaces));
}

controller::GoToMode goToMode(const QString& mode) {
    return mode == "INC" ? controller::GoToMode::Incremental
                         : mode == "MCS" ? controller::GoToMode::Machine : controller::GoToMode::Absolute;
}

}  // namespace

DroModel::DroModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    for (auto signal : {&app::Machine::appSettingsChanged, &app::Machine::stateChanged, &app::Machine::settingsChanged,
                        &app::Machine::connectionChanged, &app::Machine::workflowChanged}) {
        connect(&machine_, signal, this, [this] {
            if (homingMode_ && !singleAxisHoming()) {
                homingMode_ = false;  // the switch went away
            }
            Q_EMIT changed();
        });
    }
}

bool DroModel::connected() const {
    return machine_.controller() != nullptr && machine_.isConnected();
}

bool DroModel::canClick() const {
    return machine_.canMove();
}

bool DroModel::metric() const {
    return machine_.settings().metric;
}

QString DroModel::units() const {
    return metric() ? QStringLiteral("mm") : QStringLiteral("in");
}

bool DroModel::rotaryMode() const {
    return machine_.rotaryMode();
}

bool DroModel::homingEnabled() const {
    return connected() && machine_.homingEnabled();
}

bool DroModel::homed() const {
    controller::Controller* c = machine_.controller();
    return homingEnabled() && c && c->hasHomed();
}

bool DroModel::singleAxisHoming() const {
    return homingEnabled() && machine_.singleAxisHoming();
}

void DroModel::setHomingMode(bool on) {
    on = on && singleAxisHoming();
    if (on != homingMode_) {
        homingMode_ = on;
        Q_EMIT changed();
    }
}

bool DroModel::warnZero() const {
    return machine_.settings().warnZero;
}

QString DroModel::workspace() const {
    controller::Controller* c = machine_.controller();
    return c ? QString::fromStdString(c->runner().modal().wcs) : QStringLiteral("G54");
}

bool DroModel::workspaceEnabled() const {
    controller::Controller* c = machine_.controller();
    return connected() && c->state().status.activeState != "Run" && !c->workflow().isRunning();
}

QVariantList DroModel::rows() const {
    controller::Controller* c = machine_.controller();
    const bool shown = connected();
    const bool can = canClick();
    const bool rotary = rotaryMode();
    const std::array<double, 4> wpos = machine_.workPositionMm();
    const std::array<double, 4> mpos = machine_.machinePositionMm();
    const auto row = [&](const QString& label, char axis, std::size_t index, bool enabled, bool gotoEnabled) {
        QVariantMap r;
        r["label"] = label;
        r["axis"] = QString(QChar(axis));
        // The rotary's degrees (on Y in rotary mode) print as A.
        const char unit = label == "A" ? 'A' : axis;
        r["work"] = shown ? positionText(machine_, unit, wpos[index]) : QStringLiteral("0.00");
        r["machine"] = shown ? positionText(machine_, unit, mpos[index]) : QStringLiteral("0.00");
        r["enabled"] = can && enabled;
        r["gotoEnabled"] = can && gotoEnabled;
        return r;
    };
    // In rotary mode the rotary drives Y: the A row shows and moves it; the
    // Y row is out of use and Z has no go-to-zero (upstream's AxisRow flags).
    QVariantList rows{row("X", 'X', 0, true, true), row("Y", 'Y', 1, !rotary, !rotary),
                      row("Z", 'Z', 2, true, !rotary)};
    const bool showA = c && (rotary || c->state().status.mpos.count >= 4 ||
                             c->state().axes.letters.find('A') != std::string::npos ||
                             machine_.settings().preferences.useAaxisForGrbl);
    if (showA) {
        rows.append(row("A", rotary ? 'Y' : 'A', rotary ? 1 : 3, true, true));
    }
    return rows;
}

bool DroModel::goToAEnabled() const {
    controller::Controller* c = machine_.controller();
    return rotaryMode() || (c && c->isGrblHal() && c->state().axes.letters.find('A') != std::string::npos);
}

void DroModel::toggleUnits() {
    app::AppSettings settings = machine_.settings();
    settings.metric = !settings.metric;
    machine_.setSettings(settings);
}

void DroModel::axisButton(const QString& axis) {
    if (axis.isEmpty() || !canClick()) {
        return;
    }
    const char letter = axis.front().toLatin1();
    if (homingMode_) {
        machine_.homeAxis(letter);
    } else {
        machine_.zeroAxis(letter);
    }
}

void DroModel::zeroAll() {
    if (canClick()) {
        machine_.zeroAllAxes();
    }
}

void DroModel::goToZero(const QString& axes) {
    if (canClick()) {
        machine_.goToZero(axes.toStdString());
    }
}

bool DroModel::setWorkPosition(const QString& axis, const QString& text) {
    const QString trimmed = text.trimmed();
    const double value = js::stringToNumber(trimmed.toStdString());
    // Deviation: an empty or invalid entry is ignored (upstream sent 0).
    if (axis.isEmpty() || trimmed.isEmpty() || !std::isfinite(value) || !canClick()) {
        Q_EMIT changed();  // the field shows the position again
        return false;
    }
    machine_.setWorkPosition(axis.front().toLatin1(), value);
    return true;
}

void DroModel::selectWorkspace(const QString& wcs) {
    if (workspaceEnabled()) {
        machine_.selectWorkspace(wcs);
    }
}

void DroModel::goToCorner(const QString& corner) {
    using controller::MachineCorner;
    const MachineCorner which = corner == "BackLeft"    ? MachineCorner::BackLeft
                                : corner == "BackRight" ? MachineCorner::BackRight
                                : corner == "FrontLeft" ? MachineCorner::FrontLeft
                                                        : MachineCorner::FrontRight;
    if (canClick() && homed()) {
        machine_.goToCorner(which);
    }
}

void DroModel::park() {
    if (canClick() && homed()) {
        machine_.goToPark();
    }
}

void DroModel::home() {
    if (controller::Controller* c = machine_.controller(); c && canClick()) {
        c->home();
    }
}

void DroModel::unlock() {
    if (controller::Controller* c = machine_.controller()) {
        c->unlock();
    }
}

void DroModel::reset() {
    if (controller::Controller* c = machine_.controller()) {
        c->reset();
    }
}

QVariantList DroModel::goToPrefill(const QString& mode) const {
    if (mode == "INC") {
        return {0.0, 0.0, 0.0, 0.0};
    }
    // As the DRO shows them. Deviation: upstream fills MCS with the raw mm
    // figures, also in an inch workspace.
    const std::array<double, 4> position =
        mode == "MCS" ? machine_.machinePositionMm() : machine_.workPositionMm();
    const app::AppSettings& s = machine_.settings();
    QVariantList values;
    for (std::size_t i = 0; i < 3; ++i) {
        values.append(js::stringToNumber(units::positionText(position[i], s.metric, s.customDecimalPlaces)));
    }
    values.append(rotaryMode() ? position[1] : position[3]);
    return values;
}

void DroModel::goTo(const QString& mode, double x, double y, double z, double a) {
    if (canClick() && (mode != "MCS" || homed())) {
        machine_.goToLocation(goToMode(mode), x, y, z, a);
    }
}

}  // namespace gs::ui
