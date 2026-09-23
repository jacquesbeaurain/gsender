#include "settings_dialog.hpp"

#include "machine.hpp"

#include "gs/config/records.hpp"
#include "gs/protocol/firmware_data.hpp"
#include "gs/util/jsnumber.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QDate>
#include <QFileDialog>
#include <QFormLayout>
#include <QMessageBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <charconv>
#include <cmath>
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
    jobEndModal_ = new QCheckBox(tr("Job end notifications"));
    jobEndModal_->setToolTip(tr("Show a carving summary at the end of each job."));
    maintenanceNotifications_ = new QCheckBox(tr("Maintenance notifications"));
    maintenanceNotifications_->setToolTip(tr("Show upcoming maintenance tasks at the end of each job."));
    toastDuration_ = new QSpinBox;
    toastDuration_->setRange(-2, 10000);
    toastDuration_->setSingleStep(500);
    toastDuration_->setSuffix(" ms");
    toastDuration_->setToolTip(tr("How long notifications stay visible, in milliseconds, before auto-dismissing. (-1 "
                                  "keeps them up until manually dismissed, -2 disables them, Default 0 keeps default "
                                  "duration)"));
    generalForm->addRow(QString(), jobEndModal_);
    generalForm->addRow(QString(), maintenanceNotifications_);
    generalForm->addRow(tr("Pop-up notification duration"), toastDuration_);
    liteOption_ = new QComboBox;
    liteOption_->addItems({"Light", "Everything"});
    liteOption_->setToolTip(tr("Enable with the feather when big files are slowing down your computer. (Light turns "
                               "off 3D file view, Everything disables the visualizer)"));
    generalForm->addRow(tr("Lightweight options"), liteOption_);
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
    // Application Preferences: export, import (the port's or gSender's
    // files), restore defaults - each replaces what the dialog shows.
    auto* filesRow = new QHBoxLayout;
    auto* exportButton = new QPushButton(tr("Export Settings..."));
    auto* importButton = new QPushButton(tr("Import Settings..."));
    importButton->setToolTip(tr("A file exported here, or gSender's settings export"));
    auto* restoreButton = new QPushButton(tr("Restore Defaults"));
    filesRow->addWidget(exportButton);
    filesRow->addWidget(importButton);
    filesRow->addWidget(restoreButton);
    filesRow->addStretch(1);
    generalForm->addRow(tr("Settings"), filesRow);
    connect(exportButton, &QPushButton::clicked, this, [this] {
        const QString name = QString("gSender-cpp-settings-%1.json").arg(QDate::currentDate().toString(Qt::ISODate));
        const QString path =
            QFileDialog::getSaveFileName(this, tr("Export Settings"), name, tr("Settings (*.json);;All files (*)"));
        QString error;
        if (!path.isEmpty() && !machine_.exportSettings(path, &error)) {
            QMessageBox::warning(this, tr("Export Settings"), error);
        }
    });
    connect(importButton, &QPushButton::clicked, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Import Settings"), QString(), tr("Settings (*.json);;All files (*)"));
        if (path.isEmpty() ||
            QMessageBox::question(this, tr("Import Settings"),
                                  tr("All your current settings will be replaced. Are you sure you want to import "
                                     "your settings?")) != QMessageBox::Yes) {
            return;
        }
        QString report;
        if (machine_.importSettings(path, &report)) {
            load();
            QMessageBox::information(this, tr("Import Settings"), report);
        } else {
            QMessageBox::warning(this, tr("Import Settings"), report);
        }
    });
    connect(restoreButton, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Restore Settings"),
                                  tr("All your current settings will be removed. Are you sure you want to restore "
                                     "default settings?")) == QMessageBox::Yes) {
            machine_.restoreDefaultSettings();
            load();
        }
    });
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

    // Spindle/Laser (gSender's widgets.spindle): the ranges swapped into
    // $30/$31 with the mode, and the laser's offset from the spindle. On
    // grblHAL the laser's own settings ($730, $731, $770, $771) apply.
    auto* spindleLaser = new QWidget;
    auto* spindleForm = new QFormLayout(spindleLaser);
    const auto numberBox = [](double min, double max, int decimals, const QString& suffix) {
        auto* box = new QDoubleSpinBox;
        box->setRange(min, max);
        box->setDecimals(decimals);
        box->setSuffix(suffix);
        return box;
    };
    spindleMin_ = numberBox(0, 100000, 0, tr(" rpm"));
    spindleMax_ = numberBox(0, 100000, 0, tr(" rpm"));
    spindleMin_->setToolTip(tr("Written back as $31 when switching from laser to spindle mode"));
    spindleMax_->setToolTip(tr("Written back as $30 when switching from laser to spindle mode"));
    laserMin_ = numberBox(0, 100000, 3, QString());
    laserMax_ = numberBox(0, 100000, 3, QString());
    laserMin_->setToolTip(tr("Match this to the minimum S word setting in your laser CAM software. ($31 in laser "
                             "mode; grblHAL $731, Default 0)"));
    laserMax_->setToolTip(tr("Match this to the maximum S word setting in your laser CAM software. ($30 in laser "
                             "mode; grblHAL $730, Default 255)"));
    laserX_ = numberBox(-1000, 1000, 3, " mm");
    laserY_ = numberBox(-1000, 1000, 3, " mm");
    laserX_->setToolTip(tr("X-axis offset from the spindle (mark with a v-bit, then track the laser to that mark; "
                           "grblHAL $770)"));
    laserY_->setToolTip(tr("Y-axis offset from the spindle (grblHAL $771)"));
    laserOutline_ = new QCheckBox(tr("Laser on during outline"));
    laserOutline_->setToolTip(tr("Turn on the laser at its lowest power to see the job position better"));
    spindleForm->addRow(tr("Minimum spindle speed"), spindleMin_);
    spindleForm->addRow(tr("Maximum spindle speed"), spindleMax_);
    spindleForm->addRow(tr("Minimum laser power"), laserMin_);
    spindleForm->addRow(tr("Maximum laser power"), laserMax_);
    spindleForm->addRow(tr("Laser X offset"), laserX_);
    spindleForm->addRow(tr("Laser Y offset"), laserY_);
    spindleForm->addRow(QString(), laserOutline_);
    tabs_->addTab(spindleLaser, tr("Spindle/Laser"));

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

    // Rotary (gSender's Rotary section): the Rotary tab, what rotary mode
    // writes to Grbl's Y (upstream's "hybrid" settings: on grblHAL the board's
    // own A axis settings instead) and the A words for 4-axis Grbl.
    auto* rotaryPage = new QWidget;
    auto* rotaryForm = new QFormLayout(rotaryPage);
    rotaryControls_ = new QCheckBox(tr("Rotary controls"));
    rotaryControls_->setToolTip(
        tr("Show the Rotary tab and related functions on the main Carve page. Turning it off leaves rotary mode."));
    rotaryResolution_ = numberBox(0.000001, 100000, 8, tr(" step/deg"));
    rotaryResolution_->setToolTip(tr("Travel resolution in steps per degree. ($103, Default 19.75308642)"));
    rotaryMaxSpeed_ = numberBox(1, 1000000, 3, tr(" deg/min"));
    rotaryMaxSpeed_->setToolTip(tr("Max axis speed, also used for G0 rapids. ($113, Default 8000)"));
    forceSoftLimits_ = new QCheckBox(tr("Force soft limits"));
    forceSoftLimits_->setToolTip(tr("Enable soft limits when toggling into rotary mode. (grbl only)"));
    forceHardLimits_ = new QCheckBox(tr("Force hard limits"));
    forceHardLimits_->setToolTip(tr("Enable hard limits when toggling into rotary mode. (grbl only)"));
    aAxis_ = new QCheckBox(tr("Use A-axis for grbl"));
    aAxis_->setToolTip(tr("Enables A-axis controls and commands to be sent for devices running modified 4-axis "
                          "grbl, rather than translating A into Y. (grbl only)"));
    rotaryForm->addRow(QString(), rotaryControls_);
    rotaryForm->addRow(tr("Resolution"), rotaryResolution_);
    rotaryForm->addRow(tr("Max speed"), rotaryMaxSpeed_);
    rotaryForm->addRow(QString(), forceSoftLimits_);
    rotaryForm->addRow(QString(), forceHardLimits_);
    rotaryForm->addRow(QString(), aAxis_);
    // The section's wizard (AJogWizard): A jogged 10 degrees either way, to
    // check the direction and the resolution.
    auto* jogRow = new QHBoxLayout;
    for (const char* distance : {"-10", "10"}) {
        auto* jog = new QPushButton(distance[0] == '-' ? tr("Jog A-") : tr("Jog A+"));
        connect(jog, &QPushButton::clicked, this, [this, distance] {
            if (controller::Controller* c = machine_.controller()) {
                c->gcode(std::string("$J=G21G91A") + distance + "F1000");
            }
        });
        jogRow->addWidget(jog);
    }
    jogRow->addStretch(1);
    rotaryForm->addRow(tr("Test"), jogRow);
    tabs_->addTab(rotaryPage, tr("Rotary"));

    // Automations: G-code run around a job - the config file's event hooks,
    // which the controller runs at gcode:start/pause/resume/stop.
    auto* automations = new QWidget;
    auto* automationsLayout = new QVBoxLayout(automations);
    static const char* const kHooks[][3] = {
        {"gcode:start", QT_TR_NOOP("File start"), QT_TR_NOOP("Runs when you start a job, before the file itself runs.")},
        {"gcode:pause", QT_TR_NOOP("File pause"),
         QT_TR_NOOP("If you'd like to stop accessories or move out of the way when you pause during a job.")},
        {"gcode:resume", QT_TR_NOOP("File resume"),
         QT_TR_NOOP("Ensure that anything you set up for File pause is undone when you resume.")},
        {"gcode:stop", QT_TR_NOOP("File stop/end"),
         QT_TR_NOOP("A catch-all to ensure that stopped or ended jobs always safely turn everything off.")},
    };
    for (const auto& hook : kHooks) {
        auto* box = new QGroupBox(tr(hook[1]));
        auto* boxLayout = new QVBoxLayout(box);
        auto* about = new QLabel(tr(hook[2]));
        about->setWordWrap(true);
        auto* enabled = new QCheckBox(tr("Enabled"));
        auto* commands = new QPlainTextEdit;
        commands->setFont(mono);
        commands->setPlaceholderText(tr("; No commands set"));
        commands->setFixedHeight(72);
        boxLayout->addWidget(about);
        boxLayout->addWidget(enabled);
        boxLayout->addWidget(commands);
        automationsLayout->addWidget(box);
        events_.push_back({QString::fromLatin1(hook[0]), enabled, commands});
    }
    automationsLayout->addStretch(1);
    auto* automationsScroll = new QScrollArea;
    automationsScroll->setWidget(automations);
    automationsScroll->setWidgetResizable(true);
    automationsScroll->setFrameShape(QFrame::NoFrame);
    tabs_->addTab(automationsScroll, tr("Automations"));

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

