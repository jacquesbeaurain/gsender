#include "plugin_bridge.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"

#include <QJsonArray>

namespace gs::app {

PluginBridge::PluginBridge(Machine& machine, PluginStorage& storage, QObject* parent)
    : QObject(parent), machine_(machine), storage_(storage) {}

bool PluginBridge::hasCapability(const PluginManifest& manifest, const QString& requestType) const {
    return manifest.capabilities.requestTypes.contains(requestType);
}

bool PluginBridge::hasTopic(const PluginManifest& manifest, const QString& topic) const {
    return manifest.capabilities.topics.contains(topic);
}

BridgeResponse PluginBridge::execute(const PluginManifest& manifest, const QString& type, const QJsonObject& payload) {
    if (!hasCapability(manifest, type)) {
        return BridgeResponse::fail(QStringLiteral("Permission denied: plugin '%1' lacks capability '%2'")
                                        .arg(manifest.id, type));
    }

    if (type == QStringLiteral("machine:command")) {
        return handleMachineCommand(manifest, payload);
    }
    if (type == QStringLiteral("machine:get:context")) {
        return handleMachineContext(manifest, payload);
    }
    if (type == QStringLiteral("workspace:get:state")) {
        return handleWorkspaceState(manifest, payload);
    }
    if (type == QStringLiteral("gcode:load:to:visualizer")) {
        return handleGcodeLoad(manifest, payload);
    }
    if (type.startsWith(QStringLiteral("storage:"))) {
        return handleStorage(manifest, type, payload);
    }
    if (type.startsWith(QStringLiteral("viewer:"))) {
        return handleViewer(manifest, type, payload);
    }

    return BridgeResponse::fail(QStringLiteral("Unsupported bridge request type: %1").arg(type));
}

BridgeResponse PluginBridge::handleMachineCommand(const PluginManifest& /*manifest*/, const QJsonObject& payload) {
    const QString cmd = payload.value(QStringLiteral("command")).toString().trimmed();
    if (cmd.isEmpty()) {
        return BridgeResponse::fail(QStringLiteral("Missing or empty 'command' in payload"));
    }

    controller::Controller* c = machine_.controller();
    if (!c || !machine_.isConnected()) {
        return BridgeResponse::fail(QStringLiteral("Machine is not connected"));
    }

    c->gcode(cmd.toStdString());
    return BridgeResponse::success();
}

BridgeResponse PluginBridge::handleMachineContext(const PluginManifest& /*manifest*/, const QJsonObject& /*payload*/) {
    QJsonObject res;
    res.insert(QStringLiteral("connected"), machine_.isConnected());

    controller::Controller* c = machine_.controller();
    if (c) {
        const auto& state = c->state();
        res.insert(QStringLiteral("activeState"), QString::fromStdString(state.status.activeState));
        res.insert(QStringLiteral("isGrblHal"), c->isGrblHal());
    } else {
        res.insert(QStringLiteral("activeState"), QStringLiteral("Disconnected"));
        res.insert(QStringLiteral("isGrblHal"), false);
    }

    return BridgeResponse::success(res);
}

BridgeResponse PluginBridge::handleWorkspaceState(const PluginManifest& /*manifest*/, const QJsonObject& /*payload*/) {
    QJsonObject res;
    const bool metric = machine_.settings().metric;
    res.insert(QStringLiteral("units"), metric ? QStringLiteral("mm") : QStringLiteral("in"));

    controller::Controller* c = machine_.controller();
    if (c) {
        const auto& status = c->state().status;
        const double factor = metric ? 1.0 : 1.0 / 25.4;

        QJsonObject wpos;
        wpos.insert(QStringLiteral("x"), status.wpos.x() * factor);
        wpos.insert(QStringLiteral("y"), status.wpos.y() * factor);
        wpos.insert(QStringLiteral("z"), status.wpos.z() * factor);
        wpos.insert(QStringLiteral("a"), status.wpos.a());
        res.insert(QStringLiteral("wpos"), wpos);

        QJsonObject mpos;
        mpos.insert(QStringLiteral("x"), status.mpos.x() * factor);
        mpos.insert(QStringLiteral("y"), status.mpos.y() * factor);
        mpos.insert(QStringLiteral("z"), status.mpos.z() * factor);
        mpos.insert(QStringLiteral("a"), status.mpos.a());
        res.insert(QStringLiteral("mpos"), mpos);
    }

    return BridgeResponse::success(res);
}

BridgeResponse PluginBridge::handleGcodeLoad(const PluginManifest& manifest, const QJsonObject& payload) {
    const QString gcode = payload.value(QStringLiteral("gcode")).toString();
    if (gcode.isEmpty()) {
        return BridgeResponse::fail(QStringLiteral("Missing or empty 'gcode' in payload"));
    }

    QString name = payload.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        name = manifest.name + QStringLiteral(".gcode");
    }

