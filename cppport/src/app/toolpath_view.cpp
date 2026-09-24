#include "toolpath_view.hpp"

#include "machine.hpp"

#include "gs/controller/locations.hpp"
#include "gs/util/jsnumber.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gs::app {
namespace {


constexpr double kDegree = std::numbers::pi / 180;

}  // namespace

// ---- preview ---------------------------------------------------------------------------

ToolpathPreview::ToolpathPreview(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 200);
}

ToolpathPreview::~ToolpathPreview() = default;

void ToolpathPreview::setToolpath(const Toolpath& path) {
    rapids_ = path.rapids;
    feeds_ = path.feeds;
    update();
}

void ToolpathPreview::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), ToolpathCanvas::kBackground);
    // Bounds in XY over both kinds of move (x0,y0,z0,x1,y1,z1 per segment).
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool any = false;
    for (const std::vector<float>* segments : {&rapids_, &feeds_}) {
        for (std::size_t i = 0; i + 2 < segments->size(); i += 3) {
            const double x = (*segments)[i];
            const double y = (*segments)[i + 1];
            minX = any ? std::min(minX, x) : x;
            maxX = any ? std::max(maxX, x) : x;
            minY = any ? std::min(minY, y) : y;
            maxY = any ? std::max(maxY, y) : y;
            any = true;
        }
    }
    if (!any) {
        painter.setPen(QColor(0xa8, 0xb0, 0xb8));
        painter.drawText(rect(), Qt::AlignCenter, tr("No preview"));
        return;
    }
    const double margin = 16;
    const double scale = std::min((width() - 2 * margin) / std::max(maxX - minX, 1e-3),
                                  (height() - 2 * margin) / std::max(maxY - minY, 1e-3));
    const QPointF centre((minX + maxX) / 2, (minY + maxY) / 2);
    const auto map = [&](double x, double y) {
        return QPointF(width() / 2.0 + (x - centre.x()) * scale, height() / 2.0 - (y - centre.y()) * scale);
    };
    const auto draw = [&](const std::vector<float>& segments, const QPen& pen) {
        painter.setPen(pen);
        for (std::size_t i = 0; i + 5 < segments.size(); i += 6) {
            painter.drawLine(map(segments[i], segments[i + 1]), map(segments[i + 3], segments[i + 4]));
        }
    };
    draw(rapids_, QPen(ToolpathCanvas::kRapid, 1, Qt::DashLine));
    draw(feeds_, QPen(ToolpathCanvas::kCut, 1.5));
    // The work origin.
    const QPointF origin = map(0, 0);
    painter.setPen(QPen(QColor(0xe5, 0x39, 0x35), 2));
    painter.drawLine(origin, origin + QPointF(14, 0));
    painter.setPen(QPen(QColor(0x43, 0xa0, 0x47), 2));
    painter.drawLine(origin, origin - QPointF(0, 14));
}

// ---- the canvas ------------------------------------------------------------------------

const QColor ToolpathCanvas::kBackground(0x1e, 0x22, 0x27);
const QColor ToolpathCanvas::kRapid(0x7d, 0x87, 0x93);
const QColor ToolpathCanvas::kCut(0x4f, 0xc3, 0xf7);
const QColor ToolpathCanvas::kDone(0x4a, 0x55, 0x61);

ToolpathCanvas::ToolpathCanvas(QWidget* parent) : QWidget(parent), theme_(&visualizerTheme("Dark")) {
    setMinimumSize(360, 280);
    setMouseTracking(false);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    const auto button = [this, bar](const QString& text, const QString& tip, auto action) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, action);
        bar->addWidget(b);
        buttons_.push_back(b);
    };
    button(tr("Top"), tr("Look down on the XY plane"), [this] { setTopView(); });
    button(tr("3D"), tr("Isometric view"), [this] { set3dView(); });
    button(tr("Fit"), tr("Fit the toolpath (double-click)"), [this] { fit(); });
    auto* overlay = new QWidget(this);
    overlay->setLayout(bar);
    overlay->move(8, 8);
    overlay->adjustSize();
    styleButtons();
    updateRotation();
}

void ToolpathCanvas::setTheme(const VisualizerTheme& theme) {
    theme_ = &theme;
    styleButtons();
    update();
}

