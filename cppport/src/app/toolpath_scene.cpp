#include "toolpath_scene.hpp"

#include "machine.hpp"

#include "gs/controller/locations.hpp"
#include "gs/util/jsnumber.hpp"

#include <QCoreApplication>
#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gs::app {
namespace {

constexpr double kDegree = std::numbers::pi / 180;

QString tr(const char* text) {
    return QCoreApplication::translate("gs::app::ToolpathView", text);
}

}  // namespace

// ---- the camera ------------------------------------------------------------------------

ToolpathCamera::ToolpathCamera() {
    updateRotation();
}

void ToolpathCamera::updateRotation() {
    // Rz(yaw), then tilt about the screen X axis so +Z rises on screen:
    // screen up = cos(pitch) * y' + sin(pitch) * z.
    const double cy = std::cos(yaw_), sy = std::sin(yaw_);
    const double cp = std::cos(pitch_), sp = std::sin(pitch_);
    rotation_ = {cy, -sy, 0, cp * sy, cp * cy, sp, -sp * sy, -sp * cy, cp};
}

QPointF ToolpathCamera::project(const Point3& p) const {
    const double x = p.x - target_.x, y = p.y - target_.y, z = p.z - target_.z;
    const double rx = rotation_[0] * x + rotation_[1] * y + rotation_[2] * z;
    const double ry = rotation_[3] * x + rotation_[4] * y + rotation_[5] * z;
    double f = 1;
    if (perspective_) {
        // A camera `cameraDistance_` from the target: nearer is bigger
        // (depth towards the viewer is the rotation's third row).
        const double rz = rotation_[6] * x + rotation_[7] * y + rotation_[8] * z;
        f = cameraDistance_ / std::max(cameraDistance_ - rz, cameraDistance_ * 0.05);
    }
    return {viewport_.width() / 2.0 + pan_.x() + rx * scale_ * f,
            viewport_.height() / 2.0 + pan_.y() - ry * scale_ * f};
}

void ToolpathCamera::setFlat(bool flat, const std::optional<gcode::BoundingBox>& content) {
    flat_ = flat;
    if (flat) {
        setView(View::Top, content);
    }
}

void ToolpathCamera::setView(View view, const std::optional<gcode::BoundingBox>& content) {
    if (flat_) {
        view = View::Top;
    }
    // Screen right = cos(yaw) x - sin(yaw) y; up = cos(pitch) (sin(yaw) x +
    // cos(yaw) y) + sin(pitch) z. Side views look along the machine axes
    // with Z up: front along +Y, right along -X, left along +X.
    switch (view) {
        case View::Iso: yaw_ = -35 * kDegree; pitch_ = 55 * kDegree; break;
        case View::Top: yaw_ = 0; pitch_ = 0; break;
        case View::Front: yaw_ = 0; pitch_ = 90 * kDegree; break;
        case View::Right: yaw_ = -90 * kDegree; pitch_ = 90 * kDegree; break;
        case View::Left: yaw_ = 90 * kDegree; pitch_ = 90 * kDegree; break;
    }
    view_ = view;
    updateRotation();
    fit(content);
}

void ToolpathCamera::cycleView(const std::optional<gcode::BoundingBox>& content) {
    switch (view_) {
        case View::Iso: setView(View::Top, content); break;
        case View::Top: setView(View::Front, content); break;
        case View::Front: setView(View::Right, content); break;
        case View::Right: setView(View::Left, content); break;
        case View::Left: setView(View::Iso, content); break;
    }
}

void ToolpathCamera::zoom(double factor) {
    scale_ = std::clamp(scale_ * factor, 0.01, 5000.0);
    pan_ *= factor;  // keep the middle of the view where it is
}

void ToolpathCamera::zoomAbout(QPointF point, double factor) {
    const QPointF cursor = point - QPointF(viewport_.width() / 2.0, viewport_.height() / 2.0);
    pan_ = cursor - (cursor - pan_) * factor;
    scale_ = std::clamp(scale_ * factor, 0.01, 5000.0);
}

void ToolpathCamera::orbit(double yawDegrees, double pitchDegrees) {
    if (flat_) {
        return;
    }
    yaw_ += yawDegrees * kDegree;
    pitch_ = std::clamp(pitch_ + pitchDegrees * kDegree, 0.0, 90 * kDegree);
    updateRotation();
}

void ToolpathCamera::pan(double dx, double dy) {
    pan_ += QPointF(dx, dy);
}

void ToolpathCamera::centreOn(double x, double y) {
    target_.x = x;
    target_.y = y;
    pan_ = {};
}

