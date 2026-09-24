#include "status_area.hpp"

#include "machine.hpp"

#include "gs/controller/actions.hpp"
#include "gs/util/strings.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <iterator>

namespace gs::app {
namespace {

// The status pill's colours (upstream's Tailwind classes).
QString stateColor(const std::string& state) {
    if (state == "Idle") {
        return "#6b7280";  // gray-500
    }
    if (state == "Run" || state == "Jog" || state == "Check") {
        return "#16a34a";  // green-600
    }
    if (state == "Home") {
        return "#3b82f6";  // blue-500
    }
    if (state == "Hold" || state == "Door") {
        return "#ca8a04";  // yellow-600
    }
    if (state == "Alarm") {
        return "#ef4444";  // red-500
    }
    if (state == "Tool") {
        return "#9333ea";  // purple-600
    }
    return "#1f2937";  // gray-800: disconnected (and Sleep)
}

// A padlock, drawn: upstream shows an icon font's lock.
QIcon lockIcon(const QColor& color) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 3));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(9, 4, 14, 16), 0, 180 * 16);  // the shackle
    p.drawLine(QPointF(9, 12), QPointF(9, 15));
    p.drawLine(QPointF(23, 12), QPointF(23, 15));
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(6, 14, 20, 15), 3, 3);
    return QIcon(pixmap);
}

// MachineInfoDisplay's pins: label and the pin state letter.
struct Pin {
    const char* label;
    char letter;
};
constexpr Pin kPins[] = {
    {QT_TRANSLATE_NOOP("MachineInfoDialog", "X limit"), 'X'},
    {QT_TRANSLATE_NOOP("MachineInfoDialog", "Y limit"), 'Y'},
    {QT_TRANSLATE_NOOP("MachineInfoDialog", "Z limit"), 'Z'},
    {QT_TRANSLATE_NOOP("MachineInfoDialog", "A limit"), 'A'},
    {QT_TRANSLATE_NOOP("MachineInfoDialog", "Probe/TLS"), 'P'},
    {QT_TRANSLATE_NOOP("MachineInfoDialog", "Door"), 'D'},
    {QT_TRANSLATE_NOOP("MachineInfoDialog", "Cycle start"), 'S'},
    {QT_TRANSLATE_NOOP("MachineInfoDialog", "Hold"), 'H'},
    {QT_TRANSLATE_NOOP("MachineInfoDialog", "Soft reset"), 'R'},
};

const char* const kModals[] = {
    QT_TRANSLATE_NOOP("MachineInfoDialog", "Probe style"),
    QT_TRANSLATE_NOOP("MachineInfoDialog", "Coordinate system"),
    QT_TRANSLATE_NOOP("MachineInfoDialog", "Plane selection"),
    QT_TRANSLATE_NOOP("MachineInfoDialog", "Units"),
    QT_TRANSLATE_NOOP("MachineInfoDialog", "Distance mode"),
    QT_TRANSLATE_NOOP("MachineInfoDialog", "Feed"),
    QT_TRANSLATE_NOOP("MachineInfoDialog", "Spindle"),
    QT_TRANSLATE_NOOP("MachineInfoDialog", "Coolant"),
};

}  // namespace

// ---- Machine Information ---------------------------------------------------------------

MachineInfoDialog::MachineInfoDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Machine Information"));
    auto* layout = new QVBoxLayout(this);
    firmware_ = new QLabel;
    layout->addWidget(firmware_);
    auto* columns = new QHBoxLayout;
    auto* modalBox = new QGroupBox(tr("CNC Modals"));
    auto* modalForm = new QFormLayout(modalBox);
    for (const char* label : kModals) {
        auto* value = new QLabel("-");
        modalForm->addRow(tr(label), value);
        rows_.append({tr(label), value});
    }
    auto* pinBox = new QGroupBox(tr("Pins"));
    auto* pinForm = new QFormLayout(pinBox);
    for (const Pin& pin : kPins) {
        auto* value = new QLabel;
        value->setAlignment(Qt::AlignCenter);
        value->setMinimumWidth(36);
        pinForm->addRow(tr(pin.label), value);
        rows_.append({tr(pin.label), value});
    }
    columns->addWidget(modalBox, 3);
    columns->addWidget(pinBox, 2);
    layout->addLayout(columns);
    tool_ = new QLabel;
    layout->addWidget(tool_);
    stepperLock_ = new QCheckBox(tr("Lock stepper motors"));
    stepperLock_->setToolTip(tr("Keep the motors powered between moves ($1=255); unlocking restores the idle delay"));
    connect(stepperLock_, &QCheckBox::clicked, this, [this](bool on) { machine_.setStepperLock(on); });
    layout->addWidget(stepperLock_);
    connect(&machine_, &Machine::stateChanged, this, &MachineInfoDialog::refresh);
    connect(&machine_, &Machine::settingsChanged, this, &MachineInfoDialog::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &MachineInfoDialog::refresh);
    refresh();
}

