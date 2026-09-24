#pragma once

// The toolpath visualizer (gSender's Visualizer widget): the loaded program's
// rapids and cutting moves in 3D with orbit/pan/zoom, the tool position, and
// the job's progress (finished moves dimmed).
//
// Drawn with QPainter so it also renders on the offscreen platform (the
// automated screenshots); the data it draws is renderer-independent, so an
// OpenGL implementation can replace it if large files need one.

#include "visualizer_theme.hpp"

#include "gs/gcode/interpreter.hpp"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QPainter;
class QToolButton;

namespace gs::app {

class Machine;
struct Toolpath;

// What both visualizers share: the camera (the views, orbit/pan/zoom, fit),
// the grid, the work origin, the segments and the tool marker.
class ToolpathCanvas : public QWidget {
    Q_OBJECT

public:
    // The visualizer's camera presets; each also fits the program.
    enum class View { Iso, Top, Front, Right, Left };
    void setView(View view);
    View view() const noexcept { return view_; }
    void cycleView();  // 3D, Top, Front, Right, Left, as upstream's shortcut
    void setTopView() { setView(View::Top); }
    void set3dView() { setView(View::Iso); }
    void fit();
    void zoom(double factor);  // about the middle of the view
    // Turns the camera (degrees; not while flat) and moves the view (pixels).
    void orbit(double yawDegrees, double pitchDegrees);
    void pan(double dx, double dy);
    // Settings > Accessibility > Keyboard control: with the view focused
    // (click it or tab to it) the arrows orbit, Ctrl+arrows pan, + and -
    // zoom and Home fits.
    void setKeyboardControl(bool on);
    bool keyboardControl() const noexcept { return keyboardControl_; }
    double yawDegrees() const noexcept;
    double pitchDegrees() const noexcept;
    // Held flat: every view is the top view and dragging only pans.
    void setFlat(bool flat);
    bool flat() const noexcept { return flat_; }
    void setTheme(const VisualizerTheme& theme);
    // Perspective (upstream's default) shows depth like a camera; otherwise
    // orthographic, parallel lines kept parallel.
    void setPerspective(bool perspective);
    bool perspective() const noexcept { return perspective_; }
    const VisualizerTheme& theme() const noexcept { return *theme_; }

    static const QColor kBackground;
    static const QColor kRapid;
    static const QColor kCut;
    static const QColor kDone;

protected:
    explicit ToolpathCanvas(QWidget* parent = nullptr);

    struct Point3 {
        double x = 0, y = 0, z = 0;
    };
    QPointF project(const Point3& p) const;

    // The box fit() frames: the program's, or nothing (a 100 mm square).
    virtual std::optional<gcode::BoundingBox> contentBounds() const = 0;

    // Background, the grid (10 mm, every fifth line major) around `bounds`
    // (or the origin) - over `gridArea` instead when given - and the work
    // origin's axes.
    void paintScene(QPainter& painter, const std::optional<gcode::BoundingBox>& bounds,
                    const std::optional<QRectF>& gridArea = std::nullopt);
    // Segments (x0,y0,z0,x1,y1,z1 each) and their sender lines; `pen` gives
    // each one's pen by index and line, nullptr to leave it out. Segments
    // are batched by pen. `rotationA`: a rotary job's path turned back by
    // the rotary's angle (degrees), so what is under the tool is on top.
    void paintSegments(QPainter& painter, const std::vector<float>& segments, const std::vector<std::uint32_t>& lines,
                       const std::function<const QPen*(std::size_t index, std::uint32_t line)>& pen,
                       double rotationA = 0);
    void paintTool(QPainter& painter, const Point3& position);
    void paintCaption(QPainter& painter, const QString& caption);
    // A wireframe box, a rectangle on the XY plane, a label at a point.
    void paintBox(QPainter& painter, const gcode::BoundingBox& box, const QColor& color);
    void paintRect(QPainter& painter, const QRectF& area, const QColor& color);
    void paintLabel(QPainter& painter, const Point3& at, const QString& text, const QColor& color);
    // The camera over (x, y), angles and scale kept.
    void centreOn(double x, double y);

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateRotation();
    void styleButtons();

    // Camera: rotation about Z (yaw) then tilt towards the viewer (pitch),
    // orthographic, `scale_` pixels per millimetre, centred on `target_`.
    bool keyboardControl_ = false;
    View view_ = View::Top;
    double yaw_ = 0;
    double pitch_ = 0;
    double scale_ = 4;
    Point3 target_;
    QPointF pan_;
    std::array<double, 9> rotation_{};  // cached from yaw/pitch
    QPoint lastMouse_;
    Qt::MouseButton dragging_ = Qt::NoButton;
    bool flat_ = false;
    bool perspective_ = false;
    double cameraDistance_ = 400;  // mm from the target, for perspective
    const VisualizerTheme* theme_;
    std::vector<QToolButton*> buttons_;

protected:
    // The canvas' own buttons follow the theme; subclasses add theirs.
    void addThemedButton(QToolButton* button);
};

// The main visualizer: the loaded job, its progress and the machine's tool.
class ToolpathView final : public ToolpathCanvas {
    Q_OBJECT

public:
    explicit ToolpathView(Machine& machine, QWidget* parent = nullptr);

    // Lightweight mode (the feather; LIGHTWEIGHT_MODE): Light draws only the
    // cuts, flat from above, without the tool; Everything turns the drawing
    // off.
    void toggleLiteMode();

protected:
    std::optional<gcode::BoundingBox> contentBounds() const override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool rotaryJob() const;
    void applySettings();
    void programChanged();
    void progressChanged();

    Machine& machine_;
    std::size_t doneLines_ = 0;  // sender lines acknowledged
    QToolButton* lite_;
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
