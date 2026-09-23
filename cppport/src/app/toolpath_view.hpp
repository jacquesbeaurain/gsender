#pragma once

// The toolpath visualizer (gSender's Visualizer widget): the loaded program's
// rapids and cutting moves in 3D with orbit/pan/zoom, the tool position, and
// the job's progress (finished moves dimmed).
//
// Drawn with QPainter so it also renders on the offscreen platform (the
// automated screenshots); the data it draws is renderer-independent, so an
// OpenGL implementation can replace it if large files need one.

#include <QPointF>
#include <QWidget>

#include <array>
#include <vector>

class QToolButton;

namespace gs::app {

class Machine;
struct Toolpath;

class ToolpathView final : public QWidget {
    Q_OBJECT

public:
    explicit ToolpathView(Machine& machine, QWidget* parent = nullptr);

    void setTopView();
    void set3dView();
    void fit();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Point3 {
        double x = 0, y = 0, z = 0;
    };
    QPointF project(const Point3& p) const;
    void programChanged();
    void progressChanged();

    Machine& machine_;
    // Camera: rotation about Z (yaw) then tilt towards the viewer (pitch),
    // orthographic, `scale_` pixels per millimetre, centred on `target_`.
    double yaw_ = 0;
    double pitch_ = 0;
    double scale_ = 4;
    Point3 target_;
    QPointF pan_;
    std::array<double, 9> rotation_{};  // cached from yaw/pitch
    void updateRotation();

    std::size_t doneLines_ = 0;  // sender lines acknowledged
    QPoint lastMouse_;
    Qt::MouseButton dragging_ = Qt::NoButton;
    QToolButton* topButton_;
    QToolButton* isoButton_;
    QToolButton* fitButton_;
};

// A plan view of a toolpath that is not the loaded job (tool previews):
// fitted to the widget, cutting moves solid and rapids dashed.
class ToolpathPreview final : public QWidget {
    Q_OBJECT

public:
    explicit ToolpathPreview(QWidget* parent = nullptr);
    ~ToolpathPreview() override;
    void setToolpath(const Toolpath& path);
    QSize sizeHint() const override { return {360, 360}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<float> rapids_;
    std::vector<float> feeds_;
};

}  // namespace gs::app
