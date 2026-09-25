#include "toolpath_view.hpp"

#include "machine.hpp"


#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace gs::app {
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
    camera_.setViewport(size());

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
    camera_.centreOn(x, y);
}

void ToolpathCanvas::setPerspective(bool perspective) {
    if (camera_.perspective() != perspective) {
        camera_.setPerspective(perspective);
        update();
    }
}

void ToolpathCanvas::setFlat(bool flat) {
    camera_.setViewport(size());
    camera_.setFlat(flat, contentBounds());
    update();
}

void ToolpathCanvas::setView(View view) {
    camera_.setViewport(size());
    camera_.setView(view, contentBounds());
    update();
}

void ToolpathCanvas::cycleView() {
    camera_.setViewport(size());
    camera_.cycleView(contentBounds());
    update();
}

void ToolpathCanvas::zoom(double factor) {
    camera_.zoom(factor);
    update();
}

void ToolpathCanvas::orbit(double yawDegrees, double pitchDegrees) {
    camera_.orbit(yawDegrees, pitchDegrees);
    update();
}

double ToolpathCanvas::yawDegrees() const noexcept {
    return camera_.yawDegrees();
}

double ToolpathCanvas::pitchDegrees() const noexcept {
    return camera_.pitchDegrees();
}

void ToolpathCanvas::pan(double dx, double dy) {
    camera_.pan(dx, dy);
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
    camera_.setViewport(size());
    camera_.fit(contentBounds());
    update();
}

void ToolpathCanvas::paintScene(QPainter& painter, const std::optional<gcode::BoundingBox>& bounds,
                                const std::optional<QRectF>& gridArea) {
    scene::paintBackground(painter, camera_, *theme_, bounds, gridArea);
}

void ToolpathCanvas::paintBox(QPainter& painter, const gcode::BoundingBox& box, const QColor& color) {
    scene::paintBox(painter, camera_, box, color);
}

void ToolpathCanvas::paintRect(QPainter& painter, const QRectF& area, const QColor& color) {
    scene::paintRect(painter, camera_, area, color);
}

void ToolpathCanvas::paintLabel(QPainter& painter, const Point3& at, const QString& text, const QColor& color) {
    scene::paintLabel(painter, camera_, at, text, color);
}

void ToolpathCanvas::paintSegments(QPainter& painter, const std::vector<float>& segments,
                                   const std::vector<std::uint32_t>& lines,
                                   const std::function<const QPen*(std::size_t, std::uint32_t)>& pen,
                                   double rotationA) {
    scene::paintSegments(painter, camera_, segments, lines, pen, rotationA);
}

void ToolpathCanvas::paintTool(QPainter& painter, const Point3& position) {
    scene::paintTool(painter, camera_, *theme_, position);
}

void ToolpathCanvas::paintCaption(QPainter& painter, const QString& caption) {
    scene::paintCaption(painter, rect(), *theme_, caption);
}

void ToolpathCanvas::mousePressEvent(QMouseEvent* event) {
    dragging_ = event->button();
    lastMouse_ = event->position().toPoint();
}

void ToolpathCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPoint position = event->position().toPoint();
    const QPoint delta = position - lastMouse_;
    lastMouse_ = position;
    if (dragging_ == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier) && !camera_.flat()) {
        camera_.dragOrbit(delta);
    } else if (dragging_ != Qt::NoButton) {
        camera_.pan(delta.x(), delta.y());
    }
    update();
}

void ToolpathCanvas::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = Qt::NoButton;
}

void ToolpathCanvas::wheelEvent(QWheelEvent* event) {
    // Zoom about the cursor.
    camera_.zoomAbout(event->position(), std::pow(1.0015, event->angleDelta().y()));
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
    return mainViewBounds(machine_);
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
    setTheme(mainViewTheme(machine_));
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
    camera().setViewport(size());
    paintMainView(painter, camera(), machine_, doneLines_);
}

}  // namespace gs::app
