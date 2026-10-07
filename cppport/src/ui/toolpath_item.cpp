#include "toolpath_item.hpp"

#include "backend.hpp"
#include "machine.hpp"
#include "plugin_service.hpp"

#include "gs/controller/controller.hpp"

#include <QJsonObject>
#include <QKeyEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace gs::ui {
namespace {

using View = app::ToolpathCamera::View;

constexpr std::pair<const char*, View> kViews[] = {
    {"3d", View::Iso}, {"iso", View::Iso}, {"top", View::Top}, {"front", View::Front},
    {"right", View::Right}, {"left", View::Left}, {"back", View::Back}, {"bottom", View::Bottom}};

}  // namespace

ToolpathItem::ToolpathItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(true);
    setAntialiasing(true);
}

ToolpathItem::~ToolpathItem() {
    if (machine_ && machine_->pluginService().bridge().viewer() == this) {
        machine_->pluginService().bridge().setViewer(nullptr);
    }
}

void ToolpathItem::componentComplete() {
    QQuickPaintedItem::componentComplete();
    machine_ = &UiBackend::instance()->machine();
    registerViewer();
    connect(machine_, &app::Machine::appSettingsChanged, this, &ToolpathItem::applySettings);
    connect(machine_, &app::Machine::programChanged, this, [this] {
        doneLines_ = 0;
        fit();
    });
    connect(machine_, &app::Machine::senderStatusChanged, this, &ToolpathItem::progressChanged);
    connect(machine_, &app::Machine::workflowChanged, this, &ToolpathItem::progressChanged);
    connect(machine_, &app::Machine::stateChanged, this, [this] { update(); });
    connect(machine_, &app::Machine::connectionChanged, this, [this] {
        if (moveToHere_ && !machine_->canMoveToHere()) {
            disarmMoveToHere();
        }
        Q_EMIT moveToHereChanged();
        update();
    });
    applySettings();
}

void ToolpathItem::applySettings() {
    const app::AppSettings& settings = machine_->settings();
    camera_.setPerspective(settings.perspective && !moveToHere_);
    camera_.setFlat(settings.liteMode && settings.liteOption == "Light", contentBounds());
    changed();
}

void ToolpathItem::progressChanged() {
    controller::Controller* c = machine_->controller();
    const std::size_t done = c && !c->workflow().isIdle() ? c->sender().received() : 0;
    if (done != doneLines_) {
        doneLines_ = done;
        update();
    }
}

void ToolpathItem::changed() {
    update();
    Q_EMIT cameraChanged();
}

void ToolpathItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        fit();
    }
}

void ToolpathItem::paint(QPainter* painter) {
    if (!machine_) {
        return;
    }
    camera_.setViewport(size());
    paintContent(*painter, camera_);
    paintOverlay(*painter);
}

void ToolpathItem::paintContent(QPainter& painter, app::ToolpathCamera& camera) {
    app::paintMainView(painter, camera, *machine_, doneLines_);
}

std::optional<gcode::BoundingBox> ToolpathItem::contentBounds() const {
    return machine_ ? app::mainViewBounds(*machine_) : std::nullopt;
}

QString ToolpathItem::view() const {
    for (const auto& [name, view] : kViews) {
        if (view == camera_.view()) {
            return QString::fromLatin1(name);
        }
    }
    return {};
}

void ToolpathItem::setView(const QString& view) {
    for (const auto& [name, value] : kViews) {
        if (view == QLatin1String(name)) {
            camera_.setViewport(size());
            camera_.setView(value, contentBounds());
            changed();
        }
    }
}

void ToolpathItem::cycleView() {
    camera_.setViewport(size());
    camera_.cycleView(contentBounds());
    changed();
}

void ToolpathItem::fit() {
    camera_.setViewport(size());
    camera_.fit(contentBounds());
    changed();
}

void ToolpathItem::zoomAt(double x, double y, double factor) {
    camera_.zoomAbout(QPointF(x, y), factor);
    changed();
}

void ToolpathItem::orbit(double yawDegrees, double pitchDegrees) {
    if (!rotateEnabled_) {
        return;
    }
    camera_.orbit(yawDegrees, pitchDegrees);
    changed();
}

void ToolpathItem::pan(double dx, double dy) {
    camera_.pan(dx, dy);
    changed();
}

