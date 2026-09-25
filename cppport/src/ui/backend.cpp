#include "backend.hpp"

#include "jogger.hpp"
#include "machine.hpp"

#include "gs/controller/actions.hpp"
#include "gs/controller/controller.hpp"

#include <QJSEngine>
#include <QPointer>

namespace gs::ui {
namespace {

QPointer<UiBackend> gInstance;

}  // namespace

UiBackend::UiBackend(app::Machine& machine, QObject* parent)
    : QObject(parent), machine_(machine), jogger_(new app::Jogger(machine, this)) {
    connect(&machine_, &app::Machine::connectionChanged, this, &UiBackend::connectionChanged);
    connect(&machine_, &app::Machine::connectionChanged, this, &UiBackend::stateChanged);
    connect(&machine_, &app::Machine::stateChanged, this, &UiBackend::stateChanged);
    connect(&machine_, &app::Machine::programChanged, this, &UiBackend::programChanged);
    connect(&machine_, &app::Machine::appSettingsChanged, this, &UiBackend::appSettingsChanged);
}

void UiBackend::setInstance(UiBackend* backend) {
    gInstance = backend;
}

UiBackend* UiBackend::instance() {
    return gInstance.data();
}

UiBackend* UiBackend::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT(gInstance);
    // Owned by the application, not the engine.
    QJSEngine::setObjectOwnership(gInstance.data(), QJSEngine::CppOwnership);
    return gInstance.data();
}

bool UiBackend::connected() const {
    return machine_.isConnected();
}

bool UiBackend::connecting() const {
    return machine_.isConnecting();
}

QString UiBackend::portLabel() const {
    const QString port = machine_.port();
    if (app::Machine::isSimulatorPort(port)) {
        return tr("Simulator");
    }
    return port;
}

QString UiBackend::firmwareLabel() const {
    controller::Controller* c = machine_.controller();
    if (!c || !machine_.isConnected()) {
        return {};
    }
    return c->isGrblHal() ? QStringLiteral("grblHAL") : QStringLiteral("Grbl");
}

QString UiBackend::connectionKind() const {
    const QString port = machine_.port();
    if (app::Machine::isSimulatorPort(port)) {
        return QStringLiteral("simulator");
    }
    // An address is the Ethernet board (ConnectionStateIndicator's BsEthernet).
    const bool network = !port.isEmpty() && port.front().isDigit() && port.contains('.');
    return network ? QStringLiteral("ethernet") : QStringLiteral("usb");
}

QString UiBackend::activeState() const {
    controller::Controller* c = machine_.controller();
    return c && machine_.isConnected() ? QString::fromStdString(c->state().status.activeState) : QString();
}

QString UiBackend::stateText() const {
    const QString state = activeState();
    if (state.isEmpty()) {
        return tr("Disconnected");
    }
    return QString::fromStdString(controller::statusLabel(state.toStdString()));
}

QString UiBackend::alarmCode() const {
    controller::Controller* c = machine_.controller();
    return c && activeState() == "Alarm" ? QString::fromStdString(c->state().status.alarmCode) : QString();
}

bool UiBackend::hasProgram() const {
    return machine_.hasProgram();
}

QString UiBackend::programName() const {
    return machine_.programName();
}

bool UiBackend::darkMode() const {
    return machine_.settings().darkMode;
}

void UiBackend::setDarkMode(bool dark) {
    if (dark != darkMode()) {
        app::AppSettings settings = machine_.settings();
        settings.darkMode = dark;
        machine_.setSettings(settings);
    }
}

void UiBackend::connectSimulator(bool grblHal) {
    machine_.connectTo(grblHal ? app::Machine::kSimulatorHalPort : app::Machine::kSimulatorPort);
}

void UiBackend::disconnectMachine() {
    machine_.disconnectFromMachine();
}

}  // namespace gs::ui
