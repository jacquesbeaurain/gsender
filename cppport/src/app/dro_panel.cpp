#include "dro_panel.hpp"

#include "machine.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace gs::app {
namespace {

constexpr char kAxes[4] = {'X', 'Y', 'Z', 'A'};

// WorkspaceSelector: the work coordinate systems with their P numbers and
// accent colours (upstream's -600 shades, shared with its pendant).
struct Workspace {
    const char* code;
    const char* label;
    const char* color;
};
constexpr Workspace kWorkspaces[] = {
    {"G54", "G54 (P1)", "#2563eb"}, {"G55", "G55 (P2)", "#059669"}, {"G56", "G56 (P3)", "#d97706"},
    {"G57", "G57 (P4)", "#7c3aed"}, {"G58", "G58 (P5)", "#e11d48"}, {"G59", "G59 (P6)", "#0891b2"},
};

// RapidPositionButtons, in upstream's grid order.
struct CornerButton {
    controller::MachineCorner corner;
    char16_t arrow;
    const char* tip;
};
constexpr CornerButton kCorners[] = {
    {controller::MachineCorner::BackLeft, u'↖', QT_TRANSLATE_NOOP("PositionPanel", "Go to Back Left Corner")},
    {controller::MachineCorner::BackRight, u'↗', QT_TRANSLATE_NOOP("PositionPanel", "Go to Back Right Corner")},
    {controller::MachineCorner::FrontLeft, u'↙', QT_TRANSLATE_NOOP("PositionPanel", "Go to Front Left Corner")},
    {controller::MachineCorner::FrontRight, u'↘',
     QT_TRANSLATE_NOOP("PositionPanel", "Go to Front Right Corner")},
};

// A position as the DRO shows it: the workspace units (A in degrees).
QString positionText(const Machine& machine, int axis, double mm) {
    if (axis == 3) {
        return QString::number(mm, 'f', 3);
    }
    const AppSettings& s = machine.settings();
    return QString::fromStdString(units::positionText(mm, s.metric, s.customDecimalPlaces));
}

}  // namespace

// ---- Go To Location --------------------------------------------------------------------

GoToDialog::GoToDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Go To Location"));
    auto* layout = new QVBoxLayout(this);
    auto* modes = new QHBoxLayout;
    modes->setSpacing(0);
    const char* names[3] = {"ABS", "INC", "MCS"};
    for (int i = 0; i < 3; ++i) {
        modes_[i] = new QPushButton(names[i]);
        modes_[i]->setCheckable(true);
        modes->addWidget(modes_[i]);
        connect(modes_[i], &QPushButton::clicked, this,
                [this, i] { setMode(static_cast<controller::GoToMode>(i)); });
    }
    modes_[0]->setToolTip(tr("Absolute work coordinates"));
    modes_[1]->setToolTip(tr("Distances from here"));
    layout->addLayout(modes);
    auto* form = new QFormLayout;
    for (int i = 0; i < 4; ++i) {
        values_[i] = new QDoubleSpinBox;
        values_[i]->setRange(-100000, 100000);
        values_[i]->setAlignment(Qt::AlignRight);
        form->addRow(QString(QChar(kAxes[i])), values_[i]);
    }
    layout->addLayout(form);
    go_ = new QPushButton(tr("Go!"));
    go_->setDefault(true);
    connect(go_, &QPushButton::clicked, this, &GoToDialog::go);
    layout->addWidget(go_);
    connect(&machine_, &Machine::stateChanged, this, &GoToDialog::updateEnabled);
    connect(&machine_, &Machine::settingsChanged, this, &GoToDialog::updateEnabled);
    connect(&machine_, &Machine::connectionChanged, this, &GoToDialog::updateEnabled);
    connect(&machine_, &Machine::workflowChanged, this, &GoToDialog::updateEnabled);
    setMode(controller::GoToMode::Absolute);
}

