#pragma once

// Tools > Accessory Installation (features/AccessoryInstaller) for QML: the
// wizards - Sienci Spindle, Sienci TLS, AutoSpin, Vacuum Table - as data
// (their configurations, steps, what shows beside each step, the closing
// pages), the checks a wizard needs to pass, and what each step's page does
// to the machine (accessory_wizards). It also walks them (components/
// Wizard): the hub, a wizard's landing page, a configuration's steps one at
// a time - Next opening once a step's page is done, steps with nothing to do
// passed - and the closing page. AccessoryInstallerTool.qml draws the screen
// the model is on and each step's page.

#include "wizard_model_base.hpp"
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <set>
#include <string>
#include <vector>

namespace gs::controller {
struct LocationSettings;
}

namespace gs::app {
class Machine;
}

namespace gs::ui {

class AccessoryModel : public WizardModelBase {
    Q_OBJECT
    QML_ELEMENT

    // Each {id, title, image, helpUrl, subWizards: [{id, title, description,
    // estimatedTime, configVersion, steps: [{id, title, side: [{kind, url,
    // title, text}]}], completion: {done, next, warning}}]}. A side item's
    // kind: image, jogging, commands (autospin / spindle), tlsSettings,
    // tlsInput, link.
    Q_PROPERTY(QVariantList wizards READ wizards NOTIFY changed)
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

    // ---- walking the wizards (stepIndex, canNext, canBack: WizardModelBase) ----
    // "hub", "landing" (a wizard's page) or "run" (a configuration's steps).
    Q_PROPERTY(QString screen READ screen NOTIFY navigationChanged)
    // The wizard and configuration open, as `wizards` had them when opened
    // (empty: none), and the configuration's steps.
    Q_PROPERTY(QVariantMap wizard READ wizard NOTIFY navigationChanged)
    Q_PROPERTY(QVariantMap subWizard READ subWizard NOTIFY navigationChanged)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY navigationChanged)
    // The reasons the open wizard cannot start (failedChecks).
    Q_PROPERTY(QStringList failed READ failed NOTIFY changed)
    // Past the last step: the closing page.
    Q_PROPERTY(bool atCompletion READ atCompletion NOTIFY navigationChanged)

public:
    explicit AccessoryModel(QObject* parent = nullptr);

    QVariantList wizards() const;
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
    QString screen() const { return screen_; }
    QVariantMap wizard() const { return wizard_; }
    QVariantMap subWizard() const { return sub_; }
    QVariantList steps() const;
    QStringList failed() const;
    bool atCompletion() const noexcept { return atCompletion_; }
    int totalSteps() const override { return static_cast<int>(steps().size()); }
    bool canNext() const override;
    bool canBack() const override;

    // A wizard of the hub: its landing page, or straight into its only
    // configuration when nothing fails. False for an unknown wizard.
    Q_INVOKABLE bool openWizard(const QString& id);
    // A configuration of the open wizard, from its first step; false while
    // a check fails.
    Q_INVOKABLE bool startSubWizard(const QString& id);
    // The step's page says whether it is done (Next opens).
    Q_INVOKABLE void setStepComplete(bool complete);
    void next() override;     // the next step, or the closing page after the last
    void back() override;     // Previous: the step before, passing those skipped
    void restart() override;  // Restart Wizard: from the first step, nothing done
    // Exit: from the steps (or closing page) to the wizard's landing page.
    Q_INVOKABLE void exitSubWizard();
    Q_INVOKABLE void backToHub();

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
    void navigationChanged();
    // The page to show changed (another step, the same step again, or none):
    // the page is made afresh each time.
    void pageChanged();

private:
    long long firmwareBuild() const;
    std::string boardId() const;
    std::string setting(const char* key) const;
    void send(const std::vector<std::string>& code);
    bool load(const QString& resource, const QString& name);
    controller::LocationSettings locationSettings() const;
    QString stepId(int index) const;
    void enterStep(int index);
    void navigated();

    QString screen_ = QStringLiteral("hub");
    QVariantMap wizard_;
    QVariantMap sub_;
    std::set<int> completed_;
    bool atCompletion_ = false;
};

}  // namespace gs::ui
