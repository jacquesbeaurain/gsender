#include "settings_dialog.hpp"

#include "machine.hpp"

#include "gs/protocol/firmware_data.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <charconv>
#include <optional>

namespace gs::app {
namespace {

enum Column { kSetting, kValue, kUnits, kDescription };

// "$110" -> 110 (grblHAL keys its own descriptions by number).
std::optional<int> settingNumber(const std::string& name) {
    int number = 0;
    if (name.size() < 2 || name[0] != '$') {
        return std::nullopt;
    }
    const auto [end, ec] = std::from_chars(name.data() + 1, name.data() + name.size(), number);
    return ec == std::errc() && end == name.data() + name.size() ? std::optional<int>(number) : std::nullopt;
}

}  // namespace

// ---- firmware settings -------------------------------------------------------------

FirmwareSettingsTable::FirmwareSettingsTable(Machine& machine, QWidget* parent)
    : QWidget(parent), machine_(machine) {
    auto* layout = new QVBoxLayout(this);
    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({tr("Setting"), tr("Value"), tr("Units"), tr("Description")});
    table_->horizontalHeader()->setSectionResizeMode(kDescription, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    layout->addWidget(table_, 1);
    auto* row = new QHBoxLayout;
    status_ = new QLabel;
    reload_ = new QPushButton(tr("Reload ($$)"));
    apply_ = new QPushButton(tr("Write changes"));
    row->addWidget(status_, 1);
    row->addWidget(reload_);
    row->addWidget(apply_);
    layout->addLayout(row);

    connect(reload_, &QPushButton::clicked, this, [this] {
        if (auto* c = machine_.controller()) {
            c->gcode("$$");
        }
    });
    connect(apply_, &QPushButton::clicked, this, &FirmwareSettingsTable::apply);
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (loading_ || item->column() != kValue) {
            return;
        }
        const bool changed = item->text().trimmed() != item->data(Qt::UserRole).toString();
        QFont font = item->font();
        font.setBold(changed);
        item->setFont(font);
        refreshButtons();
    });
    connect(&machine_, &Machine::settingsChanged, this, &FirmwareSettingsTable::reload);
    for (auto signal : {&Machine::connectionChanged, &Machine::workflowChanged}) {
        connect(&machine_, signal, this, &FirmwareSettingsTable::reload);
    }
    reload();
}

void FirmwareSettingsTable::reload() {
    loading_ = true;
    table_->setRowCount(0);
    controller::Controller* c = machine_.controller();
    if (c) {
        const protocol::FirmwareSettings& settings = c->settings();
        const protocol::FirmwareTables& tables = protocol::FirmwareTables::get(c->firmware());
        for (const auto& [name, value] : settings.settings.items()) {
            QString units;
            QString description;
            // grblHAL describes its own settings ($ES/$ESH); the static tables
            // fill the gaps.
            const auto number = settingNumber(name);
            const auto own = number ? settings.descriptions.find(*number) : settings.descriptions.end();
            if (own != settings.descriptions.end()) {
                units = QString::fromStdString(own->second.unit);
                description = QString::fromStdString(own->second.description);
            } else if (const protocol::SettingInfo* info = tables.setting(name)) {
                units = QString::fromStdString(info->units);
                description = QString::fromStdString(info->message);
                if (!info->description.empty() && info->description != info->message) {
                    description += " - " + QString::fromStdString(info->description);
                }
            }
            const int row = table_->rowCount();
            table_->insertRow(row);
            auto* key = new QTableWidgetItem(QString::fromStdString(name));
            key->setFlags(key->flags() & ~Qt::ItemIsEditable);
            auto* current = new QTableWidgetItem(QString::fromStdString(value));
            current->setData(Qt::UserRole, QString::fromStdString(value));
            auto* unit = new QTableWidgetItem(units);
            unit->setFlags(unit->flags() & ~Qt::ItemIsEditable);
            auto* text = new QTableWidgetItem(description);
            text->setFlags(text->flags() & ~Qt::ItemIsEditable);
            text->setToolTip(description);
            table_->setItem(row, kSetting, key);
            table_->setItem(row, kValue, current);
            table_->setItem(row, kUnits, unit);
            table_->setItem(row, kDescription, text);
        }
        table_->resizeColumnsToContents();
        table_->horizontalHeader()->setSectionResizeMode(kDescription, QHeaderView::Stretch);
    }
    loading_ = false;
    status_->setText(c ? tr("%1 settings").arg(table_->rowCount()) : tr("Connect to read the firmware settings."));
    refreshButtons();
}

