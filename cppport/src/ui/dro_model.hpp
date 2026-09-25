#pragma once

// The DRO (features/DRO) for QML: the axis rows as the workspace shows them,
// what may be clicked, and the DRO's actions - the rules of the widget DRO
// (app/dro_panel), which the QML one replaces.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class DroModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    // canClick: connected, no job running, idle or jogging.
    Q_PROPERTY(bool canClick READ canClick NOTIFY changed)
    Q_PROPERTY(bool metric READ metric NOTIFY changed)
    Q_PROPERTY(QString units READ units NOTIFY changed)  // "mm" / "in"
    Q_PROPERTY(bool rotaryMode READ rotaryMode NOTIFY changed)
    // Homing ($22 > 0) and whether the machine has homed: the corners and
    // Park show with homing and work once homed.
    Q_PROPERTY(bool homingEnabled READ homingEnabled NOTIFY changed)
    Q_PROPERTY(bool homed READ homed NOTIFY changed)
    // $22 bit 1: the homing switch, which turns the axis buttons into
    // single-axis homing.
    Q_PROPERTY(bool singleAxisHoming READ singleAxisHoming NOTIFY changed)
    Q_PROPERTY(bool homingMode READ homingMode WRITE setHomingMode NOTIFY changed)
    // "Warn when setting zero": the zero buttons ask first.
    Q_PROPERTY(bool warnZero READ warnZero NOTIFY changed)
    // The work coordinate system ("G54"...) and whether it may change.
    Q_PROPERTY(QString workspace READ workspace NOTIFY changed)
    Q_PROPERTY(bool workspaceEnabled READ workspaceEnabled NOTIFY changed)
    // The rows: {label, axis, work, machine, enabled, gotoEnabled}. A shows
    // with rotary mode (driving Y) or a board with an A axis.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    // Machine coordinates for Go To: homing enabled and homed.
    Q_PROPERTY(bool machineCoordinatesAvailable READ homed NOTIFY changed)
    // Go To's A: in rotary mode (the rotary), or a grblHAL board with A.
    Q_PROPERTY(bool goToAEnabled READ goToAEnabled NOTIFY changed)

public:
    explicit DroModel(QObject* parent = nullptr);

    bool connected() const;
    bool canClick() const;
    bool metric() const;
    QString units() const;
    bool rotaryMode() const;
    bool homingEnabled() const;
    bool homed() const;
    bool singleAxisHoming() const;
    bool homingMode() const noexcept { return homingMode_; }
    void setHomingMode(bool on);
    bool warnZero() const;
    QString workspace() const;
    bool workspaceEnabled() const;
    QVariantList rows() const;
    bool goToAEnabled() const;

    Q_INVOKABLE void toggleUnits();
    // A row's axis button: zero it - or home it in homing mode.
    Q_INVOKABLE void axisButton(const QString& axis);
    Q_INVOKABLE void zeroAll();
    Q_INVOKABLE void goToZero(const QString& axes);  // "X", "XY"...
    // A typed work position (workspace units): false - nothing sent - when
    // it is empty, not a number, or the machine cannot move.
    Q_INVOKABLE bool setWorkPosition(const QString& axis, const QString& text);
    Q_INVOKABLE void selectWorkspace(const QString& wcs);
    Q_INVOKABLE void goToCorner(const QString& corner);  // "BackLeft"...
    Q_INVOKABLE void park();
    Q_INVOKABLE void home();
    Q_INVOKABLE void unlock();
    Q_INVOKABLE void reset();
    // Go To Location: mode "ABS", "INC" or "MCS"; the values its fields
    // start with (X, Y, Z, A), and the move.
    Q_INVOKABLE QVariantList goToPrefill(const QString& mode) const;
    Q_INVOKABLE void goTo(const QString& mode, double x, double y, double z, double a);

Q_SIGNALS:
    void changed();

private:
    app::Machine& machine_;
    bool homingMode_ = false;
};

}  // namespace gs::ui