double ToolpathCamera::yawDegrees() const noexcept {
    return yaw_ / kDegree;
}

double ToolpathCamera::pitchDegrees() const noexcept {
    return pitch_ / kDegree;
}

void ToolpathCamera::fit(const std::optional<gcode::BoundingBox>& content) {
    pan_ = {};
    const gcode::BoundingBox box = content.value_or(gcode::BoundingBox{{0, 0, 0, 0}, {100, 100, 0, 0}});
    target_ = {(box.min.x + box.max.x) / 2, (box.min.y + box.max.y) / 2, (box.min.z + box.max.z) / 2};
    // The perspective camera stands back four times the content's size.
    cameraDistance_ = 4 * std::max({100.0, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z});
    // Fit the projected corners of the box, with a margin.
    double extentX = 1, extentY = 1;
    for (int i = 0; i < 8; ++i) {
        const Point3 corner{i & 1 ? box.max.x : box.min.x, i & 2 ? box.max.y : box.min.y,
                            i & 4 ? box.max.z : box.min.z};
        const double x = corner.x - target_.x, y = corner.y - target_.y, z = corner.z - target_.z;
        extentX = std::max(extentX, std::fabs(rotation_[0] * x + rotation_[1] * y + rotation_[2] * z));
        extentY = std::max(extentY, std::fabs(rotation_[3] * x + rotation_[4] * y + rotation_[5] * z));
    }
    scale_ = std::min((viewport_.width() * 0.42) / extentX, (viewport_.height() * 0.40) / extentY);
}

// ---- drawing ---------------------------------------------------------------------------

namespace scene {

void paintBackground(QPainter& painter, const ToolpathCamera& camera, const VisualizerTheme& theme,
                     const std::optional<gcode::BoundingBox>& bounds, const std::optional<QRectF>& gridArea) {
    painter.fillRect(QRectF(QPointF(0, 0), camera.viewport()), theme.background);

    // A 10 mm grid on the XY plane around the program (or the origin), or
    // over the given area; every fifth line major.
    double gx0 = std::floor(std::min(0.0, bounds ? bounds->min.x : 0.0) / 10) * 10 - 10;
    double gy0 = std::floor(std::min(0.0, bounds ? bounds->min.y : 0.0) / 10) * 10 - 10;
    double gx1 = std::ceil(std::max(100.0, bounds ? bounds->max.x : 100.0) / 10) * 10 + 10;
    double gy1 = std::ceil(std::max(100.0, bounds ? bounds->max.y : 100.0) / 10) * 10 + 10;
    if (gridArea) {
        gx0 = gridArea->left();
        gy0 = gridArea->top();
        gx1 = gridArea->right();
        gy1 = gridArea->bottom();
    }
    QVector<QLineF> minor;
    QVector<QLineF> major;
    const auto isMajor = [](double v) { return std::fabs(std::remainder(v, 50.0)) < 1e-6; };
    for (double x = gx0; x <= gx1 + 1e-9; x += 10) {
        (isMajor(x) ? major : minor).append(QLineF(camera.project({x, gy0, 0}), camera.project({x, gy1, 0})));
    }
    for (double y = gy0; y <= gy1 + 1e-9; y += 10) {
        (isMajor(y) ? major : minor).append(QLineF(camera.project({gx0, y, 0}), camera.project({gx1, y, 0})));
    }
    QColor minorColor = theme.gridMinor;
    minorColor.setAlpha(110);
    painter.setPen(QPen(minorColor, 1));
    painter.drawLines(minor);
    QColor majorColor = theme.gridMajor;
    majorColor.setAlpha(110);
    painter.setPen(QPen(majorColor, 1));
    painter.drawLines(major);

    // Work origin axes.
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF origin = camera.project({0, 0, 0});
    const double axis = 15 / std::max(camera.scale() / 4, 0.25);
    painter.setPen(QPen(theme.axisX, 2));
    painter.drawLine(origin, camera.project({axis, 0, 0}));
    painter.setPen(QPen(theme.axisY, 2));
    painter.drawLine(origin, camera.project({0, axis, 0}));
    painter.setPen(QPen(theme.axisZ, 2));
    painter.drawLine(origin, camera.project({0, 0, axis}));
}

void paintBox(QPainter& painter, const ToolpathCamera& camera, const gcode::BoundingBox& box, const QColor& color) {
    // Box3Helper: the twelve edges, at 65 %.
    QColor edge = color;
    edge.setAlphaF(0.65f);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(edge, 1.5));
    const auto corner = [&box](int i) {
        return Point3{i & 1 ? box.max.x : box.min.x, i & 2 ? box.max.y : box.min.y, i & 4 ? box.max.z : box.min.z};
    };
    QVector<QLineF> edges;
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if (!(i & bit)) {
                edges.append(QLineF(camera.project(corner(i)), camera.project(corner(i | bit))));
            }
        }
    }
    painter.drawLines(edges);
}

