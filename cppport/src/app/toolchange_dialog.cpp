#include "toolchange_dialog.hpp"

#include "machine.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace gs::app {

ToolChangeWizardDialog::ToolChangeWizardDialog(Machine& machine, toolchange::Wizard wizard, QWidget* parent)
    : QDialog(parent), machine_(machine), wizard_(std::move(wizard)) {
    setWindowTitle(QString::fromStdString(wizard_.title));
    setModal(false);
    resize(760, 420);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(QString::fromStdString(wizard_.intro));
    intro->setWordWrap(true);
    intro->setStyleSheet("color:#b45309; font-weight:600");
    layout->addWidget(intro);

    auto* body = new QHBoxLayout;
    steps_ = new QListWidget;
    steps_->setMaximumWidth(220);
    steps_->setSelectionMode(QAbstractItemView::NoSelection);
    steps_->setFocusPolicy(Qt::NoFocus);
    body->addWidget(steps_);
    auto* panel = new QVBoxLayout;
    title_ = new QLabel;
    title_->setStyleSheet("font-size:15px; font-weight:600");
    description_ = new QLabel;
    description_->setWordWrap(true);
    description_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    tool_ = new QLabel;
    tool_->setStyleSheet("background:#dbeafe; border:1px solid #93c5fd; padding:6px; font-weight:600");
    actions_ = new QWidget;
    actionsLayout_ = new QVBoxLayout(actions_);
    actionsLayout_->setContentsMargins(0, 0, 0, 0);
    panel->addWidget(title_);
    panel->addWidget(description_);
    panel->addWidget(tool_);
    panel->addWidget(actions_);
    panel->addStretch();
    body->addLayout(panel, 1);
    layout->addLayout(body, 1);

    auto* buttons = new QDialogButtonBox;
    back_ = buttons->addButton(tr("Back"), QDialogButtonBox::ActionRole);
    next_ = buttons->addButton(tr("Next"), QDialogButtonBox::ActionRole);
    QPushButton* close = buttons->addButton(QDialogButtonBox::Close);
    close->setToolTip(tr("Close the wizard; the job stays paused"));
    layout->addWidget(buttons);
    connect(back_, &QPushButton::clicked, this, &ToolChangeWizardDialog::back);
    connect(next_, &QPushButton::clicked, this, &ToolChangeWizardDialog::next);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(&machine_, &Machine::wizardNext, this, &ToolChangeWizardDialog::actionDone);
    connect(&machine_, &Machine::stateChanged, this, &ToolChangeWizardDialog::updateView);
    connect(&machine_, &Machine::toolChangeWizardReady, this, &ToolChangeWizardDialog::updateView);
    updateView();
}

const toolchange::WizardSubstep& ToolChangeWizardDialog::current() const {
    return wizard_.steps.at(static_cast<std::size_t>(step_)).substeps.at(static_cast<std::size_t>(substep_));
}

bool ToolChangeWizardDialog::isDone(int step, int substep) const {
    return done_.count({step, substep}) != 0;
}

void ToolChangeWizardDialog::runAction(int index) {
    const auto& actions = current().actions;
    if (running_ || !machine_.isToolChangeWizardReady() || index < 0 || index >= static_cast<int>(actions.size())) {
        return;
    }
    running_ = true;
    machine_.runWizardAction(step_, substep_, actions[static_cast<std::size_t>(index)].gcode);
    updateView();
}

// wizard:next: the action's G-code went through; move on (completeSubStep).
void ToolChangeWizardDialog::actionDone(int step, int substep) {
    if (!running_ || step != step_ || substep != substep_) {
        return;  // a stale report from an earlier wizard
    }
    running_ = false;
    done_.insert({step, substep});
    advance();
}

void ToolChangeWizardDialog::advance() {
    const auto& substeps = wizard_.steps[static_cast<std::size_t>(step_)].substeps;
    if (substep_ + 1 < static_cast<int>(substeps.size())) {
        ++substep_;
    } else if (step_ + 1 < static_cast<int>(wizard_.steps.size())) {
        ++step_;
        substep_ = 0;
    } else {
        accept();  // the last action resumed the job
        return;
    }
    updateView();
}

void ToolChangeWizardDialog::next() {
    // Instructions without actions, or whose action ran, can be passed.
    if (running_ || (!current().actions.empty() && !isDone(step_, substep_))) {
        return;
    }
    done_.insert({step_, substep_});
    advance();
}

void ToolChangeWizardDialog::back() {
    if (running_) {
        return;
    }
    if (substep_ > 0) {
        --substep_;
    } else if (step_ > 0) {
        --step_;
        substep_ = static_cast<int>(wizard_.steps[static_cast<std::size_t>(step_)].substeps.size()) - 1;
    }
    updateView();
}

void ToolChangeWizardDialog::updateView() {
    steps_->clear();
    for (std::size_t i = 0; i < wizard_.steps.size(); ++i) {
        const int index = static_cast<int>(i);
        const bool stepDone = index < step_;
        auto* item = new QListWidgetItem((stepDone ? QString::fromUtf8("✓ ") : QString("   ")) +
                                         QString::fromStdString(wizard_.steps[i].title));
        QFont font = item->font();
        font.setBold(index == step_);
        item->setFont(font);
        steps_->addItem(item);
    }

    const toolchange::WizardSubstep& substep = current();
    title_->setText(QString::fromStdString(substep.title));
    description_->setText(QString::fromStdString(substep.description));
    tool_->setVisible(substep.toolBanner);
    if (substep.toolBanner) {
        const controller::Controller* c = machine_.controller();
        const QString tool = c ? QString::fromStdString(c->state().parserState.modal.tool) : QStringLiteral("-");
        tool_->setText(tr("Load tool T%1").arg(tool));
    }

    // (Deferred: this runs from the clicked button's own handler.)
    while (QLayoutItem* item = actionsLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    for (std::size_t i = 0; i < substep.actions.size(); ++i) {
        const bool ran = isDone(step_, substep_);
        auto* button = new QPushButton((ran ? QString::fromUtf8("✓ ") : QString()) +
                                       QString::fromStdString(substep.actions[i].label));
        button->setMinimumHeight(34);
        button->setEnabled(!running_ && machine_.isToolChangeWizardReady());
        connect(button, &QPushButton::clicked, this, [this, i] { runAction(static_cast<int>(i)); });
        actionsLayout_->addWidget(button);
    }
    if (running_) {
        actionsLayout_->addWidget(new QLabel(tr("Running...")));
    } else if (!machine_.isToolChangeWizardReady() && !substep.actions.empty()) {
        actionsLayout_->addWidget(new QLabel(tr("Waiting for the initial movements to finish...")));
    }
    back_->setEnabled(!running_ && (step_ > 0 || substep_ > 0));
    next_->setEnabled(!running_ && (substep.actions.empty() || isDone(step_, substep_)));
}

}  // namespace gs::app
