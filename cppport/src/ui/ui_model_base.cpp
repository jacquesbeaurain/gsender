#include "ui_model_base.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"
#include "gs/util/units.hpp"

namespace gs::ui {

UiModelBase::UiModelBase(QObject* parent)
    : QObject(parent), machine_(UiBackend::instance()->machine()) {}

UiModelBase::UiModelBase(app::Machine& machine, QObject* parent)
    : QObject(parent), machine_(machine) {}

bool UiModelBase::connected() const {
    return machine_.controller() != nullptr && machine_.isConnected();
}

bool UiModelBase::metric() const {
    return machine_.settings().metric;
}

QString UiModelBase::units() const {
    return metric() ? QStringLiteral("mm") : QStringLiteral("in");
}

bool UiModelBase::canClick() const {
    return machine_.canMove();
}

QString UiModelBase::positionText(double mm) const {
    const app::AppSettings& s = machine_.settings();
    return QString::fromStdString(units::positionText(mm, s.metric, s.customDecimalPlaces));
}

QString UiModelBase::positionText(double mm, char axis) const {
    if (axis == 'A' || axis == 'a') {
        return QString::number(mm, 'f', 3);
    }
    return positionText(mm);
}

void UiModelBase::connectMachineSignals(bool includeSettings, bool includeWorkflow) {
    connect(&machine_, &app::Machine::stateChanged, this, &UiModelBase::changed);
    connect(&machine_, &app::Machine::connectionChanged, this, &UiModelBase::changed);
    connect(&machine_, &app::Machine::appSettingsChanged, this, &UiModelBase::changed);
    if (includeSettings) {
        connect(&machine_, &app::Machine::settingsChanged, this, &UiModelBase::changed);
    }
    if (includeWorkflow) {
        connect(&machine_, &app::Machine::workflowChanged, this, &UiModelBase::changed);
    }
}

}  // namespace gs::ui
