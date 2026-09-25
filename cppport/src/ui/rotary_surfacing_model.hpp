#pragma once

// Rotary Surfacing (features/Rotary/RotarySurfacing) for QML: the stock's
// length, start and final diameters, stepdown, bit and cutting settings in -
// in the workspace's units, stored in mm - and a turning program out,
// previewed wrapped around the rotary and loaded as the job.

#include "gs/rotary/rotary.hpp"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class RotarySurfacingModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // stockLength, startHeight, finalHeight, stepdown, bitDiameter,
    // toolNumber, stepover, feedrate, spindleRPM, shouldDwell, enableRehoming.
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QVariantMap defaults READ defaults CONSTANT)
    Q_PROPERTY(QString units READ units CONSTANT)
    Q_PROPERTY(bool free READ free NOTIFY stateChanged)
    Q_PROPERTY(QString program READ program NOTIFY programChanged)
    Q_PROPERTY(int lines READ lines NOTIFY programChanged)

public:
    explicit RotarySurfacingModel(QObject* parent = nullptr);

    QVariantMap options() const;
    QVariantMap defaults() const;
    QString units() const;
    bool free() const;
    QString program() const { return program_; }
    int lines() const { return program_.isEmpty() ? 0 : static_cast<int>(program_.count('\n')) + 1; }

    Q_INVOKABLE void setOption(const QString& key, const QVariant& value);
    Q_INVOKABLE void generate();
    Q_INVOKABLE bool load();  // gSender_Rotary_Surfacing

Q_SIGNALS:
    void optionsChanged();
    void stateChanged();
    void programChanged();

private:
    void save();

    app::Machine& machine_;
    rotary::StockTurningOptions options_;  // in the workspace's units
    QString program_;
};

}  // namespace gs::ui
