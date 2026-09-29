#pragma once

#include "plugin_bridge.hpp"
#include "plugin_manifest.hpp"
#include "plugin_storage.hpp"

#include <QObject>
#include <QSet>
#include <QStringList>
#include <memory>
#include <vector>

namespace gs::app {

class Machine;

struct LoadedPlugin {
    PluginManifest manifest;
    QString directory;
    bool enabled = true;
};

/** Coordinates discovery, lifecycle, storage, and bridge communication for plugins. */
class PluginService : public QObject {
    Q_OBJECT

public:
    explicit PluginService(Machine& machine, const QString& storageDir, QObject* parent = nullptr);
    ~PluginService() override;

    /** Adds a directory path to search for installed plugins. */
    void addSearchPath(const QString& path);

    /** Scans all registered search paths for plugin manifests. */
    void scanPlugins();

    /** Currently discovered plugins. */
    const std::vector<LoadedPlugin>& plugins() const { return plugins_; }

    /** Find a loaded plugin by its unique ID. */
    const LoadedPlugin* findPlugin(const QString& id) const;

    bool isPluginEnabled(const QString& id) const;
    void setPluginEnabled(const QString& id, bool enabled);

    PluginBridge& bridge() { return bridge_; }
    PluginStorage& storage() { return storage_; }

    /** Returns all enabled contributions matching a specific slot (e.g. "tools-page"). */
    std::vector<std::pair<LoadedPlugin, PluginContribution>> contributionsForSlot(const QString& slot) const;

    /** Access the active Wasm execution host for a plugin, if running. */
    class PluginWasmHost* wasmHost(const QString& id) const;

    /** Dispatches an RPC request string into a plugin's Wasm runtime. */
    QString executeWasmRequest(const QString& id, const QString& requestJson);

Q_SIGNALS:
    void pluginsChanged();

private:
    Machine& machine_;
    PluginStorage storage_;
    PluginBridge bridge_;
    QStringList searchPaths_;
    std::vector<LoadedPlugin> plugins_;
    QSet<QString> disabledPluginIds_;
    std::map<QString, std::unique_ptr<class PluginWasmHost>> wasmHosts_;

    void setupTopicBroadcasters();
};

}  // namespace gs::app
