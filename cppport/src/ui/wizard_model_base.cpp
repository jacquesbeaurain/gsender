#include "wizard_model_base.hpp"

namespace gs::ui {

WizardModelBase::WizardModelBase(QObject* parent) : UiModelBase(parent) {}

void WizardModelBase::setStepIndex(int step) {
    if (stepIndex_ != step) {
        stepIndex_ = step;
        Q_EMIT stepChanged();
        Q_EMIT changed();
    }
}

void WizardModelBase::setSubstepIndex(int substep) {
    if (substepIndex_ != substep) {
        substepIndex_ = substep;
        Q_EMIT stepChanged();
        Q_EMIT changed();
    }
}

void WizardModelBase::setTotalSteps(int total) {
    if (totalSteps_ != total) {
        totalSteps_ = total;
        Q_EMIT stepsConfigurationChanged();
        Q_EMIT changed();
    }
}

void WizardModelBase::setCanNext(bool can) {
    if (canNext_ != can) {
        canNext_ = can;
        Q_EMIT canNextChanged();
        Q_EMIT changed();
    }
}

void WizardModelBase::next() {
    if (canNext() && stepIndex_ + 1 < totalSteps_) {
        setStepIndex(stepIndex_ + 1);
        setSubstepIndex(0);
    }
}

void WizardModelBase::back() {
    if (canBack()) {
        if (substepIndex_ > 0) {
            setSubstepIndex(substepIndex_ - 1);
        } else if (stepIndex_ > 0) {
            setStepIndex(stepIndex_ - 1);
            setSubstepIndex(0);
        }
    }
}

void WizardModelBase::restart() {
    stepIndex_ = 0;
    substepIndex_ = 0;
    Q_EMIT stepChanged();
    Q_EMIT changed();
}

void WizardModelBase::goToStep(int step, int substep) {
    stepIndex_ = step;
    substepIndex_ = substep;
    Q_EMIT stepChanged();
    Q_EMIT changed();
}

}  // namespace gs::ui
