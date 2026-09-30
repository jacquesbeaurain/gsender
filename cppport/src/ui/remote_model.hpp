#pragma once

// The Wireless CNC Control dialog (features/RemoteMode) and the top bar's
// remote indicator: remote mode's saved settings, the address picker, and
// the pendant's address for the QR code. Saving applies at once (upstream
// restarted the application).

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class RemoteService;
}

namespace gs::ui {

class RemoteModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // The saved settings.
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QString savedIp READ savedIp NOTIFY changed)
    Q_PROPERTY(int savedPort READ savedPort NOTIFY changed)
    // Remote mode was switched off because its address could not be bound.
    Q_PROPERTY(bool savedError READ savedError NOTIFY changed)
    // The server as it runs now.
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString url READ url NOTIFY changed)
    Q_PROPERTY(int clients READ clients NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // The address picker: {address, label, iface, usable, recommended},
    // the recommended one first (refreshAddresses()).
    Q_PROPERTY(QVariantList addresses READ addresses NOTIFY addressesChanged)
    Q_PROPERTY(QString recommended READ recommended NOTIFY addressesChanged)

public:
    explicit RemoteModel(QObject* parent = nullptr);
    RemoteModel(app::RemoteService& service, QObject* parent = nullptr);

    bool enabled() const;
    QString savedIp() const;
    int savedPort() const;
    bool savedError() const;
    bool running() const;
    QString url() const;
    int clients() const;
    QString error() const;
    QVariantList addresses() const { return addresses_; }
    QString recommended() const;

    Q_INVOKABLE void refreshAddresses();
    // The address to start the dialog on: the saved one, else the
    // recommended one, else the first usable one.
    Q_INVOKABLE QString initialIp() const;
    // The address's entry, or an empty map when the computer no longer has
    // it (a laptop that has changed networks).
    Q_INVOKABLE QVariantMap entry(const QString& ip) const;
    // Save: validates, saves and applies. Empty on success, else the message.
    Q_INVOKABLE QString save(const QString& ip, int port, bool enabled);
    // The pendant's address for the QR code ("http://ip:port/#/remote").
    Q_INVOKABLE QString pendantUrl(const QString& ip, int port) const;

Q_SIGNALS:
    void changed();
    void addressesChanged();

private:
    app::RemoteService& service_;
    QVariantList addresses_;
};

}  // namespace gs::ui
