#include "toolpath_view.hpp"

#include "machine.hpp"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gs::app {
namespace {

const QColor kBackground(0x1e, 0x22, 0x27);
const QColor kGrid(0x2c, 0x33, 0x3b);
const QColor kRapid(0x7d, 0x87, 0x93);
const QColor kCut(0x4f, 0xc3, 0xf7);
const QColor kDone(0x4a, 0x55, 0x61);
const QColor kTool(0xff, 0xca, 0x28);
const QColor kText(0xa8, 0xb0, 0xb8);

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
    painter.fillRect(rect(), kBackground);
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
        painter.setPen(kText);
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
    draw(rapids_, QPen(kRapid, 1, Qt::DashLine));
    draw(feeds_, QPen(kCut, 1.5));
    // The work origin.
    const QPointF origin = map(0, 0);
    painter.setPen(QPen(QColor(0xe5, 0x39, 0x35), 2));
    painter.drawLine(origin, origin + QPointF(14, 0));
    painter.setPen(QPen(QColor(0x43, 0xa0, 0x47), 2));
    painter.drawLine(origin, origin - QPointF(0, 14));
}

ToolpathView::ToolpathView(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
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
        b->setStyleSheet("QToolButton { color:#c8d0d8; padding:3px 8px; }"
                         "QToolButton:hover { background:#2f363f; border-radius:3px; }");
        connect(b, &QToolButton::clicked, this, action);
        bar->addWidget(b);
        return b;
    };
    topButton_ = button(tr("Top"), tr("Look down on the XY plane"), [this] { setTopView(); });
    isoButton_ = button(tr("3D"), tr("Isometric view"), [this] { set3dView(); });
    fitButton_ = button(tr("Fit"), tr("Fit the toolpath (double-click)"), [this] { fit(); });
    auto* overlay = new QWidget(this);
    overlay->setLayout(bar);
    overlay->move(8, 8);
    overlay->adjustSize();

    updateRotation();
    connect(&machine_, &Machine::programChanged, this, &ToolpathView::programChanged);
    connect(&machine_, &Machine::senderStatusChanged, this, &ToolpathView::progressChanged);
    connect(&machine_, &Machine::workflowChanged, this, &ToolpathView::progressChanged);
    connect(&machine_, &Machine::stateChanged, this, qOverload<>(&QWidget::update));
    connect(&machine_, &Machine::connectionChanged, this, qOverload<>(&QWidget::update));
}

void ToolpathView::updateRotation() {
    // Rz(yaw), then tilt about the screen X axis so +Z rises on screen:
    // screen up = cos(pitch) * y' + sin(pitch) * z.
    const double cy = std::cos(yaw_), sy = std::sin(yaw_);
    const double cp = std::cos(pitch_), sp = std::sin(pitch_);
    rotation_ = {cy, -sy, 0, cp * sy, cp * cy, sp, -sp * sy, -sp * cy, cp};
}

QPointF ToolpathView::project(const Point3& p) const {
    const double x = p.x - target_.x, y = p.y - target_.y, z = p.z - target_.z;
    const double rx = rotation_[0] * x + rotation_[1] * y + rotation_[2] * z;
    const double ry = rotation_[3] * x + rotation_[4] * y + rotation_[5] * z;
    return {width() / 2.0 + pan_.x() + rx * scale_, height() / 2.0 + pan_.y() - ry * scale_};
}

void ToolpathView::setTopView() {
    yaw_ = 0;
    pitch_ = 0;
    updateRotation();
    fit();
}

void ToolpathView::set3dView() {
    yaw_ = -35 * kDegree;
    pitch_ = 55 * kDegree;
    updateRotation();
    fit();
}

