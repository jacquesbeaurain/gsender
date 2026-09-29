#include "plugin_wasm_host.hpp"
#include "machine.hpp"
#include "plugin_bridge.hpp"
#include "plugin_storage.hpp"

#include "gs/controller/controller.hpp"

#include <QJsonDocument>
#include <QDebug>

namespace gs::app {

PluginWasmHost::PluginWasmHost(Machine& machine, PluginBridge& bridge, PluginStorage& storage, const PluginManifest& manifest)
    : machine_(machine), bridge_(bridge), storage_(storage), manifest_(manifest) {}

PluginWasmHost::~PluginWasmHost() {
    shutdown();
}

QString PluginWasmHost::lastError() const {
    if (!instance_) return QStringLiteral("No instance");
    return QString::fromStdString(instance_->lastErrorMessage());
}

bool PluginWasmHost::loadBinary(const uint8_t* wasmBytes, size_t size, QString* outError) {
    std::string err;
    module_ = WasmModule::loadFromBytes(wasmBytes, size, &err);
    if (!module_) {
        if (outError) *outError = QString::fromStdString(err);
        return false;
    }

    instance_ = std::make_unique<WasmInstance>(module_);
    registerHostImports();

    if (!instance_->instantiate(&err)) {
        if (outError) *outError = QString::fromStdString(err);
        instance_.reset();
        module_.reset();
        return false;
    }

    // Ensure memory is large enough for scratch buffers
    const size_t neededBytes = ScratchRespOffset + ScratchBufSize;
    if (instance_->memory().sizeBytes() < neededBytes) {
        const uint32_t currentPages = instance_->memory().sizePages();
        const uint32_t targetPages = static_cast<uint32_t>((neededBytes + WasmMemory::PageSize - 1) / WasmMemory::PageSize);
        if (targetPages > currentPages) {
            instance_->memory().grow(targetPages - currentPages);
        }
    }

    return true;
}

bool PluginWasmHost::loadFile(const QString& filePath, QString* outError) {
    std::string err;
    module_ = WasmModule::loadFromFile(filePath, &err);
    if (!module_) {
        if (outError) *outError = QString::fromStdString(err);
        return false;
    }

    instance_ = std::make_unique<WasmInstance>(module_);
    registerHostImports();

    if (!instance_->instantiate(&err)) {
        if (outError) *outError = QString::fromStdString(err);
        instance_.reset();
        module_.reset();
        return false;
    }

    const size_t neededBytes = ScratchRespOffset + ScratchBufSize;
    if (instance_->memory().sizeBytes() < neededBytes) {
        const uint32_t currentPages = instance_->memory().sizePages();
        const uint32_t targetPages = static_cast<uint32_t>((neededBytes + WasmMemory::PageSize - 1) / WasmMemory::PageSize);
        if (targetPages > currentPages) {
            instance_->memory().grow(targetPages - currentPages);
        }
    }

    return true;
}

void PluginWasmHost::registerHostImports() {
    if (!instance_) return;

    // 1. gs_host_emit_gcode(const char* gcode) -> i32
    instance_->linkHostFunction("env", "gs_host_emit_gcode", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        if (args.empty()) return int32_t(-1);
        const uint32_t ptr = static_cast<uint32_t>(std::get<int32_t>(args[0]));
        const std::string gcode = inst.memory().readString(ptr);
        QJsonObject payload;
        payload[QStringLiteral("command")] = QString::fromStdString(gcode);
        const auto res = bridge_.execute(manifest_, QStringLiteral("machine:command"), payload);
        return int32_t(res.ok ? 0 : -1);
    });

