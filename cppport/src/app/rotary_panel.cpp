#include "rotary_panel.hpp"

#include "rotary_actions.hpp"

#include "machine.hpp"
#include "toolpath_view.hpp"

#include "gs/core/resources.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace gs::app {
namespace {

// The surfacing tool: disabled unless the machine is idle or jogging (or
// not reporting at all).
bool machineFree(Machine& machine) {
    controller::Controller* c = machine.controller();
    if (!c || c->state().status.activeState.empty()) {
        return true;
    }
    const std::string& state = c->state().status.activeState;
    return state == "Idle" || state == "Jog";
}

using namespace rotary_actions;

QDoubleSpinBox* spin(double min, double max, int decimals, const QString& suffix) {
    auto* box = new QDoubleSpinBox;
    box->setRange(min, max);
    box->setDecimals(decimals);
    box->setSuffix(suffix);
    return box;
}

// "<a> & <b>" on one row, as upstream pairs its inputs.
QHBoxLayout* pair(QWidget* first, QWidget* second) {
    auto* row = new QHBoxLayout;
    row->addWidget(first, 1);
    row->addWidget(new QLabel("&"));
    row->addWidget(second, 1);
    return row;
}

QPixmap image(const char* name) {
    QPixmap pixmap;
    if (const auto bytes = resources::find(std::string("images/rotary/") + name)) {
        pixmap.loadFromData(reinterpret_cast<const uchar*>(bytes->data()), static_cast<uint>(bytes->size()), "PNG");
    }
    return pixmap;
}

}  // namespace

// ---- Rotary Surfacing -------------------------------------------------------------------

