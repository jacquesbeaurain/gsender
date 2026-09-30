#include "remote_model.hpp"

#include "backend.hpp"
#include "remote_service.hpp"

#include <QVariantMap>

namespace gs::ui {

RemoteModel::RemoteModel(QObject* parent) : RemoteModel(UiBackend::instance()->remote(), parent) {}

RemoteModel::RemoteModel(app::RemoteService& service, QObject* parent) : QObject(parent), service_(service) {
    connect(&service_, &app::RemoteService::changed, this, &RemoteModel::changed);
}

bool RemoteModel::enabled() const {
    return service_.settings().headlessStatus;
}

QString RemoteModel::savedIp() const {
    return QString::fromStdString(service_.settings().ip);
}

int RemoteModel::savedPort() const {
    return service_.settings().port;
}

bool RemoteModel::savedError() const {
    return service_.settings().error;
}

bool RemoteModel::running() const {
    return service_.running();
}

QString RemoteModel::url() const {
    return service_.url();
}

int RemoteModel::clients() const {
    return service_.clientCount();
}

QString RemoteModel::error() const {
    return service_.lastError();
}

QString RemoteModel::recommended() const {
    for (const QVariant& v : addresses_) {
        const QVariantMap m = v.toMap();
        if (m.value("recommended").toBool()) {
            return m.value("address").toString();
        }
    }
    return {};
}

void RemoteModel::refreshAddresses() {
    addresses_.clear();
    for (const remote::NetworkAddress& a : service_.addresses()) {
        addresses_.append(QVariantMap{{"address", QString::fromStdString(a.address)},
                                      {"label", QString::fromStdString(a.label)},
                                      {"iface", QString::fromStdString(a.iface)},
                                      {"usable", a.usable},
                                      {"recommended", a.recommended}});
    }
    Q_EMIT addressesChanged();
}

QString RemoteModel::initialIp() const {
    if (const QString saved = savedIp(); !saved.isEmpty()) {
        return saved;
    }
    if (const QString best = recommended(); !best.isEmpty()) {
        return best;
    }
    for (const QVariant& v : addresses_) {
        if (v.toMap().value("usable").toBool()) {
            return v.toMap().value("address").toString();
        }
    }
    return QStringLiteral("127.0.0.1");
}

QVariantMap RemoteModel::entry(const QString& ip) const {
    for (const QVariant& v : addresses_) {
        if (v.toMap().value("address").toString() == ip) {
            return v.toMap();
        }
    }
    return {};
}

QString RemoteModel::save(const QString& ip, int port, bool on) {
    remote::RemoteSettings settings = service_.settings();
    settings.ip = ip.toStdString();
    settings.port = port;
    settings.headlessStatus = on;
    const QString error = service_.apply(settings);
    Q_EMIT changed();
    return error;
}

QString RemoteModel::pendantUrl(const QString& ip, int port) const {
    return QString::fromStdString(remote::pendantUrl(ip.toStdString(), port));
}

}  // namespace gs::ui