void FirmwareSettingsTable::refreshButtons() {
    controller::Controller* c = machine_.controller();
    const bool idle = c && c->workflow().isIdle();
    bool changed = false;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QTableWidgetItem* item = table_->item(row, kValue);
        changed = changed || item->text().trimmed() != item->data(Qt::UserRole).toString();
    }
    reload_->setEnabled(idle);
    apply_->setEnabled(idle && changed);
    table_->setEnabled(c != nullptr);
}

void FirmwareSettingsTable::apply() {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    std::vector<std::string> commands;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QTableWidgetItem* item = table_->item(row, kValue);
        const QString value = item->text().trimmed();
        if (value != item->data(Qt::UserRole).toString()) {
            commands.push_back((table_->item(row, kSetting)->text() + "=" + value).toStdString());
        }
    }
    if (commands.empty()) {
        return;
    }
    commands.emplace_back("$$");  // read everything back
    c->gcode(commands);
}

// ---- dialog ------------------------------------------------------------------------------

SettingsDialog::SettingsDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Settings"));
    resize(860, 600);
    auto* layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget;
    layout->addWidget(tabs_, 1);

    auto* general = new QWidget;
    auto* generalForm = new QFormLayout(general);
    spindleDelay_ = new QDoubleSpinBox;
    spindleDelay_->setRange(0, 60);
    spindleDelay_->setDecimals(1);
    spindleDelay_->setSuffix(" s");
    spindleDelay_->setToolTip(tr("Dwell after each spindle start (M3/M4) in loaded jobs"));
    lineWarnings_ = new QCheckBox(tr("Report the offending line when a job line errors"));
    aAxis_ = new QCheckBox(tr("Grbl: send A-axis words as they are (no A to Y translation)"));
    firmware_ = new QComboBox;
    firmware_->addItems({"Grbl", "grblHAL"});
    firmware_->setToolTip(tr("Assumed when a connected board does not identify itself"));
    networkPort_ = new QSpinBox;
    networkPort_->setRange(1, 65535);
    units_ = new QComboBox;
    units_->addItems({tr("Millimetres (mm)"), tr("Inches (in)")});
    decimals_ = new QSpinBox;
    decimals_->setRange(0, 5);
    decimals_->setSpecialValueText(tr("Default"));
    decimals_->setToolTip(tr("Decimal places of the position display (default: 2 in mm, 3 in inches)"));
    // Machine positions, with a button to take the current one.
    const auto positionRow = [this](QDoubleSpinBox* (&boxes)[3]) {
        auto* row = new QHBoxLayout;
        for (int i = 0; i < 3; ++i) {
            boxes[i] = new QDoubleSpinBox;
            boxes[i]->setRange(-10000, 10000);
            boxes[i]->setDecimals(3);
            boxes[i]->setPrefix(QString("XYZ"[i]) + " ");
            row->addWidget(boxes[i]);
        }
        auto* here = new QPushButton(tr("Use current"));
        here->setToolTip(tr("The machine position now"));
        connect(here, &QPushButton::clicked, this, [this, &boxes] {
            if (controller::Controller* c = machine_.controller()) {
                for (std::size_t i = 0; i < 3; ++i) {
                    boxes[i]->setValue(c->runner().machinePosition()[i]);
                }
            }
        });
        row->addWidget(here);
        return row;
    };
    generalForm->addRow(tr("Units"), units_);
    generalForm->addRow(tr("Position decimals"), decimals_);
    generalForm->addRow(tr("Spindle delay"), spindleDelay_);
    generalForm->addRow(QString(), lineWarnings_);
    generalForm->addRow(QString(), aAxis_);
    generalForm->addRow(tr("Default firmware"), firmware_);
    generalForm->addRow(tr("Network port"), networkPort_);
    safeRetract_ = new QDoubleSpinBox;
    safeRetract_->setRange(0, 200);
    safeRetract_->setDecimals(2);
    safeRetract_->setSuffix(" mm");
    safeRetract_->setSpecialValueText(tr("None"));
    safeRetract_->setToolTip(tr("Z lifts this far before go-to-zero moves (machine Z with homing)"));
    generalForm->addRow(tr("Safe retract height"), safeRetract_);
    warnZero_ = new QCheckBox(tr("Warn when setting zero"));
    warnZero_->setToolTip(tr("The zero buttons ask first - useful if you tend to set zero accidentally"));
    generalForm->addRow(QString(), warnZero_);
    // The DRO's Park button (homing enabled, machine homed).
    QHBoxLayout* parkRow = positionRow(park_);
    auto* parkGo = new QPushButton(tr("Go to"));
    parkGo->setToolTip(tr("Move there now: up to the top of Z, over, then down (machine coordinates)"));
    connect(parkGo, &QPushButton::clicked, this, [this] {
        if (machine_.canMove()) {
            machine_.goToMachinePosition({park_[0]->value(), park_[1]->value(), park_[2]->value()});
        }
    });
    parkRow->addWidget(parkGo);
    generalForm->addRow(tr("Park location (mm)"), parkRow);
    outlineMode_ = new QComboBox;
    for (const job::OutlineMode mode :
         {job::OutlineMode::Detailed, job::OutlineMode::Square, job::OutlineMode::RapidlessSquare}) {
        outlineMode_->addItem(QString::fromUtf8(job::outlineModeName(mode).data()));
    }
    outlineMode_->setToolTip(tr("Detailed follows the toolpath's hull; Square its box; Rapidless Square the box "
                                "of its cutting moves"));
    outlineSpeed_ = new QDoubleSpinBox;
    outlineSpeed_->setRange(0, 20000);
    outlineSpeed_->setDecimals(0);
    outlineSpeed_->setSuffix(" mm/min");
    outlineSpeed_->setSpecialValueText(tr("Rapid (G0)"));
    generalForm->addRow(tr("Outline style"), outlineMode_);
    generalForm->addRow(tr("Outline speed"), outlineSpeed_);
    tabs_->addTab(general, tr("General"));

    auto* toolChange = new QWidget;
    auto* toolForm = new QFormLayout(toolChange);
    toolChange_ = new QComboBox;
    for (const char* option : kToolChangeOptions) {
        toolChange_->addItem(option);
    }
    toolChange_->setToolTip(tr("Ignore: comment M6 out. Pause: pause the job at M6. Standard Re-zero: a wizard "
                               "to change the bit and re-zero Z. Flexible Re-zero: a wizard measuring tools on the "
                               "touch plate. Fixed Tool Sensor: a wizard measuring tools on a sensor (needs homing). "
                               "Code: run the hooks below around the tool change."));
    passthrough_ = new QCheckBox(tr("Send M6 to the firmware (it handles tool changes)"));
    skipDialog_ = new QCheckBox(tr("Code: run both hooks without asking in between"));
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    preHook_ = new QPlainTextEdit;
    postHook_ = new QPlainTextEdit;
    for (QPlainTextEdit* hook : {preHook_, postHook_}) {
        hook->setFont(mono);
        hook->setMinimumHeight(110);
    }
    toolForm->addRow(tr("Strategy"), toolChange_);
    toolForm->addRow(QString(), passthrough_);
    toolForm->addRow(QString(), skipDialog_);
    toolForm->addRow(tr("Before change"), preHook_);
    toolForm->addRow(tr("After change"), postHook_);

    // Fixed Tool Sensor: machine positions.
    toolForm->addRow(tr("Fixed sensor location"), positionRow(sensor_));
    firstTool_ = new QComboBox;
    for (const char* behaviour : toolchange::kFirstToolBehaviours) {
        firstTool_->addItem(behaviour);
    }
    toolForm->addRow(tr("First tool behaviour"), firstTool_);
    moveToManual_ = new QCheckBox(tr("Move to a tool change location to change bits"));
    toolForm->addRow(QString(), moveToManual_);
    toolForm->addRow(tr("Tool change location"), positionRow(manual_));
    tabs_->addTab(toolChange, tr("Tool Change"));

    // The touch plate profile and the Probe widget's settings (gSender's
    // Probe settings section), all in mm and mm/min.
    auto* probe = new QWidget;
    auto* probeColumns = new QHBoxLayout(probe);
    auto* plateForm = new QFormLayout;
    auto* motionForm = new QFormLayout;
    probeColumns->addLayout(plateForm);
    probeColumns->addLayout(motionForm);
    const auto length = [](double max, const QString& suffix = " mm") {
        auto* box = new QDoubleSpinBox;
        box->setRange(0, max);
        box->setDecimals(3);
        box->setSuffix(suffix);
        return box;
    };
    plateType_ = new QComboBox;
    for (const probe::PlateType type : {probe::PlateType::StandardBlock, probe::PlateType::AutoZero,
                                        probe::PlateType::ZProbe, probe::PlateType::Probe3D,
                                        probe::PlateType::BitZero}) {
        plateType_->addItem(QString::fromUtf8(probe::plateTypeName(type).data()));
    }
    standardBlock_ = length(100);
    xyThickness_ = length(100);
    autoZero_ = length(100);
    zProbe_ = length(100);
    probe3D_ = length(100);
    tipDiameter3D_ = length(20);
    xyRetract3D_ = length(100);
    bitZero_ = length(100);
    bitZeroZOnly_ = length(100);
    plateForm->addRow(tr("Touch plate"), plateType_);
    plateForm->addRow(tr("Block Z thickness"), standardBlock_);
    plateForm->addRow(tr("Block XY thickness"), xyThickness_);
    plateForm->addRow(tr("AutoZero Z thickness"), autoZero_);
    plateForm->addRow(tr("Z probe thickness"), zProbe_);
    plateForm->addRow(tr("3D probe Z offset"), probe3D_);
    plateForm->addRow(tr("3D probe tip diameter"), tipDiameter3D_);
    plateForm->addRow(tr("3D probe XY retract"), xyRetract3D_);
    plateForm->addRow(tr("BitZero inset thickness"), bitZero_);
    plateForm->addRow(tr("BitZero Z-only thickness"), bitZeroZOnly_);
    fastFeed_ = length(10000, " mm/min");
    slowFeed_ = length(10000, " mm/min");
    retraction_ = length(100);
    zRetractNormal_ = length(100);
    zRetractAuto_ = length(100);
    zProbeDistance_ = length(500);
    moveSpeed_ = length(20000, " mm/min");
    moveSpeed_->setSpecialValueText(tr("Rapid (G0)"));
    connectivityTest_ = new QCheckBox(tr("Check the probe circuit before probing"));
    motionForm->addRow(tr("Fast find"), fastFeed_);
    motionForm->addRow(tr("Slow find"), slowFeed_);
    motionForm->addRow(tr("Retraction"), retraction_);
    motionForm->addRow(tr("Final Z retract"), zRetractNormal_);
    motionForm->addRow(tr("AutoZero Z retract"), zRetractAuto_);
    motionForm->addRow(tr("Z probe distance"), zProbeDistance_);
    motionForm->addRow(tr("Return move speed"), moveSpeed_);
    motionForm->addRow(QString(), connectivityTest_);
    tabs_->addTab(probe, tr("Probe"));

    tabs_->addTab(new FirmwareSettingsTable(machine_), tr("Firmware"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::save);
    load();
}

