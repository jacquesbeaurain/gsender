#include "probe_panel.hpp"

#include "machine.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace gs::app {
namespace {

constexpr probe::PlateType kPlates[] = {probe::PlateType::StandardBlock, probe::PlateType::AutoZero,
                                        probe::PlateType::ZProbe, probe::PlateType::Probe3D,
                                        probe::PlateType::BitZero};

QString cornerName(int corner) {
    switch (corner) {
        case probe::kTopLeft: return QObject::tr("Top left");
        case probe::kTopRight: return QObject::tr("Top right");
        case probe::kBottomRight: return QObject::tr("Bottom right");
        default: return QObject::tr("Bottom left");
    }
}

// The Probe button's canClick(): connected, not running a job, idle.
bool canProbe(Machine& machine) {
    controller::Controller* c = machine.controller();
    return c && !c->workflow().isRunning() && c->state().status.activeState == "Idle";
}

}  // namespace

// ---- corner view -------------------------------------------------------------------------

CornerView::CornerView(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click to move the plate to the next corner"));
    setMinimumSize(96, 96);
}

void CornerView::setCorner(int corner) {
    corner = std::clamp(corner, 0, 3);
    if (corner != corner_) {
        corner_ = corner;
        update();
        Q_EMIT cornerChanged(corner_);
    }
}

void CornerView::setPlateType(probe::PlateType type) {
    plate_ = type;
    update();
}

void CornerView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        setCorner(probe::nextCorner(corner_));
    }
}

void CornerView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double s = std::min(width(), height());
    const QPointF origin((width() - s) / 2, (height() - s) / 2);
    const auto at = [&](double x, double y) { return origin + QPointF(x * s, y * s); };

    // The stock, seen from above; screen y grows downwards.
    const QRectF stock(at(0.22, 0.22), at(0.78, 0.78));
    p.setPen(QPen(QColor(0x80, 0x80, 0x80), 1));
    p.setBrush(QColor(0xd9, 0xc8, 0xa9));
    p.drawRect(stock);

    const bool left = corner_ == probe::kBottomLeft || corner_ == probe::kTopLeft;
    const bool bottom = corner_ == probe::kBottomLeft || corner_ == probe::kBottomRight;
    const double cx = left ? 0.22 : 0.78;
    const double cy = bottom ? 0.78 : 0.22;
    const double inX = left ? 1 : -1;  // towards the stock
    const double inY = bottom ? -1 : 1;
    const QColor plate(0xe0, 0x8a, 0x2c);
    p.setPen(QPen(plate.darker(140), 1));
    p.setBrush(plate);

    QPointF bit = at(cx + 0.04 * inX, cy + 0.04 * inY);
    switch (plate_) {
        case probe::PlateType::ZProbe:
            p.drawEllipse(at(0.5, 0.5), 0.12 * s, 0.12 * s);
            bit = at(0.5, 0.5);
            break;
        case probe::PlateType::Probe3D:
            bit = at(cx + 0.05 * inX, cy + 0.05 * inY);
            break;
        case probe::PlateType::BitZero: {
            const QRectF body(at(cx - 0.12, cy - 0.12), at(cx + 0.12, cy + 0.12));
            p.drawRect(body);
            p.setBrush(palette().window());
            p.drawEllipse(at(cx, cy), 0.045 * s, 0.045 * s);
            bit = at(cx, cy);
            break;
        }
        case probe::PlateType::AutoZero:
        case probe::PlateType::StandardBlock: {
            const QRectF body = QRectF(at(cx - 0.06 * inX, cy - 0.06 * inY), at(cx + 0.2 * inX, cy + 0.2 * inY)).normalized();
            p.drawRect(body);
            if (plate_ == probe::PlateType::AutoZero) {
                p.setBrush(palette().window());
                p.drawEllipse(body.center(), 0.06 * s, 0.06 * s);
                bit = body.center();
            }
            break;
        }
    }
    p.setPen(QPen(QColor(0x30, 0x30, 0x30), 1.5));
    p.setBrush(QColor(0x9a, 0xa4, 0xb0));
    p.drawEllipse(bit, 0.035 * s, 0.035 * s);
}

// ---- panel ----------------------------------------------------------------------------------

