#include "toolpath_item.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"

#include <QPainter>

namespace gs::ui {
namespace {

using View = app::ToolpathCamera::View;

constexpr std::pair<const char*, View> kViews[] = {
    {"3d", View::Iso}, {"top", View::Top}, {"front", View::Front}, {"right", View::Right}, {"left", View::Left}};

}  // namespace

ToolpathItem::ToolpathItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(true);
    setAntialiasing(true);
}

void ToolpathItem::componentComplete() {
    QQuickPaintedItem::componentComplete();
    machine_ = &UiBackend::instance()->machine();
    connect(machine_, &app::Machine::appSettingsChanged, this, &ToolpathItem::applySettings);
    connect(machine_, &app::Machine::programChanged, this, [this] {
        doneLines_ = 0;
        fit();
    });
    connect(machine_, &app::Machine::senderStatusChanged, this, &ToolpathItem::progressChanged);
    connect(machine_, &app::Machine::workflowChanged, this, &ToolpathItem::progressChanged);
    connect(machine_, &app::Machine::stateChanged, this, [this] { update(); });
    connect(machine_, &app::Machine::connectionChanged, this, [this] { update(); });
    applySettings();
}

void ToolpathItem::applySettings() {
    const app::AppSettings& settings = machine_->settings();
    camera_.setPerspective(settings.perspective);
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
    camera_.orbit(yawDegrees, pitchDegrees);
    changed();
}

void ToolpathItem::pan(double dx, double dy) {
    camera_.pan(dx, dy);
    changed();
}

}  // namespace gs::ui
