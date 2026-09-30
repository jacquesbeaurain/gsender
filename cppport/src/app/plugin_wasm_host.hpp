#pragma once

#include "wasm_engine.hpp"
#include "plugin_manifest.hpp"

#include <QJsonObject>
#include <QString>

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace gs::app {

class Machine;
class PluginBridge;
class PluginStorage;

/** Runs one plugin's Wasm module: links the SDK's host imports (each
 *  capability-checked through the bridge) and calls the plugin's exports.
 *  Strings go into the plugin's memory through its own allocator
 *  (gsender_plugin_alloc / gsender_plugin_free). */
class PluginWasmHost {
public:
    PluginWasmHost(Machine& machine, PluginBridge& bridge, PluginStorage& storage, const PluginManifest& manifest);
    ~PluginWasmHost();

    bool loadBinary(const uint8_t* wasmBytes, size_t size, QString* outError = nullptr);
    bool loadFile(const QString& filePath, QString* outError = nullptr);

    bool init();
    void shutdown();

    /** Execute RPC request via Wasm export `gsender_plugin_handle_request`. */
    QString handleRequest(const QString& requestJson);

    /** Dispatch topic snapshot via Wasm export `gsender_plugin_on_topic_event`
     *  (only for topics the manifest grants; queued while the plugin is running). */
    void onTopicEvent(const QString& topic, const QJsonObject& data);

    bool isLoaded() const noexcept { return instance_ != nullptr; }
    bool hasTrap() const noexcept { return instance_ && instance_->lastTrap() != WasmTrap::None; }
    QString lastError() const;

    const PluginManifest& manifest() const noexcept { return manifest_; }

    /** Largest response a plugin may write per request. */
    static constexpr int32_t ResponseBufferSize = 256 * 1024;

private:
    bool instantiate(std::shared_ptr<WasmModule> module, QString* outError);
    void registerHostImports();
    std::optional<uint32_t> copyIn(const std::string& text);
    void release(uint32_t ptr);
    void flushTopicEvents();

    Machine& machine_;
    PluginBridge& bridge_;
    PluginStorage& storage_;
    PluginManifest manifest_;

    std::shared_ptr<WasmModule> module_;
    std::unique_ptr<WasmInstance> instance_;
    bool initialized_ = false;
    bool running_ = false;
    std::deque<std::pair<QString, QJsonObject>> pendingTopics_;
    QString lastError_;
};

}  // namespace gs::app
