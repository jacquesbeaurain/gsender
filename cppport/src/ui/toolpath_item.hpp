#pragma once

// The main visualizer as a Qt Quick item: the loaded job drawn by the shared
// toolpath scene (app/toolpath_scene) - the same picture as the widget
// visualizer. The camera is driven from QML (Visualizer.qml's drag, pinch,
// wheel and double-tap handlers) through the invokables.

#include "toolpath_scene.hpp"

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include <cstddef>

namespace gs::app {
class Machine;
}

namespace gs::ui {

// Not final: QML instantiates it through a subclass.
class ToolpathItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    // "3d", "top", "front", "right", "left".
    Q_PROPERTY(QString view READ view NOTIFY cameraChanged)
    Q_PROPERTY(bool flat READ flat NOTIFY cameraChanged)
    Q_PROPERTY(double yaw READ yaw NOTIFY cameraChanged)
    Q_PROPERTY(double pitch READ pitch NOTIFY cameraChanged)
    Q_PROPERTY(double scale READ cameraScale NOTIFY cameraChanged)

public:
    explicit ToolpathItem(QQuickItem* parent = nullptr);

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

Q_SIGNALS:
    void cameraChanged();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void applySettings();
    void progressChanged();
    void changed();

    app::Machine* machine_ = nullptr;
    app::ToolpathCamera camera_;
    std::size_t doneLines_ = 0;  // sender lines acknowledged
};

}  // namespace gs::ui
