#pragma once

// The Rotary tab (features/Rotary) for QML: the rotary mode switch, the
// rotary probing routines and the Mounting Setup, as the widget panel works
// (app/rotary_actions has the rules). Rotary Surfacing is a tool of the
// Tools page.

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class RotaryModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool rotaryMode READ rotaryMode NOTIFY changed)
    Q_PROPERTY(bool grblHal READ grblHal NOTIFY changed)
    Q_PROPERTY(bool canSwitch READ canSwitch NOTIFY changed)
    Q_PROPERTY(bool surfacingAvailable READ surfacingAvailable NOTIFY changed)
    Q_PROPERTY(bool probeZAvailable READ probeZAvailable NOTIFY changed)
    Q_PROPERTY(bool alignYAvailable READ alignYAvailable NOTIFY changed)
    Q_PROPERTY(bool mountingAvailable READ mountingAvailable NOTIFY changed)

public:
    explicit RotaryModel(QObject* parent = nullptr);

    bool rotaryMode() const;
    bool grblHal() const;
    bool canSwitch() const;
    bool surfacingAvailable() const;
    bool probeZAvailable() const;
    bool alignYAvailable() const;
    bool mountingAvailable() const;

    // What entering rotary mode will do (rich text), asked first.
    Q_INVOKABLE QString enableConfirmation() const;
    // The switch, once asked. False when nothing changed.
    Q_INVOKABLE bool setRotaryMode(bool rotary);
    // "Probe Rotary Z-Axis" / "Y-Axis Alignment", once asked.
    Q_INVOKABLE bool runProbe(bool yAlignment);
    // Mounting Setup: the illustration for the choices, and loading the
    // program that bores the track's holes.
    Q_INVOKABLE QString mountingImage(bool linesUp, int holes) const;
    Q_INVOKABLE bool loadMounting(bool linesUp, bool quarterInchBit, int holes, bool longExtension);

Q_SIGNALS:
    void changed();

private:
    app::Machine& machine_;
};

}  // namespace gs::ui
