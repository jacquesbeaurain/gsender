#include "plugin_wasm_host.hpp"
#include "machine.hpp"
#include "plugin_bridge.hpp"
#include "plugin_storage.hpp"

#include "gs/controller/controller.hpp"

#include <QDebug>
#include <QJsonDocument>

namespace gs::app {

namespace {

// Strings the host reads out of a plugin: keys and names are short, G-code
// and JSON payloads can be long.
constexpr size_t kMaxKeyLength = 4096;
constexpr size_t kMaxTextLength = 16 * 1024 * 1024;

uint32_t pointerArg(const std::vector<WasmVal>& args, size_t i) {
    return static_cast<uint32_t>(std::get<int32_t>(args.at(i)));
}

QString readText(WasmInstance& inst, uint32_t ptr, size_t maxLen) {
    return QString::fromUtf8(inst.memory().readString(ptr, maxLen));
}

}  // namespace

PluginWasmHost::PluginWasmHost(Machine& machine, PluginBridge& bridge, PluginStorage& storage, const PluginManifest& manifest)
    : machine_(machine), bridge_(bridge), storage_(storage), manifest_(manifest) {}

PluginWasmHost::~PluginWasmHost() {
    shutdown();
}

QString PluginWasmHost::lastError() const {
    if (!lastError_.isEmpty()) return lastError_;
    if (!instance_) return QStringLiteral("No instance");
    return QString::fromStdString(instance_->lastErrorMessage());
}

bool PluginWasmHost::loadBinary(const uint8_t* wasmBytes, size_t size, QString* outError) {
    std::string err;
    auto module = WasmModule::loadFromBytes(wasmBytes, size, &err);
    if (!module) {
        if (outError) *outError = QString::fromStdString(err);
        return false;
    }
    return instantiate(std::move(module), outError);
}

bool PluginWasmHost::loadFile(const QString& filePath, QString* outError) {
    std::string err;
    auto module = WasmModule::loadFromFile(filePath, &err);
    if (!module) {
        if (outError) *outError = QString::fromStdString(err);
        return false;
    }
    return instantiate(std::move(module), outError);
}

bool PluginWasmHost::instantiate(std::shared_ptr<WasmModule> module, QString* outError) {
    auto fail = [&](const QString& message) {
        instance_.reset();
        module_.reset();
        lastError_ = message;
        if (outError) *outError = message;
        return false;
    };

    for (const char* required : {"gsender_plugin_alloc", "gsender_plugin_free"}) {
        const WasmExport* exp = module->findExport(required);
        if (!exp || exp->kind != 0) {
            return fail(QStringLiteral("Plugin does not export %1 (link the SDK's runtime.c, or export an allocator)")
                            .arg(QString::fromLatin1(required)));
        }
    }
    if (!module->hasMemory()) {
        return fail(QStringLiteral("Plugin has no linear memory"));
    }

    module_ = std::move(module);
    instance_ = std::make_unique<WasmInstance>(module_);
    // The plugin runs on the UI thread: keep a single call well under a second.
    instance_->setFuelPerCall(100'000'000);
    registerHostImports();

    std::string err;
    if (!instance_->instantiate(&err)) {
        return fail(QString::fromStdString(err));
    }
    lastError_.clear();
    return true;
}

std::optional<uint32_t> PluginWasmHost::copyIn(const std::string& text) {
    const auto ptr = instance_->invoke("gsender_plugin_alloc", {static_cast<int32_t>(text.size() + 1)});
    if (!ptr) return std::nullopt;
    const uint32_t addr = static_cast<uint32_t>(std::get<int32_t>(*ptr));
    if (addr == 0 || !instance_->memory().writeString(addr, text, text.size() + 1)) {
        return std::nullopt;
    }
    return addr;
}

void PluginWasmHost::release(uint32_t ptr) {
    if (ptr != 0) {
        instance_->invoke("gsender_plugin_free", {static_cast<int32_t>(ptr)});
    }
}

void PluginWasmHost::registerHostImports() {
    if (!instance_) return;

    instance_->linkHostFunction("env", "gs_host_emit_gcode", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        QJsonObject payload;
        payload[QStringLiteral("command")] = readText(inst, pointerArg(args, 0), kMaxTextLength);
        return int32_t(bridge_.execute(manifest_, QStringLiteral("machine:command"), payload).ok ? 0 : -1);
    });

