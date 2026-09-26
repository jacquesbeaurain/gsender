#include "tool_change_service.hpp"
#include "machine.hpp"
#include "gs/controller/controller.hpp"

namespace gs::app {

ToolChangeService::ToolChangeService(Machine& machine, QObject* parent)
    : QObject(parent), machine_(machine) {}

bool ToolChangeService::isWizardStrategy(const std::string& option) {
    return option == "Standard Re-zero" || option == "Flexible Re-zero" || option == "Fixed Tool Sensor";
}

std::optional<toolchange::Wizard> ToolChangeService::startToolChangeWizard(const std::string& option, int count,
                                                                         bool fullFirstWizard) {
    controller::Controller* c = machine_.controller();
    if (!c || !isWizardStrategy(option)) {
        return std::nullopt;
    }
    const toolchange::ProbeSettings probe = toolchange::toolChangeProbeSettings(machine_.settings().probe);
    toolchange::MachineFacts facts;
    facts.reportInches = c->runner().setting("$13");
    facts.reportInches = facts.reportInches.empty() ? "0" : facts.reportInches;
    facts.softLimits = c->runner().setting("$20");
    facts.zMaxTravel = c->runner().setting("$132");
    facts.machineZ = c->runner().machinePosition()[2];
    facts.tool = c->runner().modal().tool;

    toolchange::Wizard wizard;
    if (option == "Standard Re-zero") {
        wizard = toolchange::standardRezero(probe, facts);
    } else if (option == "Flexible Re-zero") {
        wizard = toolchange::flexibleRezero(count, probe, facts);
    } else {
        const std::string& first = machine_.settings().firstToolBehaviour;
        const bool full = count > 1 || first == toolchange::kFirstToolBehaviours[0] ||
                          (first == toolchange::kFirstToolBehaviours[1] && fullFirstWizard);
        wizard = full ? toolchange::fixedToolSensor(count, probe, facts, machine_.settings().toolChangePosition,
                                                    machine_.settings().manualPosition, machine_.settings().moveToManualPosition)
                      : toolchange::probeToolLength(probe, facts, machine_.settings().toolChangePosition);
    }
    wizardReady_ = false;
    if (wizard.startDirect) {
        c->gcode(wizard.start);
        wizardReady_ = true;
    } else {
        std::string text;
        for (const std::string& line : wizard.start) {
            text += line;
            text += '\n';
        }
        c->wizardStart(text, [this] {
            wizardReady_ = true;
            Q_EMIT machine_.toolChangeWizardReady();
        });
    }
    return wizard;
}

void ToolChangeService::runWizardAction(int step, int substep, const std::vector<std::string>& gcode) {
    if (controller::Controller* c = machine_.controller()) {
        c->wizardStep(step, substep);
        c->gcode(gcode);
    }
}

void ToolChangeService::completeToolChangeWizard() {
}

void ToolChangeService::cancelToolChangeWizard() {
}

}  // namespace gs::app
