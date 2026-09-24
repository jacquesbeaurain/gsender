#pragma once

// Tools > Accessory Installation (features/AccessoryInstaller and its
// components/Wizard framework): a hub of accessory wizards. Each has a
// landing page - the checks it needs to pass (connected, homed, grblHAL),
// its configurations - and each configuration runs its steps one page at a
// time, Next opening once a page is done, with a panel beside it (a
// picture, the jog controls, the commands it sends, a help link) and a
// closing page. The wizards themselves are in accessory_wizards.

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <set>
#include <vector>

class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace gs::app {

class Jogger;
class Machine;

// A step's page. It says when it is done (onComplete / onUncomplete);
// `finish` closes the installer (a page that loaded a file into the
// visualizer - upstream navigates home).
class WizardPage : public QWidget {
    Q_OBJECT
public:
    explicit WizardPage(QWidget* parent = nullptr) : QWidget(parent) {}
    bool isComplete() const noexcept { return complete_; }

Q_SIGNALS:
    void completionChanged(bool complete);
    void finish();

protected:
    void setComplete(bool complete);

private:
    bool complete_ = false;
};

// Beside a step (secondaryContent): a picture, the jog controls, a widget of
// the wizard's, or the help link.
struct WizardSideItem {
    enum class Kind { Image, Jogging, Widget, Link };
    Kind kind = Kind::Image;
    QString image;                     // a Qt resource
    std::function<QWidget*()> widget;  // Kind::Widget
    QString title;                     // a heading above it
    QString text;                      // the link's lead-in
    QString url;
};

struct WizardStep {
    QString id;
    QString title;
    std::function<WizardPage*()> page;
    std::vector<WizardSideItem> side;
    // A step with nothing to do is passed over, forwards and back
    // (autoComplete).
    std::function<bool()> skip;
};

struct SubWizard {
    QString id;
    QString title;
    QString description;
    QString estimatedTime;
    QString configVersion;
    std::vector<WizardStep> steps;
    std::function<QWidget*()> completion;  // shown after the last step
};

struct WizardCheck {
    bool ok = true;
    QString reason;
};

struct AccessoryWizard {
    QString id;
    QString title;
    QString image;  // the landing page's picture (none: a placeholder)
    QString helpUrl;
    std::vector<std::function<WizardCheck()>> validations;
    std::vector<SubWizard> subWizards;
};

class AccessoryInstallerDialog final : public QDialog {
    Q_OBJECT
public:
    AccessoryInstallerDialog(Machine& machine, Jogger& jogger, std::vector<AccessoryWizard> wizards,
                             QWidget* parent = nullptr);

    // ---- what the buttons do (and tests) ----
    QStringList wizardTitles() const;
    // A wizard's landing page - or, with one configuration and nothing
    // failing, straight into it (WizardManager's auto-select).
    bool openWizard(const QString& id);
    QStringList failedChecks() const;  // the landing page's reasons
    bool startSubWizard(const QString& id);  // false while a check fails
    void backToHub();

    QString stepTitle() const;  // empty off the steps
    int stepNumber() const;     // 1-based
    int stepCount() const;
    WizardPage* page() const { return page_; }
    bool canGoNext() const;
    bool next();  // false unless the page is done
    bool previous();
    bool atCompletion() const { return completion_; }
    void restart();  // "Restart Wizard"
    void exitSubWizard();  // "Exit": back to the landing page

private:
    void showHub();
    void showLanding();
    void refreshLanding();
    void showStep();
    void showCompletion();
    void enterStep(int index);  // passing over skipped steps
    void refreshNavigation();
    QWidget* sideItem(const WizardSideItem& item);
    const AccessoryWizard* findWizard(const QString& id) const;

    Machine& machine_;
    Jogger& jogger_;
    std::vector<AccessoryWizard> wizards_;
    const AccessoryWizard* wizard_ = nullptr;
    const SubWizard* subWizard_ = nullptr;
    int step_ = 0;
    std::set<int> completed_;
    bool completion_ = false;

    QStackedWidget* screens_;
    // landing
    QLabel* landingTitle_;
    QLabel* landingInfo_;
    QLabel* landingChecks_;
    QLabel* landingImage_;
    QLabel* landingHelp_;
    QVBoxLayout* subWizardButtons_;
    // steps
    QLabel* progressText_;
    QProgressBar* progress_;
    QLabel* stepHeading_;
    QWidget* pageHolder_;
    QVBoxLayout* pageLayout_;
    QWidget* sidePanel_;  // the side's scroll area
    QWidget* side_;
    QVBoxLayout* sideLayout_;
    QWidget* navigation_;
    QPushButton* previous_;
    QPushButton* next_;
    QPushButton* exitWizard_;
    QPushButton* restart_;
    WizardPage* page_ = nullptr;
};

}  // namespace gs::app