    instance_->linkHostFunction("env", "gs_host_get_wpos", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        if (!bridge_.hasCapability(manifest_, QStringLiteral("workspace:get:state"))) {
            return int32_t(-1);
        }
        double coords[4] = {0.0, 0.0, 0.0, 0.0};
        if (controller::Controller* c = machine_.controller()) {
            const auto& status = c->state().status;
            coords[0] = status.wpos.x();
            coords[1] = status.wpos.y();
            coords[2] = status.wpos.z();
            coords[3] = status.wpos.a();
        }
        return int32_t(inst.memory().write(pointerArg(args, 0), coords, sizeof(coords)) ? 0 : -1);
    });

    instance_->linkHostFunction("env", "gs_host_storage_get", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        const int32_t bufLen = std::get<int32_t>(args.at(2));
        if (!bridge_.hasCapability(manifest_, QStringLiteral("storage:get")) || bufLen <= 0) {
            return int32_t(-1);
        }
        const auto val = storage_.get(manifest_.id, readText(inst, pointerArg(args, 0), kMaxKeyLength));
        if (!val) return int32_t(-1);
        const std::string text = val->toStdString();
        if (text.size() + 1 > static_cast<size_t>(bufLen)) return int32_t(-1);
        if (!inst.memory().writeString(pointerArg(args, 1), text, static_cast<size_t>(bufLen))) return int32_t(-1);
        return static_cast<int32_t>(text.size());
    });

    instance_->linkHostFunction("env", "gs_host_storage_set", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        QJsonObject payload;
        payload[QStringLiteral("key")] = readText(inst, pointerArg(args, 0), kMaxKeyLength);
        payload[QStringLiteral("value")] = readText(inst, pointerArg(args, 1), kMaxTextLength);
        return int32_t(bridge_.execute(manifest_, QStringLiteral("storage:set"), payload).ok ? 0 : -1);
    });

    instance_->linkHostFunction("env", "gs_host_storage_delete", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        QJsonObject payload;
        payload[QStringLiteral("key")] = readText(inst, pointerArg(args, 0), kMaxKeyLength);
        return int32_t(bridge_.execute(manifest_, QStringLiteral("storage:delete"), payload).ok ? 0 : -1);
    });

    instance_->linkHostFunction("env", "gs_host_load_gcode", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        QJsonObject payload;
        payload[QStringLiteral("gcode")] = readText(inst, pointerArg(args, 0), kMaxTextLength);
        payload[QStringLiteral("name")] = readText(inst, pointerArg(args, 1), kMaxKeyLength);
        return int32_t(bridge_.execute(manifest_, QStringLiteral("gcode:load:to:visualizer"), payload).ok ? 0 : -1);
    });

    instance_->linkHostFunction("env", "gs_host_log", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        const int32_t level = std::get<int32_t>(args.at(0));
        const QString msg = readText(inst, pointerArg(args, 1), kMaxKeyLength);
        const QString line = QStringLiteral("[plugin %1] %2").arg(manifest_.id, msg);
        if (level >= 3) {
            qWarning().noquote() << line;
        } else if (level >= 1) {
            qInfo().noquote() << line;
        } else {
            qDebug().noquote() << line;
        }
        return std::nullopt;
    });

    instance_->linkHostFunction("env", "gs_host_request", [this](WasmInstance& inst, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
        const QString type = readText(inst, pointerArg(args, 0), kMaxKeyLength);
        const QByteArray payloadText = inst.memory().readString(pointerArg(args, 1), kMaxTextLength).c_str();
        const int32_t bufLen = std::get<int32_t>(args.at(3));

        QJsonObject reply;
        const QJsonDocument payloadDoc = QJsonDocument::fromJson(payloadText.isEmpty() ? QByteArray("{}") : payloadText);
        if (!payloadDoc.isObject()) {
            reply[QStringLiteral("ok")] = false;
            reply[QStringLiteral("error")] = QStringLiteral("Payload is not a JSON object");
        } else {
            const BridgeResponse res = bridge_.execute(manifest_, type, payloadDoc.object());
            reply[QStringLiteral("ok")] = res.ok;
            reply[QStringLiteral("result")] = res.result;
            if (!res.ok) reply[QStringLiteral("error")] = res.error;
        }
        const QByteArray text = QJsonDocument(reply).toJson(QJsonDocument::Compact);
        if (bufLen <= 0 || text.size() + 1 > bufLen) {
            return static_cast<int32_t>(-(text.size() + 1));
        }
        if (!inst.memory().writeString(pointerArg(args, 2), text.toStdString(), static_cast<size_t>(bufLen))) {
            inst.raiseHostTrap("gs_host_request: output buffer outside plugin memory");
            return int32_t(-1);
        }
        return static_cast<int32_t>(text.size());
    });
}