void ToolpathCanvas::addThemedButton(QToolButton* button) {
    buttons_.push_back(button);
    styleButtons();
}

void ToolpathCanvas::styleButtons() {
    const QString style = QString("QToolButton { color:%1; padding:3px 8px; }"
                                  "QToolButton:hover { background:rgba(128,128,128,60); border-radius:3px; }"
                                  "QToolButton:checked { background:#3b82f6; color:white; border-radius:3px; }")
                              .arg(theme_->text.name());
    for (QToolButton* button : buttons_) {
        button->setStyleSheet(style);
    }
}

void ToolpathCanvas::centreOn(double x, double y) {
    target_.x = x;
    target_.y = y;
    pan_ = {};
}

void ToolpathCanvas::updateRotation() {
    // Rz(yaw), then tilt about the screen X axis so +Z rises on screen:
    // screen up = cos(pitch) * y' + sin(pitch) * z.
    const double cy = std::cos(yaw_), sy = std::sin(yaw_);
    const double cp = std::cos(pitch_), sp = std::sin(pitch_);
    rotation_ = {cy, -sy, 0, cp * sy, cp * cy, sp, -sp * sy, -sp * cy, cp};
}

QPointF ToolpathCanvas::project(const Point3& p) const {
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
    return {width() / 2.0 + pan_.x() + rx * scale_ * f, height() / 2.0 + pan_.y() - ry * scale_ * f};
}

void ToolpathCanvas::setPerspective(bool perspective) {
    if (perspective_ != perspective) {
        perspective_ = perspective;
        update();
    }
}

void ToolpathCanvas::setFlat(bool flat) {
    flat_ = flat;
    if (flat) {
        setView(View::Top);
    }
}

void ToolpathCanvas::setView(View view) {
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
    fit();
}

void ToolpathCanvas::cycleView() {
    switch (view_) {
        case View::Iso: setView(View::Top); break;
        case View::Top: setView(View::Front); break;
        case View::Front: setView(View::Right); break;
        case View::Right: setView(View::Left); break;
        case View::Left: setView(View::Iso); break;
    }
}

void ToolpathCanvas::zoom(double factor) {
    scale_ = std::clamp(scale_ * factor, 0.01, 5000.0);
    pan_ *= factor;  // keep the middle of the view where it is
    update();
}

void ToolpathCanvas::orbit(double yawDegrees, double pitchDegrees) {
    if (flat_) {
        return;
    }
    yaw_ += yawDegrees * kDegree;
    pitch_ = std::clamp(pitch_ + pitchDegrees * kDegree, 0.0, 90 * kDegree);
    updateRotation();
    update();
}

double ToolpathCanvas::yawDegrees() const noexcept {
    return yaw_ / kDegree;
}

double ToolpathCanvas::pitchDegrees() const noexcept {
    return pitch_ / kDegree;
}

void ToolpathCanvas::pan(double dx, double dy) {
    pan_ += QPointF(dx, dy);
    update();
}

void ToolpathCanvas::setKeyboardControl(bool on) {
    keyboardControl_ = on;
    setFocusPolicy(on ? Qt::StrongFocus : Qt::NoFocus);
    if (!on && hasFocus()) {
        clearFocus();
    }
}