void SettingsDialog::setEventHook(const QString& event, const QString& commands, bool enabled) {
    for (EventEditor& editor : events_) {
        if (editor.event == event) {
            editor.commands->setPlainText(commands);
            editor.enabled->setChecked(enabled);
        }
    }
}

void SettingsDialog::load() {
    const config::EventStore hooks(machine_.config());
    for (EventEditor& editor : events_) {
        const auto record = hooks.find(editor.event.toStdString());
        editor.enabled->setChecked(record && record->enabled);
        editor.commands->setPlainText(record ? QString::fromStdString(record->commands) : QString());
    }
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
    jobEndModal_->setChecked(s.jobEndModal);
    maintenanceNotifications_->setChecked(s.maintenanceNotifications);
    toastDuration_->setValue(s.toastDuration);
    liteOption_->setCurrentText(QString::fromStdString(s.liteOption));
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
    spindleMin_->setValue(s.spindle.spindleMin);
    spindleMax_->setValue(s.spindle.spindleMax);
    laserMin_->setValue(s.spindle.laser.minPower);
    laserMax_->setValue(s.spindle.laser.maxPower);
    laserX_->setValue(s.spindle.laser.xOffset);
    laserY_->setValue(s.spindle.laser.yOffset);
    laserOutline_->setChecked(s.spindle.laser.onOutline);
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

    rotaryControls_->setChecked(s.rotary.showControls);
    const auto firmwareValue = [&s](const char* key) {
        for (const auto& [name, value] : s.rotary.firmware) {
            if (name == key) {
                return value;
            }
        }
        return std::string();
    };
    const auto number = [](const std::string& text) {
        const double value = js::stringToNumber(text);
        return std::isfinite(value) ? value : 0.0;
    };
    controller::Controller* c = machine_.controller();
    rotaryFromBoard_ = c && c->isGrblHal() && !c->runner().setting("$103").empty() &&
                       !c->runner().setting("$113").empty();
    if (rotaryFromBoard_) {
        boardResolution_ = number(c->runner().setting("$103"));
        boardMaxSpeed_ = number(c->runner().setting("$113"));
        rotaryResolution_->setValue(boardResolution_);
        rotaryMaxSpeed_->setValue(boardMaxSpeed_);
    } else {
        rotaryResolution_->setValue(number(firmwareValue("$101")));
        rotaryMaxSpeed_->setValue(number(firmwareValue("$111")));
    }
    forceSoftLimits_->setChecked(firmwareValue("$20") == "1");
    forceHardLimits_->setChecked(firmwareValue("$21") == "1");
}

