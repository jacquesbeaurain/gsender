#pragma once

// XY Squaring (features/Squaring) for QML: mark three points in a triangle,
// measure its sides, and see how far X is from square to Y (and the steps/mm
// the measurements suggest), as the widget dialog does.

#include "gs/calibration/calibration.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class SquaringModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int mainStep READ mainStep NOTIFY changed)  // 0 setup, 1 mark, 2 measure, 3 results
    Q_PROPERTY(int subStep READ subStep NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    Q_PROPERTY(QString instruction READ instruction NOTIFY changed)
    // Each {button, hasValue, value, completed, current, enabled}.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY changed)
    Q_PROPERTY(QString units READ units NOTIFY changed)
    Q_PROPERTY(QString resultText READ resultText NOTIFY changed)  // styled text
    Q_PROPERTY(bool updateNeeded READ updateNeeded NOTIFY changed)
    Q_PROPERTY(QString updateText READ updateText NOTIFY changed)
    // The diagram: points marked, the point being marked, the side being
    // measured, the axis being moved ("X", "Y" or ""), the sides measured.
    Q_PROPERTY(int markedPoints READ markedPoints NOTIFY changed)
    Q_PROPERTY(int activePoint READ activePoint NOTIFY changed)
    Q_PROPERTY(int activeSide READ activeSide NOTIFY changed)
    Q_PROPERTY(QString moving READ moving NOTIFY changed)
    Q_PROPERTY(QVariantList sides READ sides NOTIFY changed)

public:
    explicit SquaringModel(QObject* parent = nullptr);

    int mainStep() const { return mainStep_; }
    int subStep() const { return subStep_; }
    QString title() const;
    QString description() const;
    QString instruction() const;
    QVariantList rows() const;
    bool canGoNext() const;
    QString units() const;
    QString resultText() const;
    bool updateNeeded() const;
    QString updateText() const;
    int markedPoints() const;
    int activePoint() const;
    int activeSide() const;
    QString moving() const;
    QVariantList sides() const;

    Q_INVOKABLE void next();
    Q_INVOKABLE void back();  // upstream resets this step and the one before
    Q_INVOKABLE void restart();
    // The current step's row `index`, as its button: marks, moves (the X
    // and Y rows) and measurements (their values must be positive).
    Q_INVOKABLE bool completeRow(int index);
    Q_INVOKABLE void setRowValue(int index, double value);
    Q_INVOKABLE void updateFirmware();  // once asked

Q_SIGNALS:
    void changed();

private:
    struct Row {
        QString button;
        QString description;
        bool hasValue = false;
        double value = 0;
        bool completed = false;
    };
    void buildSteps();
    const std::vector<Row>& currentRows() const { return rows_[static_cast<std::size_t>(mainStep_)]; }
    calibration::StepsAdjustment adjustment() const;

    app::Machine& machine_;
    int mainStep_ = 0;
    int subStep_ = 0;
    std::vector<std::vector<Row>> rows_;
    calibration::Triangle triangle_;
    calibration::SquaringMoves moves_;
};

}  // namespace gs::ui