void ToolpathItem::setKeyboardControl(bool on) {
    if (on == keyboardControl_) {
        return;
    }
    keyboardControl_ = on;
    // (Qt keeps activeFocusOnTab while the item has the focus: drop it first.)
    if (!on && hasActiveFocus()) {
        setFocus(false);
    }
    setActiveFocusOnTab(on);
    Q_EMIT keyboardControlChanged();
}

void ToolpathItem::keyPressEvent(QKeyEvent* event) {
    if (!keyboardControl_) {
        QQuickPaintedItem::keyPressEvent(event);
        return;
    }
    const bool panning = (event->modifiers() & Qt::ControlModifier) != 0;
    const double step = 0.1 * std::min(width(), height());
    switch (event->key()) {
        case Qt::Key_Left: panning ? pan(-step, 0) : orbit(-15, 0); break;
        case Qt::Key_Right: panning ? pan(step, 0) : orbit(15, 0); break;
        case Qt::Key_Up: panning ? pan(0, -step) : orbit(0, 15); break;
        case Qt::Key_Down: panning ? pan(0, step) : orbit(0, -15); break;
        case Qt::Key_Plus:
        case Qt::Key_Equal: zoomAt(width() / 2, height() / 2, 1.25); break;
        case Qt::Key_Minus: zoomAt(width() / 2, height() / 2, 0.8); break;
        case Qt::Key_Home: fit(); break;
        default: QQuickPaintedItem::keyPressEvent(event); return;
    }
    event->accept();
}

// ---- plugins ---------------------------------------------------------------------------

void ToolpathItem::setPluginHost(bool host) {
    if (host == pluginHost_) {
        return;
    }
    pluginHost_ = host;
    registerViewer();
    Q_EMIT pluginHostChanged();
}

void ToolpathItem::registerViewer() {
    if (!machine_) {
        return;
    }
    app::PluginBridge& bridge = machine_->pluginService().bridge();
    if (pluginHost_) {
        bridge.setViewer(this);
    } else if (bridge.viewer() == this) {
        bridge.setViewer(nullptr);
    }
}

std::optional<app::PluginViewer::WorldPoint> ToolpathItem::screenToWorld(double px, double py) const {
    app::ToolpathCamera camera = camera_;
    camera.setViewport(size());
    const auto p = camera.unproject(QPointF(px, py));
    if (!p) {
        return std::nullopt;
    }
    return WorldPoint{p->x, p->y, p->z};
}

std::optional<QPointF> ToolpathItem::worldToScreen(const WorldPoint& world) const {
    app::ToolpathCamera camera = camera_;
    camera.setViewport(size());
    return camera.project({world.x, world.y, world.z});
}

bool ToolpathItem::setCameraView(const QString& view) {
    static const QStringList kPluginViews{QStringLiteral("3d"), QStringLiteral("top"), QStringLiteral("front"),
                                          QStringLiteral("left"), QStringLiteral("right")};
    if (!kPluginViews.contains(view)) {
        return false;
    }
    setView(view);
    return true;
}

void ToolpathItem::setRotateEnabled(bool enabled) {
    if (enabled != rotateEnabled_) {
        rotateEnabled_ = enabled;
        Q_EMIT rotateEnabledChanged();
    }
}

bool ToolpathItem::isRotaryFile() const {
    return machine_ && app::isRotaryJob(*machine_);
}

void ToolpathItem::armPick(const QString& mode) {
    if (moveToHere_) {
        disarmMoveToHere();  // a plugin's pick replaces ours
    }
    if (mode != pickMode_) {
        pickMode_ = mode;
        Q_EMIT pickModeChanged();
    }
}

void ToolpathItem::disarmPick() {
    armPick(QString());
}

bool ToolpathItem::moveToHereAvailable() const {
    return machine_ && machine_->controller() != nullptr;
}

void ToolpathItem::toggleMoveToHere() {
    if (moveToHere_) {
        disarmMoveToHere();
    } else {
        armMoveToHere();
    }
}

