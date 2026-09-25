#pragma once

// The connection (features/Connection) for QML: the ports to offer - boards
// gSender recognizes, the Ethernet board at "Connect to IP", unrecognized
// ports, and the built-in simulators - and the connection's state.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class ConnectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // "disconnected", "connecting", "connected" or "error" (the last attempt
    // failed; the error says why).
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // Ports: {path, name (PortListings' last ten characters), detail}.
    Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(QVariantList unrecognizedPorts READ unrecognizedPorts NOTIFY portsChanged)
    Q_PROPERTY(QString ethernetAddress READ ethernetAddress NOTIFY changed)
    Q_PROPERTY(int ethernetPort READ ethernetPort NOTIFY changed)
    Q_PROPERTY(int baudRate READ baudRate NOTIFY changed)

public:
    explicit ConnectionModel(QObject* parent = nullptr);

    QString state() const;
    QString error() const { return error_; }
    QVariantList ports() const { return ports_; }
    QVariantList unrecognizedPorts() const { return unrecognized_; }
    QString ethernetAddress() const;
    int ethernetPort() const;
    int baudRate() const;

    // Lists the serial ports again (the popup does as it opens).
    Q_INVOKABLE void refresh();
    // A port, an address or a simulator; remembered as the last used.
    Q_INVOKABLE void connectTo(const QString& path);
    Q_INVOKABLE void connectEthernet();
    Q_INVOKABLE void disconnectMachine();

Q_SIGNALS:
    void changed();
    void portsChanged();

private:
    app::Machine& machine_;
    QVariantList ports_;
    QVariantList unrecognized_;
    QString error_;
};

}  // namespace gs::ui
