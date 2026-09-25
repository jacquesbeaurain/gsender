#include "connection_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/transport/port_list.hpp"

#include <QVariantMap>

namespace gs::ui {
namespace {

// PortListings' truncatePortName: the last path element's last ten characters.
QString portName(const QString& path) {
    const QString last = path.section('/', -1);
    return last.size() > 10 ? last.right(10) : last;
}

}  // namespace

ConnectionModel::ConnectionModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    connect(&machine_, &app::Machine::connectionChanged, this, [this] {
        if (machine_.isConnected() || machine_.isConnecting()) {
            error_.clear();
        }
        Q_EMIT changed();
    });
    connect(&machine_, &app::Machine::connectionFailed, this, [this](const QString& reason) {
        error_ = reason.isEmpty() ? tr("Unable to connect.") : reason;
        Q_EMIT changed();
    });
    connect(&machine_, &app::Machine::appSettingsChanged, this, &ConnectionModel::changed);
    refresh();
}

QString ConnectionModel::state() const {
    if (machine_.isConnected()) {
        return QStringLiteral("connected");
    }
    if (machine_.isConnecting()) {
        return QStringLiteral("connecting");
    }
    return error_.isEmpty() ? QStringLiteral("disconnected") : QStringLiteral("error");
}

QString ConnectionModel::ethernetAddress() const {
    return QString::fromStdString(machine_.settings().ethernetAddress());
}

int ConnectionModel::ethernetPort() const {
    return machine_.settings().networkPort;
}

int ConnectionModel::baudRate() const {
    return machine_.settings().baudRate;
}

void ConnectionModel::refresh() {
    ports_.clear();
    unrecognized_.clear();
    for (const transport::SerialPortInfo& port : transport::listSerialPorts()) {
        QVariantMap entry;
        const QString path = QString::fromStdString(port.path);
        entry["path"] = path;
        entry["name"] = portName(path);
        entry["detail"] = QString::fromStdString(port.manufacturer.empty() ? port.friendlyName : port.manufacturer);
        (transport::isRecognizedPort(port.vendorId, port.productId) ? ports_ : unrecognized_).append(entry);
    }
    Q_EMIT portsChanged();
}

void ConnectionModel::connectTo(const QString& path) {
    if (path.isEmpty() || machine_.isConnected() || machine_.isConnecting()) {
        return;
    }
    error_.clear();
    app::AppSettings settings = machine_.settings();
    settings.port = path.toStdString();
    machine_.setSettings(settings);
    machine_.connectTo(path, settings.baudRate, settings.networkPort);
    Q_EMIT changed();
}

void ConnectionModel::connectEthernet() {
    connectTo(ethernetAddress());
}

void ConnectionModel::disconnectMachine() {
    machine_.disconnectFromMachine();
}

}  // namespace gs::ui
