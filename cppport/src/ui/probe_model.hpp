#pragma once

// The Probe tab (features/Probe) for QML: the plate (with the touch plate
// switcher), the routines it offers (Z, XYZ, XY, X, Y), the tool diameter
// for routines that need one (with AutoZero's and BitZero's Auto and Tip,
// and diameters added here - workspace.tools), the plate's corner, the
// routine's picture; and the run step: the probe circuit check, then the
// routine.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class ProbeModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool canClick READ canClick NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString plateType READ plateType NOTIFY changed)
    Q_PROPERTY(QStringList plateTypes READ plateTypes CONSTANT)
    Q_PROPERTY(bool plateSwitcher READ plateSwitcher NOTIFY changed)
    // The routines {id, label} ("XYZ Touch", "XYZ") and the chosen one.
    Q_PROPERTY(QVariantList commands READ commands NOTIFY changed)
    Q_PROPERTY(int selected READ selected NOTIFY changed)
    Q_PROPERTY(QString commandId READ commandId NOTIFY changed)
    Q_PROPERTY(bool needsTool READ needsTool NOTIFY changed)
    // The tool: "Auto", "Tip" or a diameter; the choices {value, label,
    // removable}; the units' name.
    Q_PROPERTY(QString tool READ tool NOTIFY changed)
    Q_PROPERTY(QVariantList tools READ tools NOTIFY changed)
    Q_PROPERTY(QString units READ units NOTIFY changed)
    Q_PROPERTY(int corner READ corner NOTIFY changed)
    Q_PROPERTY(QString cornerName READ cornerName NOTIFY changed)
    // The picture (ProbeImage): a qrc URL of upstream's GIF.
    Q_PROPERTY(QString image READ image NOTIFY changed)
    Q_PROPERTY(bool probe3D READ probe3D NOTIFY changed)
    // The run step.
    Q_PROPERTY(bool probeTriggered READ probeTriggered NOTIFY changed)
    Q_PROPERTY(bool circuitChecked READ circuitChecked NOTIFY changed)
    Q_PROPERTY(bool simulated READ simulated NOTIFY changed)

public:
    explicit ProbeModel(QObject* parent = nullptr);

    bool canClick() const;
    bool connected() const;
    QString plateType() const;
    QStringList plateTypes() const;
    bool plateSwitcher() const;
    QVariantList commands() const;
    int selected() const { return selected_; }
    QString commandId() const;
    bool needsTool() const;
    QString tool() const { return tool_; }
    QVariantList tools() const;
    QString units() const;
    int corner() const;
    QString cornerName() const;
    QString image() const;
    bool probe3D() const;
    bool probeTriggered() const;
    bool circuitChecked() const { return checked_; }
    bool simulated() const;

    Q_INVOKABLE void selectCommand(int index);
    // The routine scroll shortcuts, wrapping.
    Q_INVOKABLE void stepCommand(int delta);
    Q_INVOKABLE void setPlateType(const QString& name);
    Q_INVOKABLE void selectTool(const QString& value);
    // A custom diameter in the workspace units: kept and chosen. False when
    // it is not a positive number.
    Q_INVOKABLE bool addTool(const QString& text);
    Q_INVOKABLE void removeTool(const QString& value);
    Q_INVOKABLE void nextCorner();

    // Opening the run step: the check starts over (passed already when
    // the settings turn it off); on the simulator a plate goes under the bit.
    Q_INVOKABLE void beginRun();
    // "Confirm Probe" by hand.
    Q_INVOKABLE void confirmCircuit();
    // Runs the routine; false when it cannot.
    Q_INVOKABLE bool start();

Q_SIGNALS:
    void changed();

private:
    int probeTypeIndex() const;  // Diameter, Auto, Tip
    double toolDiameter() const;
    void settingsChanged();

    app::Machine& machine_;
    int selected_ = 0;
    QString tool_;
    bool checked_ = false;
};

}  // namespace gs::ui
