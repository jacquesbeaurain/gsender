#include "surfacing_dialog.hpp"

#include "machine.hpp"
#include "toolpath_view.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace gs::app {
namespace {

constexpr surfacing::StartPosition kStarts[] = {
    surfacing::StartPosition::BackLeft, surfacing::StartPosition::BackRight, surfacing::StartPosition::FrontLeft,
    surfacing::StartPosition::FrontRight, surfacing::StartPosition::Center};

QDoubleSpinBox* spin(double min, double max, int decimals, const QString& suffix) {
    auto* box = new QDoubleSpinBox;
    box->setRange(min, max);
    box->setDecimals(decimals);
    box->setSuffix(suffix);
    return box;
}

// Upstream disables the tool unless the machine is idle or jogging (or not
// reporting at all).
bool machineFree(Machine& machine) {
    controller::Controller* c = machine.controller();
    if (!c || c->state().status.activeState.empty()) {
        return true;
    }
    const std::string& state = c->state().status.activeState;
    return state == "Idle" || state == "Jog";
}

}  // namespace

SurfacingDialog::SurfacingDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Surfacing"));
    resize(900, 560);
    auto* layout = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout;
    layout->addLayout(columns, 1);

    auto* form = new QFormLayout;
    columns->addLayout(form);
    width_ = spin(1, 10000, 2, " mm");
    length_ = spin(1, 10000, 2, " mm");
    form->addRow(tr("X (width)"), width_);
    form->addRow(tr("Y (length)"), length_);
    skimDepth_ = spin(0.01, 100, 2, " mm");
    maxDepth_ = spin(0.01, 100, 2, " mm");
    form->addRow(tr("Cut depth"), skimDepth_);
    form->addRow(tr("Max depth"), maxDepth_);
    depthWarning_ = new QLabel;
    depthWarning_->setStyleSheet("color:#c62828");
    depthWarning_->setWordWrap(true);
    form->addRow(depthWarning_);
    bitDiameter_ = spin(0.1, 100, 3, " mm");
    toolNumber_ = new QSpinBox;
    toolNumber_->setRange(0, 999);
    toolNumber_->setSpecialValueText(tr("None"));
    toolNumber_->setToolTip(tr("When set, M6 T<n> runs before the spindle starts"));
    form->addRow(tr("Bit diameter"), bitDiameter_);
    form->addRow(tr("Tool number"), toolNumber_);
    stepover_ = spin(1, 100, 0, " %");
    stepover_->setToolTip(tr("Of the bit diameter; at most 80 % is used"));
    form->addRow(tr("Stepover"), stepover_);
    feedrate_ = spin(1, 50000, 0, " mm/min");
    form->addRow(tr("Feed rate"), feedrate_);
    auto* spindleRow = new QHBoxLayout;
    spindleRPM_ = spin(0, 100000, 0, " RPM");
    spindle_ = new QComboBox;
    spindle_->addItems({"M3", "M4"});
    spindle_->setToolTip(tr("M3 clockwise, M4 counter-clockwise"));
    dwell_ = new QCheckBox(tr("Delay"));
    dwell_->setToolTip(tr("Wait 4 s after starting and stopping the spindle"));
    spindleRow->addWidget(spindleRPM_, 1);
    spindleRow->addWidget(spindle_);
    spindleRow->addWidget(dwell_);
    form->addRow(tr("Spindle"), spindleRow);
    auto* coolantRow = new QHBoxLayout;
    mist_ = new QCheckBox(tr("Mist (M7)"));
    flood_ = new QCheckBox(tr("Flood (M8)"));
    coolantRow->addWidget(mist_);
    coolantRow->addWidget(flood_);
    coolantRow->addStretch();
    form->addRow(tr("Coolant"), coolantRow);
    start_ = new QComboBox;
    start_->addItems({tr("Back left"), tr("Back right"), tr("Front left"), tr("Front right"), tr("Centre")});
    start_->setToolTip(tr("Where X0 Y0 is on the stock"));
    form->addRow(tr("Start position"), start_);
    pattern_ = new QComboBox;
    pattern_->addItems({tr("Spiral"), tr("Zig-zag")});
    form->addRow(tr("Pattern"), pattern_);
    flipped_ = new QCheckBox(tr("Flip the cut direction"));
    form->addRow(QString(), flipped_);

    views_ = new QTabWidget;
    preview_ = new ToolpathPreview;
    gcode_ = new QPlainTextEdit;
    gcode_->setReadOnly(true);
    gcode_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    gcode_->setLineWrapMode(QPlainTextEdit::NoWrap);
    views_->addTab(preview_, tr("Preview"));
    views_->addTab(gcode_, tr("G-code"));
    columns->addWidget(views_, 1);

    auto* buttons = new QDialogButtonBox;
    generate_ = buttons->addButton(tr("Generate G-code"), QDialogButtonBox::ActionRole);
    load_ = buttons->addButton(tr("Load as Job"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(generate_, &QPushButton::clicked, this, &SurfacingDialog::generate);
    connect(load_, &QPushButton::clicked, this, [this] {
        if (loadIntoMachine()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QDoubleSpinBox* box : {skimDepth_, maxDepth_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, &SurfacingDialog::refresh);
    }
    connect(&machine_, &Machine::stateChanged, this, &SurfacingDialog::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &SurfacingDialog::refresh);

    setOptions(machine_.settings().surfacing);
    refresh();
}

surfacing::Options SurfacingDialog::options() const {
    surfacing::Options o;
    o.width = width_->value();
    o.length = length_->value();
    o.skimDepth = skimDepth_->value();
    o.maxDepth = maxDepth_->value();
    o.bitDiameter = bitDiameter_->value();
    o.toolNumber = toolNumber_->value();
    o.stepover = stepover_->value();
    o.feedrate = feedrate_->value();
    o.spindleRPM = spindleRPM_->value();
    o.spindle = spindle_->currentText().toStdString();
    o.shouldDwell = dwell_->isChecked();
    o.mist = mist_->isChecked();
    o.flood = flood_->isChecked();
    o.startPosition = kStarts[std::clamp(start_->currentIndex(), 0, 4)];
    o.type = pattern_->currentIndex() == 1 ? surfacing::Pattern::ZigZag : surfacing::Pattern::Spiral;
    o.cutDirectionFlipped = flipped_->isChecked();
    return o;
}

void SurfacingDialog::setOptions(const surfacing::Options& o) {
    width_->setValue(o.width);
    length_->setValue(o.length);
    skimDepth_->setValue(o.skimDepth);
    maxDepth_->setValue(o.maxDepth);
    bitDiameter_->setValue(o.bitDiameter);
    toolNumber_->setValue(o.toolNumber);
    stepover_->setValue(o.stepover);
    feedrate_->setValue(o.feedrate);
    spindleRPM_->setValue(o.spindleRPM);
    spindle_->setCurrentIndex(o.spindle == "M4" ? 1 : 0);
    dwell_->setChecked(o.shouldDwell);
    mist_->setChecked(o.mist);
    flood_->setChecked(o.flood);
    start_->setCurrentIndex(static_cast<int>(std::find(std::begin(kStarts), std::end(kStarts), o.startPosition) -
                                             std::begin(kStarts)));
    pattern_->setCurrentIndex(o.type == surfacing::Pattern::ZigZag ? 1 : 0);
    flipped_->setChecked(o.cutDirectionFlipped);
}

void SurfacingDialog::refresh() {
    const bool tooDeep = skimDepth_->value() > maxDepth_->value();
    depthWarning_->setText(tooDeep ? tr("The cut depth (%1 mm) exceeds the max depth (%2 mm).")
                                         .arg(skimDepth_->value())
                                         .arg(maxDepth_->value())
                                   : QString());
    depthWarning_->setVisible(tooDeep);
    const bool free = machineFree(machine_);
    generate_->setEnabled(free);
    load_->setEnabled(free && !program_.isEmpty());
}

void SurfacingDialog::generate() {
    if (!machineFree(machine_)) {
        return;
    }
    save();
    const std::string text = surfacing::generate(options(), true);
    program_ = QString::fromStdString(text);
    gcode_->setPlainText(program_);
    views_->setTabText(1, tr("G-code (%1 lines)").arg(program_.count('\n') + 1));
    preview_->setToolpath(traceToolpath(text));
    refresh();
}

bool SurfacingDialog::loadIntoMachine() {
    if (program_.isEmpty() || !machineFree(machine_)) {
        return false;
    }
    machine_.loadProgram("gSender_Surfacing.gcode", program_.toStdString());
    return true;
}

void SurfacingDialog::save() {
    AppSettings settings = machine_.settings();
    settings.surfacing = options();
    machine_.setSettings(settings);
}

void SurfacingDialog::done(int result) {
    save();
    QDialog::done(result);
}

}  // namespace gs::app