void GoToDialog::setMode(controller::GoToMode mode) {
    mode_ = mode;
    for (int i = 0; i < 3; ++i) {
        modes_[i]->setChecked(i == static_cast<int>(mode));
    }
    fill();
    updateEnabled();
}

void GoToDialog::setTarget(double x, double y, double z, double a) {
    values_[0]->setValue(x);
    values_[1]->setValue(y);
    values_[2]->setValue(z);
    values_[3]->setValue(a);
}

double GoToDialog::value(int axis) const {
    return values_[axis]->value();
}

void GoToDialog::fill() {
    const AppSettings& s = machine_.settings();
    for (int i = 0; i < 3; ++i) {
        values_[i]->setSuffix(s.metric ? tr(" mm") : tr(" in"));
        values_[i]->setDecimals(s.metric ? 3 : 4);
    }
    values_[3]->setSuffix(tr(" deg"));
    values_[3]->setDecimals(3);
    if (mode_ == controller::GoToMode::Incremental) {
        setTarget(0, 0, 0, 0);
        return;
    }
    // As the DRO shows them. Deviation: upstream fills MCS with the raw mm
    // figures, also in an inch workspace.
    const std::array<double, 4> position =
        mode_ == controller::GoToMode::Machine ? machine_.machinePositionMm() : machine_.workPositionMm();
    for (int i = 0; i < 3; ++i) {
        values_[i]->setValue(
            js::stringToNumber(units::positionText(position[static_cast<std::size_t>(i)], s.metric, s.customDecimalPlaces)));
    }
    values_[3]->setValue(machine_.rotaryMode() ? position[1] : position[3]);
}

void GoToDialog::updateEnabled() {
    controller::Controller* c = machine_.controller();
    // MCS needs homing enabled ($22) and a homed machine.
    const bool homing = c && js::stringToNumber(c->runner().setting("$22", "0")) != 0;
    const bool mcsAvailable = homing && c->hasHomed();
    modes_[2]->setVisible(homing);
    modes_[2]->setEnabled(mcsAvailable);
    modes_[2]->setToolTip(mcsAvailable ? tr("Machine coordinates")
                                       : tr("Requires homing enabled ($22>0) and machine homed"));
    if (mode_ == controller::GoToMode::Machine && !mcsAvailable) {
        setMode(controller::GoToMode::Absolute);
        return;
    }
    // MCS moves X, Y (and A) only; A needs a grblHAL board with an A axis.
    values_[2]->setEnabled(mode_ != controller::GoToMode::Machine);
    // Rotary mode: A (the rotary) instead of Y.
    values_[1]->setEnabled(!machine_.rotaryMode());
    values_[3]->setEnabled(machine_.rotaryMode() ||
                           (c && c->isGrblHal() && c->state().axes.letters.find('A') != std::string::npos));
    go_->setEnabled(machine_.canMove());
}

void GoToDialog::go() {
    if (!machine_.canMove()) {
        return;
    }
    machine_.goToLocation(mode_, value(0), value(1), value(2), value(3));
}

// ---- the DRO ----------------------------------------------------------------------------

