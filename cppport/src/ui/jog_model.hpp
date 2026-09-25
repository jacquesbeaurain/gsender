#pragma once

// Jogging (features/Jogging) for QML: the presets and step/speed fields
// (the backend's Jogger, shared with the shortcuts), tap-to-step and
// hold-to-jog, the stop button, and when A is offered.

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Jogger;
class Machine;
}

namespace gs::ui {

class JogModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString preset READ preset NOTIFY changed)  // "Rapid", "Normal", "Precise"
    // The fields, in the workspace units (A in degrees).
    Q_PROPERTY(double xyStep READ xyStep NOTIFY changed)
    Q_PROPERTY(double zStep READ zStep NOTIFY changed)
    Q_PROPERTY(double aStep READ aStep NOTIFY changed)
    Q_PROPERTY(double feedrate READ feedrate NOTIFY changed)
    Q_PROPERTY(QString units READ units NOTIFY changed)
    // Jogging allowed: connected, no job running, not in an alarm.
    Q_PROPERTY(bool canJog READ canJog NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    // Y is the rotary in rotary mode: the XY wheel's Y sectors are off.
    Q_PROPERTY(bool rotaryMode READ rotaryMode NOTIFY changed)
    // AJog: the rotary controls on a grblHAL board or in rotary mode, or
    // Grbl's A passthrough.
    Q_PROPERTY(bool showA READ showA NOTIFY changed)

public:
    explicit JogModel(QObject* parent = nullptr);

    QString preset() const;
    double xyStep() const;
    double zStep() const;
    double aStep() const;
    double feedrate() const;
    QString units() const;
    bool canJog() const;
    bool connected() const;
    bool rotaryMode() const;
    bool showA() const;

    Q_INVOKABLE void selectPreset(const QString& preset);
    // A field typed: "xy", "z", "a" or "feedrate"; in use until a preset is
    // chosen.
    Q_INVOKABLE void setField(const QString& field, double value);
    // A field's - or + button (JogInput): one step down or up.
    Q_INVOKABLE void nudge(const QString& field, bool increment);
    // Press: X/Y/Z directions (-1, 0, 1); released within the threshold it
    // steps, held it jogs until release().
    Q_INVOKABLE void press(int x, int y, int z);
    Q_INVOKABLE void pressA(int direction);
    Q_INVOKABLE void release();
    // StopButton (cancelJog): cancels a jog; outside Idle and Jog it resets
    // (grblHAL's soft reset).
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void changed();

private:
    app::Machine& machine_;
    app::Jogger& jogger_;
};

}  // namespace gs::ui