void SettingsDialog::showPage(Page page) {
    tabs_->setCurrentIndex(static_cast<int>(page));
}

void SettingsDialog::load() {
    const AppSettings& s = machine_.settings();
    spindleDelay_->setValue(s.preferences.spindleDelay);
    lineWarnings_->setChecked(s.preferences.showLineWarnings);
    aAxis_->setChecked(s.preferences.useAaxisForGrbl);
    firmware_->setCurrentIndex(s.defaultFirmware == protocol::Firmware::GrblHal ? 1 : 0);
    networkPort_->setValue(s.networkPort);
    units_->setCurrentIndex(s.metric ? 0 : 1);
    decimals_->setValue(s.customDecimalPlaces);
    safeRetract_->setValue(s.safeRetractHeight);
    warnZero_->setChecked(s.warnZero);
    const double park[3] = {s.park.x, s.park.y, s.park.z};
    for (int i = 0; i < 3; ++i) {
        park_[i]->setValue(park[i]);
    }
    outlineMode_->setCurrentText(QString::fromUtf8(job::outlineModeName(s.outlineMode).data()));
    outlineSpeed_->setValue(s.outlineSpeed);
    const int option = toolChange_->findText(QString::fromStdString(s.toolChange.option));
    toolChange_->setCurrentIndex(option >= 0 ? option : 0);
    passthrough_->setChecked(s.toolChange.passthrough);
    skipDialog_->setChecked(s.toolChange.skipDialog);
    preHook_->setPlainText(QString::fromStdString(s.toolChange.preHook));
    postHook_->setPlainText(QString::fromStdString(s.toolChange.postHook));
    const double sensor[3] = {s.toolChangePosition.x, s.toolChangePosition.y, s.toolChangePosition.z};
    const double manual[3] = {s.manualPosition.x, s.manualPosition.y, s.manualPosition.z};
    for (int i = 0; i < 3; ++i) {
        sensor_[i]->setValue(sensor[i]);
        manual_[i]->setValue(manual[i]);
    }
    firstTool_->setCurrentText(QString::fromStdString(s.firstToolBehaviour));
    moveToManual_->setChecked(s.moveToManualPosition);

    const probe::ProbeSettings& p = s.probe;
    plateType_->setCurrentText(QString::fromUtf8(probe::plateTypeName(p.plateType).data()));
    standardBlock_->setValue(p.zThickness.standardBlock);
    xyThickness_->setValue(p.xyThickness);
    autoZero_->setValue(p.zThickness.autoZero);
    zProbe_->setValue(p.zThickness.zProbe);
    probe3D_->setValue(p.zThickness.probe3D);
    tipDiameter3D_->setValue(p.tipDiameter3D);
    xyRetract3D_->setValue(p.xyRetract3D);
    bitZero_->setValue(p.zThickness.bitZero);
    bitZeroZOnly_->setValue(p.zThickness.bitZeroZOnly);
    fastFeed_->setValue(p.probeFastFeedrate);
    slowFeed_->setValue(p.probeFeedrate);
    retraction_->setValue(p.retractionDistance);
    zRetractNormal_->setValue(p.zRetractNormal);
    zRetractAuto_->setValue(p.zRetractAuto);
    zProbeDistance_->setValue(p.zProbeDistance);
    moveSpeed_->setValue(p.probeMovementSpeed);
    connectivityTest_->setChecked(p.connectivityTest);
}