PositionPanel::PositionPanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* box = new QGroupBox(tr("Position"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);
    auto* layout = new QVBoxLayout(box);

    // Units badge, Go To, corners and park, the work coordinate system.
    auto* top = new QHBoxLayout;
    units_ = new QPushButton;
    units_->setFlat(true);
    units_->setToolTip(tr("Workspace units - click to switch"));
    connect(units_, &QPushButton::clicked, this, [this] {
        AppSettings settings = machine_.settings();
        settings.metric = !settings.metric;
        machine_.setSettings(settings);
    });
    goTo_ = new QPushButton(tr("Go To..."));
    goTo_->setToolTip(tr("Go To Location"));
    connect(goTo_, &QPushButton::clicked, this, &PositionPanel::openGoTo);
    auto* cornerGrid = new QGridLayout;
    cornerGrid->setSpacing(0);
    for (int i = 0; i < 4; ++i) {
        corners_[i] = new QToolButton;
        corners_[i]->setText(QString(QChar(kCorners[i].arrow)));
        corners_[i]->setToolTip(tr(kCorners[i].tip));
        corners_[i]->setAutoRaise(true);
        cornerGrid->addWidget(corners_[i], i / 2, i % 2);
        connect(corners_[i], &QToolButton::clicked, this, [this, i] { machine_.goToCorner(kCorners[i].corner); });
    }
    park_ = new QPushButton(tr("Park"));
    park_->setToolTip(tr("Go to Park Location"));
    connect(park_, &QPushButton::clicked, this, [this] { machine_.goToPark(); });
    workspace_ = new QComboBox;
    for (const Workspace& w : kWorkspaces) {
        workspace_->addItem(w.label, w.code);
        workspace_->setItemData(workspace_->count() - 1, QColor(w.color), Qt::ForegroundRole);
    }
    workspace_->setToolTip(tr("Select a workspace"));
    connect(workspace_, &QComboBox::activated, this,
            [this](int index) { machine_.selectWorkspace(workspace_->itemData(index).toString()); });
    connect(workspace_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            workspace_->setStyleSheet(QString("QComboBox { color: %1; font-weight: 600; }").arg(kWorkspaces[index].color));
        }
    });
    workspace_->setStyleSheet(QString("QComboBox { color: %1; font-weight: 600; }").arg(kWorkspaces[0].color));
    top->addWidget(units_);
    top->addWidget(goTo_);
    top->addLayout(cornerGrid);
    top->addWidget(park_);
    top->addStretch(1);
    top->addWidget(new QLabel(tr("Workspace:")));
    top->addWidget(workspace_);
    layout->addLayout(top);

    // Per axis: zero (or home), the work position (type a value + Enter to
    // set it), the machine position, go to zero.
    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Work")), 0, 1, Qt::AlignCenter);
    grid->addWidget(new QLabel(tr("Machine")), 0, 2, Qt::AlignRight);
    grid->addWidget(new QLabel(tr("Go to")), 0, 3, Qt::AlignCenter);
    QFont big = font();
    big.setPointSizeF(big.pointSizeF() * 1.8);
    big.setBold(true);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    for (int i = 0; i < 4; ++i) {
        const char axis = kAxes[i];
        axisButton_[i] = new QPushButton;
        axisButton_[i]->setFont(big);
        axisButton_[i]->setMinimumWidth(64);
        connect(axisButton_[i], &QPushButton::clicked, this, [this, i] { axisClicked(i); });
        work_[i] = new QLineEdit("0.00");
        work_[i]->setFont(big);
        work_[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        work_[i]->setMinimumWidth(130);
        work_[i]->setStyleSheet("QLineEdit { color: #1d4ed8; }");
        work_[i]->setToolTip(tr("Work position - type a value and press Enter to make this position read it"));
        work_[i]->installEventFilter(this);
        connect(work_[i], &QLineEdit::returnPressed, this, [this, i] {
            enterWorkPosition(i, work_[i]->text());
            work_[i]->clearFocus();
        });
        machinePos_[i] = new QLabel("0.00");
        machinePos_[i]->setFont(mono);
        machinePos_[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        machinePos_[i]->setMinimumWidth(80);
        machinePos_[i]->setStyleSheet("color:#777");
        goZero_[i] = new QPushButton(QString(QChar(axis)));
        goZero_[i]->setToolTip(tr("Go to %1-axis zero").arg(axis));
        connect(goZero_[i], &QPushButton::clicked, this,
                [this, i] { machine_.goToZero(std::string(1, rowAxis(i))); });
        grid->addWidget(axisButton_[i], i + 1, 0);
        grid->addWidget(work_[i], i + 1, 1);
        grid->addWidget(machinePos_[i], i + 1, 2);
        grid->addWidget(goZero_[i], i + 1, 3);
    }
    rowA_[0] = axisButton_[3];
    rowA_[1] = work_[3];
    rowA_[2] = machinePos_[3];
    rowA_[3] = goZero_[3];
    layout->addLayout(grid);

    // Zero all, homing (with the single-axis switch), XY zero, unlock, reset.
    auto* bottom = new QHBoxLayout;
    zeroAll_ = new QPushButton(tr("Zero All"));
    zeroAll_->setToolTip(tr("Zero all axes"));
    connect(zeroAll_, &QPushButton::clicked, this, &PositionPanel::zeroAll);
    home_ = new QPushButton(tr("Home"));
    home_->setToolTip(tr("Run homing"));
    connect(home_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            c->home();
        }
    });
    singleAxis_ = new QCheckBox(tr("Single axis"));
    singleAxis_->setToolTip(tr("Toggle single axis homing: the axis buttons home instead of zeroing"));
    connect(singleAxis_, &QCheckBox::toggled, this, &PositionPanel::setHomingMode);
    goXY_ = new QPushButton(tr("Go to XY"));
    goXY_->setToolTip(tr("Go to XY zero"));
    connect(goXY_, &QPushButton::clicked, this, [this] { machine_.goToZero("XY"); });
    unlock_ = new QPushButton(tr("Unlock"));
    connect(unlock_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            c->unlock();
        }
    });
    reset_ = new QPushButton(tr("Reset"));
    reset_->setToolTip(tr("Soft reset (Ctrl-X)"));
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            c->reset();
        }
    });
    bottom->addWidget(zeroAll_);
    bottom->addWidget(home_);
    bottom->addWidget(singleAxis_);
    bottom->addWidget(goXY_);
    bottom->addStretch(1);
    bottom->addWidget(unlock_);
    bottom->addWidget(reset_);
    layout->addLayout(bottom);

    connect(&machine_, &Machine::appSettingsChanged, this, &PositionPanel::refresh);
    connect(&machine_, &Machine::stateChanged, this, &PositionPanel::refresh);
    connect(&machine_, &Machine::settingsChanged, this, &PositionPanel::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &PositionPanel::refresh);
    connect(&machine_, &Machine::workflowChanged, this, &PositionPanel::refresh);
    refresh();
}

