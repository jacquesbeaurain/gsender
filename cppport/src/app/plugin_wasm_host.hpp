#pragma once

#include "wasm_engine.hpp"
#include "plugin_manifest.hpp"

#include <QString>
#include <QJsonObject>
#include <memory>
#include <string>

namespace gs::app {

class Machine;
class PluginBridge;
class PluginStorage;

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

    /** Dispatch topic snapshot via Wasm export `gsender_plugin_on_topic_event`. */
    void onTopicEvent(const QString& topic, const QJsonObject& data);

    bool isLoaded() const noexcept { return instance_ != nullptr; }
    bool hasTrap() const noexcept { return instance_ && instance_->lastTrap() != WasmTrap::None; }
    QString lastError() const;

    const PluginManifest& manifest() const noexcept { return manifest_; }

private:
    void registerHostImports();

    Machine& machine_;
    PluginBridge& bridge_;
    PluginStorage& storage_;
    PluginManifest manifest_;

    std::shared_ptr<WasmModule> module_;
    std::unique_ptr<WasmInstance> instance_;

    // Scratch memory offset inside linear memory for string passing
    static constexpr uint32_t ScratchReqOffset = 1024;
    static constexpr uint32_t ScratchRespOffset = 8192;
    static constexpr uint32_t ScratchBufSize = 65536;
};

}  // namespace gs::app