void ToolpathCanvas::keyPressEvent(QKeyEvent* event) {
    if (!keyboardControl_) {
        QWidget::keyPressEvent(event);
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
        case Qt::Key_Equal: zoom(1.25); break;
        case Qt::Key_Minus: zoom(0.8); break;
        case Qt::Key_Home: fit(); break;
        default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void ToolpathCanvas::fit() {
    pan_ = {};
    const gcode::BoundingBox box = contentBounds().value_or(gcode::BoundingBox{{0, 0, 0, 0}, {100, 100, 0, 0}});
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
    scale_ = std::min((width() * 0.42) / extentX, (height() * 0.40) / extentY);
    update();
}

void ToolpathCanvas::paintScene(QPainter& painter, const std::optional<gcode::BoundingBox>& bounds,
                                const std::optional<QRectF>& gridArea) {
    painter.fillRect(rect(), theme_->background);

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
        (isMajor(x) ? major : minor).append(QLineF(project({x, gy0, 0}), project({x, gy1, 0})));
    }
    for (double y = gy0; y <= gy1 + 1e-9; y += 10) {
        (isMajor(y) ? major : minor).append(QLineF(project({gx0, y, 0}), project({gx1, y, 0})));
    }
    QColor minorColor = theme_->gridMinor;
    minorColor.setAlpha(110);
    painter.setPen(QPen(minorColor, 1));
    painter.drawLines(minor);
    QColor majorColor = theme_->gridMajor;
    majorColor.setAlpha(110);
    painter.setPen(QPen(majorColor, 1));
    painter.drawLines(major);

    // Work origin axes.
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF origin = project({0, 0, 0});
    painter.setPen(QPen(theme_->axisX, 2));
    painter.drawLine(origin, project({15 / std::max(scale_ / 4, 0.25), 0, 0}));
    painter.setPen(QPen(theme_->axisY, 2));
    painter.drawLine(origin, project({0, 15 / std::max(scale_ / 4, 0.25), 0}));
    painter.setPen(QPen(theme_->axisZ, 2));
    painter.drawLine(origin, project({0, 0, 15 / std::max(scale_ / 4, 0.25)}));
}

void ToolpathCanvas::paintBox(QPainter& painter, const gcode::BoundingBox& box, const QColor& color) {
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
                edges.append(QLineF(project(corner(i)), project(corner(i | bit))));
            }
        }
    }
    painter.drawLines(edges);
}

void ToolpathCanvas::paintRect(QPainter& painter, const QRectF& area, const QColor& color) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 1.5));
    const QPointF a = project({area.left(), area.top(), 0});
    const QPointF b = project({area.right(), area.top(), 0});
    const QPointF c = project({area.right(), area.bottom(), 0});
    const QPointF d = project({area.left(), area.bottom(), 0});
    painter.drawPolygon(QPolygonF({a, b, c, d}));
}

void ToolpathCanvas::paintLabel(QPainter& painter, const Point3& at, const QString& text, const QColor& color) {
    QFont font = painter.font();
    font.setPointSizeF(7.5);
    painter.setFont(font);
    QColor ink = color;
    ink.setAlphaF(0.8f);
    painter.setPen(ink);
    const QPointF p = project(at);
    const QRectF box(p.x() - 60, p.y() - 8, 120, 16);
    painter.drawText(box, Qt::AlignCenter, text);
}

void ToolpathCanvas::paintSegments(QPainter& painter, const std::vector<float>& segments,
                                   const std::vector<std::uint32_t>& lines,
                                   const std::function<const QPen*(std::size_t, std::uint32_t)>& pen,
                                   double rotationA) {
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
        batches[lastBatch].second.append(QLineF(project(point(i)), project(point(i + 3))));
    }
    // Many short segments: antialiasing costs more than it gives here.
    painter.setRenderHint(QPainter::Antialiasing, segments.size() < 6 * 200000);
    for (const auto& [chosen, drawn] : batches) {
        painter.setPen(*chosen);
        painter.drawLines(drawn);
    }
}

void ToolpathCanvas::paintTool(QPainter& painter, const Point3& position) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tool = project(position);
    QColor fill = theme_->tool;
    fill.setAlpha(70);
    painter.setPen(QPen(theme_->tool, 2));
    painter.setBrush(fill);
    painter.drawEllipse(tool, 7, 7);
    painter.drawLine(tool + QPointF(-11, 0), tool + QPointF(11, 0));
    painter.drawLine(tool + QPointF(0, -11), tool + QPointF(0, 11));
    painter.setBrush(Qt::NoBrush);
}

void ToolpathCanvas::paintCaption(QPainter& painter, const QString& caption) {
    painter.setPen(theme_->text);
    painter.drawText(rect().adjusted(10, 0, -10, -8), Qt::AlignBottom | Qt::AlignLeft, caption);
}

void ToolpathCanvas::mousePressEvent(QMouseEvent* event) {
    dragging_ = event->button();
    lastMouse_ = event->position().toPoint();
}

void ToolpathCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPoint position = event->position().toPoint();
    const QPoint delta = position - lastMouse_;
    lastMouse_ = position;
    if (dragging_ == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier) && !flat_) {
        yaw_ += delta.x() * 0.5 * kDegree;
        pitch_ = std::clamp(pitch_ - delta.y() * 0.5 * kDegree, 0.0, 90 * kDegree);
        updateRotation();
    } else if (dragging_ != Qt::NoButton) {
        pan_ += delta;
    }
    update();
}

void ToolpathCanvas::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = Qt::NoButton;
}

void ToolpathCanvas::wheelEvent(QWheelEvent* event) {
    // Zoom about the cursor.
    const double factor = std::pow(1.0015, event->angleDelta().y());
    const QPointF cursor = event->position() - QPointF(width() / 2.0, height() / 2.0);
    pan_ = cursor - (cursor - pan_) * factor;
    scale_ = std::clamp(scale_ * factor, 0.01, 5000.0);
    update();
}

void ToolpathCanvas::mouseDoubleClickEvent(QMouseEvent*) {
    fit();
}

void ToolpathCanvas::resizeEvent(QResizeEvent*) {
    fit();
}

// ---- the main visualizer ------------------------------------------------------------------

ToolpathView::ToolpathView(Machine& machine, QWidget* parent) : ToolpathCanvas(parent), machine_(machine) {
    lite_ = new QToolButton(this);
    lite_->setText(tr("Lite"));
    lite_->setCheckable(true);
    lite_->setAutoRaise(true);
    lite_->setToolTip(tr("Lightweight mode (Shift+M): for big files. Light draws the cuts flat, Everything turns "
                         "the visualizer off (Settings > General)"));
    addThemedButton(lite_);
    connect(lite_, &QToolButton::clicked, this, &ToolpathView::toggleLiteMode);
    connect(&machine_, &Machine::appSettingsChanged, this, &ToolpathView::applySettings);
    applySettings();
    connect(&machine_, &Machine::programChanged, this, &ToolpathView::programChanged);
    connect(&machine_, &Machine::senderStatusChanged, this, &ToolpathView::progressChanged);
    connect(&machine_, &Machine::workflowChanged, this, &ToolpathView::progressChanged);
    connect(&machine_, &Machine::stateChanged, this, qOverload<>(&QWidget::update));
    connect(&machine_, &Machine::connectionChanged, this, qOverload<>(&QWidget::update));
}

std::optional<gcode::BoundingBox> ToolpathView::contentBounds() const {
    const bool empty = !machine_.hasProgram() || machine_.analysis().totalLines == 0 ||
                       (machine_.toolpath().feeds.empty() && machine_.toolpath().rapids.empty());
    if (empty) {
        return std::nullopt;
    }
    // A rotary job is framed as drawn: wrapped around X.
    return rotaryJob() && machine_.toolpath().bounded ? machine_.toolpath().bounds : machine_.analysis().bounds;
}

bool ToolpathView::rotaryJob() const {
    return machine_.hasProgram() && !machine_.isAnalyzing() &&
           machine_.analysis().fileType != gcode::FileType::Default;
}

void ToolpathView::resizeEvent(QResizeEvent* event) {
    ToolpathCanvas::resizeEvent(event);
    lite_->move(width() - lite_->width() - 8, 8);
}

void ToolpathView::toggleLiteMode() {
    AppSettings settings = machine_.settings();
    settings.liteMode = !settings.liteMode;
    machine_.setSettings(settings);
}

void ToolpathView::applySettings() {
    const AppSettings& settings = machine_.settings();
    setTheme(visualizerTheme(QString::fromStdString(settings.visualizerTheme)));
    setPerspective(settings.perspective);
    setKeyboardControl(settings.accessibility.visualizerKeyboardControl);
    lite_->setChecked(settings.liteMode);
    lite_->adjustSize();
    lite_->move(width() - lite_->width() - 8, 8);
    setFlat(settings.liteMode && settings.liteOption == "Light");
    update();
}

void ToolpathView::programChanged() {
    doneLines_ = 0;
    fit();
}

void ToolpathView::progressChanged() {
    controller::Controller* c = machine_.controller();
    const std::size_t done = c && !c->workflow().isIdle() ? c->sender().received() : 0;
    if (done != doneLines_) {
        doneLines_ = done;
        update();
    }
}