void PositionPanel::setHomingMode(bool on) {
    homingMode_ = on;
    if (singleAxis_->isChecked() != on) {
        singleAxis_->setChecked(on);  // re-enters with the same value
        return;
    }
    refresh();
}

void PositionPanel::enterWorkPosition(int axis, const QString& text) {
    const QString trimmed = text.trimmed();
    const double value = js::stringToNumber(trimmed.toStdString());
    // Deviation: an empty or invalid entry is ignored (upstream sent 0).
    if (trimmed.isEmpty() || !std::isfinite(value) || !machine_.canMove()) {
        refresh();
        return;
    }
    machine_.setWorkPosition(rowAxis(axis), value);
}

bool PositionPanel::eventFilter(QObject* watched, QEvent* event) {
    for (QLineEdit* edit : work_) {
        if (watched != edit) {
            continue;
        }
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            edit->clearFocus();
            return true;
        }
        if (event->type() == QEvent::FocusIn) {
            QTimer::singleShot(0, edit, &QLineEdit::selectAll);
        } else if (event->type() == QEvent::FocusOut) {
            // Show the position again once the focus has gone.
            QTimer::singleShot(0, this, &PositionPanel::refresh);
        }
    }
    return QWidget::eventFilter(watched, event);
}

char PositionPanel::rowAxis(int row) const {
    return row == 3 && machine_.rotaryMode() ? 'Y' : kAxes[row];
}

void PositionPanel::axisClicked(int axis) {
    const char letter = rowAxis(axis);
    if (homingMode_) {
        machine_.homeAxis(letter);
        return;
    }
    if (machine_.settings().warnZero &&
        !confirmZero(tr("Zero %1 Axis").arg(letter), tr("Are you sure you want to zero the %1 axis?").arg(letter))) {
        return;
    }
    machine_.zeroAxis(letter);
}

