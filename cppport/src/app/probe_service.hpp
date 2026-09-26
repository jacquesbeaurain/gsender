#pragma once

#include "gs/probe/probing.hpp"
#include "rotary_actions.hpp"

#include <QObject>
#include <string>
#include <vector>

namespace gs::app {

class Machine;

class ProbeService : public QObject {
    Q_OBJECT

public:
    explicit ProbeService(Machine& machine, QObject* parent = nullptr);

    std::vector<std::string> probeRoutine(probe::Axes axes, probe::ProbeType type, double toolDiameter,
                                          int corner) const;
    bool runProbe(std::vector<std::string> code);
    bool probeTriggered() const;
    void placeSimulatedPlate(probe::ProbeType type, double toolDiameter, int corner,
                             probe::Axes axes = {true, true, true});
    bool runRotaryProbe(bool yAlignment);

private:
    Machine& machine_;
};

}  // namespace gs::app