RotarySurfacingDialog::RotarySurfacingDialog(Machine& machine, QWidget* parent)
    : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Rotary Surfacing"));
    resize(960, 600);
    auto* layout = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout;
    layout->addLayout(columns, 1);

    auto* left = new QVBoxLayout;
    columns->addLayout(left);
    auto* intro = new QLabel(tr("Make sure that your tool clears the surface of your material without running into "
                                "the limits of your Z-axis. You should also use the probing feature to zero your "
                                "Z-axis to the centerline before surfacing."));
    intro->setWordWrap(true);
    intro->setMaximumWidth(440);
    left->addWidget(intro);
    auto* form = new QFormLayout;
    left->addLayout(form);
    left->addStretch();

    const bool metric = machine_.settings().metric;
    const QString unit = metric ? QStringLiteral("mm") : QStringLiteral("in");
    const QString length = " " + unit;
    stockLength_ = spin(0.001, 10000, 3, length);
    form->addRow(tr("Length"), stockLength_);
    startHeight_ = spin(0.001, 10000, 3, length);
    finalHeight_ = spin(0.001, 10000, 3, length);
    form->addRow(new QLabel(tr("Start & final diameter")), pair(startHeight_, finalHeight_));
    stepdown_ = spin(0.001, 1000, 3, length);
    form->addRow(tr("Stepdown"), stepdown_);
    bitDiameter_ = spin(0.001, 100, 3, length);
    toolNumber_ = new QSpinBox;
    toolNumber_->setRange(0, 999);
    toolNumber_->setSpecialValueText(tr("None"));
    form->addRow(new QLabel(tr("Bit diameter & tool")), pair(bitDiameter_, toolNumber_));
    stepover_ = spin(1, 100, 0, " %");
    form->addRow(tr("Stepover"), stepover_);
    feedrate_ = spin(1, 50000, metric ? 0 : 2, length + "/min");
    form->addRow(tr("Feed rate"), feedrate_);
    auto* spindleRow = new QHBoxLayout;
    spindleRPM_ = spin(0, 100000, 0, " RPM");
    dwell_ = new QCheckBox(tr("Delay"));
    spindleRow->addWidget(spindleRPM_, 1);
    spindleRow->addWidget(dwell_);
    form->addRow(tr("Spindle RPM"), spindleRow);
    rehoming_ = new QCheckBox(tr("Enable rehoming"));
    form->addRow(QString(), rehoming_);
    auto* rehomingNote = new QLabel(tr("Cut faster and cleaner by only rotating one direction, but you will need to "
                                       "rehome your A-axis at the end."));
    rehomingNote->setWordWrap(true);
    rehomingNote->setStyleSheet("color:palette(mid)");
    form->addRow(QString(), rehomingNote);

    // Upstream's tooltips: the defaults, in the workspace units.
    const rotary::StockTurningOptions defaults =
        metric ? rotary::StockTurningOptions{} : rotary::toImperial(rotary::StockTurningOptions{});
    const auto tip = [](QWidget* widget, double value, const QString& suffix) {
        widget->setToolTip(QObject::tr("Default is %1%2").arg(value).arg(suffix));
    };
    tip(stockLength_, defaults.stockLength, length);
    tip(startHeight_, defaults.startHeight, length);
    tip(finalHeight_, defaults.finalHeight, length);
    tip(stepdown_, defaults.stepdown, length);
    tip(bitDiameter_, defaults.bitDiameter, length);
    tip(toolNumber_, defaults.toolNumber, QString());
    tip(stepover_, defaults.stepover, "%");
    tip(feedrate_, defaults.feedrate, length + "/min");
    tip(spindleRPM_, defaults.spindleRPM, " RPM");
    dwell_->setToolTip(tr("Default is %1").arg(defaults.shouldDwell ? tr("on") : tr("off")));
    rehoming_->setToolTip(tr("Default is %1").arg(defaults.enableRehoming ? tr("on") : tr("off")));

    views_ = new QTabWidget;
    previewPage_ = new QStackedWidget;
    auto* none = new QLabel(tr("No g-code generated yet.\nPlease generate g-code first."));
    none->setAlignment(Qt::AlignCenter);
    none->setStyleSheet("color:palette(mid)");
    preview_ = new ToolpathPreview;
    preview_->setToolTip(tr("The stock from above, the toolpath wrapped around the rotary"));
    previewPage_->addWidget(none);
    previewPage_->addWidget(preview_);
    gcode_ = new QPlainTextEdit;
    gcode_->setReadOnly(true);
    gcode_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    gcode_->setLineWrapMode(QPlainTextEdit::NoWrap);
    views_->addTab(previewPage_, tr("Visualizer Preview"));
    views_->addTab(gcode_, tr("G-Code"));
    views_->setTabEnabled(1, false);
    columns->addWidget(views_, 1);

    auto* buttons = new QDialogButtonBox;
    generate_ = buttons->addButton(tr("Generate G-Code"), QDialogButtonBox::ActionRole);
    load_ = buttons->addButton(tr("Load to Main Visualizer"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(generate_, &QPushButton::clicked, this, &RotarySurfacingDialog::generate);
    connect(load_, &QPushButton::clicked, this, [this] {
        if (loadIntoMachine()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(&machine_, &Machine::stateChanged, this, &RotarySurfacingDialog::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &RotarySurfacingDialog::refresh);

    const rotary::StockTurningOptions& stored = machine_.settings().rotary.stockTurning;
    setOptions(metric ? stored : rotary::toImperial(stored));
    refresh();
}

rotary::StockTurningOptions RotarySurfacingDialog::options() const {
    rotary::StockTurningOptions o;
    o.stockLength = stockLength_->value();
    o.startHeight = startHeight_->value();
    o.finalHeight = finalHeight_->value();
    o.stepdown = stepdown_->value();
    o.bitDiameter = bitDiameter_->value();
    o.toolNumber = toolNumber_->value();
    o.stepover = stepover_->value();
    o.feedrate = feedrate_->value();
    o.spindleRPM = spindleRPM_->value();
    o.shouldDwell = dwell_->isChecked();
    o.enableRehoming = rehoming_->isChecked();
    return o;
}

void RotarySurfacingDialog::setOptions(const rotary::StockTurningOptions& o) {
    stockLength_->setValue(o.stockLength);
    startHeight_->setValue(o.startHeight);
    finalHeight_->setValue(o.finalHeight);
    stepdown_->setValue(o.stepdown);
    bitDiameter_->setValue(o.bitDiameter);
    toolNumber_->setValue(o.toolNumber);
    stepover_->setValue(o.stepover);
    feedrate_->setValue(o.feedrate);
    spindleRPM_->setValue(o.spindleRPM);
    dwell_->setChecked(o.shouldDwell);
    rehoming_->setChecked(o.enableRehoming);
}

void RotarySurfacingDialog::refresh() {
    const bool free = machineFree(machine_);
    generate_->setEnabled(free);
    load_->setEnabled(free && !program_.isEmpty());
}

void RotarySurfacingDialog::generate() {
    if (!machineFree(machine_)) {
        return;
    }
    save();
    const std::string text =
        rotary::stockTurningProgram(options(), machine_.settings().metric, machine_.rotaryMode());
    program_ = QString::fromStdString(text);
    gcode_->setPlainText(program_);
    views_->setTabEnabled(1, true);
    views_->setTabText(1, tr("G-Code (%1 lines)").arg(program_.count('\n') + 1));
    preview_->setToolpath(traceToolpath(text));
    previewPage_->setCurrentWidget(preview_);
    refresh();
}

bool RotarySurfacingDialog::loadIntoMachine() {
    if (program_.isEmpty() || !machineFree(machine_)) {
        return false;
    }
    machine_.loadProgram("gSender_Rotary_Surfacing", program_.toStdString());
    return true;
}

void RotarySurfacingDialog::save() {
    AppSettings settings = machine_.settings();
    settings.rotary.stockTurning = settings.metric ? options() : rotary::toMetric(options());
    machine_.setSettings(settings);
}

void RotarySurfacingDialog::done(int result) {
    save();
    QDialog::done(result);
}

// ---- Mounting Setup ---------------------------------------------------------------------

MountingSetupDialog::MountingSetupDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Rotary Mounting Setup"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Make sure your router is mounted as far down as possible with the bit inserted not "
                                "too far into the collet to prevent bottoming out."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* grid = new QGridLayout;
    layout->addLayout(grid);
    int row = 0;
    const auto choice = [this, grid, &row](const QString& label, const QString& first, const QString& second) {
        auto* group = new QButtonGroup(this);
        grid->addWidget(new QLabel(label), row, 0);
        int id = 0;
        for (const QString& text : {first, second}) {
            auto* button = new QRadioButton(text);
            group->addButton(button, id);
            grid->addWidget(button, row, 1 + id);
            ++id;
        }
        connect(group, &QButtonGroup::idClicked, this, &MountingSetupDialog::refresh);
        ++row;
        return group;
    };
    // Button 0 is the first choice upstream lists.
    linesUp_ = choice(tr("Does the mounting track line up without interference?"), tr("Lines up"),
                      tr("Does not line up"));
    bit_ = choice(tr("End Mill Diameter"), QString::fromUtf8("¼\""), QString::fromUtf8("⅛\""));
    holes_ = choice(tr("Number of Holes"), "6", "10");
    extension_ = choice(tr("Extension Track Length"), "400mm", "460mm");
    grid->setColumnStretch(3, 1);  // the choices beside their questions
    grid->setHorizontalSpacing(18);

    illustration_ = new QLabel;
    illustration_->setAlignment(Qt::AlignCenter);
    illustration_->setMinimumHeight(280);
    layout->addWidget(illustration_, 1);

    auto* buttons = new QDialogButtonBox;
    QPushButton* load = buttons->addButton(tr("Load G-Code to Visualizer"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(load, &QPushButton::clicked, this, [this] {
        if (loadIntoMachine()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setSetup({});  // upstream's defaults each time it opens
}

rotary::MountingSetup MountingSetupDialog::setup() const {
    rotary::MountingSetup s;
    s.linesUp = linesUp_->checkedId() == 0;
    s.quarterInchBit = bit_->checkedId() == 0;
    s.holes = holes_->checkedId() == 1 ? 10 : 6;
    s.longExtension = extension_->checkedId() == 1;
    return s;
}

void MountingSetupDialog::setSetup(const rotary::MountingSetup& s) {
    linesUp_->button(s.linesUp ? 0 : 1)->setChecked(true);
    bit_->button(s.quarterInchBit ? 0 : 1)->setChecked(true);
    holes_->button(s.holes == 10 ? 1 : 0)->setChecked(true);
    extension_->button(s.longExtension ? 1 : 0)->setChecked(true);
    refresh();
}

void MountingSetupDialog::refresh() {
    // getIllustrationImage(): the custom boring layout unless the track
    // lines up; then the standard track or the one with the extension.
    const rotary::MountingSetup s = setup();
    const char* name = !s.linesUp       ? "custom-boring-track-top-view.png"
                       : s.holes == 10 ? "extension-track-top-view.png"
                                       : "standard-track-top-view.png";
    const QPixmap pixmap = image(name);
    illustration_->setPixmap(pixmap.isNull() ? QPixmap() : pixmap.scaledToWidth(760, Qt::SmoothTransformation));
}

bool MountingSetupDialog::loadIntoMachine() {
    controller::Controller* c = machine_.controller();
    const std::optional<std::string> program = rotary::mountingProgram(setup());
    if (!program || (c && c->workflow().isRunning())) {
        return false;
    }
    machine_.loadProgram("gSender_Rotary_Mounting_Setup", *program);
    Q_EMIT machine_.notice(tr("Loaded rotary mounting setup macro"));
    return true;
}

// ---- the panel --------------------------------------------------------------------------

RotaryPanel::RotaryPanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QVBoxLayout(this);
    auto* toggle = new QHBoxLayout;
    fourAxis_ = new QLabel(tr("4-Axis"));
    mode_ = new QCheckBox(tr("Rotary"));
    mode_->setObjectName("rotaryMode");
    toggle->addStretch();
    toggle->addWidget(fourAxis_);
    toggle->addWidget(mode_);
    toggle->addStretch();
    layout->addLayout(toggle);

    auto* grid = new QGridLayout;
    const auto button = [](const QString& text, const char* name, const QString& tip) {
        auto* b = new QPushButton(text);
        b->setObjectName(name);
        b->setToolTip(tip);
        return b;
    };
    surfacing_ = button(tr("Rotary Surfacing"), "rotarySurfacing", tr("Open rotary surfacing tool"));
    mounting_ = button(tr("Mounting Setup"), "mountingSetup", tr("Open mounting setup tool"));
    probeZ_ = button(tr("Probe Rotary Z-Axis"), "probeRotaryZ", tr("Run rotary Z-axis probing"));
    alignY_ = button(tr("Y-Axis Alignment"), "alignYAxis", tr("Run rotary Y-axis alignment"));
    grid->addWidget(surfacing_, 0, 0);
    grid->addWidget(mounting_, 0, 1);
    grid->addWidget(probeZ_, 1, 0);
    grid->addWidget(alignY_, 1, 1);
    layout->addLayout(grid);
    layout->addStretch();

    // The switch shows the mode: a declined change leaves it as it was.
    connect(mode_, &QCheckBox::clicked, this, [this](bool checked) {
        setRotaryMode(checked);
        refresh();
    });
    connect(surfacing_, &QPushButton::clicked, this, &RotaryPanel::openSurfacing);
    connect(mounting_, &QPushButton::clicked, this, &RotaryPanel::openMountingSetup);
    connect(probeZ_, &QPushButton::clicked, this, &RotaryPanel::probeRotaryZ);
    connect(alignY_, &QPushButton::clicked, this, &RotaryPanel::alignYAxis);
    connect(&machine_, &Machine::stateChanged, this, &RotaryPanel::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &RotaryPanel::refresh);
    connect(&machine_, &Machine::appSettingsChanged, this, &RotaryPanel::refresh);
    refresh();
}

void RotaryPanel::refresh() {
    const bool grblHal = !isGrbl(machine_);
    fourAxis_->setVisible(grblHal);
    mode_->setToolTip(grblHal ? tr("Enable 4-axis or Rotary mode") : tr("Toggle Rotary mode"));
    mode_->setChecked(machine_.rotaryMode());
    mode_->setEnabled(modeSwitchAvailable(machine_));
    surfacing_->setEnabled(surfacingAvailable(machine_));
    mounting_->setEnabled(mountingAvailable(machine_));
    probeZ_->setEnabled(probeZAvailable(machine_));
    alignY_->setEnabled(alignYAvailable(machine_));
}

bool RotaryPanel::confirm(const QString& title, const QString& text, const QString& confirmLabel) {
    if (confirmer_) {
        return confirmer_(title, text, confirmLabel);
    }
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, this);
    QPushButton* proceed = box.addButton(confirmLabel, QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == proceed;
}

bool RotaryPanel::setRotaryMode(bool rotaryMode) {
    controller::Controller* c = machine_.controller();
    if (!modeSwitchAvailable(machine_) || rotaryMode == machine_.rotaryMode()) {
        return false;
    }
    if (rotaryMode) {
        const QString text = enableConfirmation(c->isGrblHal());
        if (!confirm(tr("Enable Rotary Mode"), text, tr("OK"))) {
            return false;
        }
    }
    if (!machine_.setRotaryMode(rotaryMode)) {
        return false;
    }
    Q_EMIT machine_.notice(rotaryMode ? tr("Rotary Mode Enabled") : tr("Rotary Mode Disabled"));
    return true;
}

bool RotaryPanel::toggleRotaryMode() {
    const bool changed = setRotaryMode(!machine_.rotaryMode());
    refresh();
    return changed;
}

bool RotaryPanel::runProbe(bool yAlignment) {
    // runProbing(): asked, then run with gcode:safe.
    const QString name = yAlignment ? tr("Y-Axis Alignment") : tr("Rotary Z-Axis");
    if (!confirm(tr("%1 probing").arg(name), tr("Click 'Run' to start the %1 probing cycle").arg(name), tr("Run"))) {
        return false;
    }
    if (!machine_.runRotaryProbe(yAlignment)) {
        return false;
    }
    Q_EMIT machine_.notice(tr("Running %1 probing commands").arg(name));
    return true;
}

bool RotaryPanel::probeRotaryZ() {
    return probeZAvailable(machine_) && runProbe(false);
}

bool RotaryPanel::alignYAxis() {
    return alignYAvailable(machine_) && runProbe(true);
}

void RotaryPanel::openSurfacing() {
    if (!surfacingAvailable(machine_)) {
        return;
    }
    RotarySurfacingDialog dialog(machine_, this);
    dialog.exec();
}

void RotaryPanel::openMountingSetup() {
    if (!mountingAvailable(machine_)) {
        return;
    }
    MountingSetupDialog dialog(machine_, this);
    dialog.exec();
}

}  // namespace gs::app