void ToolpathView::fit() {
    pan_ = {};
    const job::ProgramAnalysis& a = machine_.analysis();
    const bool empty = !machine_.hasProgram() || a.totalLines == 0 ||
                       (machine_.toolpath().feeds.empty() && machine_.toolpath().rapids.empty());
    const gcode::BoundingBox box =
        empty ? gcode::BoundingBox{{0, 0, 0, 0}, {100, 100, 0, 0}} : a.bounds;
    target_ = {(box.min.x + box.max.x) / 2, (box.min.y + box.max.y) / 2, (box.min.z + box.max.z) / 2};
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
    painter.fillRect(rect(), kBackground);

    // A 10 mm grid on the XY plane around the program (or the origin).
    const job::ProgramAnalysis& a = machine_.analysis();
    const bool hasPath = machine_.hasProgram() && !machine_.isAnalyzing();
    const double gx0 = std::floor(std::min(0.0, hasPath ? a.bounds.min.x : 0.0) / 10) * 10 - 10;
    const double gy0 = std::floor(std::min(0.0, hasPath ? a.bounds.min.y : 0.0) / 10) * 10 - 10;
    const double gx1 = std::ceil(std::max(100.0, hasPath ? a.bounds.max.x : 100.0) / 10) * 10 + 10;
    const double gy1 = std::ceil(std::max(100.0, hasPath ? a.bounds.max.y : 100.0) / 10) * 10 + 10;
    painter.setPen(QPen(kGrid, 1));
    QVector<QLineF> grid;
    for (double x = gx0; x <= gx1 + 1e-9; x += 10) {
        grid.append(QLineF(project({x, gy0, 0}), project({x, gy1, 0})));
    }
    for (double y = gy0; y <= gy1 + 1e-9; y += 10) {
        grid.append(QLineF(project({gx0, y, 0}), project({gx1, y, 0})));
    }
    painter.drawLines(grid);

    // Work origin axes.
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF origin = project({0, 0, 0});
    painter.setPen(QPen(QColor(0xe5, 0x39, 0x35), 2));
    painter.drawLine(origin, project({15 / std::max(scale_ / 4, 0.25), 0, 0}));
    painter.setPen(QPen(QColor(0x43, 0xa0, 0x47), 2));
    painter.drawLine(origin, project({0, 15 / std::max(scale_ / 4, 0.25), 0}));
    painter.setPen(QPen(QColor(0x1e, 0x88, 0xe5), 2));
    painter.drawLine(origin, project({0, 0, 15 / std::max(scale_ / 4, 0.25)}));

    if (hasPath) {
        const Toolpath& path = machine_.toolpath();
        const auto draw = [&](const std::vector<float>& segments, const std::vector<std::uint32_t>& lines,
                              const QPen& pending, const QPen& done) {
            QVector<QLineF> todo;
            QVector<QLineF> finished;
            todo.reserve(static_cast<qsizetype>(segments.size() / 6));
            for (std::size_t i = 0, s = 0; i + 5 < segments.size(); i += 6, ++s) {
                const QLineF line(project({segments[i], segments[i + 1], segments[i + 2]}),
                                  project({segments[i + 3], segments[i + 4], segments[i + 5]}));
                if (s < lines.size() && lines[s] < doneLines_) {
                    finished.append(line);
                } else {
                    todo.append(line);
                }
            }
            painter.setPen(done);
            painter.drawLines(finished);
            painter.setPen(pending);
            painter.drawLines(todo);
        };
        // Many short segments: antialiasing costs more than it gives here.
        painter.setRenderHint(QPainter::Antialiasing, path.feeds.size() < 6 * 200000);
        draw(path.rapids, path.rapidLines, QPen(kRapid, 1, Qt::DashLine), QPen(kDone, 1, Qt::DashLine));
        draw(path.feeds, path.feedLines, QPen(kCut, 1.5), QPen(kDone, 1.5));
    }

    // The tool, at the work position.
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (controller::Controller* c = machine_.controller()) {
        const auto& status = c->state().status;
        const double unit = c->settings().settings.get("$13") == "1" ? 25.4 : 1.0;
        const QPointF tool = project({status.wpos.x() * unit, status.wpos.y() * unit, status.wpos.z() * unit});
        painter.setPen(QPen(kTool, 2));
        painter.setBrush(QColor(0xff, 0xca, 0x28, 70));
        painter.drawEllipse(tool, 7, 7);
        painter.drawLine(tool + QPointF(-11, 0), tool + QPointF(11, 0));
        painter.drawLine(tool + QPointF(0, -11), tool + QPointF(0, 11));
        painter.setBrush(Qt::NoBrush);
    }

    // Caption.
    painter.setPen(kText);
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
    painter.drawText(rect().adjusted(10, 0, -10, -8), Qt::AlignBottom | Qt::AlignLeft, caption);
}

void ToolpathView::mousePressEvent(QMouseEvent* event) {
    dragging_ = event->button();
    lastMouse_ = event->position().toPoint();
}

void ToolpathView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint position = event->position().toPoint();
    const QPoint delta = position - lastMouse_;
    lastMouse_ = position;
    if (dragging_ == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier)) {
        yaw_ += delta.x() * 0.5 * kDegree;
        pitch_ = std::clamp(pitch_ - delta.y() * 0.5 * kDegree, 0.0, 90 * kDegree);
        updateRotation();
    } else if (dragging_ != Qt::NoButton) {
        pan_ += delta;
    }
    update();
}

void ToolpathView::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = Qt::NoButton;
}

void ToolpathView::wheelEvent(QWheelEvent* event) {
    // Zoom about the cursor.
    const double factor = std::pow(1.0015, event->angleDelta().y());
    const QPointF cursor = event->position() - QPointF(width() / 2.0, height() / 2.0);
    pan_ = cursor - (cursor - pan_) * factor;
    scale_ = std::clamp(scale_ * factor, 0.01, 5000.0);
    update();
}

void ToolpathView::mouseDoubleClickEvent(QMouseEvent*) {
    fit();
}

void ToolpathView::resizeEvent(QResizeEvent*) {
    fit();
}

}  // namespace gs::app