void SettingsDialog::save() {
    // Event hooks: upstream's EventInput creates a hook with its first
    // commands (trigger "gcode"); a hook left without commands is disabled.
    config::EventStore hooks(machine_.config());
    for (const EventEditor& editor : events_) {
        const std::string key = editor.event.toStdString();
        const std::string commands = editor.commands->toPlainText().toStdString();
        const bool enabled = editor.enabled->isChecked();
        if (const auto record = hooks.find(key)) {
            if (record->commands != commands || record->enabled != enabled) {
                config::EventChanges changes;
                changes.commands = commands;
                changes.enabled = enabled;
                hooks.update(key, changes);
            }
        } else if (!commands.empty()) {
            hooks.create(key, "gcode", commands, enabled);
        }
    }
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
    s.jobEndModal = jobEndModal_->isChecked();
    s.maintenanceNotifications = maintenanceNotifications_->isChecked();
    s.toastDuration = toastDuration_->value();
    s.liteOption = liteOption_->currentText().toStdString();
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
    s.spindle.spindleMin = spindleMin_->value();
    s.spindle.spindleMax = spindleMax_->value();
    s.spindle.laser.minPower = laserMin_->value();
    s.spindle.laser.maxPower = laserMax_->value();
    s.spindle.laser.xOffset = laserX_->value();
    s.spindle.laser.yOffset = laserY_->value();
    s.spindle.laser.onOutline = laserOutline_->isChecked();
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

    const auto setFirmwareValue = [&s](const char* key, std::string value) {
        for (auto& [name, current] : s.rotary.firmware) {
            if (name == key) {
                current = std::move(value);
                return;
            }
        }
        s.rotary.firmware.emplace_back(key, std::move(value));
    };
    // Written the way upstream's inputs store them: numbers as typed, the
    // switches as "1"/"0".
    std::vector<std::string> boardChanges;
    controller::Controller* c = machine_.controller();
    if (rotaryFromBoard_) {
        if (rotaryResolution_->value() != boardResolution_) {
            boardChanges.push_back("$103=" + js::numberToString(rotaryResolution_->value()));
        }
        if (rotaryMaxSpeed_->value() != boardMaxSpeed_) {
            boardChanges.push_back("$113=" + js::numberToString(rotaryMaxSpeed_->value()));
        }
    } else {
        setFirmwareValue("$101", js::numberToString(rotaryResolution_->value()));
        setFirmwareValue("$111", js::numberToString(rotaryMaxSpeed_->value()));
    }
    setFirmwareValue("$20", forceSoftLimits_->isChecked() ? "1" : "0");
    setFirmwareValue("$21", forceHardLimits_->isChecked() ? "1" : "0");
    // Turning the Rotary controls off leaves rotary mode (without a board to
    // tell, only the mode changes - as upstream).
    const bool leaveRotary = s.rotary.showControls && !rotaryControls_->isChecked() && s.rotary.rotaryMode;
    s.rotary.showControls = rotaryControls_->isChecked();
    if (leaveRotary && !c) {
        s.rotary.rotaryMode = false;
    }
    machine_.setSettings(s);
    if (leaveRotary && c) {
        machine_.setRotaryMode(false);
    }
    if (c && !boardChanges.empty()) {
        boardChanges.push_back("$$");
        c->gcode(boardChanges);
        boardResolution_ = rotaryResolution_->value();  // sent: not again on the next Apply
        boardMaxSpeed_ = rotaryMaxSpeed_->value();
    }
}

}  // namespace gs::app