QString MachineInfoDialog::row(const QString& label) const {
    for (const auto& [name, value] : rows_) {
        if (name == label) {
            return value->text();
        }
    }
    return {};
}

void MachineInfoDialog::refresh() {
    const controller::Controller* c = machine_.controller();
    const std::string version = c ? std::string(str::trim(c->settings().version)) : std::string();
    firmware_->setText(tr("<b>Firmware version:</b> %1")
                           .arg(version.empty() ? tr("disconnected") : QString::fromStdString(version)));
    const protocol::ModalState* modal = c ? &c->runner().modal() : nullptr;
    std::string coolant;
    if (modal) {
        for (const std::string& code : modal->coolant) {
            coolant += (coolant.empty() ? "" : " ") + code;
        }
    }
    // The port's probe routines always use G38.2 (upstream's probeCommand).
    const std::string modals[] = {"G38.2",         modal ? modal->wcs : "",      modal ? modal->plane : "",
                                  modal ? modal->units : "", modal ? modal->distance : "", modal ? modal->feedrate : "",
                                  modal ? modal->spindle : "", coolant};
    for (std::size_t i = 0; i < std::size(kModals); ++i) {
        rows_[static_cast<qsizetype>(i)].second->setText(c ? QString::fromStdString(modals[i]) : QStringLiteral("-"));
    }
    const std::string pins = c ? c->state().status.pinState : std::string();
    for (std::size_t i = 0; i < std::size(kPins); ++i) {
        QLabel* value = rows_[static_cast<qsizetype>(std::size(kModals) + i)].second;
        const bool on = pins.find(kPins[i].letter) != std::string::npos;
        value->setText(on ? tr("On") : tr("Off"));
        value->setStyleSheet(QString("QLabel { background: %1; color: white; border-radius: 4px; padding: 0 6px; }")
                                 .arg(on ? "#22c55e" : "#ef4444"));
    }
    const int tool = c ? c->state().status.currentTool : -1;
    tool_->setVisible(tool >= 0);
    tool_->setText(tr("Current tool: T%1").arg(tool));
    stepperLock_->setEnabled(c != nullptr);
    stepperLock_->setChecked(machine_.stepperLocked());
}

// ---- the status overlay -----------------------------------------------------------------

StatusArea::StatusArea(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(8);
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    info_ = new QToolButton;
    info_->setText("i");
    info_->setToolTip(tr("Machine Information"));
    connect(info_, &QToolButton::clicked, this, &StatusArea::toggleMachineInfo);
    state_ = new QLabel;
    state_->setAlignment(Qt::AlignCenter);
    state_->setMinimumSize(240, 44);
    QFont big = state_->font();
    big.setPointSizeF(big.pointSizeF() * 1.6);
    state_->setFont(big);
    help_ = new QToolButton;
    help_->setObjectName("alarmHelp");
    help_->setText("?");
    help_->setToolTip(tr("What this alarm means"));
    connect(help_, &QToolButton::clicked, this, &StatusArea::showAlarmHelp);
    lock_ = new QToolButton;
    lock_->setAutoRaise(true);
    lock_->setIconSize(QSize(26, 26));
    lock_->setToolTip(tr("Unlock Machine"));
    connect(lock_, &QToolButton::clicked, this, &StatusArea::clickLockIcon);
    row->addWidget(info_);
    row->addWidget(state_);
    row->addWidget(help_);
    row->addWidget(lock_);
    layout->addLayout(row);
    alarm_ = new QPushButton;
    alarm_->setStyleSheet("QPushButton { background: #dc2626; color: white; border: 1px solid #991b1b; "
                          "border-radius: 14px; padding: 6px 16px; font-weight: 600; }");
    connect(alarm_, &QPushButton::clicked, this, &StatusArea::clickAlarmButton);
    layout->addWidget(alarm_, 0, Qt::AlignHCenter);

    parent->installEventFilter(this);
    connect(&machine_, &Machine::stateChanged, this, &StatusArea::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &StatusArea::refresh);
    refresh();
}

QString StatusArea::stateText() const {
    return state_->text();
}

bool StatusArea::alarmButtonShown() const {
    return !alarm_->isHidden();
}

QString StatusArea::alarmButtonText() const {
    return alarm_->text();
}

bool StatusArea::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        place();
    }
    return QWidget::eventFilter(watched, event);
}

