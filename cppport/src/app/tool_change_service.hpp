#pragma once

#include "gs/toolchange/wizards.hpp"

#include <QObject>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace gs::app {

class Machine;

class ToolChangeService : public QObject {
    Q_OBJECT

public:
    explicit ToolChangeService(Machine& machine, QObject* parent = nullptr);

    static bool isWizardStrategy(const std::string& option);

    std::optional<toolchange::Wizard> startToolChangeWizard(const std::string& option, int count,
                                                            bool fullFirstWizard = true);
    bool isToolChangeWizardReady() const noexcept { return wizardReady_; }
    void setToolChangeWizardReady(bool ready) noexcept { wizardReady_ = ready; }
    void runWizardAction(int step, int substep, const std::vector<std::string>& gcode);
    void completeToolChangeWizard();
    void cancelToolChangeWizard();

private:
    Machine& machine_;
    bool wizardReady_ = false;
};

}  // namespace gs::app
