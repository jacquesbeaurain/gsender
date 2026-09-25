#pragma once

// Tool changes for QML (features/Helper's Wizard, controllerSagas'
// gcode:toolChange): the wizard a job's M6 opens with a wizard strategy -
// its steps and substeps, the actions whose G-code must go through before
// moving on (wizard:next), Back/Next/Complete, minimising, cancelling (the
// job stays paused) - the first tool's question for the Fixed Tool Sensor,
// and the "Code" strategy's change-the-tool-then-continue prompt.

#include "gs/toolchange/wizards.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <set>
#include <utility>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class ToolChangeModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString intro READ intro NOTIFY changed)
    // [{title, substeps: [title]}] and where the wizard is.
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    Q_PROPERTY(int step READ step NOTIFY changed)
    Q_PROPERTY(int substep READ substep NOTIFY changed)
    // The substep shown: {title, description, toolBanner, actions: [label]}.
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
    Q_PROPERTY(QString toolLabel READ toolLabel NOTIFY changed)  // "T2"
    Q_PROPERTY(QString comment READ comment NOTIFY changed)      // the M6 line's comment
    Q_PROPERTY(bool running READ running NOTIFY changed)         // an action's G-code is out
    Q_PROPERTY(bool ready READ ready NOTIFY changed)             // the start-up G-code went
    Q_PROPERTY(bool currentDone READ currentDone NOTIFY changed)
    Q_PROPERTY(bool canBack READ canBack NOTIFY changed)
    Q_PROPERTY(bool canNext READ canNext NOTIFY changed)
    Q_PROPERTY(bool last READ last NOTIFY changed)
    // The progress dots: the substeps in all, and the current one's place.
    Q_PROPERTY(int flatCount READ flatCount NOTIFY changed)
    Q_PROPERTY(int flatIndex READ flatIndex NOTIFY changed)

public:
    explicit ToolChangeModel(QObject* parent = nullptr);

    bool active() const { return wizard_.has_value(); }
    QString title() const;
    QString intro() const;
    QVariantList steps() const;
    int step() const { return step_; }
    int substep() const { return substep_; }
    QVariantMap current() const;
    QString toolLabel() const;
    QString comment() const { return comment_; }
    bool running() const { return running_; }
    bool ready() const;
    bool currentDone() const;
    bool canBack() const;
    bool canNext() const;
    bool last() const;
    int flatCount() const;
    int flatIndex() const;

    // The first tool's answer: the full wizard, or measure only.
    Q_INVOKABLE void answerFirstTool(bool fullWizard);
    Q_INVOKABLE void runAction(int index);
    Q_INVOKABLE void next();
    Q_INVOKABLE void back();
    // Closes the wizard; the job stays paused.
    Q_INVOKABLE void cancel();
    // "Code": the tool is changed - the post-hook runs and the job resumes.
    Q_INVOKABLE void continueCodeChange();

Q_SIGNALS:
    void changed();
    // Fixed Tool Sensor, first tool, "Ask": run the whole wizard?
    void firstToolQuestion(const QString& comment);
    // "Code": the pre-hook ran; change the tool, then continueCodeChange().
    void codeChangeWaiting(const QString& comment);

private:
    const toolchange::WizardSubstep* currentSubstep() const;
    void start(bool fullWizard);
    void actionDone(int step, int substep);
    void advance();

    app::Machine& machine_;
    std::optional<toolchange::Wizard> wizard_;
    int step_ = 0;
    int substep_ = 0;
    bool running_ = false;
    std::set<std::pair<int, int>> done_;
    QString option_;
    int count_ = 0;
    QString comment_;
};

}  // namespace gs::ui
