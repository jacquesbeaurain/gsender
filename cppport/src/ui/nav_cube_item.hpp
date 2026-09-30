#pragma once

#include "toolpath_item.hpp"

#include <QColor>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QQuickPaintedItem>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace gs::ui {

class NavCubeItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(ToolpathItem* view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(double yaw READ yaw WRITE setYaw NOTIFY orientationChanged)
    Q_PROPERTY(double pitch READ pitch WRITE setPitch NOTIFY orientationChanged)
    Q_PROPERTY(QString activeFace READ activeFace NOTIFY orientationChanged)

public:
    explicit NavCubeItem(QQuickItem* parent = nullptr);

    ToolpathItem* view() const noexcept { return view_; }
    void setView(ToolpathItem* view);

    double yaw() const noexcept;
    void setYaw(double yawDeg);

    double pitch() const noexcept;
    void setPitch(double pitchDeg);

    QString activeFace() const;

    void paint(QPainter* painter) override;

public:
    struct FaceDef {
        const char* label;
        const char* viewName;
        std::array<int, 4> vertices;
        double nx, ny, nz;
    };

    struct ProjectedFace {
        int index = 0;
        const FaceDef* def = nullptr;
        QPolygonF poly;
        double depth = 0;
        bool visible = false;
    };

    std::vector<ProjectedFace> projectFaces() const;
    int hitTestFace(const QPointF& pos) const;

Q_SIGNALS:
    void viewChanged();
    void orientationChanged();
    void faceClicked(const QString& face);

protected:
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    ToolpathItem* view_ = nullptr;
    double localYaw_ = -35.0;
    double localPitch_ = 55.0;
    int hoveredFace_ = -1;
    bool isDragging_ = false;
    QPointF pressPos_;
    QPointF lastPos_;
};

}  // namespace gs::ui