void ToolpathView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const job::ProgramAnalysis& a = machine_.analysis();
    const AppSettings& settings = machine_.settings();
    const VisualizerTheme& colors = theme();
    const bool hasPath = machine_.hasProgram() && !machine_.isAnalyzing();
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
    if (controller::Controller* c = machine_.controller()) {
        const auto& status = c->state().status;
        const protocol::OrderedMap& firmware = c->settings().settings;
        const double unit = firmware.get("$13") == "1" ? 25.4 : 1.0;
        const bool onY = machine_.rotaryMode();
        tool = Point3{status.wpos.x() * unit, onY ? 0.0 : status.wpos.y() * unit, status.wpos.z() * unit};
        if (rotaryJob()) {
            rotaryAngle = onY ? status.wpos.y() * unit : status.wpos.a();
        }
        // The camera follows the tool while a job runs.
        if (settings.followTool && c->workflow().isRunning()) {
            centreOn(tool->x, tool->y);
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
            const config::MachineProfile& profile = machine_.machineProfile();
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
    const std::optional<gcode::BoundingBox> bounds = contentBounds();
    paintScene(painter, hasPath ? bounds : std::nullopt, gridArea);
    if (bed) {
        paintRect(painter, *bed, colors.machineBed);
    }
    if (keepout) {
        paintRect(painter, *keepout, colors.keepout);
    }

    if (hasPath && !off) {
        const Toolpath& path = machine_.toolpath();
        QColor rapidColor = colors.rapid;
        rapidColor.setAlphaF(0.3f);  // rapidOpacity
        const QPen rapid(rapidColor, 1, Qt::DashLine);
        const QPen rapidDone(colors.processed, 1, Qt::DashLine);
        const QPen cut(colors.cutting, 1.5);
        const QPen cutDone(colors.processed, 1.5);
        if (!lite) {
            paintSegments(
                painter, path.rapids, path.rapidLines,
                [&](std::size_t, std::uint32_t line) {
                    return line >= doneLines_ ? &rapid : settings.hideProcessedLines ? nullptr : &rapidDone;
                },
                rotaryAngle);
        }
        paintSegments(
            painter, path.feeds, path.feedLines,
            [&](std::size_t, std::uint32_t line) {
                return line >= doneLines_ ? &cut : settings.hideProcessedLines ? nullptr : &cutDone;
            },
            rotaryAngle);
        // The job's extent, and its six coordinates when labelled
        // (gviewer's bounding box labels).
        if (settings.showBoundingBox && bounds) {
            paintBox(painter, *bounds, colors.boundingBox);
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
                paintLabel(painter, {b.min.x - w, midY, b.min.z - w}, label(b.min.x), colors.boundingBox);
                paintLabel(painter, {b.max.x + w, midY, b.min.z - w}, label(b.max.x), colors.boundingBox);
                paintLabel(painter, {midX, b.min.y - w, b.min.z - w}, label(b.min.y), colors.boundingBox);
                paintLabel(painter, {midX, b.max.y + w, b.min.z - w}, label(b.max.y), colors.boundingBox);
                paintLabel(painter, {midX, midY, b.min.z - w}, label(b.min.z), colors.boundingBox);
                paintLabel(painter, {midX, midY, b.max.z + w}, label(b.max.z), colors.boundingBox);
            }
        }
    }
    if (tool && !lite) {
        paintTool(painter, *tool);
    }
    if (off) {
        painter.setPen(colors.text);
        painter.drawText(rect(), Qt::AlignCenter, tr("Lightweight mode: the visualizer is off"));
    }

    QString caption;
    if (!machine_.hasProgram()) {
        caption = tr("No file loaded");
    } else if (machine_.isAnalyzing()) {
        caption = tr("%1 - analysing...").arg(machine_.programName());
    } else {
        caption = tr("%1 - %2 x %3 x %4 mm")
                      .arg(machine_.programName())
                      .arg(a.bounds.max.x - a.bounds.min.x, 0, 'f', 1)
                      .arg(a.bounds.max.y - a.bounds.min.y, 0, 'f', 1)
                      .arg(a.bounds.max.z - a.bounds.min.z, 0, 'f', 1);
    }
    paintCaption(painter, caption);
}

}  // namespace gs::app
