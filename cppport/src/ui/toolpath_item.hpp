#pragma once

// The main visualizer as a Qt Quick item: the loaded job drawn by the shared
// toolpath scene (app/toolpath_scene) - the same picture as the widget
// visualizer. The camera is driven from QML (Visualizer.qml's drag, pinch,
// wheel and double-tap handlers) through the invokables.

#include "plugin_bridge.hpp"
#include "toolpath_scene.hpp"

#include <QJsonArray>
#include <QMap>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include <cstddef>

namespace gs::app {
class Machine;
}

namespace gs::ui {

// Not final: QML instantiates it through a subclass.
class ToolpathItem : public QQuickPaintedItem, public app::PluginViewer {
    Q_OBJECT
    QML_ELEMENT

    // "3d", "top", "front", "right", "left".
    Q_PROPERTY(QString view READ view NOTIFY cameraChanged)
    Q_PROPERTY(bool flat READ flat NOTIFY cameraChanged)
    Q_PROPERTY(double yaw READ yaw NOTIFY cameraChanged)
    Q_PROPERTY(double pitch READ pitch NOTIFY cameraChanged)
    Q_PROPERTY(double scale READ cameraScale NOTIFY cameraChanged)
    // The main visualizer serves plugins (their viewer:* requests); set by Visualizer.qml.
    Q_PROPERTY(bool pluginHost READ pluginHost WRITE setPluginHost NOTIFY pluginHostChanged)
    // Plugin-driven: orbiting locked, and a pick gesture armed ("", "click" or "hold").
    Q_PROPERTY(bool rotateEnabled READ rotateEnabled NOTIFY rotateEnabledChanged)
    Q_PROPERTY(QString pickMode READ pickMode NOTIFY pickModeChanged)

public:
    explicit ToolpathItem(QQuickItem* parent = nullptr);
    ~ToolpathItem() override;

    void paint(QPainter* painter) override;

    QString view() const;
    bool flat() const noexcept { return camera_.flat(); }
    double yaw() const noexcept { return camera_.yawDegrees(); }
    double pitch() const noexcept { return camera_.pitchDegrees(); }
    double cameraScale() const noexcept { return camera_.scale(); }

    Q_INVOKABLE void setView(const QString& view);
    Q_INVOKABLE void cycleView();
    Q_INVOKABLE void fit();
    Q_INVOKABLE void zoomAt(double x, double y, double factor);  // about a point of the item
    Q_INVOKABLE void orbit(double yawDegrees, double pitchDegrees);
    Q_INVOKABLE void pan(double dx, double dy);

    bool pluginHost() const noexcept { return pluginHost_; }
    void setPluginHost(bool host);
    bool rotateEnabled() const noexcept { return rotateEnabled_; }
    QString pickMode() const { return pickMode_; }
    // The pick gesture (ToolpathGestures): a click or a completed hold at a
    // point of the item, and a hold's progress (0..1).
    Q_INVOKABLE void pickAt(double x, double y);
    Q_INVOKABLE void pickHoldProgress(double t);

    // app::PluginViewer
    std::optional<WorldPoint> screenToWorld(double px, double py) const override;
    std::optional<QPointF> worldToScreen(const WorldPoint& world) const override;
    bool setCameraView(const QString& view) override;
    void setRotateEnabled(bool enabled) override;
    bool isRotaryFile() const override;
    void armPick(const QString& mode) override;
    void disarmPick() override;
    void setOverlay(const QString& pluginId, const QJsonArray& markers) override;

Q_SIGNALS:
    void cameraChanged();
    void pluginHostChanged();
    void rotateEnabledChanged();
    void pickModeChanged();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    // What is drawn and what Fit frames: the main visualizer by default.
    virtual void paintContent(QPainter& painter, app::ToolpathCamera& camera);
    virtual std::optional<gcode::BoundingBox> contentBounds() const;
    app::Machine* machine() const noexcept { return machine_; }

private:
    void applySettings();
    void progressChanged();
    void changed();
    void registerViewer();
    void paintOverlay(QPainter& painter);

    app::Machine* machine_ = nullptr;
    app::ToolpathCamera camera_;
    std::size_t doneLines_ = 0;  // sender lines acknowledged
    bool pluginHost_ = false;
    bool rotateEnabled_ = true;
    QString pickMode_;
    QMap<QString, QJsonArray> overlays_;  // by plugin
};

}  // namespace gs::ui
