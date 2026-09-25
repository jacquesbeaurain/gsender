#pragma once

// The toolpath visualizer's camera and drawing, independent of the UI
// toolkit: the widget visualizers (toolpath_view) and the QML one
// (ui/toolpath_item) both draw through these, with QPainter - which also
// renders on the offscreen platform, for the automated screenshots.

#include "visualizer_theme.hpp"

#include "gs/gcode/interpreter.hpp"

#include <QColor>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QPainter;

namespace gs::app {

class Machine;

struct Point3 {
    double x = 0, y = 0, z = 0;
};

// Rotation about Z (yaw) then a tilt towards the viewer (pitch), `scale`
// pixels per millimetre about `target`, orthographic or - upstream's
// default - perspective, over a viewport of a given size.
class ToolpathCamera {
public:
    // The visualizer's presets; each also fits the content.
    enum class View { Iso, Top, Front, Right, Left };

    ToolpathCamera();

    void setViewport(QSizeF size) { viewport_ = size; }
    QSizeF viewport() const noexcept { return viewport_; }

    // `content` frames fit() (nothing: a 100 mm square at the origin).
    void setView(View view, const std::optional<gcode::BoundingBox>& content);
    View view() const noexcept { return view_; }
    // 3D, Top, Front, Right, Left, as upstream's shortcut.
    void cycleView(const std::optional<gcode::BoundingBox>& content);
    void fit(const std::optional<gcode::BoundingBox>& content);

    void zoom(double factor);                          // about the middle
    void zoomAbout(QPointF point, double factor);      // about a viewport point
    void orbit(double yawDegrees, double pitchDegrees);  // not while flat
    void pan(double dx, double dy);
    // A drag of `delta` pixels: 0.5 degree a pixel.
    void dragOrbit(QPointF delta) { orbit(delta.x() * 0.5, -delta.y() * 0.5); }
    void centreOn(double x, double y);

    // Held flat: every view is the top view and orbiting does nothing.
    void setFlat(bool flat, const std::optional<gcode::BoundingBox>& content);
    bool flat() const noexcept { return flat_; }
    void setPerspective(bool perspective) { perspective_ = perspective; }
    bool perspective() const noexcept { return perspective_; }

    double yawDegrees() const noexcept;
    double pitchDegrees() const noexcept;
    double scale() const noexcept { return scale_; }

    QPointF project(const Point3& p) const;

private:
    void updateRotation();

    QSizeF viewport_{360, 280};
    View view_ = View::Top;
    double yaw_ = 0;
    double pitch_ = 0;
    double scale_ = 4;
    Point3 target_;
    QPointF pan_;
    std::array<double, 9> rotation_{};  // cached from yaw/pitch
    bool flat_ = false;
    bool perspective_ = false;
    double cameraDistance_ = 400;  // mm from the target, for perspective
};

namespace scene {

// Background, the grid (10 mm, every fifth line major) around `bounds` (or
// the origin) - over `gridArea` instead when given - and the work origin's
// axes.
void paintBackground(QPainter& painter, const ToolpathCamera& camera, const VisualizerTheme& theme,
                     const std::optional<gcode::BoundingBox>& bounds,
                     const std::optional<QRectF>& gridArea = std::nullopt);
// Segments (x0,y0,z0,x1,y1,z1 each) and their sender lines; `pen` gives
// each one's pen by index and line, nullptr to leave it out. Segments are
// batched by pen. `rotationA`: a rotary job's path turned back by the
// rotary's angle (degrees), so what is under the tool is on top.
void paintSegments(QPainter& painter, const ToolpathCamera& camera, const std::vector<float>& segments,
                   const std::vector<std::uint32_t>& lines,
                   const std::function<const QPen*(std::size_t index, std::uint32_t line)>& pen,
                   double rotationA = 0);
void paintTool(QPainter& painter, const ToolpathCamera& camera, const VisualizerTheme& theme, const Point3& position);
void paintCaption(QPainter& painter, const QRectF& area, const VisualizerTheme& theme, const QString& caption);
// A wireframe box, a rectangle on the XY plane, a label at a point.
void paintBox(QPainter& painter, const ToolpathCamera& camera, const gcode::BoundingBox& box, const QColor& color);
void paintRect(QPainter& painter, const ToolpathCamera& camera, const QRectF& area, const QColor& color);
void paintLabel(QPainter& painter, const ToolpathCamera& camera, const Point3& at, const QString& text,
                const QColor& color);

}  // namespace scene

// The main visualizer: the loaded job, its progress (sender lines done), the
// machine's tool, bed and keepout, the bounding box, lightweight mode and the
// caption, as the settings say. The camera follows the tool while a job runs
// (with "Follow tool").
void paintMainView(QPainter& painter, ToolpathCamera& camera, const Machine& machine, std::size_t doneLines);
// What the main view frames: the analysed program (a rotary job as drawn),
// or nothing.
std::optional<gcode::BoundingBox> mainViewBounds(const Machine& machine);
// The theme the settings choose.
const VisualizerTheme& mainViewTheme(const Machine& machine);

}  // namespace gs::app