void SettingsDialog::save() {
    AppSettings s = machine_.settings();
    s.preferences.spindleDelay = spindleDelay_->value();
    s.preferences.showLineWarnings = lineWarnings_->isChecked();
    s.preferences.useAaxisForGrbl = aAxis_->isChecked();
    s.defaultFirmware = firmware_->currentIndex() == 1 ? protocol::Firmware::GrblHal : protocol::Firmware::Grbl;
    s.networkPort = networkPort_->value();
    s.metric = units_->currentIndex() == 0;
    s.customDecimalPlaces = decimals_->value();
    s.safeRetractHeight = safeRetract_->value();
    s.warnZero = warnZero_->isChecked();
    s.park = {park_[0]->value(), park_[1]->value(), park_[2]->value()};
    s.outlineMode = job::outlineModeFromName(outlineMode_->currentText().toStdString()).value_or(s.outlineMode);
    s.outlineSpeed = outlineSpeed_->value();
    s.toolChange.option = toolChange_->currentText().toStdString();
    s.toolChange.passthrough = passthrough_->isChecked();
    s.toolChange.skipDialog = skipDialog_->isChecked();
    s.toolChange.preHook = preHook_->toPlainText().toStdString();
    s.toolChange.postHook = postHook_->toPlainText().toStdString();
    s.toolChangePosition = {sensor_[0]->value(), sensor_[1]->value(), sensor_[2]->value()};
    s.manualPosition = {manual_[0]->value(), manual_[1]->value(), manual_[2]->value()};
    s.firstToolBehaviour = firstTool_->currentText().toStdString();
    s.moveToManualPosition = moveToManual_->isChecked();

    probe::ProbeSettings& p = s.probe;
    p.plateType = probe::plateTypeFromName(plateType_->currentText().toStdString()).value_or(p.plateType);
    p.zThickness.standardBlock = standardBlock_->value();
    p.xyThickness = xyThickness_->value();
    p.zThickness.autoZero = autoZero_->value();
    p.zThickness.zProbe = zProbe_->value();
    p.zThickness.probe3D = probe3D_->value();
    p.tipDiameter3D = tipDiameter3D_->value();
    p.xyRetract3D = xyRetract3D_->value();
    p.zThickness.bitZero = bitZero_->value();
    p.zThickness.bitZeroZOnly = bitZeroZOnly_->value();
    p.probeFastFeedrate = fastFeed_->value();
    p.probeFeedrate = slowFeed_->value();
    p.retractionDistance = retraction_->value();
    p.zRetractNormal = zRetractNormal_->value();
    p.zRetractAuto = zRetractAuto_->value();
    p.zProbeDistance = zProbeDistance_->value();
    p.probeMovementSpeed = moveSpeed_->value();
    p.connectivityTest = connectivityTest_->isChecked();
    machine_.setSettings(s);
}

}  // namespace gs::app
