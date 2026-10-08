#include "flash_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include <QFileInfo>
#include <QTime>
#include <QTimerEvent>
#include <QVariantMap>

namespace gs::ui {

FlashModel::FlashModel(QObject* parent) : UiModelBase(parent) {
    if (UiBackend* backend = UiBackend::instance()) {
        connect(backend, &UiBackend::simulatorEnabledChanged, this, &FlashModel::portsChanged);
    }
}

QStringList FlashModel::ports() const {
    UiBackend* backend = UiBackend::instance();
    if (!backend || !backend->simulatorEnabled()) {
        return {};
    }
    return {app::Machine::kSimulatorPort, app::Machine::kSimulatorHalPort};
}

bool FlashModel::available() const {
    return !ports().isEmpty();
}

bool FlashModel::canStart(const QString& port, const QString& controllerType, const QUrl& file) const {
    if (port.isEmpty() || state_ == Flashing) {
        return false;
    }
    return controllerType == QLatin1String("grbl") || !file.isEmpty();
}

QString FlashModel::start(const QString& port, const QString& controllerType, const QUrl& file) {
    if (!canStart(port, controllerType, file)) {
        return tr("No port specified - please connect to the device to determine what is being flashed.");
    }
    if (!ports().contains(port)) {
        return tr("Firmware flashing of real boards is not available in this version of gSender.");
    }
    hal_ = controllerType != QLatin1String("grbl");
    file_ = file.isEmpty() ? QString() : QFileInfo(file.toLocalFile()).fileName();
    log_.clear();
    progress_ = 0;
    // The flash takes the machine's port: it is disconnected first.
    if (machine_.isConnected() || machine_.isConnecting()) {
        machine_.disconnectFromMachine();
    }
    add(QStringLiteral("Info"), tr("Flashing %1 firmware to %2").arg(hal_ ? "grblHAL" : "Grbl", port));
    setState(Flashing);
    Q_EMIT progressChanged();
    timer_.start(tickMs_, this);
    return {};
}

void FlashModel::reset() {
    timer_.stop();
    log_.clear();
    progress_ = 0;
    Q_EMIT logChanged();
    Q_EMIT progressChanged();
    setState(Idle);
}

void FlashModel::timerEvent(QTimerEvent* event) {
    if (event->timerId() != timer_.timerId()) {
        UiModelBase::timerEvent(event);
        return;
    }
    ++progress_;
    switch (progress_) {
    case 1:
        add(QStringLiteral("Info"), hal_ ? tr("Entering the bootloader") : tr("Opening the board's bootloader"));
        break;
    case 10:
        add(QStringLiteral("Info"), tr("Erasing the flash"));
        break;
    case 25:
        add(QStringLiteral("Info"), file_.isEmpty() ? tr("Writing the bundled firmware") : tr("Writing %1").arg(file_));
        break;
    case 85:
        add(QStringLiteral("Info"), tr("Verifying"));
        break;
    default:
        break;
    }
    Q_EMIT progressChanged();
    if (progress_ >= kTotal) {
        timer_.stop();
        add(QStringLiteral("Success"), tr("Flash completed, please reconnect to your device."));
        setState(Complete);
    }
}

void FlashModel::add(const QString& type, const QString& content) {
    log_.prepend(QVariantMap{{"time", QTime::currentTime().toString(Qt::TextDate)}, {"type", type}, {"content", content}});
    Q_EMIT logChanged();
}

void FlashModel::setState(State state) {
    if (state != state_) {
        state_ = state;
        Q_EMIT stateChanged();
    }
}

}  // namespace gs::ui
