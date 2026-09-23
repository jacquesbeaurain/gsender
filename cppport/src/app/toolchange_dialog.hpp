#pragma once

// gSender's tool change wizard: the steps down the side, the current
// instruction with its actions and the tool to load. An action runs its
// G-code and the wizard moves on when the controller reports the lines done
// (wizard:next); the last action resumes the job and closes the wizard.

#include "gs/toolchange/wizards.hpp"

#include <QDialog>

#include <set>
#include <utility>

class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

namespace gs::app {

class Machine;

class ToolChangeWizardDialog final : public QDialog {
    Q_OBJECT
public:
    ToolChangeWizardDialog(Machine& machine, toolchange::Wizard wizard, QWidget* parent = nullptr);

    int step() const noexcept { return step_; }
    int substep() const noexcept { return substep_; }
    bool isRunning() const noexcept { return running_; }  // an action's G-code is out
    const toolchange::Wizard& wizard() const noexcept { return wizard_; }

    // As the buttons do.
    void runAction(int index);
    void next();
    void back();

private:
    const toolchange::WizardSubstep& current() const;
    bool isDone(int step, int substep) const;
    void actionDone(int step, int substep);
    void advance();
    void updateView();

    Machine& machine_;
    toolchange::Wizard wizard_;
    int step_ = 0;
    int substep_ = 0;
    bool running_ = false;
    std::set<std::pair<int, int>> done_;
    QListWidget* steps_;
    QLabel* title_;
    QLabel* description_;
    QLabel* tool_;
    QWidget* actions_;
    QVBoxLayout* actionsLayout_;
    QPushButton* back_;
    QPushButton* next_;
};

}  // namespace gs::app
