#pragma once

#include "gs/controller/locations.hpp"
#include "gs/toolchange/wizards.hpp"

#include <QObject>
#include <QString>
#include <array>
#include <string_view>

namespace gs::app {

class Machine;

class JogService : public QObject {
    Q_OBJECT

public:
    explicit JogService(Machine& machine, QObject* parent = nullptr);

    void zeroAxis(char axis);
    void zeroAllAxes();
    void goToZero(std::string_view axes);
    std::array<double, 4> workPositionMm() const;
    std::array<double, 4> machinePositionMm() const;
    bool canMove() const;
    bool homingEnabled() const;
    bool singleAxisHoming() const;
    void selectWorkspace(const QString& wcs);
    void setWorkPosition(char axis, double value);
    void homeAxis(char axis);
    void goToCorner(controller::MachineCorner corner);
    void goToPark();
    void goToMachinePosition(const toolchange::MachinePosition& position);
    void goToLocation(controller::GoToMode mode, double x, double y, double z, double a);

private:
    Machine& machine_;
};

}  // namespace gs::app