ProbePanel::ProbePanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QHBoxLayout(this);
    auto* form = new QFormLayout;
    plate_ = new QComboBox;
    for (const probe::PlateType type : kPlates) {
        plate_->addItem(QString::fromUtf8(probe::plateTypeName(type).data()));
    }
    form->addRow(tr("Plate"), plate_);

    auto* commands = new QWidget;
    commandRow_ = new QHBoxLayout(commands);
    commandRow_->setContentsMargins(0, 0, 0, 0);
    commandRow_->setSpacing(2);
    commandButtons_ = new QButtonGroup(this);
    commandButtons_->setExclusive(true);
    form->addRow(tr("Routine"), commands);

    toolLabel_ = new QLabel(tr("Tool"));
    tool_ = new QComboBox;
    tool_->setEditable(true);
    tool_->setToolTip(tr("Bit diameter in mm; AutoZero can also find it (Auto) or probe with a V-bit tip (Tip)"));
    form->addRow(toolLabel_, tool_);

    cornerLabel_ = new QLabel;
    form->addRow(tr("Corner"), cornerLabel_);
    probe_ = new QPushButton(tr("Probe"));
    probe_->setMinimumHeight(32);
    form->addRow(probe_);
    layout->addLayout(form, 1);

    cornerView_ = new CornerView;
    layout->addWidget(cornerView_);

    connect(plate_, &QComboBox::activated, this, [this] { storePlateAndCorner(); });
    connect(cornerView_, &CornerView::cornerChanged, this, [this] { storePlateAndCorner(); });
    connect(commandButtons_, &QButtonGroup::idClicked, this, &ProbePanel::selectCommand);
    connect(probe_, &QPushButton::clicked, this, [this] { openRunDialog(); });
    connect(&machine_, &Machine::appSettingsChanged, this, &ProbePanel::settingsChanged);
    for (auto signal : {&Machine::stateChanged, &Machine::connectionChanged, &Machine::workflowChanged}) {
        connect(&machine_, signal, this, &ProbePanel::refresh);
    }
    settingsChanged();
}

void ProbePanel::settingsChanged() {
    const probe::ProbeSettings& settings = machine_.settings().probe;
    const auto index = std::find(std::begin(kPlates), std::end(kPlates), settings.plateType) - std::begin(kPlates);
    plate_->setCurrentIndex(static_cast<int>(index));
    cornerView_->setPlateType(settings.plateType);
    {
        const QSignalBlocker block(cornerView_);
        cornerView_->setCorner(settings.direction);
    }
    rebuildCommands();
    rebuildTools();
    refresh();
}

void ProbePanel::storePlateAndCorner() {
    AppSettings settings = machine_.settings();
    settings.probe.plateType = kPlates[std::clamp(plate_->currentIndex(), 0, 4)];
    settings.probe.direction = cornerView_->corner();
    if (settings.probe.plateType != machine_.settings().probe.plateType ||
        settings.probe.direction != machine_.settings().probe.direction) {
        machine_.setSettings(settings);  // comes back through settingsChanged()
    }
}

void ProbePanel::rebuildCommands() {
    const std::string current = available_.empty() ? std::string() : command().id;
    available_ = probe::probeCommands(machine_.settings().probe.plateType);
    for (QAbstractButton* button : commandButtons_->buttons()) {
        commandButtons_->removeButton(button);
        delete button;
    }
    selected_ = 0;
    for (std::size_t i = 0; i < available_.size(); ++i) {
        const QString id = QString::fromStdString(available_[i].id);
        auto* button = new QPushButton(id.section(' ', 0, 0));
        button->setCheckable(true);
        button->setToolTip(tr("Probe using %1").arg(id));
        button->setMinimumWidth(40);
        commandButtons_->addButton(button, static_cast<int>(i));
        commandRow_->addWidget(button);
        if (available_[i].id == current) {
            selected_ = static_cast<int>(i);
        }
    }
    commandButtons_->button(selected_)->setChecked(true);
}

void ProbePanel::rebuildTools() {
    const QString current = tool_->currentText();
    tool_->clear();
    if (machine_.settings().probe.plateType == probe::PlateType::AutoZero) {
        tool_->addItems({probe::probeTypeName(probe::ProbeType::Auto).data(),
                         probe::probeTypeName(probe::ProbeType::Tip).data()});
    }
    for (const probe::ToolDiameter& tool : probe::defaultTools()) {
        tool_->addItem(QString::number(tool.metric) + " mm");
    }
    const int keep = tool_->findText(current);
    tool_->setCurrentIndex(keep >= 0 ? keep : 0);
}

void ProbePanel::selectCommand(int index) {
    if (index < 0 || index >= static_cast<int>(available_.size())) {
        return;
    }
    selected_ = index;
    commandButtons_->button(index)->setChecked(true);
    refresh();
}

void ProbePanel::stepCommand(int delta) {
    const int count = static_cast<int>(available_.size());
    selectCommand(((selected_ + delta) % count + count) % count);
}

probe::ProbeType ProbePanel::probeType() const {
    if (machine_.settings().probe.plateType == probe::PlateType::AutoZero) {
        if (const auto type = probe::probeTypeFromName(tool_->currentText().trimmed().toStdString())) {
            return *type;
        }
    }
    return probe::ProbeType::Diameter;
}