    machine_.loadProgram(name, gcode.toStdString());
    return BridgeResponse::success();
}

BridgeResponse PluginBridge::handleStorage(const PluginManifest& manifest, const QString& type, const QJsonObject& payload) {
    if (type == QStringLiteral("storage:get")) {
        const QString key = payload.value(QStringLiteral("key")).toString();
        const auto val = storage_.get(manifest.id, key);
        QJsonObject res;
        res.insert(QStringLiteral("found"), val.has_value());
        if (val.has_value()) {
            res.insert(QStringLiteral("value"), *val);
        }
        return BridgeResponse::success(res);
    }
    if (type == QStringLiteral("storage:set")) {
        const QString key = payload.value(QStringLiteral("key")).toString();
        const QString val = payload.value(QStringLiteral("value")).toString();
        const bool ok = storage_.set(manifest.id, key, val);
        return ok ? BridgeResponse::success() : BridgeResponse::fail(QStringLiteral("Failed to write to storage"));
    }
    if (type == QStringLiteral("storage:delete")) {
        const QString key = payload.value(QStringLiteral("key")).toString();
        const bool ok = storage_.deleteKey(manifest.id, key);
        return ok ? BridgeResponse::success() : BridgeResponse::fail(QStringLiteral("Failed to delete key"));
    }
    if (type == QStringLiteral("storage:get:all")) {
        const auto map = storage_.getAll(manifest.id);
        QJsonObject res;
        for (auto it = map.begin(); it != map.end(); ++it) {
            res.insert(it.key(), it.value());
        }
        return BridgeResponse::success(res);
    }
    if (type == QStringLiteral("storage:set:all")) {
        const QJsonObject entries = payload.value(QStringLiteral("entries")).toObject();
        QMap<QString, QString> map;
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            map.insert(it.key(), it.value().toString());
        }
        const bool ok = storage_.setAll(manifest.id, map);
        return ok ? BridgeResponse::success() : BridgeResponse::fail(QStringLiteral("Failed to set all keys"));
    }
    if (type == QStringLiteral("storage:clear")) {
        const bool ok = storage_.clear(manifest.id);
        return ok ? BridgeResponse::success() : BridgeResponse::fail(QStringLiteral("Failed to clear storage"));
    }

    return BridgeResponse::fail(QStringLiteral("Unknown storage request: %1").arg(type));
}

BridgeResponse PluginBridge::handleViewer(const PluginManifest& manifest, const QString& type, const QJsonObject& payload) {
    if (type == QStringLiteral("viewer:camera:set")) {
        const QString preset = payload.value(QStringLiteral("view")).toString();
        Q_EMIT cameraViewRequested(preset);
        return BridgeResponse::success();
    }
    if (type == QStringLiteral("viewer:overlay:set")) {
        Q_EMIT overlayMarkerUpdated(manifest.id, payload);
        return BridgeResponse::success();
    }

    return BridgeResponse::fail(QStringLiteral("Unknown viewer request: %1").arg(type));
}

void PluginBridge::broadcastTopic(const QString& topic, const QJsonObject& data) {
    Q_EMIT topicEvent(topic, data);
}

}  // namespace gs::app
