#pragma once

#include "plugin_service.hpp"
#include <QJSValue>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace gs::ui {

/** The 'gsender' bridge context object injected into loaded plugin QML components. */
class PluginQmlContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("PluginQmlContext is provided by the plugin host")

    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(QString pluginName READ pluginName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool official READ official CONSTANT)
    Q_PROPERTY(QString directory READ directory CONSTANT)

public:
    PluginQmlContext(app::PluginService& service, const QString& pluginId, QObject* parent = nullptr);
    ~PluginQmlContext() override = default;

    QString pluginId() const { return pluginId_; }
    QString pluginName() const;
    QString version() const;
    bool official() const;
    QString directory() const;

    /** Sends a capability-enforced request to the host. Supports callback or direct return. */
    Q_INVOKABLE QVariantMap send(const QString& type, const QVariantMap& payload = {}, const QJSValue& callback = QJSValue());

    /** Subscribes to a broadcast topic (e.g. "workspace", "controller"). */
    Q_INVOKABLE void subscribe(const QString& topic, const QJSValue& callback);

    /** Convenience capability-enforced storage accessors. */
    Q_INVOKABLE QVariant storageGet(const QString& key, const QVariant& fallback = QVariant());
    Q_INVOKABLE void storageSet(const QString& key, const QVariant& value);
    Q_INVOKABLE void storageDelete(const QString& key);
    Q_INVOKABLE QVariantMap storageGetAll();
    Q_INVOKABLE void storageClear();

Q_SIGNALS:
    void topicReceived(const QString& topic, const QVariantMap& data);

private:
    app::PluginService& service_;
    QString pluginId_;
    QMap<QString, QList<QJSValue>> topicCallbacks_;

    const app::LoadedPlugin* plugin() const;
    void onTopicEvent(const QString& topic, const QJsonObject& data);
};

}  // namespace gs::ui