void paintRect(QPainter& painter, const ToolpathCamera& camera, const QRectF& area, const QColor& color) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 1.5));
    const QPointF a = camera.project({area.left(), area.top(), 0});
    const QPointF b = camera.project({area.right(), area.top(), 0});
    const QPointF c = camera.project({area.right(), area.bottom(), 0});
    const QPointF d = camera.project({area.left(), area.bottom(), 0});
    painter.drawPolygon(QPolygonF({a, b, c, d}));
}

void paintLabel(QPainter& painter, const ToolpathCamera& camera, const Point3& at, const QString& text,
                const QColor& color) {
    QFont font = painter.font();
    font.setPointSizeF(7.5);
    painter.setFont(font);
    QColor ink = color;
    ink.setAlphaF(0.8f);
    painter.setPen(ink);
    const QPointF p = camera.project(at);
    const QRectF box(p.x() - 60, p.y() - 8, 120, 16);
    painter.drawText(box, Qt::AlignCenter, text);
}

void paintSegments(QPainter& painter, const ToolpathCamera& camera, const std::vector<float>& segments,
                   const std::vector<std::uint32_t>& lines,
                   const std::function<const QPen*(std::size_t, std::uint32_t)>& pen, double rotationA) {
    // Turned back by the rotary's angle about X (gviewer's
    // setToolpathRotationA: the toolpath's root turned by +A), so the part
    // under the tool is on top.
    const double angle = rotationA * kDegree;
    const double cosA = std::cos(angle);
    const double sinA = std::sin(angle);
    const auto point = [&](std::size_t i) -> Point3 {
        const double y = segments[i + 1];
        const double z = segments[i + 2];
        return rotationA == 0 ? Point3{segments[i], y, z}
                              : Point3{segments[i], y * cosA - z * sinA, y * sinA + z * cosA};
    };
    // Batched by pen, in the order the pens first appear (runs of one pen
    // skip the search).
    std::vector<std::pair<const QPen*, QVector<QLineF>>> batches;
    const QPen* lastPen = nullptr;
    std::size_t lastBatch = 0;
    for (std::size_t i = 0, s = 0; i + 5 < segments.size(); i += 6, ++s) {
        const QPen* chosen = pen(s, s < lines.size() ? lines[s] : 0);
        if (!chosen) {
            continue;
        }
        if (chosen != lastPen) {
            const auto found =
                std::find_if(batches.begin(), batches.end(), [chosen](const auto& b) { return b.first == chosen; });
            lastBatch = static_cast<std::size_t>(found - batches.begin());
            if (found == batches.end()) {
                batches.emplace_back(chosen, QVector<QLineF>());
            }
            lastPen = chosen;
        }
        batches[lastBatch].second.append(QLineF(camera.project(point(i)), camera.project(point(i + 3))));
    }
    // Many short segments: antialiasing costs more than it gives here.
    painter.setRenderHint(QPainter::Antialiasing, segments.size() < 6 * 200000);
    for (const auto& [chosen, drawn] : batches) {
        painter.setPen(*chosen);
        painter.drawLines(drawn);
    }
}

void paintTool(QPainter& painter, const ToolpathCamera& camera, const VisualizerTheme& theme, const Point3& position) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tool = camera.project(position);
    QColor fill = theme.tool;
    fill.setAlpha(70);
    painter.setPen(QPen(theme.tool, 2));
    painter.setBrush(fill);
    painter.drawEllipse(tool, 7, 7);
    painter.drawLine(tool + QPointF(-11, 0), tool + QPointF(11, 0));
    painter.drawLine(tool + QPointF(0, -11), tool + QPointF(0, 11));
    painter.setBrush(Qt::NoBrush);
}

void paintCaption(QPainter& painter, const QRectF& area, const VisualizerTheme& theme, const QString& caption) {
    painter.setPen(theme.text);
    painter.drawText(area.adjusted(10, 0, -10, -8), Qt::AlignBottom | Qt::AlignLeft, caption);
}

}  // namespace scene

// ---- the main view ---------------------------------------------------------------------

namespace {

bool rotaryJob(const Machine& machine) {
    return machine.hasProgram() && !machine.isAnalyzing() && machine.analysis().fileType != gcode::FileType::Default;
}

}  // namespace