void ToolpathItem::armMoveToHere() {
    if (!machine_) {
        return;
    }
    // The picked XY is not a work coordinate on the rotary-turned toolpath.
    if (isRotaryFile()) {
        Q_EMIT machine_->notice(tr("Move To Here isn't available for rotary files"));
        return;
    }
    if (!machine_->canMoveToHere()) {
        Q_EMIT machine_->notice(tr("Machine must be connected and idle to move"));
        return;
    }
    if (!pickMode_.isEmpty()) {
        disarmPick();
    }
    priorCamera_ = camera_;
    moveToHere_ = true;
    // The top view, orthographic (no foreshortening to skew the picked XY),
    // orbiting locked.
    setRotateEnabled(false);
    camera_.setPerspective(false);
    setView(QStringLiteral("top"));
    pickMode_ = QStringLiteral("hold");
    Q_EMIT pickModeChanged();
    Q_EMIT moveToHereChanged();
}

void ToolpathItem::disarmMoveToHere() {
    if (!moveToHere_) {
        return;
    }
    moveToHere_ = false;
    setRotateEnabled(true);
    if (priorCamera_) {
        camera_ = *priorCamera_;
        priorCamera_.reset();
    }
    camera_.setPerspective(machine_->settings().perspective);
    pickMode_.clear();
    Q_EMIT pickModeChanged();
    Q_EMIT moveToHereChanged();
    changed();
}

void ToolpathItem::pickAt(double x, double y) {
    if (moveToHere_) {
        const auto world = screenToWorld(x, y);
        if (!world) {
            return;
        }
        if (!machine_->canMoveToHere()) {
            Q_EMIT machine_->notice(tr("Machine must be connected and idle to move"));
            disarmMoveToHere();
            return;
        }
        const double mx = std::round(world->x * 1000) / 1000;
        const double my = std::round(world->y * 1000) / 1000;
        machine_->moveToHere(mx, my);
        Q_EMIT machine_->successNotice(tr("Moving to X%1 Y%2").arg(mx, 0, 'f', 2).arg(my, 0, 'f', 2));
        disarmMoveToHere();  // after a successful move
        return;
    }
    // A pick stays armed: the plugin decides when it has enough.
    if (pickMode_.isEmpty() || !machine_ || isRotaryFile()) {
        return;
    }
    const auto world = screenToWorld(x, y);
    if (!world) {
        return;
    }
    const QPointF screen = worldToScreen(*world).value_or(QPointF(x, y));
    machine_->pluginService().bridge().viewerPicked(*world, screen);
}

void ToolpathItem::pickHoldProgress(double t) {
    if (pickMode_ == u"hold" && machine_ && !moveToHere_) {
        machine_->pluginService().bridge().viewerHoldProgress(t);
    }
}

void ToolpathItem::setOverlay(const QString& pluginId, const QJsonArray& markers) {
    if (markers.isEmpty()) {
        overlays_.remove(pluginId);
    } else {
        overlays_.insert(pluginId, markers);
    }
    update();
}

void ToolpathItem::paintOverlay(QPainter& painter) {
    if (overlays_.isEmpty()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setPointSizeF(8);
    painter.setFont(font);
    const QColor fallback = app::mainViewTheme(*machine_).tool;
    for (const QJsonArray& markers : std::as_const(overlays_)) {
        for (const QJsonValue& value : markers) {
            const QJsonObject m = value.toObject();
            const QPointF at = camera_.project({m.value(QStringLiteral("x")).toDouble(),
                                                m.value(QStringLiteral("y")).toDouble(),
                                                m.value(QStringLiteral("z")).toDouble()});
            QColor color(m.value(QStringLiteral("color")).toString());
            if (!color.isValid()) {
                color = fallback;
            }
            const double size = std::clamp(m.value(QStringLiteral("size")).toDouble(6), 2.0, 40.0);
            const QString shape = m.value(QStringLiteral("shape")).toString(QStringLiteral("circle"));
            painter.setPen(QPen(color, 2));
            if (shape == u"cross") {
                painter.setBrush(Qt::NoBrush);
                painter.drawLine(at + QPointF(-size, -size), at + QPointF(size, size));
                painter.drawLine(at + QPointF(-size, size), at + QPointF(size, -size));
            } else if (shape == u"ring") {
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(at, size, size);
            } else {
                painter.setBrush(color);
                painter.drawEllipse(at, size, size);
            }
            const QString label = m.value(QStringLiteral("label")).toString().left(40);
            if (!label.isEmpty()) {
                painter.drawText(QRectF(at.x() + size + 3, at.y() - 8, 200, 16), Qt::AlignLeft | Qt::AlignVCenter,
                                 label);
            }
        }
    }
    painter.restore();
}

}  // namespace gs::ui
