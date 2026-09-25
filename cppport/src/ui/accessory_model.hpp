#pragma once

// Tools > Accessory Installation (features/AccessoryInstaller) for QML: the
// wizards - Sienci Spindle, Sienci TLS, AutoSpin, Vacuum Table - as data
// (their configurations, steps, what shows beside each step, the closing
// pages), the checks a wizard needs to pass, and what each step's page does
// to the machine, as the widget wizards (accessory_wizards) do. The page
// (AccessoryInstallerTool.qml) runs the hub, the landing pages and the
// steps.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <string>
#include <vector>

namespace gs::controller {
struct LocationSettings;
}

namespace gs::app {
class Machine;
}

namespace gs::ui {

class AccessoryModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Each {id, title, image, helpUrl, subWizards: [{id, title, description,
    // estimatedTime, configVersion, steps: [{id, title, side: [{kind, url,
    // title, text}]}], completion: {done, next, warning}}]}. A side item's
    // kind: image, jogging, commands (autospin / spindle), tlsSettings,
    // tlsInput, link.
    Q_PROPERTY(QVariantList wizards READ wizards NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString units READ units NOTIFY changed)
    Q_PROPERTY(bool probeActive READ probeActive NOTIFY changed)
    // The machine position (mm), or empty without a controller.
    Q_PROPERTY(QVariantList machinePosition READ machinePosition NOTIFY changed)
    Q_PROPERTY(QStringList firstToolBehaviours READ firstToolBehaviours CONSTANT)
    Q_PROPERTY(QVariantList vacuumSizes READ vacuumSizes CONSTANT)  // each {value, label}
    // The TLS's settings ($6, $668 before ATCi): {label, value, ok, verdict}.
    Q_PROPERTY(QVariantList tlsSettings READ tlsSettings NOTIFY changed)
    // An SLB Lite with the TLS input switched on in $65: the input's state.
    Q_PROPERTY(bool tlsInputShown READ tlsInputShown NOTIFY changed)
    Q_PROPERTY(QVariantList tlsInput READ tlsInput NOTIFY changed)
    Q_PROPERTY(bool tlsInputReady READ tlsInputReady NOTIFY changed)
    // "Commands to be sent": {label, lines}.
    Q_PROPERTY(QVariantMap autoSpinPreview READ autoSpinPreview NOTIFY changed)
    Q_PROPERTY(QVariantMap spindlePreview READ spindlePreview NOTIFY changed)
    // AutoSpin's test: $31 - $30, whether the spindle runs, what it reports.
    Q_PROPERTY(int spindleMin READ spindleMin NOTIFY changed)
    Q_PROPERTY(int spindleMax READ spindleMax NOTIFY changed)
    Q_PROPERTY(bool spindleRunning READ spindleRunning NOTIFY changed)
    Q_PROPERTY(double spindleReported READ spindleReported NOTIFY changed)
    Q_PROPERTY(bool atciFirmware READ atciFirmware NOTIFY changed)  // the build brought ATCi

public:
    explicit AccessoryModel(QObject* parent = nullptr);

    QVariantList wizards() const;
    bool connected() const;
    QString units() const;
    bool probeActive() const;
    QVariantList machinePosition() const;
    QStringList firstToolBehaviours() const;
    QVariantList vacuumSizes() const;
    QVariantList tlsSettings() const;
    bool tlsInputShown() const;
    QVariantList tlsInput() const;
    bool tlsInputReady() const;
    QVariantMap autoSpinPreview() const;
    QVariantMap spindlePreview() const;
    int spindleMin() const;
    int spindleMax() const;
    bool spindleRunning() const;
    double spindleReported() const;
    bool atciFirmware() const;

    // useValidations(): the reasons a wizard cannot start.
    Q_INVOKABLE QStringList failedChecks(const QString& wizard) const;
    // A step with nothing to do (autoComplete).
    Q_INVOKABLE bool skipStep(const QString& step) const;

    // ---- the pages ----
    Q_INVOKABLE void zeroXY();
    // The bundled programs, loaded as the job; false when unreadable.
    Q_INVOKABLE bool loadVacuumMounting(const QString& size);
    Q_INVOKABLE bool loadVacuumGrid();
    Q_INVOKABLE void applyTlsOptions(const QString& firstTool, bool manualLocation);
    // A position in workspace units, and back to mm.
    Q_INVOKABLE QString positionText(double mm) const;
    Q_INVOKABLE double positionMm(const QString& text) const;
    Q_INVOKABLE void setTlsLocation(double x, double y, double z);
    Q_INVOKABLE void setManualPosition(double x, double y, double z);
    Q_INVOKABLE QVariantList recommendedManualPosition() const;  // mm, or empty
    Q_INVOKABLE void goToPosition(double x, double y, double z);
    Q_INVOKABLE void enableTlsInput();
    Q_INVOKABLE void applyAutoSpin();
    Q_INVOKABLE void startSpindle(int rpm);
    Q_INVOKABLE void changeSpindleSpeed(int rpm);
    Q_INVOKABLE void stopSpindle();
    Q_INVOKABLE void applySpindle();
    Q_INVOKABLE void applyModbus();

Q_SIGNALS:
    void changed();

private:
    long long firmwareBuild() const;
    std::string boardId() const;
    std::string setting(const char* key) const;
    void send(const std::vector<std::string>& code);
    bool load(const QString& resource, const QString& name);
    controller::LocationSettings locationSettings() const;

    app::Machine& machine_;
};

}  // namespace gs::ui