std::optional<gcode::BoundingBox> mainViewBounds(const Machine& machine) {
    const bool empty = !machine.hasProgram() || machine.analysis().totalLines == 0 ||
                       (machine.toolpath().feeds.empty() && machine.toolpath().rapids.empty());
    if (empty) {
        return std::nullopt;
    }
    // A rotary job is framed as drawn: wrapped around X.
    return rotaryJob(machine) && machine.toolpath().bounded ? machine.toolpath().bounds : machine.analysis().bounds;
}

const VisualizerTheme& mainViewTheme(const Machine& machine) {
    return visualizerTheme(QString::fromStdString(machine.settings().visualizerTheme));
}

void paintMainView(QPainter& painter, ToolpathCamera& camera, const Machine& machine, std::size_t doneLines) {
    const job::ProgramAnalysis& a = machine.analysis();
    const AppSettings& settings = machine.settings();
    const VisualizerTheme& colors = mainViewTheme(machine);
    const QRectF area(QPointF(0, 0), camera.viewport());
    const bool hasPath = machine.hasProgram() && !machine.isAnalyzing();
    // Lightweight: Light draws the cuts only (upstream's SVG view skips G0)
    // and no tool; Everything draws nothing.
    const bool lite = settings.liteMode;
    const bool off = lite && settings.liteOption == "Everything";

    // The tool at the work position, and the rotary's angle: A, or in rotary
    // mode Y (the rotary is on Y; the tool stays over the centreline).
    // Deviation: upstream turned the path by A only, which Grbl's rotary
    // mode never reports.
    std::optional<Point3> tool;
    double rotaryAngle = 0;
    std::optional<QRectF> bed;
    std::optional<QRectF> keepout;
    if (controller::Controller* c = machine.controller()) {
        const auto& status = c->state().status;
        const protocol::OrderedMap& firmware = c->settings().settings;
        const double unit = firmware.get("$13") == "1" ? 25.4 : 1.0;
        const bool onY = machine.rotaryMode();
        tool = Point3{status.wpos.x() * unit, onY ? 0.0 : status.wpos.y() * unit, status.wpos.z() * unit};
        if (rotaryJob(machine)) {
            rotaryAngle = onY ? status.wpos.y() * unit : status.wpos.a();
        }
        // The camera follows the tool while a job runs.
        if (settings.followTool && c->workflow().isRunning()) {
            camera.centreOn(tool->x, tool->y);
        }
        // The machine bed once homed (buildMachineBedOptions): the machine
        // profile's size from the homing corner, and grblHAL's ATC keepout,
        // in work coordinates.
        const double homing = js::stringToNumber(firmware.get("$22"));
        if (settings.showMachineBed && std::isfinite(homing) && homing > 0 && c->hasHomed()) {
            const auto setting = [&firmware](const char* key, double fallback) {
                const double value = js::stringToNumber(firmware.get(key));
                return (firmware.find(key) != nullptr) && std::isfinite(value) ? value : fallback;
            };
            const double wcoX = (status.mpos.x() - status.wpos.x()) * unit;
            const double wcoY = (status.mpos.y() - status.wpos.y()) * unit;
            const config::MachineProfile& profile = machine.machineProfile();
            const controller::WorkRect r = controller::machineBedWorkRect(
                firmware.get("$23"), profile.width > 0 ? profile.width : 800, profile.depth > 0 ? profile.depth : 800,
                wcoX, wcoY);
            bed = QRectF(QPointF(r.minX, r.minY), QPointF(r.maxX, r.maxY));
            const bool keepoutKnown = (firmware.find("$683") != nullptr) && (firmware.find("$684") != nullptr) &&
                                      (firmware.find("$685") != nullptr) && (firmware.find("$686") != nullptr) &&
                                      (firmware.find("$687") != nullptr);
            if (keepoutKnown && setting("$683", 0) != 0) {
                const double xMin = setting("$684", 0), xMax = setting("$686", 0);
                const double yMin = setting("$685", 0), yMax = setting("$687", 0);
                if (!(xMax - xMin == 0 && yMax - yMin == 0)) {
                    const controller::WorkRect k = controller::keepoutWorkRect(xMin, xMax, yMin, yMax, wcoX, wcoY);
                    keepout = QRectF(QPointF(k.minX, k.minY), QPointF(k.maxX, k.maxY));
                }
            }
        }
    }

    // The grid, trimmed to the bed (to the 10 mm - 1 inch - lines past it).
    std::optional<QRectF> gridArea;
    if (settings.trimGridToBed && bed) {
        const double step = settings.metric ? 10 : 25.4;
        gridArea = QRectF(QPointF(std::floor((bed->left() + 1e-6) / step) * step,
                                  std::floor((bed->top() + 1e-6) / step) * step),
                          QPointF(std::ceil((bed->right() - 1e-6) / step) * step,
                                  std::ceil((bed->bottom() - 1e-6) / step) * step));
    }
    const std::optional<gcode::BoundingBox> bounds = mainViewBounds(machine);
    scene::paintBackground(painter, camera, colors, hasPath ? bounds : std::nullopt, gridArea);
    if (bed) {
        scene::paintRect(painter, camera, *bed, colors.machineBed);
    }
    if (keepout) {
        scene::paintRect(painter, camera, *keepout, colors.keepout);
    }

    if (hasPath && !off) {
        const Toolpath& path = machine.toolpath();
        QColor rapidColor = colors.rapid;
        rapidColor.setAlphaF(0.3f);  // rapidOpacity
        const QPen rapid(rapidColor, 1, Qt::DashLine);
        const QPen rapidDone(colors.processed, 1, Qt::DashLine);
        const QPen cut(colors.cutting, 1.5);
        const QPen cutDone(colors.processed, 1.5);
        if (!lite) {
            scene::paintSegments(
                painter, camera, path.rapids, path.rapidLines,
                [&](std::size_t, std::uint32_t line) {
                    return line >= doneLines ? &rapid : settings.hideProcessedLines ? nullptr : &rapidDone;
                },
                rotaryAngle);
        }
        scene::paintSegments(
            painter, camera, path.feeds, path.feedLines,
            [&](std::size_t, std::uint32_t line) {
                return line >= doneLines ? &cut : settings.hideProcessedLines ? nullptr : &cutDone;
            },
            rotaryAngle);
        // The job's extent, and its six coordinates when labelled
        // (gviewer's bounding box labels).
        if (settings.showBoundingBox && bounds) {
            scene::paintBox(painter, camera, *bounds, colors.boundingBox);
            if (settings.boundingBoxLabels) {
                const gcode::BoundingBox& b = *bounds;
                const auto label = [&settings](double mm) {
                    const double v = settings.metric ? mm : mm / 25.4;
                    const QString number =
                        std::fabs(v) < 1e-9 ? QStringLiteral("0") : QString::fromStdString(js::toFixed(v, 3));
                    return number + (settings.metric ? " mm" : " in");
                };
                const double w = std::max({2.0, (b.max.x - b.min.x) * 0.02, (b.max.y - b.min.y) * 0.02,
                                           (b.max.z - b.min.z) * 0.02});
                const double midX = (b.min.x + b.max.x) / 2;
                const double midY = (b.min.y + b.max.y) / 2;
                const QColor& ink = colors.boundingBox;
                scene::paintLabel(painter, camera, {b.min.x - w, midY, b.min.z - w}, label(b.min.x), ink);
                scene::paintLabel(painter, camera, {b.max.x + w, midY, b.min.z - w}, label(b.max.x), ink);
                scene::paintLabel(painter, camera, {midX, b.min.y - w, b.min.z - w}, label(b.min.y), ink);
                scene::paintLabel(painter, camera, {midX, b.max.y + w, b.min.z - w}, label(b.max.y), ink);
                scene::paintLabel(painter, camera, {midX, midY, b.min.z - w}, label(b.min.z), ink);
                scene::paintLabel(painter, camera, {midX, midY, b.max.z + w}, label(b.max.z), ink);
            }
        }
    }
    if (tool && !lite) {
        scene::paintTool(painter, camera, colors, *tool);
    }
    if (off) {
        painter.setPen(colors.text);
        painter.drawText(area, Qt::AlignCenter, tr("Lightweight mode: the visualizer is off"));
    }

    QString caption;
    if (!machine.hasProgram()) {
        caption = tr("No file loaded");
    } else if (machine.isAnalyzing()) {
        caption = tr("%1 - analysing...").arg(machine.programName());
    } else {
        caption = tr("%1 - %2 x %3 x %4 mm")
                      .arg(machine.programName())
                      .arg(a.bounds.max.x - a.bounds.min.x, 0, 'f', 1)
                      .arg(a.bounds.max.y - a.bounds.min.y, 0, 'f', 1)
                      .arg(a.bounds.max.z - a.bounds.min.z, 0, 'f', 1);
    }
    scene::paintCaption(painter, area, colors, caption);
}

}  // namespace gs::app
