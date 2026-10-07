#include "nav_cube_item.hpp"
#include "toolpath_item.hpp"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace gs::ui {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegree = kPi / 180.0;

struct Point3D {
    double x = 0, y = 0, z = 0;
};

// Machine coordinates: +X Right, -X Left, +Y Back, -Y Front, +Z Top, -Z Bottom
const NavCubeItem::FaceDef kFaces[6] = {
    {"TOP", "top", {4, 5, 6, 7}, 0, 0, 1},
    {"BOTTOM", "bottom", {3, 2, 1, 0}, 0, 0, -1},
    {"FRONT", "front", {0, 1, 5, 4}, 0, -1, 0},
    {"BACK", "back", {2, 3, 7, 6}, 0, 1, 0},
    {"LEFT", "left", {3, 0, 4, 7}, -1, 0, 0},
    {"RIGHT", "right", {1, 2, 6, 5}, 1, 0, 0},
};

}  // namespace

NavCubeItem::NavCubeItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAntialiasing(true);
    setOpaquePainting(false);
}

void NavCubeItem::setView(ToolpathItem* view) {
    if (view_ != view) {
        if (view_) {
            disconnect(view_, nullptr, this, nullptr);
        }
        view_ = view;
        if (view_) {
            connect(view_, &ToolpathItem::cameraChanged, this, [this] {
                update();
                Q_EMIT orientationChanged();
            });
        }
        update();
        Q_EMIT viewChanged();
        Q_EMIT orientationChanged();
    }
}

double NavCubeItem::yaw() const noexcept {
    return view_ ? view_->yaw() : localYaw_;
}

void NavCubeItem::setYaw(double yawDeg) {
    if (view_) {
        // driven by view
    } else if (std::abs(localYaw_ - yawDeg) > 1e-4) {
        localYaw_ = yawDeg;
        update();
        Q_EMIT orientationChanged();
    }
}

double NavCubeItem::pitch() const noexcept {
    return view_ ? view_->pitch() : localPitch_;
}

void NavCubeItem::setPitch(double pitchDeg) {
    if (view_) {
        // driven by view
    } else if (std::abs(localPitch_ - pitchDeg) > 1e-4) {
        localPitch_ = pitchDeg;
        update();
        Q_EMIT orientationChanged();
    }
}

QString NavCubeItem::activeFace() const {
    const auto faces = projectFaces();
    for (const auto& f : faces) {
        if (f.depth > 0.96 && f.def) {
            return QString::fromLatin1(f.def->viewName);
        }
    }
    return view_ ? view_->view() : QString();
}

std::vector<NavCubeItem::ProjectedFace> NavCubeItem::projectFaces() const {
    // gviewer's cube: 90 px edges (0.6 of the item, which leaves its corners
    // room), drawn about the item's middle.
    const double s = std::min(width(), height()) * 0.3;
    if (s <= 1.0) {
        return {};
    }

    const Point3D kVertices[8] = {
        {-s, -s, -s},  // 0
        {s, -s, -s},   // 1
        {s, s, -s},    // 2
        {-s, s, -s},   // 3
        {-s, -s, s},   // 4
        {s, -s, s},    // 5
        {s, s, s},     // 6
        {-s, s, s},    // 7
    };

    const double yRad = yaw() * kDegree;
    const double pRad = pitch() * kDegree;
    const double cy = std::cos(yRad), sy = std::sin(yRad);
    const double cp = std::cos(pRad), sp = std::sin(pRad);

    // Matches ToolpathCamera::updateRotation:
    // Screen X: cy * x - sy * y
    // Screen Y (up): cp * sy * x + cp * cy * y + sp * z
    // Screen Z (viewer): -sp * sy * x - sp * cy * y + cp * z
    const double R[9] = {
        cy, -sy, 0,
        cp * sy, cp * cy, sp,
        -sp * sy, -sp * cy, cp
    };

    const QPointF center(width() / 2.0, height() / 2.0);
    QPointF proj[8];
    for (int i = 0; i < 8; ++i) {
        const double x = kVertices[i].x;
        const double y = kVertices[i].y;
        const double z = kVertices[i].z;
        const double rx = R[0] * x + R[1] * y + R[2] * z;
        const double ry = R[3] * x + R[4] * y + R[5] * z;
        proj[i] = QPointF(center.x() + rx, center.y() - ry);
    }

    std::vector<ProjectedFace> faces;
    faces.reserve(6);

    for (int i = 0; i < 6; ++i) {
        const FaceDef& def = kFaces[i];
        const double n_screen_z = R[6] * def.nx + R[7] * def.ny + R[8] * def.nz;
        if (n_screen_z > 0.01) {
            ProjectedFace pf;
            pf.index = i;
            pf.def = &def;
            pf.depth = n_screen_z;
            pf.visible = true;
            pf.poly << proj[def.vertices[0]]
                    << proj[def.vertices[1]]
                    << proj[def.vertices[2]]
                    << proj[def.vertices[3]];
            faces.push_back(std::move(pf));
        }
    }

    std::sort(faces.begin(), faces.end(), [](const auto& a, const auto& b) {
        return a.depth < b.depth;
    });

    return faces;
}

