#include "plugin_qml_context.hpp"

#include <QJSEngine>
#include <QJsonObject>
#include <QJsonValue>

namespace gs::ui {

PluginQmlContext::PluginQmlContext(app::PluginService& service, const QString& pluginId, QObject* parent)
    : QObject(parent), service_(service), pluginId_(pluginId) {
    connect(&service_.bridge(), &app::PluginBridge::topicEvent, this, &PluginQmlContext::onTopicEvent);
}

const app::LoadedPlugin* PluginQmlContext::plugin() const {
    return service_.findPlugin(pluginId_);
}

QString PluginQmlContext::pluginName() const {
    const auto* p = plugin();
    return p ? p->manifest.name : QString();
}

QString PluginQmlContext::version() const {
    const auto* p = plugin();
    return p ? p->manifest.version : QString();
}

bool PluginQmlContext::official() const {
    const auto* p = plugin();
    return p ? p->manifest.isOfficial() : false;
}

QString PluginQmlContext::directory() const {
    const auto* p = plugin();
    return p ? p->directory : QString();
}

QVariantMap PluginQmlContext::send(const QString& type, const QVariantMap& payload, const QJSValue& callback) {
    const auto* p = plugin();
    if (!p) {
        QVariantMap err;
        err[QStringLiteral("ok")] = false;
        err[QStringLiteral("error")] = QStringLiteral("Plugin is not loaded");
        if (callback.isCallable()) {
            QJSEngine* engine = qjsEngine(this);
            if (engine) {
                const_cast<QJSValue&>(callback).call({engine->toScriptValue(err)});
            }
        }
        return err;
    }

    const QJsonObject jsonPayload = QJsonObject::fromVariantMap(payload);
    const auto res = service_.bridge().execute(p->manifest, type, jsonPayload);

    QVariantMap result;
    result[QStringLiteral("ok")] = res.ok;
    result[QStringLiteral("result")] = res.result.toVariantMap();
    result[QStringLiteral("error")] = res.error;

    if (callback.isCallable()) {
        QJSEngine* engine = qjsEngine(this);
        if (engine) {
            const_cast<QJSValue&>(callback).call({engine->toScriptValue(result)});
        }
    }
    return result;
}

void PluginQmlContext::subscribe(const QString& topic, const QJSValue& callback) {
    const auto* p = plugin();
    if (!p || !service_.bridge().hasTopic(p->manifest, topic)) {
        return;
    }
    if (callback.isCallable()) {
        topicCallbacks_[topic].append(callback);
    }
}

QVariant PluginQmlContext::storageGet(const QString& key, const QVariant& fallback) {
    QJsonObject payload;
    payload[QStringLiteral("key")] = key;
    const auto* p = plugin();
    if (!p) return fallback;
    const auto res = service_.bridge().execute(p->manifest, QStringLiteral("storage:get"), payload);
    if (!res.ok) return fallback;
    return res.result.value(QStringLiteral("value")).toVariant();
}

void PluginQmlContext::storageSet(const QString& key, const QVariant& value) {
    QJsonObject payload;
    payload[QStringLiteral("key")] = key;
    payload[QStringLiteral("value")] = value.toString();
    const auto* p = plugin();
    if (p) {
        service_.bridge().execute(p->manifest, QStringLiteral("storage:set"), payload);
    }
}

void PluginQmlContext::storageDelete(const QString& key) {
    QJsonObject payload;
    payload[QStringLiteral("key")] = key;
    const auto* p = plugin();
    if (p) {
        service_.bridge().execute(p->manifest, QStringLiteral("storage:delete"), payload);
    }
}

QVariantMap PluginQmlContext::storageGetAll() {
    const auto* p = plugin();
    if (!p) return {};
    const auto res = service_.bridge().execute(p->manifest, QStringLiteral("storage:get:all"), {});
    if (!res.ok) return {};
    return res.result.value(QStringLiteral("entries")).toObject().toVariantMap();
}

void PluginQmlContext::storageClear() {
    const auto* p = plugin();
    if (p) {
        service_.bridge().execute(p->manifest, QStringLiteral("storage:clear"), {});
    }
}

void PluginQmlContext::onTopicEvent(const QString& topic, const QJsonObject& data) {
    const auto it = topicCallbacks_.find(topic);
    const QVariantMap varData = data.toVariantMap();
    if (it != topicCallbacks_.end()) {
        QJSEngine* engine = qjsEngine(this);
        for (QJSValue& cb : it.value()) {
            if (cb.isCallable() && engine) {
                cb.call({engine->toScriptValue(varData)});
            }
        }
    }
    Q_EMIT topicReceived(topic, varData);
}

}  // namespace gs::ui