    // 2. gs_host_get_wpos(gs_dro_coords_t* out_wpos) -> i32
    instance_->linkHostFunction("env", "gs_host_get_wpos", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        if (args.empty()) return int32_t(-1);
        const uint32_t ptr = static_cast<uint32_t>(std::get<int32_t>(args[0]));
        if (!bridge_.hasCapability(manifest_, QStringLiteral("workspace:get:state"))) {
            return int32_t(-1);
        }

        controller::Controller* c = machine_.controller();
        double coords[4] = {0.0, 0.0, 0.0, 0.0};
        if (c) {
            const auto& status = c->state().status;
            coords[0] = status.wpos.x();
            coords[1] = status.wpos.y();
            coords[2] = status.wpos.z();
            coords[3] = status.wpos.a();
        }

        if (!inst.memory().write(ptr, coords, sizeof(coords))) {
            return int32_t(-1);
        }
        return int32_t(0);
    });

    // 3. gs_host_storage_get(const char* key, char* out_buf, int32_t buf_len) -> i32
    instance_->linkHostFunction("env", "gs_host_storage_get", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        if (args.size() < 3) return int32_t(-1);
        const uint32_t keyPtr = static_cast<uint32_t>(std::get<int32_t>(args[0]));
        const uint32_t bufPtr = static_cast<uint32_t>(std::get<int32_t>(args[1]));
        const int32_t bufLen = std::get<int32_t>(args[2]);

        if (!bridge_.hasCapability(manifest_, QStringLiteral("storage:get"))) {
            return int32_t(-1);
        }

        const std::string key = inst.memory().readString(keyPtr);
        const auto val = storage_.get(manifest_.id, QString::fromStdString(key));
        if (!val.has_value()) return int32_t(-1);

        const std::string valStr = val->toStdString();
        if (valStr.size() + 1 > static_cast<size_t>(bufLen)) return int32_t(-1);

        if (!inst.memory().writeString(bufPtr, valStr, bufLen)) return int32_t(-1);
        return int32_t(valStr.size());
    });

    // 4. gs_host_storage_set(const char* key, const char* value) -> i32
    instance_->linkHostFunction("env", "gs_host_storage_set", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        if (args.size() < 2) return int32_t(-1);
        const uint32_t keyPtr = static_cast<uint32_t>(std::get<int32_t>(args[0]));
        const uint32_t valPtr = static_cast<uint32_t>(std::get<int32_t>(args[1]));

        if (!bridge_.hasCapability(manifest_, QStringLiteral("storage:set"))) {
            return int32_t(-1);
        }

        const std::string key = inst.memory().readString(keyPtr);
        const std::string val = inst.memory().readString(valPtr);
        const bool ok = storage_.set(manifest_.id, QString::fromStdString(key), QString::fromStdString(val));
        return int32_t(ok ? 0 : -1);
    });

    // 5. gs_host_storage_delete(const char* key) -> i32
    instance_->linkHostFunction("env", "gs_host_storage_delete", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        if (args.empty()) return int32_t(-1);
        const uint32_t keyPtr = static_cast<uint32_t>(std::get<int32_t>(args[0]));

        if (!bridge_.hasCapability(manifest_, QStringLiteral("storage:delete"))) {
            return int32_t(-1);
        }

        const std::string key = inst.memory().readString(keyPtr);
        const bool ok = storage_.deleteKey(manifest_.id, QString::fromStdString(key));
        return int32_t(ok ? 0 : -1);
    });

    // 6. gs_host_load_gcode(const char* gcode_text, const char* name) -> i32
    instance_->linkHostFunction("env", "gs_host_load_gcode", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        if (args.size() < 2) return int32_t(-1);
        const uint32_t gcodePtr = static_cast<uint32_t>(std::get<int32_t>(args[0]));
        const uint32_t namePtr = static_cast<uint32_t>(std::get<int32_t>(args[1]));

        if (!bridge_.hasCapability(manifest_, QStringLiteral("gcode:load:to:visualizer"))) {
            return int32_t(-1);
        }

        const std::string gcode = inst.memory().readString(gcodePtr, 1024 * 1024);
        const std::string name = inst.memory().readString(namePtr);
        QJsonObject payload;
        payload[QStringLiteral("gcode")] = QString::fromStdString(gcode);
        payload[QStringLiteral("name")] = QString::fromStdString(name);
        const auto res = bridge_.execute(manifest_, QStringLiteral("gcode:load:to:visualizer"), payload);
        return int32_t(res.ok ? 0 : -1);
    });

    // 7. gs_host_log(int32_t level, const char* message) -> void
    instance_->linkHostFunction("env", "gs_host_log", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        if (args.size() < 2) return std::nullopt;
        const int32_t level = std::get<int32_t>(args[0]);
        const uint32_t msgPtr = static_cast<uint32_t>(std::get<int32_t>(args[1]));
        const std::string msg = inst.memory().readString(msgPtr);
        qDebug() << "[WasmPlugin:" << manifest_.id << "lvl=" << level << "]" << QString::fromStdString(msg);
        return std::nullopt;
    });
}

bool PluginWasmHost::init() {
    if (!instance_) return false;
    auto res = instance_->invoke("gsender_plugin_init");
    if (!res.has_value()) return false;
    return std::get<int32_t>(*res) == 0;
}

void PluginWasmHost::shutdown() {
    if (instance_) {
        instance_->invoke("gsender_plugin_shutdown");
    }
}

QString PluginWasmHost::handleRequest(const QString& requestJson) {
    if (!instance_) return QStringLiteral("{\"ok\":false,\"error\":\"No Wasm instance\"}");

    const std::string reqStr = requestJson.toStdString();
    if (!instance_->memory().writeString(ScratchReqOffset, reqStr, ScratchBufSize)) {
        return QStringLiteral("{\"ok\":false,\"error\":\"Scratch memory write failed\"}");
    }

    auto res = instance_->invoke("gsender_plugin_handle_request", {
        int32_t(ScratchReqOffset),
        int32_t(ScratchRespOffset),
        int32_t(ScratchBufSize)
    });

    if (!res.has_value() || std::get<int32_t>(*res) < 0) {
        return QStringLiteral("{\"ok\":false,\"error\":\"Wasm handle_request trap: %1\"}")
            .arg(QString::fromStdString(instance_->lastErrorMessage()));
    }

    const std::string resp = instance_->memory().readString(ScratchRespOffset, ScratchBufSize);
    return QString::fromStdString(resp);
}

void PluginWasmHost::onTopicEvent(const QString& topic, const QJsonObject& data) {
    if (!instance_) return;
    if (!bridge_.hasTopic(manifest_, topic)) return;

    const std::string topicStr = topic.toStdString();
    const std::string dataStr = QJsonDocument(data).toJson(QJsonDocument::Compact).toStdString();

    const uint32_t topicOffset = ScratchReqOffset;
    const uint32_t dataOffset = ScratchReqOffset + 256;

    if (instance_->memory().writeString(topicOffset, topicStr, 256) &&
        instance_->memory().writeString(dataOffset, dataStr, ScratchBufSize - 256)) {
        instance_->invoke("gsender_plugin_on_topic_event", {
            int32_t(topicOffset),
            int32_t(dataOffset)
        });
    }
}

}  // namespace gs::app
