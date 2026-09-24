#include "pie_chart.hpp"

#include "gs/util/jsnumber.hpp"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <cmath>
#include <numbers>

namespace gs::app {
namespace {

constexpr double kLegendBoxWidth = 40;  // Chart.js legend.labels.boxWidth
constexpr double kLegendPadding = 10;   // legend.labels.padding
constexpr double kBorderWidth = 2;      // ArcElement's borderWidth

// Chart.js runs clockwise from the top; Qt counterclockwise from 3 o'clock.
double qtAngle(double clockwiseFromTop) {
    return 90 - clockwiseFromTop;
}

}  // namespace

PieChart::PieChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PieChart::setDoughnut(bool doughnut) {
    doughnut_ = doughnut;
    update();
}

void PieChart::setSlices(const std::vector<QString>& labels, const std::vector<double>& values,
                         const std::vector<QColor>& colors) {
    slices_.clear();
    for (std::size_t i = 0; i < labels.size() && i < values.size(); ++i) {
        slices_.push_back({labels[i], values[i], colors.empty() ? QColor(Qt::gray) : colors[i % colors.size()]});
    }
    hidden_.assign(slices_.size(), false);
    update();
}

void PieChart::toggle(int index) {
    if (index >= 0 && index < static_cast<int>(hidden_.size())) {
        hidden_[index] = !hidden_[index];
        update();
    }
}

bool PieChart::isShown(int index) const {
    return index >= 0 && index < static_cast<int>(hidden_.size()) && !hidden_[index];
}

QSize PieChart::sizeHint() const {
    return {220, 240};
}

QSize PieChart::minimumSizeHint() const {
    return {120, 140};
}

PieChart::Geometry PieChart::geometry() const {
    Geometry g;
    const QFontMetricsF metrics(font());
    const double boxHeight = metrics.height() * 0.8;
    const double lineHeight = std::max(boxHeight, metrics.height()) + kLegendPadding / 2;
    // Items flow in centred lines.
    std::vector<std::vector<int>> lines(1);
    std::vector<double> widths(1, 0);
    const auto itemWidth = [&](int i) {
        return kLegendBoxWidth + kLegendPadding / 2 + metrics.horizontalAdvance(slices_[i].label);
    };
    for (int i = 0; i < static_cast<int>(slices_.size()); ++i) {
        const double w = itemWidth(i);
        if (!lines.back().empty() && widths.back() + kLegendPadding + w > width()) {
            lines.emplace_back();
            widths.push_back(0);
        }
        widths.back() += (lines.back().empty() ? 0 : kLegendPadding) + w;
        lines.back().push_back(i);
    }
    g.legend.resize(slices_.size());
    double y = kLegendPadding / 2;
    for (std::size_t line = 0; line < lines.size(); ++line) {
        double x = (width() - widths[line]) / 2;
        for (const int i : lines[line]) {
            const double w = itemWidth(i);
            g.legend[i] = QRectF(x, y, w, lineHeight);
            x += w + kLegendPadding;
        }
        if (!lines[line].empty()) {
            y += lineHeight;
        }
    }
    const double top = slices_.empty() ? 0 : y + kLegendPadding / 2;
    const double side = std::max(0.0, std::min<double>(width(), height() - top) - kBorderWidth);
    g.pie = QRectF((width() - side) / 2, top + (height() - top - side) / 2, side, side);
    return g;
}

double PieChart::shownTotal() const {
    double total = 0;
    for (std::size_t i = 0; i < slices_.size(); ++i) {
        if (!hidden_[i] && slices_[i].value > 0) {
            total += slices_[i].value;
        }
    }
    return total;
}

bool PieChart::sliceAngles(int index, double& start, double& span) const {
    const double total = shownTotal();
    if (!isShown(index) || total <= 0 || slices_[index].value <= 0) {
        return false;
    }
    start = 0;
    for (int i = 0; i < index; ++i) {
        if (isShown(i) && slices_[i].value > 0) {
            start += 360 * slices_[i].value / total;
        }
    }
    span = 360 * slices_[index].value / total;
    return true;
}

int PieChart::sliceAt(const QPointF& point) const {
    const QRectF pie = geometry().pie;
    const QPointF center = pie.center();
    const double dx = point.x() - center.x();
    const double dy = point.y() - center.y();
    const double distance = std::hypot(dx, dy);
    const double radius = pie.width() / 2;
    if (distance > radius || (doughnut_ && distance < radius / 2)) {
        return -1;
    }
    double angle = std::atan2(dx, -dy) * 180 / std::numbers::pi;  // clockwise from the top
    if (angle < 0) {
        angle += 360;
    }
    for (int i = 0; i < static_cast<int>(slices_.size()); ++i) {
        double start = 0;
        double span = 0;
        if (sliceAngles(i, start, span) && angle >= start && angle < start + span) {
            return i;
        }
    }
    return -1;
}

QPointF PieChart::sliceCenter(int index) const {
    double start = 0;
    double span = 0;
    if (!sliceAngles(index, start, span)) {
        return {-1, -1};
    }
    const QRectF pie = geometry().pie;
    const double radius = pie.width() / 2 * (doughnut_ ? 0.75 : 0.5);
    const double angle = (start + span / 2) * std::numbers::pi / 180;
    return pie.center() + QPointF(radius * std::sin(angle), -radius * std::cos(angle));
}

QString PieChart::toolTipText(int index) const {
    if (index < 0 || index >= static_cast<int>(slices_.size())) {
        return {};
    }
    const Slice& slice = slices_[index];
    const QString value = valueText_ ? valueText_(slice.value)
                                     : series_ + ": " + QString::fromStdString(js::numberToString(slice.value));
    return "<b>" + slice.label.toHtmlEscaped() + "</b><br>" + value.toHtmlEscaped();
}

void PieChart::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const Geometry g = geometry();
    const QFontMetricsF metrics(font());
    const QColor text = palette().color(QPalette::WindowText);

