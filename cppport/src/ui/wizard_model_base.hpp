#pragma once

#include "ui_model_base.hpp"
#include <QtQml/qqmlregistration.h>

namespace gs::ui {

class WizardModelBase : public UiModelBase {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int stepIndex READ stepIndex WRITE setStepIndex NOTIFY stepChanged)
    Q_PROPERTY(int substepIndex READ substepIndex WRITE setSubstepIndex NOTIFY stepChanged)
    Q_PROPERTY(int totalSteps READ totalSteps NOTIFY stepsConfigurationChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY canNextChanged)
    Q_PROPERTY(bool canBack READ canBack NOTIFY canBackChanged)
    Q_PROPERTY(bool isLastStep READ isLastStep NOTIFY stepChanged)
    Q_PROPERTY(bool isFirstStep READ isFirstStep NOTIFY stepChanged)

public:
    explicit WizardModelBase(QObject* parent = nullptr);

    int stepIndex() const { return stepIndex_; }
    virtual void setStepIndex(int step);

    int substepIndex() const { return substepIndex_; }
    virtual void setSubstepIndex(int substep);

    virtual int totalSteps() const { return totalSteps_; }
    virtual bool canNext() const { return canNext_; }
    virtual bool canBack() const { return stepIndex_ > 0 || substepIndex_ > 0; }
    virtual bool isLastStep() const { return totalSteps_ > 0 && stepIndex_ >= totalSteps_ - 1; }
    virtual bool isFirstStep() const { return stepIndex_ == 0 && substepIndex_ == 0; }

    Q_INVOKABLE virtual void next();
    Q_INVOKABLE virtual void back();
    Q_INVOKABLE virtual void restart();
    Q_INVOKABLE virtual void goToStep(int step, int substep = 0);

Q_SIGNALS:
    void stepChanged();
    void canNextChanged();
    void canBackChanged();
    void stepsConfigurationChanged();
    void wizardFinished();
    void wizardCancelled();

protected:
    void setTotalSteps(int total);
    void setCanNext(bool can);

    int stepIndex_ = 0;
    int substepIndex_ = 0;
    int totalSteps_ = 0;
    bool canNext_ = false;
};

}  // namespace gs::ui
