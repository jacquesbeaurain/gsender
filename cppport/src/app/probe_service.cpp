#include "probe_service.hpp"
#include "machine.hpp"
#include "rotary_actions.hpp"
#include "gs/controller/controller.hpp"
#include "gs/sim/grbl_simulator.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

namespace gs::app {

ProbeService::ProbeService(Machine& machine, QObject* parent)
    : QObject(parent), machine_(machine) {}

std::vector<std::string> ProbeService::probeRoutine(probe::Axes axes, probe::ProbeType type, double toolDiameter,
                                                    int corner) const {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return {};
    }
    const auto setting = [&](const char* key, const char* fallback) {
        const std::string value = c->runner().setting(key);
        return value.empty() ? std::string(fallback) : value;
    };
    probe::MachineFacts facts;
    facts.reportInches = setting("$13", "0");
    facts.homing = setting("$22", "0");
    facts.zMaxTravel = setting("$132", "0");
    facts.machineZ = c->runner().machinePosition()[2];
    const probe::ProbingOptions options =
        probe::makeProbingOptions(machine_.settings().probe, machine_.settings().metric, axes, type, toolDiameter, facts);
    return probe::probeCode(options, corner);
}

bool ProbeService::runProbe(std::vector<std::string> code) {
    controller::Controller* c = machine_.controller();
    if (!c || code.empty() || !c->workflow().isIdle()) {
        return false;
    }
    code.push_back(c->runner().modal().distance);
    c->gcodeSafe(code, "G21");
    machine_.probing_ = true;
    return true;
}

bool ProbeService::probeTriggered() const {
    controller::Controller* c = machine_.controller();
    return c && c->state().status.probeActive;
}

void ProbeService::placeSimulatedPlate(probe::ProbeType type, double toolDiameter, int corner, probe::Axes axes) {
    sim::GrblSimulator* simulator = machine_.simulator();
    if (!simulator) {
        return;
    }
    const sim::SimAxes at = simulator->machinePosition();
    const probe::ProbeSettings& p = machine_.settings().probe;
    const double sx = corner == probe::kBottomLeft || corner == probe::kTopLeft ? 1 : -1;
    const double sy = corner == probe::kBottomLeft || corner == probe::kBottomRight ? 1 : -1;
    const double diameterMm = machine_.settings().metric ? toolDiameter : units::in2mm(toolDiameter);
    double radius = type == probe::ProbeType::Diameter ? diameterMm / 2 : 0;
    std::vector<sim::Solid> solids;
    switch (p.plateType) {
        case probe::PlateType::StandardBlock: {
            const double thickness = p.zThickness.standardBlock;
            solids = sim::touchPlateOnCorner(corner, at[0] + 5 * sx, at[1] + 5 * sy, at[2] - 10 - thickness, thickness,
                                             p.xyThickness);
            break;
        }
        case probe::PlateType::ZProbe: {
            const double top = at[2] - 10;
            solids = {sim::Solid{{at[0] - 25, at[1] - 25, top - p.zThickness.zProbe}, {at[0] + 25, at[1] + 25, top}}};
            break;
        }
        case probe::PlateType::Probe3D:
            radius = p.tipDiameter3D / 2;
            solids = sim::touchPlateOnCorner(corner, at[0] - 5 * sx, at[1] - 5 * sy, at[2] - 10, 0, 0, 100, 30);
            break;
        case probe::PlateType::AutoZero: {
            const double floor = at[2] - 10;
            solids = {sim::Solid{{at[0] - 35, at[1] - 35, floor - 5}, {at[0] + 35, at[1] + 35, floor}}};
            const auto ring = [&](double inner, double bottom, double top) {
                solids.push_back({{at[0] - 35, at[1] - 35, bottom}, {at[0] - inner, at[1] + 35, top}});
                solids.push_back({{at[0] + inner, at[1] - 35, bottom}, {at[0] + 35, at[1] + 35, top}});
                solids.push_back({{at[0] - inner, at[1] - 35, bottom}, {at[0] + inner, at[1] - inner, top}});
                solids.push_back({{at[0] - inner, at[1] + inner, bottom}, {at[0] + inner, at[1] + 35, top}});
            };
            ring(10, floor, floor + 1);
            ring(20, floor + 1, floor + 12);
            break;
        }
        case probe::PlateType::BitZero: {
            const double thickness = 13;
            double cx = at[0] - 1;
            double cy = at[1] + 0.5;
            double stockTop = at[2] - 5;
            if (!axes.x && !axes.y) {
                cx = at[0] - 20 * sx;
                cy = at[1] - 20 * sy;
                stockTop = at[2] - 10 - thickness;
            }
            const double top = stockTop + thickness;
            solids = {
                {{cx - 30, cy - 30, stockTop}, {cx - 10, cy + 30, top}},
                {{cx + 10, cy - 30, stockTop}, {cx + 30, cy + 30, top}},
                {{cx - 10, cy - 30, stockTop}, {cx + 10, cy - 10, top}},
                {{cx - 10, cy + 10, stockTop}, {cx + 10, cy + 30, top}},
            };
            break;
        }
    }
    simulator->setProbeSolids(std::move(solids));
    simulator->setToolRadius(radius);
}

bool ProbeService::runRotaryProbe(bool yAlignment) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return false;
    }
    const bool inches = c->runner().setting("$13") == "1";
    c->gcodeSafe(yAlignment ? rotary::yAxisAlignmentProbing(inches) : rotary::zAxisProbing(inches),
                 inches ? "G20" : "G21");
    return true;
}

}  // namespace gs::app