    for (int i = 0; i < static_cast<int>(slices_.size()); ++i) {
        const QRectF item = g.legend[i];
        const double boxHeight = metrics.height() * 0.8;
        const QRectF box(item.left(), item.center().y() - boxHeight / 2, kLegendBoxWidth, boxHeight);
        painter.setPen(QPen(slices_[i].color.darker(110), 1));
        painter.setBrush(slices_[i].color);
        painter.drawRect(box);
        const QRectF label(box.right() + kLegendPadding / 2, item.top(), item.right() - box.right(), item.height());
        painter.setPen(text);
        painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, slices_[i].label);
        if (hidden_[i]) {  // struck through
            const double y = item.center().y();
            painter.drawLine(QPointF(label.left(), y),
                             QPointF(label.left() + metrics.horizontalAdvance(slices_[i].label), y));
        }
    }

    const QRectF outer = g.pie;
    const QRectF inner = outer.adjusted(outer.width() / 4, outer.height() / 4, -outer.width() / 4, -outer.height() / 4);
    painter.setPen(QPen(palette().color(QPalette::Base), kBorderWidth));
    for (int i = 0; i < static_cast<int>(slices_.size()); ++i) {
        double start = 0;
        double span = 0;
        if (!sliceAngles(i, start, span)) {
            continue;
        }
        QPainterPath path;
        if (doughnut_) {
            path.arcMoveTo(outer, qtAngle(start));
            path.arcTo(outer, qtAngle(start), -span);
            path.arcTo(inner, qtAngle(start + span), span);
        } else {
            path.moveTo(outer.center());
            path.arcTo(outer, qtAngle(start), -span);
        }
        path.closeSubpath();
        painter.setBrush(slices_[i].color);
        painter.drawPath(path);
    }
}

void PieChart::mousePressEvent(QMouseEvent* event) {
    const Geometry g = geometry();
    for (int i = 0; i < static_cast<int>(g.legend.size()); ++i) {
        if (g.legend[i].contains(event->position())) {
            toggle(i);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

bool PieChart::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int index = sliceAt(help->pos());
        if (index >= 0) {
            QToolTip::showText(help->globalPos(), toolTipText(index), this);
        } else {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QWidget::event(event);
}

}  // namespace gs::app
