#pragma once

#include "plugin_manifest.hpp"
#include "plugin_parsers.hpp"
#include "plugin_storage.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>

#include <optional>

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

/** What plugins may do to the main visualizer (upstream's visualizerBridge handle).
 *  Screen coordinates are pixels of the visualizer, world ones millimetres. */
class PluginViewer {
public:
    struct WorldPoint {
        double x = 0, y = 0, z = 0;
    };

    virtual ~PluginViewer() = default;

    /** The point of the work plane (Z 0) under a pixel; nothing when the plane is edge-on. */
    virtual std::optional<WorldPoint> screenToWorld(double px, double py) const = 0;
    virtual std::optional<QPointF> worldToScreen(const WorldPoint& world) const = 0;
    /** "3d", "top", "front", "left" or "right"; false for anything else. */
    virtual bool setCameraView(const QString& view) = 0;
    virtual void setRotateEnabled(bool enabled) = 0;
    virtual bool isRotaryFile() const = 0;
    /** "click" or "hold". Picks come back through PluginBridge::viewerPicked. */
    virtual void armPick(const QString& mode) = 0;
    virtual void disarmPick() = 0;
    /** One plugin's markers ({id, x, y, z?, shape?, color?, size?, label?}); empty clears them. */
    virtual void setOverlay(const QString& pluginId, const QJsonArray& markers) = 0;
};

/** Dispatches capability-governed bridge calls between plugins and gSender host services. */
class PluginBridge : public QObject {
    Q_OBJECT

public:
    explicit PluginBridge(Machine& machine, PluginStorage& storage, QObject* parent = nullptr);
    ~PluginBridge() override;

    /** True if the plugin's manifest explicitly permits the request type. */
    bool hasCapability(const PluginManifest& manifest, const QString& requestType) const;

    /** True if the plugin's manifest explicitly permits subscribing to the topic. */
    bool hasTopic(const PluginManifest& manifest, const QString& topic) const;

    /** Topics addressed to one plugin (its parsers' matches, its queries' replies):
     *  delivered to that plugin only, without a topic grant. */
    static bool isPrivateTopic(const QString& topic);

    /** Whether a plugin gets an event on the topic: a private one, or a granted broadcast. */
    bool mayReceive(const PluginManifest& manifest, const QString& topic) const;

    /** Executes a bridge request on behalf of a plugin, enforcing capabilities. */
    BridgeResponse execute(const PluginManifest& manifest, const QString& type, const QJsonObject& payload);

    /** Broadcasts a reactive topic update to interested plugins. */
    void broadcastTopic(const QString& topic, const QJsonObject& data);

    /** The raw-line parsers plugins declare or register. */
    PluginParserChain& parsers() noexcept { return *parsers_; }

    /** A plugin was disabled or unloaded: drop its parsers, overlay and latch. */
    void releasePlugin(const QString& pluginId);

    /** The main visualizer, while it exists (it registers itself). */
    void setViewer(PluginViewer* viewer);
    PluginViewer* viewer() const noexcept { return viewer_; }
    /** The visualizer reports a pick or a hold's progress (0..1); both go out on "viewer". */
    void viewerPicked(const PluginViewer::WorldPoint& world, QPointF screen);
    void viewerHoldProgress(double t);

    /** The plugin-asserted busy latch (machine:busy:set). */
    bool busy() const noexcept { return busy_; }
    QString busyLabel() const { return busyLabel_; }

    // The latch's timings (upstream's): sustained Idle before it releases,
    // how long it waits for motion to start, and an absolute cap.
    static constexpr int BusyIdleDebounceMs = 1500;
    static constexpr int BusyArmGraceMs = 15000;
    static constexpr int BusyMaxMs = 15 * 60 * 1000;

Q_SIGNALS:
    /** An event for plugins: to one plugin, or (empty id) to every plugin granted the topic. */
    void pluginEvent(const QString& pluginId, const QString& topic, const QJsonObject& data);
    void busyChanged();

private:
    Machine& machine_;
    PluginStorage& storage_;
    PluginParserChain* parsers_ = nullptr;
    PluginViewer* viewer_ = nullptr;
    int nextQueryId_ = 1;

    bool busy_ = false;
    bool busyArmed_ = false;
    QString busyLabel_;
    QString busyOwner_;
    QTimer busyIdleTimer_;
    QTimer busyGraceTimer_;
    QTimer busyMaxTimer_;

    bool machineIdle() const;
    void releaseBusy();
    void onBusyStateChange();

    BridgeResponse handleMachineCommand(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleMachineContext(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleMachineQuery(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleParserRequest(const PluginManifest& manifest, const QString& type, const QJsonObject& payload);
    BridgeResponse handleBusy(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleWorkspaceState(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleGcodeLoad(const PluginManifest& manifest, const QJsonObject& payload);
    BridgeResponse handleStorage(const PluginManifest& manifest, const QString& type, const QJsonObject& payload);
    BridgeResponse handleViewer(const PluginManifest& manifest, const QString& type, const QJsonObject& payload);
};

}  // namespace gs::app
