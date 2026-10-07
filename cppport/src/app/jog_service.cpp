#include "jog_service.hpp"
#include "machine.hpp"
#include "gs/controller/actions.hpp"
#include "gs/controller/controller.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

namespace gs::app {

namespace {

controller::LocationSettings locationSettings(const controller::Controller& c) {
    const protocol::Runner& runner = c.runner();
    return {runner.setting("$22"), runner.setting("$23"), runner.setting("$27"), runner.setting("$130"),
            runner.setting("$131")};
}

}  // namespace

JogService::JogService(Machine& machine, QObject* parent)
    : QObject(parent), machine_(machine) {}

void JogService::zeroAxis(char axis) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcode(controller::zeroAxisCommand(axis));
    }
}

void JogService::zeroAllAxes() {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    const bool hasA = c->state().axes.letters.find('A') != std::string::npos;
    for (const std::string& command : controller::zeroAllCommands(c->isGrblHal(), hasA)) {
        c->gcode(command);
    }
}

void JogService::goToZero(std::string_view axes) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    const std::string homing = c->runner().setting("$22");
    const bool homingEnabled = !homing.empty() && js::stringToNumber(homing) != 0;
    c->gcodeSafe(controller::goToZeroCommands(axes, homingEnabled, machine_.settings().safeRetractHeight,
                                              c->runner().machinePosition()[2]),
                 "G21");
}

std::array<double, 4> JogService::workPositionMm() const {
    std::array<double, 4> out{};
    if (const controller::Controller* c = machine_.controller()) {
        const bool inches = c->runner().setting("$13") == "1";
        for (std::size_t i = 0; i < 4; ++i) {
            const double value = c->state().status.wpos.axis("xyza"[i]);
            out[i] = inches && i < 3 ? units::in2mm(value) : value;
        }
    }
    return out;
}

std::array<double, 4> JogService::machinePositionMm() const {
    std::array<double, 4> out{};
    if (const controller::Controller* c = machine_.controller()) {
        const bool inches = c->runner().setting("$13") == "1";
        for (std::size_t i = 0; i < 4; ++i) {
            const double value = c->state().status.mpos.axis("xyza"[i]);
            out[i] = inches && i < 3 ? units::in2mm(value) : value;
        }
    }
    return out;
}

bool JogService::canMove() const {
    controller::Controller* c = machine_.controller();
    if (!c || c->workflow().isRunning()) {
        return false;
    }
    const std::string& state = c->state().status.activeState;
    return state == "Idle" || state == "Jog";
}

bool JogService::homingEnabled() const {
    const controller::Controller* c = machine_.controller();
    return c && js::stringToNumber(c->runner().setting("$22", "0")) > 0;
}

bool JogService::singleAxisHoming() const {
    const controller::Controller* c = machine_.controller();
    return c && controller::singleAxisHomingEnabled(c->runner().setting("$22", "0"));
}

void JogService::selectWorkspace(const QString& wcs) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcode(wcs.toStdString());
    }
}

void JogService::setWorkPosition(char axis, double value) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcodeSafe({controller::manualOffsetCommand(axis, value)}, machine_.settings().metric ? "G21" : "G20");
    }
}

void JogService::homeAxis(char axis) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcode(controller::homeAxisCommand(axis));
    }
}

void JogService::goToCorner(controller::MachineCorner corner) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    const controller::LocationSettings settings = locationSettings(*c);
    const std::vector<std::string> gcode =
        controller::cornerCommands(corner, settings, c->homingFlag(), settings.pullOffDistance(), c->isGrblHal());
    if (gcode.empty()) {
        Q_EMIT machine_.notice(tr("Unable to find machine limits - make sure they're set in preferences"));
        return;
    }
    c->gcode(gcode);
}

void JogService::goToPark() {
    if (controller::Controller* c = machine_.controller()) {
        const toolchange::MachinePosition& park = machine_.settings().park;
        c->gcode(controller::parkCommands({park.x, park.y, park.z}, locationSettings(*c)));
    }
}

void JogService::goToMachinePosition(const toolchange::MachinePosition& position) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcode(controller::locationCommands({position.x, position.y, position.z}, locationSettings(*c)));
    }
}

void JogService::goToLocation(controller::GoToMode mode, double x, double y, double z, double a) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    controller::GoToLocation location = locationState(*c);
    location.mode = mode;
    location.x = x;
    location.y = y;
    location.z = z;
    location.a = a;
    c->gcodeSafe(controller::goToLocationCommands(location), machine_.settings().metric ? "G21" : "G20");
}

void JogService::moveToHere(double xMm, double yMm) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    // Deviation: upstream sends the picked millimetres under G20 in an inch
    // workspace; here they are converted.
    const bool metric = machine_.settings().metric;
    controller::GoToLocation location = locationState(*c);
    location.x = metric ? xMm : units::convertToImperial(xMm);
    location.y = metric ? yMm : units::convertToImperial(yMm);
    c->gcodeSafe(controller::safeXYMoveCommands(location), metric ? "G21" : "G20");
}

controller::GoToLocation JogService::locationState(controller::Controller& c) const {
    controller::GoToLocation location;
    location.yAvailable = !machine_.settings().rotary.rotaryMode;
    location.aAvailable =
        machine_.settings().rotary.rotaryMode || (c.isGrblHal() && c.state().axes.letters.find('A') != std::string::npos);
    location.metric = machine_.settings().metric;
    location.homingEnabled = js::stringToNumber(c.runner().setting("$22", "0")) != 0;
    location.safeRetractHeight = machine_.settings().safeRetractHeight;
    location.machineZ = machinePositionMm()[2];
    const double workZ = workPositionMm()[2];
    location.workZ = machine_.settings().metric ? workZ : units::convertToImperial(workZ);
    return location;
}

}  // namespace gs::app
