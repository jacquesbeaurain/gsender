#include "plugin_bridge.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"

#include <QJsonArray>

#include <algorithm>

namespace gs::app {

namespace {

const QString kIdle = QStringLiteral("Idle");

}  // namespace

PluginBridge::PluginBridge(Machine& machine, PluginStorage& storage, QObject* parent)
    : QObject(parent), machine_(machine), storage_(storage) {
    parsers_ = new PluginParserChain(
        [this](const QString& pluginId, const QString& topic, const QJsonObject& data) {
            Q_EMIT pluginEvent(pluginId, topic, data);
        },
        [this] { return machineIdle(); }, this);
    connect(&machine_, &Machine::rawLine, parsers_, &PluginParserChain::feed);
    connect(&machine_, &Machine::connectionChanged, this, [this] {
        if (!machine_.isConnected()) {
            parsers_->reset();
            releaseBusy();
        }
    });

    for (QTimer* timer : {&busyIdleTimer_, &busyGraceTimer_, &busyMaxTimer_}) {
        timer->setSingleShot(true);
    }
    busyIdleTimer_.setInterval(BusyIdleDebounceMs);
    busyGraceTimer_.setInterval(BusyArmGraceMs);
    busyMaxTimer_.setInterval(BusyMaxMs);
    connect(&busyIdleTimer_, &QTimer::timeout, this, &PluginBridge::releaseBusy);
    connect(&busyGraceTimer_, &QTimer::timeout, this, [this] {
        if (!busyArmed_) releaseBusy();
    });
    connect(&busyMaxTimer_, &QTimer::timeout, this, &PluginBridge::releaseBusy);
    connect(&machine_, &Machine::stateChanged, this, [this] {
        if (busy_) onBusyStateChange();
    });
}

PluginBridge::~PluginBridge() = default;

bool PluginBridge::hasCapability(const PluginManifest& manifest, const QString& requestType) const {
    return manifest.capabilities.requestTypes.contains(requestType);
}

bool PluginBridge::hasTopic(const PluginManifest& manifest, const QString& topic) const {
    return manifest.capabilities.topics.contains(topic);
}

bool PluginBridge::isPrivateTopic(const QString& topic) {
    return topic == u"parser" || topic == u"query";
}

bool PluginBridge::mayReceive(const PluginManifest& manifest, const QString& topic) const {
    return isPrivateTopic(topic) || hasTopic(manifest, topic);
}

bool PluginBridge::machineIdle() const {
    controller::Controller* c = machine_.controller();
    return c && c->workflow().isIdle();
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
    if (type == QStringLiteral("machine:query")) {
        return handleMachineQuery(manifest, payload);
    }
    if (type == QStringLiteral("machine:parser:register") || type == QStringLiteral("machine:parser:unregister")) {
        return handleParserRequest(manifest, type, payload);
    }
    if (type == QStringLiteral("machine:busy:set")) {
        return handleBusy(manifest, payload);
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

BridgeResponse PluginBridge::handleMachineQuery(const PluginManifest& manifest, const QJsonObject& payload) {
    QString cmd = payload.value(QStringLiteral("cmd")).toString();
    if (cmd.isEmpty()) cmd = payload.value(QStringLiteral("command")).toString();
    cmd = cmd.trimmed();
    if (cmd.isEmpty()) {
        return BridgeResponse::fail(QStringLiteral("A command is required"));
    }
    if (cmd.contains(u'\n') || cmd.contains(u'\r')) {
        return BridgeResponse::fail(QStringLiteral("A query is a single line"));
    }
    controller::Controller* c = machine_.controller();
    if (!c || !machine_.isConnected()) {
        return BridgeResponse::fail(QStringLiteral("Machine is not connected"));
    }

    const QJsonObject opts = payload.value(QStringLiteral("opts")).toObject();
    const bool allowDuringJob = opts.value(QStringLiteral("allowDuringJob")).toBool();
    if (!machineIdle() && !allowDuringJob) {
        return BridgeResponse::fail(QStringLiteral("Machine is busy running a job"));
    }
    const QJsonValue until = opts.value(QStringLiteral("until"));
    // While a job streams, ok/error cannot be told apart from the job's own.
    if (allowDuringJob && !until.isObject()) {
        return BridgeResponse::fail(QStringLiteral(
            "A query with allowDuringJob needs an explicit \"until\" pattern: ok/error cannot be told apart from the "
            "running job's responses"));
    }
    if (parsers_->captureActive()) {
        return BridgeResponse::fail(QStringLiteral("Another query is already in flight"));
    }

    PluginParserChain::QueryOptions options;
    if (until.isString()) {
        options.untilKind = until.toString();
        if (options.untilKind != u"ok" && options.untilKind != u"error" && options.untilKind != u"ok-or-error") {
            return BridgeResponse::fail(QStringLiteral("\"until\" must be ok, error, ok-or-error or { source, flags }"));
        }
    } else if (until.isObject()) {
        QString error;
        options.untilPattern = compilePluginRegex(until, &error);
        if (!options.untilPattern) {
            return BridgeResponse::fail(QStringLiteral("Invalid \"until\" pattern: %1").arg(error));
        }
        options.untilKind.clear();
    } else if (!until.isUndefined() && !until.isNull()) {
        return BridgeResponse::fail(QStringLiteral("\"until\" must be ok, error, ok-or-error or { source, flags }"));
    }
    if (opts.value(QStringLiteral("maxLines")).isDouble()) {
        options.maxLines = std::clamp(opts.value(QStringLiteral("maxLines")).toInt(), 1, 1000);
    }
    if (opts.value(QStringLiteral("timeout")).isDouble()) {
        options.timeoutMs = std::clamp(opts.value(QStringLiteral("timeout")).toInt(), 50, 60000);
    }
    options.includeStatusReports = opts.value(QStringLiteral("includeStatusReports")).toBool();

    const int queryId = nextQueryId_++;
    const QString pluginId = manifest.id;
    // The capture opens before the write, so no reply line can slip past it.
    parsers_->beginCapture(options, [this, pluginId, queryId](const QJsonObject& result) {
        QJsonObject event = result;
        event.insert(QStringLiteral("queryId"), queryId);
        Q_EMIT pluginEvent(pluginId, QStringLiteral("query"), event);
    });
    // Echoed: a plugin's write must not be invisible to the operator.
    c->writeln(cmd.toStdString(), true);

    QJsonObject res;
    res.insert(QStringLiteral("queryId"), queryId);
    return BridgeResponse::success(res);
}

BridgeResponse PluginBridge::handleParserRequest(const PluginManifest& manifest, const QString& type,
                                                 const QJsonObject& payload) {
    if (type == u"machine:parser:unregister") {
        const QJsonValue id = payload.value(QStringLiteral("id"));
        if (!id.isUndefined() && !id.isNull() && !id.isString()) {
            return BridgeResponse::fail(QStringLiteral("\"id\" must be a string"));
        }
        parsers_->unregisterRuntime(manifest.id, id.toString());
        return BridgeResponse::success();
    }

    QJsonArray specs;
    if (payload.value(QStringLiteral("spec")).isObject()) {
        specs.append(payload.value(QStringLiteral("spec")));
    } else if (payload.value(QStringLiteral("specs")).isArray()) {
        specs = payload.value(QStringLiteral("specs")).toArray();
    } else {
        return BridgeResponse::fail(QStringLiteral("A parser spec with an id is required"));
    }
    const QJsonObject outcome = parsers_->registerRuntime(manifest.id, specs);
    if (outcome.value(QStringLiteral("registered")).toArray().isEmpty()) {
        const QJsonArray errors = outcome.value(QStringLiteral("errors")).toArray();
        QStringList messages;
        for (const QJsonValue& e : errors) {
            const QJsonObject o = e.toObject();
            messages << QStringLiteral("%1: %2").arg(o.value(QStringLiteral("id")).toString(),
                                                     o.value(QStringLiteral("error")).toString());
        }
        return BridgeResponse::fail(QStringLiteral("No parser registered: %1").arg(messages.join(QStringLiteral("; "))));
    }
    return BridgeResponse::success(outcome);
}

// The latch lets a plugin driving motion line by line (so the controller
// dips to Idle between moves) show a steady busy status. The host releases
// it: after the machine has left Idle and come back for a while, when motion
// never starts, at an absolute cap, or on disconnect.
BridgeResponse PluginBridge::handleBusy(const PluginManifest& manifest, const QJsonObject& payload) {
    if (!payload.value(QStringLiteral("busy")).toBool()) {
        if (busy_ && busyOwner_ == manifest.id) releaseBusy();
        return BridgeResponse::success();
    }
    if (!machine_.isConnected()) {
        return BridgeResponse::fail(QStringLiteral("Machine is not connected"));
    }
    busyIdleTimer_.stop();
    busyGraceTimer_.stop();
    busyMaxTimer_.stop();
    busyArmed_ = false;
    busy_ = true;
    busyOwner_ = manifest.id;
    busyLabel_ = payload.value(QStringLiteral("label")).toString().left(60);
    busyGraceTimer_.start();
    busyMaxTimer_.start();
    Q_EMIT busyChanged();
    onBusyStateChange();
    return BridgeResponse::success();
}

void PluginBridge::onBusyStateChange() {
    if (!busy_) return;
    controller::Controller* c = machine_.controller();
    if (!c) {
        releaseBusy();
        return;
    }
    if (QString::fromStdString(c->state().status.activeState) != kIdle) {
        // Moving: arm the release and hold off the countdowns.
        busyArmed_ = true;
        busyGraceTimer_.stop();
        busyIdleTimer_.stop();
        return;
    }
    if (busyArmed_ && !busyIdleTimer_.isActive()) {
        busyIdleTimer_.start();
    }
}

void PluginBridge::releaseBusy() {
    busyIdleTimer_.stop();
    busyGraceTimer_.stop();
    busyMaxTimer_.stop();
    busyArmed_ = false;
    busyOwner_.clear();
    if (busy_) {
        busy_ = false;
        busyLabel_.clear();
        Q_EMIT busyChanged();
    }
}

void PluginBridge::releasePlugin(const QString& pluginId) {
    parsers_->removePlugin(pluginId);
    if (viewer_) viewer_->setOverlay(pluginId, {});
    if (busy_ && busyOwner_ == pluginId) releaseBusy();
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
    if (!viewer_) {
        return BridgeResponse::fail(QStringLiteral("The visualizer is not available"));
    }
    const auto number = [&](const char* key, double fallback = qQNaN()) {
        const QJsonValue v = payload.value(QLatin1String(key));
        return v.isDouble() ? v.toDouble() : fallback;
    };
    if (type == QStringLiteral("viewer:screen-to-world")) {
        const double px = number("px"), py = number("py");
        if (qIsNaN(px) || qIsNaN(py)) return BridgeResponse::fail(QStringLiteral("px and py are required"));
        QJsonObject res;
        if (const auto world = viewer_->screenToWorld(px, py)) {
            res = {{"x", world->x}, {"y", world->y}, {"z", world->z}};
        }
        return BridgeResponse::success(res);  // empty: nothing under that pixel
    }
    if (type == QStringLiteral("viewer:world-to-screen")) {
        const double x = number("x"), y = number("y");
        if (qIsNaN(x) || qIsNaN(y)) return BridgeResponse::fail(QStringLiteral("x and y are required"));
        QJsonObject res;
        if (const auto screen = viewer_->worldToScreen({x, y, number("z", 0)})) {
            res = {{"x", screen->x()}, {"y", screen->y()}};
        }
        return BridgeResponse::success(res);
    }
    if (type == QStringLiteral("viewer:camera:set")) {
        const QString view = payload.value(QStringLiteral("view")).toString();
        if (!viewer_->setCameraView(view)) {
            return BridgeResponse::fail(QStringLiteral("Unknown view: %1").arg(view));
        }
        return BridgeResponse::success();
    }
    if (type == QStringLiteral("viewer:camera:lock-rotate")) {
        viewer_->setRotateEnabled(!payload.value(QStringLiteral("locked")).toBool());
        return BridgeResponse::success();
    }
    if (type == QStringLiteral("viewer:pick:arm")) {
        if (viewer_->isRotaryFile()) {
            return BridgeResponse::fail(QStringLiteral("Picking is not available for rotary files"));
        }
        controller::Controller* c = machine_.controller();
        if (!c || QString::fromStdString(c->state().status.activeState) != kIdle) {
            return BridgeResponse::fail(QStringLiteral("Machine must be connected and idle to pick a point"));
        }
        viewer_->armPick(payload.value(QStringLiteral("mode")).toString() == u"click" ? QStringLiteral("click")
                                                                                       : QStringLiteral("hold"));
        return BridgeResponse::success();
    }
    if (type == QStringLiteral("viewer:pick:disarm")) {
        viewer_->disarmPick();
        return BridgeResponse::success();
    }
    if (type == QStringLiteral("viewer:overlay:set")) {
        QJsonArray markers;
        for (const QJsonValue& v : payload.value(QStringLiteral("markers")).toArray()) {
            const QJsonObject m = v.toObject();
            if (m.value(QStringLiteral("x")).isDouble() && m.value(QStringLiteral("y")).isDouble()) {
                markers.append(m);
            }
            if (markers.size() >= 256) break;
        }
        viewer_->setOverlay(manifest.id, markers);
        return BridgeResponse::success();
    }

    return BridgeResponse::fail(QStringLiteral("Unknown viewer request: %1").arg(type));
}

void PluginBridge::setViewer(PluginViewer* viewer) {
    viewer_ = viewer;
}

void PluginBridge::viewerPicked(const PluginViewer::WorldPoint& world, QPointF screen) {
    broadcastTopic(QStringLiteral("viewer"),
                   {{"kind", "pick"},
                    {"world", QJsonObject{{"x", world.x}, {"y", world.y}, {"z", world.z}}},
                    {"screen", QJsonObject{{"x", screen.x()}, {"y", screen.y()}}}});
}

void PluginBridge::viewerHoldProgress(double t) {
    broadcastTopic(QStringLiteral("viewer"), {{"kind", "hold-progress"}, {"t", std::clamp(t, 0.0, 1.0)}});
}

void PluginBridge::broadcastTopic(const QString& topic, const QJsonObject& data) {
    Q_EMIT pluginEvent(QString(), topic, data);
}

}  // namespace gs::app