int NavCubeItem::hitTestFace(const QPointF& pos) const {
    const auto faces = projectFaces();
    // Test in front-to-back order
    for (auto it = faces.rbegin(); it != faces.rend(); ++it) {
        if (it->poly.containsPoint(pos, Qt::OddEvenFill)) {
            return it->index;
        }
    }
    return -1;
}

void NavCubeItem::paint(QPainter* painter) {
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const auto faces = projectFaces();
    const QString curView = view_ ? view_->view() : QString();

    for (const auto& pf : faces) {
        const bool hovered = (hoveredFace_ == pf.index);
        const bool active = (curView == QLatin1String(pf.def->viewName)) || (pf.depth > 0.97);

        QColor fillColor;
        QColor borderColor;
        QColor textColor;

        if (active) {
            fillColor = QColor(22, 42, 68, 235);
            borderColor = QColor(96, 165, 250, 245);
            textColor = QColor(96, 165, 250, 255);
        } else if (hovered) {
            fillColor = QColor(32, 46, 64, 245);
            borderColor = QColor(160, 195, 230, 220);
            textColor = QColor(255, 255, 255, 255);
        } else {
            fillColor = QColor(14, 20, 28, 225);
            borderColor = QColor(120, 142, 165, 90);
            textColor = QColor(225, 235, 245, 215);
        }

        // Draw quad face with smoothly rounded corners
        QPainterPath roundPath;
        const int n = 4;
        const double cornerR = 8.0;
        for (int vi = 0; vi < n; ++vi) {
            const QPointF prev = pf.poly[(vi + n - 1) % n];
            const QPointF curr = pf.poly[vi];
            const QPointF next = pf.poly[(vi + 1) % n];

            const QPointF d1 = prev - curr;
            const QPointF d2 = next - curr;
            const double l1 = std::hypot(d1.x(), d1.y());
            const double l2 = std::hypot(d2.x(), d2.y());
            const double r = std::min({cornerR, l1 * 0.35, l2 * 0.35});

            const QPointF p1 = curr + (d1 / l1) * r;
            const QPointF p2 = curr + (d2 / l2) * r;

            if (vi == 0) {
                roundPath.moveTo(p1);
            } else {
                roundPath.lineTo(p1);
            }
            roundPath.quadTo(curr, p2);
        }
        roundPath.closeSubpath();

        painter->setPen(QPen(borderColor, active ? 1.6 : 1.0));
        painter->setBrush(fillColor);
        painter->drawPath(roundPath);

        // Center of polygon
        QPointF c(0, 0);
        for (int vi = 0; vi < 4; ++vi) {
            c += pf.poly[vi];
        }
        c /= 4.0;

        // The label lies in its face (CSS 3D in gviewer): drawn in the face's
        // own plane, one local unit a pixel of the cube's edge.
        const double edge = std::min(width(), height()) * 0.6;
        const QPointF u = pf.poly[1] - pf.poly[0];
        QPointF v = pf.poly[3] - pf.poly[0];
        if (u.x() * v.y() - u.y() * v.x() < 0) {
            v = -v;  // keep the text upright, not mirrored
        }
        painter->save();
        painter->setTransform(QTransform(u.x() / edge, u.y() / edge, v.x() / edge, v.y() / edge, c.x(), c.y()),
                              true);

        QFont font = painter->font();
        font.setBold(true);
        font.setPixelSize(11);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        painter->setFont(font);

        painter->setPen(textColor);
        painter->drawText(QRectF(-28, -10, 56, 20), Qt::AlignCenter, QLatin1String(pf.def->label));
        painter->restore();
    }
}

void NavCubeItem::hoverMoveEvent(QHoverEvent* event) {
    const int hit = hitTestFace(event->position());
    if (hit != hoveredFace_) {
        hoveredFace_ = hit;
        update();
    }
}

void NavCubeItem::hoverLeaveEvent(QHoverEvent*) {
    if (hoveredFace_ != -1) {
        hoveredFace_ = -1;
        update();
    }
}

void NavCubeItem::mousePressEvent(QMouseEvent* event) {
    pressPos_ = event->position();
    lastPos_ = event->position();
    isDragging_ = false;
    event->accept();
}

void NavCubeItem::mouseMoveEvent(QMouseEvent* event) {
    if ((event->position() - pressPos_).manhattanLength() > 3.0) {
        isDragging_ = true;
    }
    if (isDragging_ && view_) {
        const QPointF delta = event->position() - lastPos_;
        view_->orbit(delta.x() * 0.5, -delta.y() * 0.5);
    }
    lastPos_ = event->position();
    event->accept();
}

void NavCubeItem::mouseReleaseEvent(QMouseEvent* event) {
    if (!isDragging_) {
        const int hit = hitTestFace(event->position());
        if (hit >= 0) {
            const QString viewName = QString::fromLatin1(kFaces[hit].viewName);
            if (view_) {
                view_->setView(viewName);
            }
            Q_EMIT faceClicked(viewName);
        }
    }
    isDragging_ = false;
    event->accept();
}

}  // namespace gs::ui
