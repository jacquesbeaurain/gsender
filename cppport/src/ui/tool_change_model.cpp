#include "tool_change_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"

namespace gs::ui {

ToolChangeModel::ToolChangeModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    connect(&machine_, &app::Machine::toolChangeWizardRequested, this,
            [this](const QString& option, int count, const QString& comment) {
                option_ = option;
                count_ = count;
                comment_ = comment;
                const app::AppSettings& settings = machine_.settings();
                // showFirstToolchangePrompt(): the first tool may only be measured.
                if (option == "Fixed Tool Sensor" && count <= 1 &&
                    settings.firstToolBehaviour == toolchange::kFirstToolBehaviours[1]) {
                    Q_EMIT firstToolQuestion(comment);
                    return;
                }
                start(true);
            });
    connect(&machine_, &app::Machine::wizardNext, this, &ToolChangeModel::actionDone);
    connect(&machine_, &app::Machine::toolChangeWaiting, this, &ToolChangeModel::codeChangeWaiting);
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::toolChangeWizardReady,
                        &app::Machine::connectionChanged}) {
        connect(&machine_, signal, this, &ToolChangeModel::changed);
    }
}

void ToolChangeModel::answerFirstTool(bool fullWizard) {
    start(fullWizard);
}

void ToolChangeModel::start(bool fullWizard) {
    std::optional<toolchange::Wizard> wizard =
        machine_.startToolChangeWizard(option_.toStdString(), count_, fullWizard);
    if (!wizard) {
        return;
    }
    wizard_ = std::move(wizard);
    step_ = substep_ = 0;
    running_ = false;
    done_.clear();
    Q_EMIT changed();
}

const toolchange::WizardSubstep* ToolChangeModel::currentSubstep() const {
    if (!wizard_ || step_ >= static_cast<int>(wizard_->steps.size())) {
        return nullptr;
    }
    const auto& substeps = wizard_->steps[static_cast<std::size_t>(step_)].substeps;
    return substep_ < static_cast<int>(substeps.size()) ? &substeps[static_cast<std::size_t>(substep_)] : nullptr;
}

QString ToolChangeModel::title() const {
    return wizard_ ? QString::fromStdString(wizard_->title) : QString();
}

QString ToolChangeModel::intro() const {
    return wizard_ ? QString::fromStdString(wizard_->intro) : QString();
}

QVariantList ToolChangeModel::steps() const {
    QVariantList list;
    if (!wizard_) {
        return list;
    }
    for (const toolchange::WizardStep& step : wizard_->steps) {
        QStringList substeps;
        for (const toolchange::WizardSubstep& substep : step.substeps) {
            substeps << QString::fromStdString(substep.title);
        }
        list.append(QVariantMap{{"title", QString::fromStdString(step.title)}, {"substeps", substeps}});
    }
    return list;
}

QVariantMap ToolChangeModel::current() const {
    const toolchange::WizardSubstep* substep = currentSubstep();
    if (!substep) {
        return {};
    }
    QStringList actions;
    for (const toolchange::WizardAction& action : substep->actions) {
        actions << QString::fromStdString(action.label);
    }
    return {{"title", QString::fromStdString(substep->title)},
            {"description", QString::fromStdString(substep->description)},
            {"toolBanner", substep->toolBanner},
            {"actions", actions}};
}

QString ToolChangeModel::toolLabel() const {
    const controller::Controller* c = machine_.controller();
    const QString tool = c ? QString::fromStdString(c->state().parserState.modal.tool).trimmed() : QString();
    if (tool.isEmpty()) {
        return {};
    }
    return tool.startsWith('T', Qt::CaseInsensitive) ? tool.toUpper() : "T" + tool;
}

bool ToolChangeModel::ready() const {
    return machine_.isToolChangeWizardReady();
}

bool ToolChangeModel::currentDone() const {
    return done_.count({step_, substep_}) != 0;
}

bool ToolChangeModel::canBack() const {
    return active() && !running_ && (step_ > 0 || substep_ > 0);
}

bool ToolChangeModel::canNext() const {
    // Instructions without actions, or whose action ran, can be passed - on
    // an idle machine.
    const toolchange::WizardSubstep* substep = currentSubstep();
    const controller::Controller* c = machine_.controller();
    const bool idle = c && c->state().status.activeState == "Idle";
    return substep && !running_ && idle && (substep->actions.empty() || currentDone());
}

bool ToolChangeModel::last() const {
    return wizard_ && step_ == static_cast<int>(wizard_->steps.size()) - 1 &&
           substep_ == static_cast<int>(wizard_->steps.back().substeps.size()) - 1;
}

int ToolChangeModel::flatCount() const {
    int count = 0;
    if (wizard_) {
        for (const auto& step : wizard_->steps) {
            count += static_cast<int>(step.substeps.size());
        }
    }
    return count;
}

int ToolChangeModel::flatIndex() const {
    int index = 0;
    if (wizard_) {
        for (int i = 0; i < step_; ++i) {
            index += static_cast<int>(wizard_->steps[static_cast<std::size_t>(i)].substeps.size());
        }
    }
    return index + substep_;
}

void ToolChangeModel::runAction(int index) {
    const toolchange::WizardSubstep* substep = currentSubstep();
    if (!substep || running_ || !ready() || index < 0 || index >= static_cast<int>(substep->actions.size())) {
        return;
    }
    running_ = true;
    machine_.runWizardAction(step_, substep_, substep->actions[static_cast<std::size_t>(index)].gcode);
    Q_EMIT changed();
}

// wizard:next: the action's G-code went through; move on (completeSubStep).
void ToolChangeModel::actionDone(int step, int substep) {
    if (!running_ || step != step_ || substep != substep_) {
        return;  // a stale report from an earlier wizard
    }
    running_ = false;
    done_.insert({step, substep});
    advance();
}

void ToolChangeModel::advance() {
    const auto& substeps = wizard_->steps[static_cast<std::size_t>(step_)].substeps;
    if (substep_ + 1 < static_cast<int>(substeps.size())) {
        ++substep_;
    } else if (step_ + 1 < static_cast<int>(wizard_->steps.size())) {
        ++step_;
        substep_ = 0;
    } else {
        wizard_.reset();  // the last action resumed the job
    }
    Q_EMIT changed();
}

void ToolChangeModel::next() {
    const toolchange::WizardSubstep* substep = currentSubstep();
    if (!substep || running_ || (!substep->actions.empty() && !currentDone())) {
        return;
    }
    done_.insert({step_, substep_});
    advance();
}

void ToolChangeModel::back() {
    if (!canBack()) {
        return;
    }
    if (substep_ > 0) {
        --substep_;
    } else {
        --step_;
        substep_ = static_cast<int>(wizard_->steps[static_cast<std::size_t>(step_)].substeps.size()) - 1;
    }
    Q_EMIT changed();
}

void ToolChangeModel::cancel() {
    wizard_.reset();
    running_ = false;
    done_.clear();
    Q_EMIT changed();
}

void ToolChangeModel::continueCodeChange() {
    if (controller::Controller* c = machine_.controller()) {
        c->toolChangePost();
    }
}

}  // namespace gs::ui