void StatusArea::place() {
    adjustSize();
    if (QWidget* parent = parentWidget()) {
        move((parent->width() - width()) / 2, 0);
    }
    raise();
}

void StatusArea::refresh() {
    const controller::Controller* c = machine_.controller();
    const std::string state = c ? c->state().status.activeState : std::string();
    const std::string code = c ? c->state().status.alarmCode : std::string();
    QString text = QString::fromStdString(controller::statusLabel(state));
    if (state == "Alarm" && !code.empty()) {
        text += QString(" (%1)").arg(QString::fromStdString(code));
    }
    state_->setText(text);
    const QString sheet =
        QString("QLabel { background: %1; color: white; border-radius: 10px; padding: 4px 24px; }").arg(stateColor(state));
    if (state_->styleSheet() != sheet) {
        state_->setStyleSheet(sheet);
    }
    help_->setVisible(state == "Alarm");
    const bool active = state == "Hold" || state == "Alarm";
    lock_->setIcon(lockIcon(active ? QColor("#ca8a04") : QColor("#9ca3af")));
    lock_->setEnabled(c != nullptr);
    alarm_->setVisible(state == "Alarm");
    alarm_->setText(controller::alarmButtonHomes(state, code) ? tr("Click to Run Homing")
                                                              : tr("Click to Unlock Machine"));
    place();
}

void StatusArea::clickAlarmButton() {
    const controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    const auto& status = c->state().status;
    act(static_cast<int>(controller::alarmButtonAction(status.activeState, status.alarmCode)), false);
}

void StatusArea::clickLockIcon() {
    const controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    const auto& status = c->state().status;
    act(static_cast<int>(controller::lockIconAction(status.activeState, status.alarmCode)),
        controller::lockIconRepopulates(status.activeState, status.alarmCode));
}

void StatusArea::act(int actionValue, bool repopulate) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    auto action = static_cast<controller::UnlockAction>(actionValue);
    if (action == controller::UnlockAction::ConfirmHomingFailure) {
        switch (askHomingFailure(QString::fromStdString(c->state().status.alarmCode))) {
            case HomingFailureChoice::Rehome: action = controller::UnlockAction::Home; break;
            case HomingFailureChoice::UnlockAnyway: action = controller::UnlockAction::Unlock; break;
            case HomingFailureChoice::Cancel: return;
        }
    }
    controller::runUnlockAction(*c, action);
    if (repopulate) {
        c->populateConfig();
    }
}

StatusArea::HomingFailureChoice StatusArea::askHomingFailure(const QString& code) {
    if (chooser_) {
        return chooser_(code);
    }
    // confirmUnlockAfterHomingFailure(). Deviation: closing the question
    // does nothing; upstream's dialog unlocked on any close.
    QString text = tr("The last homing cycle failed, so the machine position is unknown. Re-home the machine before "
                      "continuing. Unlocking without re-homing may let jogging or a job run past the limit switches.");
    if (controller::isLimitSwitchFaultAlarm(code.toStdString())) {
        text += "\n\n" + tr("ALARM:8 and ALARM:9 mean a limit switch was not found or would not release, so "
                            "re-homing will keep failing until the switch or its wiring is fixed. To use the machine "
                            "without homing until then: choose Unlock Anyway, open Machine > Firmware Settings, turn "
                            "off \"Homing cycle enable\" ($22) and apply.");
    }
    QMessageBox box(QMessageBox::Warning, tr("Homing Not Complete"), text, QMessageBox::NoButton, window());
    QPushButton* rehome = box.addButton(tr("Rehome"), QMessageBox::AcceptRole);
    QPushButton* unlock = box.addButton(tr("Unlock Anyway"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(rehome);
    box.exec();
    if (box.clickedButton() == rehome) {
        return HomingFailureChoice::Rehome;
    }
    return box.clickedButton() == unlock ? HomingFailureChoice::UnlockAnyway : HomingFailureChoice::Cancel;
}

void StatusArea::showAlarmHelp() {
    const controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    const std::string& code = c->state().status.alarmCode;
    Q_EMIT alarmHelpRequested(tr("Alarm Code %1").arg(QString::fromStdString(code)),
                              machine_.alarmDescription(code).toHtmlEscaped(),
                              "https://resources.sienci.com/view/gs-gsender-grbl-alarm-error-codes/#alarms");
}

void StatusArea::toggleMachineInfo() {
    if (!infoDialog_) {
        infoDialog_ = new MachineInfoDialog(machine_, window());
    }
    if (infoDialog_->isVisible()) {
        infoDialog_->hide();
        return;
    }
    infoDialog_->show();
    infoDialog_->raise();
}

}  // namespace gs::app