bool PluginWasmHost::init() {
    if (!instance_) return false;
    running_ = true;
    const auto res = instance_->invoke("gsender_plugin_init");
    running_ = false;
    initialized_ = res.has_value() && std::get<int32_t>(*res) == 0;
    if (!initialized_ && instance_->lastTrap() != WasmTrap::None) {
        qWarning().noquote() << QStringLiteral("[plugin %1] init trapped: %2").arg(manifest_.id, lastError());
    }
    flushTopicEvents();
    return initialized_;
}

void PluginWasmHost::shutdown() {
    if (instance_ && initialized_ && instance_->hasExport("gsender_plugin_shutdown")) {
        instance_->invoke("gsender_plugin_shutdown");
    }
    initialized_ = false;
    pendingTopics_.clear();
}

QString PluginWasmHost::handleRequest(const QString& requestJson) {
    auto error = [](const QString& message) {
        QJsonObject obj;
        obj[QStringLiteral("ok")] = false;
        obj[QStringLiteral("error")] = message;
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    };
    if (!instance_ || !initialized_) return error(QStringLiteral("Wasm plugin is not running"));
    if (running_) return error(QStringLiteral("Wasm plugin is busy"));
    if (!instance_->hasExport("gsender_plugin_handle_request")) return error(QStringLiteral("Plugin handles no requests"));

    running_ = true;
    QString result;
    const auto request = copyIn(requestJson.toStdString());
    const auto response = instance_->invoke("gsender_plugin_alloc", {ResponseBufferSize});
    const uint32_t responsePtr = response ? static_cast<uint32_t>(std::get<int32_t>(*response)) : 0;
    if (!request || responsePtr == 0) {
        result = error(QStringLiteral("Plugin could not allocate request buffers"));
    } else {
        const auto written = instance_->invoke("gsender_plugin_handle_request", {
            static_cast<int32_t>(*request), static_cast<int32_t>(responsePtr), ResponseBufferSize});
        if (!written) {
            result = error(QStringLiteral("Wasm handle_request trap: %1").arg(QString::fromStdString(instance_->lastErrorMessage())));
        } else if (std::get<int32_t>(*written) < 0) {
            result = error(QStringLiteral("Plugin failed to handle the request"));
        } else {
            const size_t len = std::min<size_t>(static_cast<size_t>(std::get<int32_t>(*written)), ResponseBufferSize);
            result = QString::fromStdString(instance_->memory().readString(responsePtr, len));
        }
    }
    if (request) release(*request);
    release(responsePtr);
    running_ = false;
    flushTopicEvents();
    return result;
}

void PluginWasmHost::onTopicEvent(const QString& topic, const QJsonObject& data) {
    if (!instance_ || !initialized_ || !bridge_.hasTopic(manifest_, topic)) return;
    if (!instance_->hasExport("gsender_plugin_on_topic_event")) return;
    pendingTopics_.emplace_back(topic, data);
    if (pendingTopics_.size() > 1000) pendingTopics_.pop_front();
    if (!running_) flushTopicEvents();
}

void PluginWasmHost::flushTopicEvents() {
    // Events the plugin's own calls caused arrive here, after its call returned.
    for (int i = 0; i < 1000 && !pendingTopics_.empty() && initialized_; ++i) {
        const auto [topic, data] = pendingTopics_.front();
        pendingTopics_.pop_front();
        running_ = true;
        const auto topicPtr = copyIn(topic.toStdString());
        const auto dataPtr = copyIn(QJsonDocument(data).toJson(QJsonDocument::Compact).toStdString());
        if (topicPtr && dataPtr) {
            instance_->invoke("gsender_plugin_on_topic_event", {static_cast<int32_t>(*topicPtr), static_cast<int32_t>(*dataPtr)});
        }
        if (topicPtr) release(*topicPtr);
        if (dataPtr) release(*dataPtr);
        running_ = false;
    }
}

}  // namespace gs::app
