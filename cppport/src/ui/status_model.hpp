#pragma once

// The machine status's controls (MachineStatus, UnlockButton, MachineInfo)
// for QML: the alarm's button - unlock or run homing - and its "?", the lock
// beside the status, the homing-failure question, and Machine Information
// (firmware, CNC modals, pins, tool, the stepper lock). The rules are the
// core's (controller/actions).

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class StatusModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool alarm READ alarm NOTIFY changed)
    // The button under the status in an alarm: "Click to Unlock Machine" or
    // "Click to Run Homing".
    Q_PROPERTY(bool alarmButtonHomes READ alarmButtonHomes NOTIFY changed)
    // The lock beside the status: lit (yellow) in a hold or an alarm.
    Q_PROPERTY(bool lockActive READ lockActive NOTIFY changed)
    // Machine Information.
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY changed)
    Q_PROPERTY(QVariantList modals READ modals NOTIFY changed)  // {label, value}
    Q_PROPERTY(QVariantList pins READ pins NOTIFY changed)      // {label, on}
    Q_PROPERTY(int tool READ tool NOTIFY changed)               // -1 unknown
    Q_PROPERTY(bool stepperLocked READ stepperLocked NOTIFY changed)

public:
    explicit StatusModel(QObject* parent = nullptr);

    bool connected() const;
    bool alarm() const;
    bool alarmButtonHomes() const;
    bool lockActive() const;
    QString firmwareVersion() const;
    QVariantList modals() const;
    QVariantList pins() const;
    int tool() const;
    bool stepperLocked() const;

    // The alarm button and the lock: false when done; true when the homing
    // failed and the operator must choose (resolveHomingFailure).
    Q_INVOKABLE bool clickAlarmButton();
    Q_INVOKABLE bool clickLock();
    // "rehome", "unlock" or "cancel".
    Q_INVOKABLE void resolveHomingFailure(const QString& choice);
    // The homing-failure question's text (confirmUnlockAfterHomingFailure).
    Q_INVOKABLE QString homingFailureText() const;
    // The alarm's "?": the Helper panel with its explanation.
    Q_INVOKABLE void showAlarmHelp();
    Q_INVOKABLE void setStepperLock(bool lock);

Q_SIGNALS:
    void changed();

private:
    bool act(int action, bool repopulate);

    app::Machine& machine_;
    bool pendingRepopulate_ = false;
};

}  // namespace gs::ui
