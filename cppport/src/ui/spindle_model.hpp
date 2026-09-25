#pragma once

// The Spindle/Laser and Coolant tabs (features/Spindle, features/Coolant)
// for QML. Their buttons work on an idle machine with no job running
// (canClick); what is on follows the board's modal state (M3/M4/M5,
// M7/M8/M9).

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QTimer;

namespace gs::app {
class Machine;
}

namespace gs::ui {

class CoolantModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool canClick READ canClick NOTIFY changed)
    Q_PROPERTY(bool mistActive READ mistActive NOTIFY changed)
    Q_PROPERTY(bool floodActive READ floodActive NOTIFY changed)

public:
    explicit CoolantModel(QObject* parent = nullptr);

    bool canClick() const;
    bool mistActive() const;
    bool floodActive() const;

    Q_INVOKABLE void mist();   // M7
    Q_INVOKABLE void flood();  // M8
    Q_INVOKABLE void off();    // M9

Q_SIGNALS:
    void changed();

private:
    bool hasCoolant(const char* code) const;
    void command(const char* code);

    app::Machine& machine_;
};

class SpindleModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool canClick READ canClick NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    // Laser mode ($32): the Laser controls instead of the Spindle's.
    Q_PROPERTY(bool laserMode READ laserMode NOTIFY changed)
    // The modal spindle state: M3 forward, M4 reverse (the laser lit on either).
    Q_PROPERTY(bool forward READ forward NOTIFY changed)
    Q_PROPERTY(bool reverse READ reverse NOTIFY changed)
    Q_PROPERTY(bool laserOn READ laserOn NOTIFY changed)
    // The speed within $31..$30, and how it is set ("Slider" or "Number").
    Q_PROPERTY(double speed READ speed NOTIFY changed)
    Q_PROPERTY(double speedMin READ speedMin NOTIFY changed)
    Q_PROPERTY(double speedMax READ speedMax NOTIFY changed)
    Q_PROPERTY(bool speedSlider READ speedSlider NOTIFY changed)
    // The laser: power (%) and the test's duration (s).
    Q_PROPERTY(double power READ power NOTIFY changed)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
    // grblHAL's spindles {id, label}, the enabled one's id; shown on grblHAL.
    Q_PROPERTY(bool grblHal READ grblHal NOTIFY changed)
    Q_PROPERTY(QVariantList spindles READ spindles NOTIFY changed)
    Q_PROPERTY(int spindleId READ spindleId NOTIFY changed)

public:
    explicit SpindleModel(QObject* parent = nullptr);

    bool canClick() const;
    bool connected() const;
    bool laserMode() const;
    bool forward() const;
    bool reverse() const;
    bool laserOn() const;
    double speed() const { return speed_; }
    double speedMin() const;
    double speedMax() const;
    bool speedSlider() const;
    double power() const { return power_; }
    double duration() const;
    bool grblHal() const;
    QVariantList spindles() const;
    int spindleId() const;

    // The buttons: M3 / laser on (focus), M4 / laser test, M5; the mode.
    Q_INVOKABLE void startClockwise();
    Q_INVOKABLE void startCounterClockwise();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggleMode();
    // A change reaches a running spindle or lit laser - and the settings -
    // 300 ms after the last one (upstream's debounce).
    Q_INVOKABLE void setSpeed(double rpm);
    Q_INVOKABLE void setPower(double percent);
    Q_INVOKABLE void setDuration(double seconds);
    Q_INVOKABLE void selectSpindle(int id);
    // The flush of both debounces now (tests).
    Q_INVOKABLE void applyNow();

Q_SIGNALS:
    void changed();

private:
    void command(const std::string& gcode);
    void applySpeed();
    void applyPower();
    void sync();  // the settings' values, unless a change is waiting

    app::Machine& machine_;
    double speed_ = 0;
    double power_ = 0;
    bool spindleOn_ = false;  // started here: speed changes follow
    bool laserLit_ = false;
    QTimer* speedTimer_;
    QTimer* powerTimer_;
};

}  // namespace gs::ui
