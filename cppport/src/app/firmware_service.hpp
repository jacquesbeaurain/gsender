#pragma once

#include "gs/protocol/runner.hpp"

#include <QObject>
#include <QString>
#include <string>
#include <vector>

namespace gs::app {

class Machine;

class FirmwareService : public QObject {
    Q_OBJECT

public:
    explicit FirmwareService(Machine& machine, QObject* parent = nullptr);

    double settingNumber(const std::string& key) const;
    void writeFirmwareSettings(const std::vector<std::string>& lines);
    bool stepperLocked() const;
    void setStepperLock(bool lock);
    bool laserMode() const;
    void setLaserMode(bool laser);
    double laserMaxPower() const;
    void selectSpindle(int id);
    bool setRotaryMode(bool rotaryMode);
    bool runTuningMove(char axis, double distance);
    void runSquaringMove(char axis, double distance);

private:
    Machine& machine_;
};

}  // namespace gs::app
