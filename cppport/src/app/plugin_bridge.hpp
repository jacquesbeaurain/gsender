#pragma once

#include "plugin_manifest.hpp"
#include "plugin_storage.hpp"

#include <QJsonObject>
#include <QObject>

namespace gs::app {

class Machine;

struct BridgeResponse {
    bool ok = false;
    QJsonObject result;
    QString error;

    static BridgeResponse success(const QJsonObject& result = {}) {
        return {true, result, QString()};
    }
    static BridgeResponse fail(const QString& error) {
        return {false, {}, error};
    }
};

/** Dispatches capability-governed bridge calls between plugins and gSender host services. */
class PluginBridge : public QObject {
    Q_OBJECT

public:
    explicit PluginBridge(Machine& machine, PluginStorage& storage, QObject* parent = nullptr);

    /** True if the plugin's manifest explicitly permits the request type. */
    bool hasCapability(const PluginManifest& manifest, const QString& requestType) const;

    /** True if the plugin's manifest explicitly permits subscribing to the topic. */
    bool hasTopic(const PluginManifest& manifest, const QString& topic) const;

    /** Executes a bridge request on behalf of a plugin, enforcing capabilities. */
    BridgeResponse execute(const PluginManifest& manifest, const QString& type, const QJsonObject& payload);

    /** Broadcasts a reactive topic update to interested plugins. */
    void broadcastTopic(const QString& topic, const QJsonObject& data);

Q_SIGNALS:
    void topicEvent(const QString& topic, const QJsonObject& data);
    void overlayMarkerUpdated(const QString& pluginId, const QJsonObject& markerData);
    void cameraViewRequested(const QString& viewPreset);

private:
    Machine& machine_;
    PluginStorage& storage_;

    BridgeResponse handleMachineCommand(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleMachineContext(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleWorkspaceState(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleGcodeLoad(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleStorage(const PluginManifest& manifest, const QString& type, const QJsonObject& payload);
    BridgeResponse handleViewer(const PluginManifest& manifest, const QString& type, const QJsonObject& payload);
};

}  // namespace gs::app
