#pragma once

// Wasteboard Surfacing (features/Surfacing) for QML: the stock, depths, bit
// and cutting settings in - in the workspace's units, stored in mm - and a
// surfacing program out, previewed and loaded as the job.

#include "gs/surfacing/surfacing.hpp"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class SurfacingModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // width, length, skimDepth, maxDepth, bitDiameter, toolNumber, stepover,
    // feedrate, spindleRPM, spindle ("M3"/"M4"), shouldDwell, mist, flood,
    // startPosition ("backLeft", "backRight", "frontLeft", "frontRight",
    // "center"), pattern ("spiral"/"zigzag"), cutDirectionFlipped.
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QVariantMap defaults READ defaults CONSTANT)
    Q_PROPERTY(QString units READ units CONSTANT)
    Q_PROPERTY(QString depthWarning READ depthWarning NOTIFY optionsChanged)
    Q_PROPERTY(bool free READ free NOTIFY stateChanged)  // idle, jogging or not reporting
    Q_PROPERTY(QString program READ program NOTIFY programChanged)
    Q_PROPERTY(int lines READ lines NOTIFY programChanged)

public:
    explicit SurfacingModel(QObject* parent = nullptr);

    QVariantMap options() const;
    QVariantMap defaults() const;
    QString units() const;
    QString depthWarning() const;
    bool free() const;
    QString program() const { return program_; }
    int lines() const { return program_.isEmpty() ? 0 : static_cast<int>(program_.count('\n')) + 1; }

    Q_INVOKABLE void setOption(const QString& key, const QVariant& value);
    // "Generate G-code" (the settings kept); "Load to Main Visualizer"
    // (gSender_Surfacing.gcode): false when there is none or it is busy.
    Q_INVOKABLE void generate();
    Q_INVOKABLE bool load();

Q_SIGNALS:
    void optionsChanged();
    void stateChanged();
    void programChanged();

private:
    void save();

    app::Machine& machine_;
    surfacing::Options options_;  // in the workspace's units
    QString program_;
};

}  // namespace gs::ui
