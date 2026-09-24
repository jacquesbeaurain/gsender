#pragma once

// The machine status over the visualizer (gSender's MachineStatus and
// UnlockButton): the state in its colour, an alarm's code with its
// description, the lock icon, the alarm button that unlocks or homes, and
// Machine Information (MachineInfo) - firmware, modal state, pins, the
// stepper lock.

#include <QDialog>
#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace gs::app {

class Machine;

// Machine Information: the firmware version, the CNC modals, the input
// pins, the tool and "Lock stepper motors".
class MachineInfoDialog final : public QDialog {
    Q_OBJECT
public:
    explicit MachineInfoDialog(Machine& machine, QWidget* parent = nullptr);
    // The value shown for a modal ("Coordinate system", ...) or pin
    // ("X limit" -> "On"/"Off"), for tests.
    QString row(const QString& label) const;

private:
    void refresh();

    Machine& machine_;
    QLabel* firmware_;
    QList<QPair<QString, QLabel*>> rows_;
    QLabel* tool_;
    QCheckBox* stepperLock_;
};

class StatusArea final : public QWidget {
    Q_OBJECT
public:
    // Overlays itself on the top centre of `parent` (the visualizer).
    StatusArea(Machine& machine, QWidget* parent);

    QString stateText() const;
    bool alarmButtonShown() const;
    QString alarmButtonText() const;
    void clickAlarmButton();
    void clickLockIcon();
    void toggleMachineInfo();

    // The homing-failure question (ALARM 6-9): re-home, unlock anyway, or
    // nothing. Replaceable for tests; a message box by default.
    enum class HomingFailureChoice { Cancel, Rehome, UnlockAnyway };
    using HomingFailureChooser = std::function<HomingFailureChoice(const QString& code)>;
    void setHomingFailureChooser(HomingFailureChooser chooser) { chooser_ = std::move(chooser); }

Q_SIGNALS:
    // The alarm's "?" (AlarmDescriptionIcon): its explanation for the
    // Helper's info panel, with upstream's alarm and error codes page.
    void alarmHelpRequested(const QString& title, const QString& description, const QString& resourceLink);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    void place();
    void act(int action, bool repopulate);
    HomingFailureChoice askHomingFailure(const QString& code);
    void showAlarmHelp();

    Machine& machine_;
    QToolButton* info_;
    QLabel* state_;
    QToolButton* help_;
    QToolButton* lock_;
    QPushButton* alarm_;
    MachineInfoDialog* infoDialog_ = nullptr;
    HomingFailureChooser chooser_;
};

}  // namespace gs::app
