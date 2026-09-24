#pragma once

// A pie or doughnut chart with its legend - what the Stats page draws with
// Chart.js (react-chartjs-2's Pie and Doughnut): slices clockwise from the
// top with white borders, the legend above (a click hides or shows a
// slice), the slice's label and value as the tooltip.

#include <QColor>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

namespace gs::app {

class PieChart final : public QWidget {
    Q_OBJECT
public:
    struct Slice {
        QString label;
        double value = 0;
        QColor color;
    };

    explicit PieChart(QWidget* parent = nullptr);

    void setDoughnut(bool doughnut);  // cut out the middle half
    // The tooltip's value: "<series>: <value>" by default (the dataset's
    // label), or what `text` makes of the value.
    void setSeriesLabel(const QString& label) { series_ = label; }
    void setValueText(std::function<QString(double)> text) { valueText_ = std::move(text); }
    // Colours repeat when there are more slices than colours.
    void setSlices(const std::vector<QString>& labels, const std::vector<double>& values,
                   const std::vector<QColor>& colors);
    const std::vector<Slice>& slices() const noexcept { return slices_; }

    // The legend's click.
    void toggle(int index);
    bool isShown(int index) const;
    // The slice under a point (-1 for none) and what its tooltip says.
    int sliceAt(const QPointF& point) const;
    QString toolTipText(int index) const;
    // The middle of a shown slice's arc (tests).
    QPointF sliceCenter(int index) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct Geometry {
        std::vector<QRectF> legend;  // per slice
        QRectF pie;
    };
    Geometry geometry() const;
    double shownTotal() const;
    // A shown slice's start and span, degrees clockwise from the top.
    bool sliceAngles(int index, double& start, double& span) const;

    std::vector<Slice> slices_;
    std::vector<bool> hidden_;
    bool doughnut_ = false;
    QString series_;
    std::function<QString(double)> valueText_;
};

}  // namespace gs::app