double ProbePanel::toolDiameter() const {
    if (probeType() != probe::ProbeType::Diameter) {
        return 0;
    }
    QString text = tool_->currentText().trimmed();
    text.remove("mm").remove(' ');
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok && value > 0 ? value : 0;
}

int ProbePanel::corner() const {
    return cornerView_->corner();
}

void ProbePanel::refresh() {
    const bool needsTool = command().needsTool;
    toolLabel_->setVisible(needsTool);
    tool_->setVisible(needsTool);
    cornerLabel_->setText(cornerName(corner()));
    probe_->setEnabled(canProbe(machine_));
}

RunProbeDialog* ProbePanel::openRunDialog() {
    if (!canProbe(machine_)) {
        return nullptr;
    }
    if (command().needsTool && probeType() == probe::ProbeType::Diameter && toolDiameter() <= 0) {
        tool_->setFocus();
        return nullptr;
    }
    auto* dialog = new RunProbeDialog(machine_, command(), probeType(), toolDiameter(), corner(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    return dialog;
}

// ---- run dialog -----------------------------------------------------------------------------

RunProbeDialog::RunProbeDialog(Machine& machine, probe::ProbeCommand command, probe::ProbeType type,
                               double toolDiameter, int corner, QWidget* parent)
    : QDialog(parent),
      machine_(machine),
      command_(std::move(command)),
      type_(type),
      toolDiameter_(toolDiameter),
      corner_(corner) {
    setWindowTitle(tr("Probe - %1").arg(QString::fromStdString(command_.id)));
    const bool is3D = machine_.settings().probe.plateType == probe::PlateType::Probe3D;
    auto* layout = new QVBoxLayout(this);
    QStringList steps{tr("1. Check the tool is positioned correctly: over the plate, %1 corner.")
                          .arg(cornerName(corner_).toLower())};
    steps << (is3D ? tr("2. Gently push the probe needle to check the circuit is triggered properly "
                        "(the light turns green).")
                   : tr("2. Lift your touch plate to the tool to check the circuit is good (the light turns "
                        "green), then put it back where it was."));
    if (!is3D) {
        steps << tr("3. In some cases, holding the touch plate still while probing gives a more consistent "
                    "measurement.");
    }
    if (machine_.isSimulated()) {
        // The operator's part: the simulated plate goes under the bit.
        machine_.placeSimulatedPlate(type_, toolDiameter_, corner_);
        steps << tr("Simulator: a touch plate has been placed under the bit. Confirm the circuit by hand.");
    }
    auto* text = new QLabel(steps.join("\n\n"));
    text->setWordWrap(true);
    text->setMinimumWidth(420);
    layout->addWidget(text);

    auto* circuit = new QHBoxLayout;
    light_ = new QLabel;
    light_->setFixedSize(14, 14);
    circuit_ = new QLabel;
    circuit->addWidget(light_);
    circuit->addWidget(circuit_, 1);
    layout->addLayout(circuit);

    auto* buttons = new QDialogButtonBox;
    start_ = buttons->addButton(tr("Start Probe"), QDialogButtonBox::AcceptRole);
    confirm_ = buttons->addButton(tr("Confirm manually"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(start_, &QPushButton::clicked, this, [this] { start(); });
    connect(confirm_, &QPushButton::clicked, this, &RunProbeDialog::confirmCircuit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(&machine_, &Machine::stateChanged, this, &RunProbeDialog::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &RunProbeDialog::refresh);

    confirmed_ = !machine_.settings().probe.connectivityTest;
    refresh();
}

void RunProbeDialog::confirmCircuit() {
    confirmed_ = true;
    refresh();
}

void RunProbeDialog::refresh() {
    const bool triggered = machine_.probeTriggered();
    confirmed_ = confirmed_ || triggered;
    light_->setStyleSheet(QString("border-radius:7px; background:%1; border:1px solid #666")
                              .arg(triggered ? "#2fb344" : (confirmed_ ? "#8fd19e" : "#bbbbbb")));
    circuit_->setText(triggered ? tr("Probe circuit closed")
                                : (confirmed_ ? tr("Probe circuit checked") : tr("Probe circuit open")));
    confirm_->setEnabled(!confirmed_);
    start_->setEnabled(confirmed_ && canProbe(machine_));
    start_->setText(confirmed_ ? tr("Start Probe") : tr("Waiting for probe circuit check..."));
}

bool RunProbeDialog::start() {
    if (!confirmed_) {
        return false;
    }
    std::vector<std::string> code = machine_.probeRoutine(command_.axes, type_, toolDiameter_, corner_);
    if (!machine_.runProbe(std::move(code))) {
        return false;
    }
    Q_EMIT machine_.notice(tr("Initiated probing cycle"));
    accept();
    return true;
}

}  // namespace gs::app