void PositionPanel::zeroAll() {
    if (machine_.settings().warnZero &&
        !confirmZero(tr("Zero All Axes"), tr("Are you sure you want to zero all axes?"))) {
        return;
    }
    machine_.zeroAllAxes();
}

bool PositionPanel::confirmZero(const QString& title, const QString& question) {
    QMessageBox box(QMessageBox::Question, title, question, QMessageBox::NoButton, this);
    QPushButton* proceed = box.addButton(tr("Continue"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == proceed;
}

void PositionPanel::openGoTo() {
    if (!goToDialog_) {
        goToDialog_ = new GoToDialog(machine_, this);
    }
    goToDialog_->setMode(goToDialog_->mode());  // filled afresh on opening, as upstream
    goToDialog_->show();
    goToDialog_->raise();
    goToDialog_->activateWindow();
}

void PositionPanel::refresh() {
    controller::Controller* c = machine_.controller();
    const bool connected = c != nullptr;
    const bool canMove = machine_.canMove();
    const bool running = c && c->workflow().isRunning();
    const std::string state = c ? c->state().status.activeState : std::string();
    units_->setText(machine_.settings().metric ? tr("Units: mm") : tr("Units: in"));

    // The work coordinate system (not while running).
    workspace_->setEnabled(connected && state != "Run" && !running);
    if (c) {
        const std::string& wcs = c->runner().modal().wcs;
        for (int i = 0; i < 6; ++i) {
            if (wcs == kWorkspaces[i].code && workspace_->currentIndex() != i) {
                workspace_->setCurrentIndex(i);
            }
        }
    }

    // Corners and park: with homing enabled, once homed.
    const bool homing = connected && machine_.homingEnabled();
    const bool homed = c && c->hasHomed();
    goTo_->setEnabled(canMove);
    for (QToolButton* corner : corners_) {
        corner->setVisible(homing);
        corner->setEnabled(canMove && homed);
    }
    park_->setVisible(homing);
    park_->setEnabled(canMove && homed);
    home_->setVisible(homing);
    home_->setEnabled(canMove);  // in an alarm, the status area's button homes
    const bool single = homing && machine_.singleAxisHoming();
    singleAxis_->setVisible(single);
    singleAxis_->setEnabled(canMove);
    if (!single && homingMode_) {
        setHomingMode(false);
        return;
    }

    const std::array<double, 4> wpos = machine_.workPositionMm();
    const std::array<double, 4> mpos = machine_.machinePositionMm();
    const bool rotary = machine_.rotaryMode();
    for (int i = 0; i < 4; ++i) {
        const char axis = kAxes[i];
        // In rotary mode the rotary drives Y: the A row shows and moves it
        // (in degrees), the Y row is out of use.
        const bool unused = rotary && i == 1;
        axisButton_[i]->setText(homingMode_ ? QString("H%1").arg(axis) : QString("%1%2").arg(axis).arg(0));
        axisButton_[i]->setToolTip(homingMode_ ? tr("Home your %1-axis").arg(axis) : tr("Zero your %1-axis").arg(axis));
        axisButton_[i]->setEnabled(canMove && !unused);
        work_[i]->setEnabled(canMove && !unused);
        goZero_[i]->setEnabled(canMove && !unused);
        const auto index = static_cast<std::size_t>(rotary && i == 3 ? 1 : i);
        const bool shown = connected && !unused;
        if (!work_[i]->hasFocus()) {
            work_[i]->setText(shown ? positionText(machine_, i, wpos[index]) : QStringLiteral("-"));
        }
        machinePos_[i]->setText(shown ? positionText(machine_, i, mpos[index]) : QStringLiteral("-"));
    }
    const bool showA = c && (rotary || c->state().status.mpos.count >= 4 ||
                             c->state().axes.letters.find('A') != std::string::npos);
    for (QWidget* widget : rowA_) {
        widget->setVisible(showA);
    }
    zeroAll_->setEnabled(canMove);
    goXY_->setEnabled(canMove);
    unlock_->setEnabled(connected);
    reset_->setEnabled(connected);
}

}  // namespace gs::app
