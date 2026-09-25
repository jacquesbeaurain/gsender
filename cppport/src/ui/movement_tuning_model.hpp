#pragma once

// Movement Tuning (features/MovementTuning) for QML: mark where the axis is,
// move it a set distance, measure how far it went, and rescale its steps/mm
// ($100-$102), as the widget dialog does.

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class MovementTuningModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // "intro", "mark", "move", "measure", "result".
    Q_PROPERTY(QString step READ step NOTIFY changed)
    Q_PROPERTY(QString axis READ axis WRITE setAxis NOTIFY changed)
    Q_PROPERTY(double moveDistance READ moveDistance WRITE setMoveDistance NOTIFY changed)
    Q_PROPERTY(double travelled READ travelled WRITE setTravelled NOTIFY changed)
    Q_PROPERTY(QString units READ units NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool canMove READ canMove NOTIFY changed)
    Q_PROPERTY(QString instruction READ instruction NOTIFY changed)
    Q_PROPERTY(bool accurate READ accurate NOTIFY changed)
    Q_PROPERTY(QString resultText READ resultText NOTIFY changed)  // styled text
    Q_PROPERTY(QString updateText READ updateText NOTIFY changed)  // what Update asks

public:
    explicit MovementTuningModel(QObject* parent = nullptr);

    QString step() const;
    QString axis() const { return QString(QChar(axis_)); }
    void setAxis(const QString& axis);  // the distances back to the axis' defaults
    double moveDistance() const { return moveDistance_; }
    void setMoveDistance(double distance);
    double travelled() const { return travelled_; }
    void setTravelled(double distance);
    QString units() const;
    bool connected() const;
    bool canMove() const;
    QString instruction() const;
    bool accurate() const { return moveDistance_ == travelled_; }
    QString resultText() const;
    QString updateText() const;

    Q_INVOKABLE bool start();          // "Start Movement Tuning"
    Q_INVOKABLE void markLocation();   // "Mark First Location"
    Q_INVOKABLE bool moveAxis();       // "Move X-axis"
    Q_INVOKABLE void confirmTravelled();  // "Set Distance Travelled"
    Q_INVOKABLE double recommendedStepsPerMm() const;
    Q_INVOKABLE void updateFirmware();   // once asked
    Q_INVOKABLE void restart();

Q_SIGNALS:
    void changed();

private:
    enum Step { Intro, Mark, Move, Measure, Result };

    app::Machine& machine_;
    Step step_ = Intro;
    char axis_ = 'X';
    double moveDistance_ = 0;
    double travelled_ = 0;
};

}  // namespace gs::ui
