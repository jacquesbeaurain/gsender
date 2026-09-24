#include "settings_dialog.hpp"

#include "machine.hpp"
#include "visualizer_theme.hpp"

#include "gs/config/records.hpp"
#include "gs/protocol/firmware_data.hpp"
#include "gs/util/jsnumber.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <charconv>
#include <cmath>
#include <optional>

namespace gs::app {
namespace {

enum Column { kSetting, kValue, kUnits, kDefault, kDescription, kRestore };

// A changed setting's row (readable in light and dark palettes).
const QColor kChanged(250, 204, 21, 60);

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

// ---- pin indicators ------------------------------------------------------------------

PinIndicators::PinIndicators(Machine& machine, const QString& pins, QWidget* parent)
    : QWidget(parent), machine_(machine), pins_(pins) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    for (const QChar pin : pins_) {
        auto* light = new QLabel(QString(pin));
        light->setAlignment(Qt::AlignCenter);
        light->setFixedSize(26, 22);
        light->setToolTip(pin == 'P' ? tr("Probe pin") : tr("%1 limit switch").arg(pin));
        row->addWidget(light);
        lights_.push_back(light);
    }
    row->addStretch(1);
    connect(&machine_, &Machine::stateChanged, this, &PinIndicators::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &PinIndicators::refresh);
    refresh();
}

bool PinIndicators::lit(QChar pin) const {
    controller::Controller* c = machine_.controller();
    return c && QString::fromStdString(c->state().status.pinState).contains(pin);
}

void PinIndicators::refresh() {
    for (std::size_t i = 0; i < lights_.size(); ++i) {
        const bool on = lit(pins_[static_cast<qsizetype>(i)]);
        lights_[i]->setStyleSheet(QString("QLabel { background:%1; color:white; border-radius:4px; font-weight:600; }")
                                      .arg(on ? "#22c55e" : "#6b7280"));
    }
}

// ---- firmware settings -------------------------------------------------------------

FirmwareSettingsTable::FirmwareSettingsTable(Machine& machine, QWidget* parent)
    : QWidget(parent), machine_(machine) {
    auto* layout = new QVBoxLayout(this);

    // The ProfileBar: the machine, its defaults, EEPROM files.
    auto* bar = new QHBoxLayout;
    profile_ = new QComboBox;
    for (const config::MachineProfile& profile : config::machineProfiles()) {
        profile_->addItem(QString::fromStdString(config::machineProfileName(profile)), profile.id);
    }
    profile_->setToolTip(tr("The machine whose default settings these are compared with and restored from"));
    defaults_ = new QPushButton(tr("Defaults"));
    defaults_->setToolTip(tr("Restore the machine's default settings"));
    import_ = new QPushButton(tr("Import..."));
    import_->setToolTip(tr("Write the settings of an exported EEPROM file"));
    export_ = new QPushButton(tr("Export..."));
    export_->setToolTip(tr("Save the board's settings to a file"));
    bar->addWidget(new QLabel(tr("Machine")));
    bar->addWidget(profile_, 1);
    bar->addWidget(defaults_);
    bar->addWidget(import_);
    bar->addWidget(export_);
    layout->addLayout(bar);
    auto* filters = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search settings..."));
    search_->setClearButtonEnabled(true);
    onlyModified_ = new QCheckBox(tr("Only show changed settings"));
    filters->addWidget(search_, 1);
    filters->addWidget(onlyModified_);
    filters->addSpacing(12);
    filters->addWidget(new QLabel(tr("Limit switches")));
    filters->addWidget(new PinIndicators(machine_, "XYZA"));
    layout->addLayout(filters);

    table_ = new QTableWidget(0, 6);
    table_->setHorizontalHeaderLabels({tr("Setting"), tr("Value"), tr("Units"), tr("Default"), tr("Description"), {}});
    table_->horizontalHeader()->setSectionResizeMode(kDescription, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);
    auto* row = new QHBoxLayout;
    status_ = new QLabel;
    reload_ = new QPushButton(tr("Reload ($$)"));
    apply_ = new QPushButton(tr("Write changes"));
    row->addWidget(status_, 1);
    row->addWidget(reload_);
    row->addWidget(apply_);
    layout->addLayout(row);

    connect(profile_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!loading_ && index >= 0) {
            setMachineProfile(profile_->itemData(index).toInt());
        }
    });
    connect(defaults_, &QPushButton::clicked, this, [this] {
        // RestoreDefaultDialog
        const config::MachineProfile& profile = machine_.machineProfile();
        const QString name = QString::fromStdString(profile.name + " " + profile.type).trimmed();
        if (QMessageBox::question(this, tr("Restore Defaults"),
                                  tr("Are you sure you want to restore your <b>%1</b> back to its default state?")
                                      .arg(name.toHtmlEscaped()),
                                  QMessageBox::No | QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
            restoreDefaults();
        }
    });
    connect(import_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import EEPROM Settings"), QString(),
                                                          tr("Settings (*.json);;All files (*)"));
        if (!path.isEmpty()) {
            importFile(path);
        }
    });
    connect(export_, &QPushButton::clicked, this, [this] {
        const QString name = QString("gSender-firmware-settings-%1.json")
                                 .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HH-mm-ss"));
        const QString path = QFileDialog::getSaveFileName(this, tr("Export EEPROM Settings"), name,
                                                          tr("Settings (*.json);;All files (*)"));
        QString error;
        if (!path.isEmpty() && !exportFile(path, &error)) {
            QMessageBox::warning(this, tr("Export EEPROM Settings"), error);
        }
    });
    connect(search_, &QLineEdit::textChanged, this, &FirmwareSettingsTable::applyFilter);
    connect(onlyModified_, &QCheckBox::toggled, this, &FirmwareSettingsTable::applyFilter);
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
        for (QTableWidgetItem* marked : {item, table_->item(item->row(), kSetting)}) {
            QFont font = marked->font();
            font.setBold(changed);
            marked->setFont(font);
        }
        refreshButtons();
    });
    connect(&machine_, &Machine::settingsChanged, this, &FirmwareSettingsTable::reload);
    for (auto signal : {&Machine::connectionChanged, &Machine::workflowChanged}) {
        connect(&machine_, signal, this, &FirmwareSettingsTable::reload);
    }
    reload();
}

void FirmwareSettingsTable::setMachineProfile(int id) {
    AppSettings settings = machine_.settings();
    if (settings.machineProfileId == id) {
        return;
    }
    settings.machineProfileId = id;
    machine_.setSettings(settings);
    reload();
}

void FirmwareSettingsTable::reload() {
    loading_ = true;
    const int selected = machine_.machineProfile().id;
    profile_->setCurrentIndex(std::max(0, profile_->findData(selected)));
    table_->setRowCount(0);
    controller::Controller* c = machine_.controller();
    if (c) {
        const protocol::FirmwareSettings& settings = c->settings();
        const protocol::FirmwareTables& tables = protocol::FirmwareTables::get(c->firmware());
        const config::MachineProfile& profile = machine_.machineProfile();
        const config::BoardContext board = machine_.boardContext();
        for (const auto& [name, value] : settings.settings.items()) {
            QString units;
            QString description;
            int dataType = -1;
            // grblHAL describes its own settings ($ES/$ESH); the static tables
            // fill the gaps.
            const auto number = settingNumber(name);
            const auto own = number ? settings.descriptions.find(*number) : settings.descriptions.end();
            QStringList labels;  // bits or choices
            int kind = -1;
            if (own != settings.descriptions.end()) {
                units = QString::fromStdString(own->second.unit);
                description = QString::fromStdString(own->second.description);
                dataType = own->second.dataType;
                kind = dataType;
                for (const std::string& entry : own->second.format) {
                    labels << QString::fromStdString(entry);
                }
            } else if (const protocol::SettingInfo* info = tables.setting(name)) {
                units = QString::fromStdString(info->units);
                description = QString::fromStdString(info->message);
                if (!info->description.empty() && info->description != info->message) {
                    description += " - " + QString::fromStdString(info->description);
                }
                // The static tables' input types, as SettingsDescriptions
                // maps them (a status report mask is a switch).
                const std::string& type = info->inputType;
                kind = type == "switch" || type == "mask-status-report" ? 0
                       : type == "axis-mask"                             ? 4
                       : type == "select"                                ? 3
                       : type == "mask"                                  ? 1
                                                                          : -1;
                if (const boost::json::value* values = info->raw.if_contains("values");
                    values && values->is_object()) {
                    for (const auto& [key, label] : values->as_object()) {
                        labels << QString::fromStdString(label.is_string() ? std::string(label.as_string())
                                                                           : std::string(key));
                    }
                }
            }
            if (kind == 4) {
                // The machine's axes (Grbl's three).
                const std::string letters = c->state().axes.letters.empty() ? "XYZ" : c->state().axes.letters;
                labels.clear();
                for (const char letter : letters) {
                    labels << QString(QChar(letter));
                }
            }
            const std::optional<std::string> fallback = config::defaultValue(profile, board, name);
            const bool isDefault = config::isDefaultValue(value, fallback, dataType);
            const int row = table_->rowCount();
            table_->insertRow(row);
            auto* key = new QTableWidgetItem(QString::fromStdString(name));
            key->setFlags(key->flags() & ~Qt::ItemIsEditable);
            auto* current = new QTableWidgetItem(QString::fromStdString(value));
            current->setData(Qt::UserRole, QString::fromStdString(value));
            auto* unit = new QTableWidgetItem(units);
            unit->setFlags(unit->flags() & ~Qt::ItemIsEditable);
            auto* standard = new QTableWidgetItem(fallback ? QString::fromStdString(*fallback) : QStringLiteral("-"));
            standard->setFlags(standard->flags() & ~Qt::ItemIsEditable);
            standard->setData(Qt::UserRole, !isDefault);  // changed
            auto* text = new QTableWidgetItem(description);
            text->setFlags(text->flags() & ~Qt::ItemIsEditable);
            text->setToolTip(description);
            table_->setItem(row, kSetting, key);
            table_->setItem(row, kValue, current);
            table_->setItem(row, kUnits, unit);
            table_->setItem(row, kDefault, standard);
            table_->setItem(row, kDescription, text);
            addValueEditor(row, kind, labels);
            if (!isDefault) {
                for (QTableWidgetItem* item : {key, current, unit, standard, text}) {
                    item->setBackground(kChanged);
                }
                auto* restore = new QToolButton;
                restore->setText(tr("Reset"));
                restore->setToolTip(tr("Reset to default value"));
                restore->setAutoRaise(true);
                const QString setting = QString::fromStdString(name);
                connect(restore, &QToolButton::clicked, this, [this, setting] {
                    if (QMessageBox::question(this, tr("Reset setting"),
                                              tr("Are you sure you want to reset this value to default?")) ==
                        QMessageBox::Yes) {
                        restoreSetting(setting);
                    }
                });
                table_->setCellWidget(row, kRestore, restore);
            }
        }
        table_->resizeColumnsToContents();
        table_->setColumnWidth(kValue, std::max(table_->columnWidth(kValue), 170));
        table_->horizontalHeader()->setSectionResizeMode(kDescription, QHeaderView::Stretch);
    }
    loading_ = false;
    applyFilter();
    refreshButtons();
}

void FirmwareSettingsTable::addValueEditor(int row, int kind, const QStringList& labels) {
    QTableWidgetItem* item = table_->item(row, kValue);
    const bool edited = kind == 0 || ((kind == 1 || kind == 2 || kind == 3 || kind == 4) && !labels.isEmpty());
    if (!edited) {
        return;
    }
    // The editor shows the value; the cell keeps it (for Write changes).
    item->setForeground(Qt::transparent);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    const auto current = [item] { return static_cast<long long>(js::stringToNumber(item->text().toStdString())); };
    const auto write = [this, item](const QString& value) {
        if (item->text() != value) {
            item->setText(value);  // itemChanged marks the change
        }
    };
    if (kind == 0) {
        // BooleanInput
        auto* box = new QCheckBox;
        box->setChecked(current() != 0);
        box->setStyleSheet("QCheckBox { margin-left: 6px; }");
        connect(box, &QCheckBox::toggled, this, [write](bool on) { write(on ? "1" : "0"); });
        table_->setCellWidget(row, kValue, box);
    } else if ((kind == 1 || kind == 2 || kind == 4) && !labels.isEmpty()) {
        // BitfieldInput / ExclusiveBitfieldInput / AxisMaskInput: the bits
        // on a menu, the sum on the button.
        auto* button = new QToolButton;
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        auto* menu = new QMenu(button);
        std::vector<QAction*> bits;
        for (int bit = 0; bit < labels.size(); ++bit) {
            QAction* action = menu->addAction(labels[bit]);
            action->setCheckable(true);
            bits.push_back(action);
        }
        button->setMenu(menu);
        const auto show = [button, bits, current, kind] {
            const long long value = current();
            QStringList on;
            for (std::size_t bit = 0; bit < bits.size(); ++bit) {
                const bool set = (value >> bit) & 1;
                bits[bit]->setChecked(set);
                // Exclusive: the other bits only count with the first set.
                bits[bit]->setEnabled(kind != 2 || bit == 0 || (value & 1));
                if (set) {
                    on << bits[bit]->text();
                }
            }
            button->setText(QString("%1  %2").arg(value).arg(on.isEmpty() ? QStringLiteral("-") : on.join(", ")));
        };
        for (std::size_t bit = 0; bit < bits.size(); ++bit) {
            connect(bits[bit], &QAction::toggled, this, [write, current, bit, show](bool on) {
                const long long mask = 1LL << bit;
                const long long value = on ? (current() | mask) : (current() & ~mask);
                write(QString::number(value));
                show();
            });
        }
        show();
        table_->setCellWidget(row, kValue, button);
    } else if (kind == 3 && !labels.isEmpty()) {
        // RadioButtonInput: the choice's index is the value.
        auto* choice = new QComboBox;
        choice->addItems(labels);
        const long long value = current();
        choice->setCurrentIndex(value >= 0 && value < labels.size() ? static_cast<int>(value) : -1);
        connect(choice, &QComboBox::currentIndexChanged, this, [write](int index) {
            if (index >= 0) {
                write(QString::number(index));
            }
        });
        table_->setCellWidget(row, kValue, choice);
    }
}

int FirmwareSettingsTable::modifiedCount() const {
    int count = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        count += table_->item(row, kDefault)->data(Qt::UserRole).toBool() ? 1 : 0;
    }
    return count;
}

void FirmwareSettingsTable::setFilter(const QString& text) {
    search_->setText(text);
}

void FirmwareSettingsTable::setOnlyModified(bool only) {
    onlyModified_->setChecked(only);
}

int FirmwareSettingsTable::visibleRows() const {
    int visible = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        visible += table_->isRowHidden(row) ? 0 : 1;
    }
    return visible;
}

void FirmwareSettingsTable::applyFilter() {
    const QString needle = search_->text().trimmed();
    const bool changedOnly = onlyModified_->isChecked();
    for (int row = 0; row < table_->rowCount(); ++row) {
        bool match = needle.isEmpty();
        for (const int column : {kSetting, kUnits, kDescription}) {
            match = match || table_->item(row, column)->text().contains(needle, Qt::CaseInsensitive);
        }
        const bool changed = table_->item(row, kDefault)->data(Qt::UserRole).toBool();
        table_->setRowHidden(row, !match || (changedOnly && !changed));
    }
    controller::Controller* c = machine_.controller();
    status_->setText(c ? tr("%1 settings, %2 changed from the defaults").arg(table_->rowCount()).arg(modifiedCount())
                       : tr("Connect to read the firmware settings."));
}

bool FirmwareSettingsTable::restoreDefaults() {
    controller::Controller* c = machine_.controller();
    if (!c || !c->workflow().isIdle() || !config::canRestoreDefaults(machine_.machineProfile())) {
        return false;
    }
    c->gcode(config::restoreDefaultsCommands(machine_.machineProfile(), machine_.boardContext()));
    Q_EMIT machine_.successNotice(tr("Restored default settings for your machine."));
    return true;
}

bool FirmwareSettingsTable::restoreSetting(const QString& setting) {
    controller::Controller* c = machine_.controller();
    if (!c || !c->workflow().isIdle()) {
        return false;
    }
    const std::optional<std::string> value =
        config::defaultValue(machine_.machineProfile(), machine_.boardContext(), setting.toStdString());
    if (!value) {
        return false;
    }
    c->gcode(std::vector<std::string>{setting.toStdString() + "=" + *value, "$$"});
    Q_EMIT machine_.successNotice(
        tr("Restored %1 to default value of %2").arg(setting, QString::fromStdString(*value)));
    return true;
}

bool FirmwareSettingsTable::importFile(const QString& path, QString* error) {
    controller::Controller* c = machine_.controller();
    QFile file(path);
    if (!c || !file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = c ? file.errorString() : tr("Not connected");
        }
        return false;
    }
    const QByteArray text = file.readAll();
    const std::optional<std::vector<std::string>> commands = config::importEepromCommands(
        std::string_view(text.constData(), static_cast<std::size_t>(text.size())), &machine_.machineProfile());
    if (!commands) {
        const QString message = tr("Failed to import settings. Please check the file format.");
        if (error) {
            *error = message;
        }
        Q_EMIT machine_.errorReported(tr("Import"), message);
        return false;
    }
    c->gcode(*commands);
    Q_EMIT machine_.successNotice(tr("EEPROM Settings imported"));
    return true;
}

bool FirmwareSettingsTable::exportFile(const QString& path, QString* error) const {
    controller::Controller* c = machine_.controller();
    QFile file(path);
    if (!c || !file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = c ? file.errorString() : tr("Not connected");
        }
        return false;
    }
    const std::string json = config::exportEeprom(c->settings().settings);
    file.write(json.data(), static_cast<qint64>(json.size()));
    return true;
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
    defaults_->setEnabled(idle && config::canRestoreDefaults(machine_.machineProfile()));
    import_->setEnabled(idle);
    export_->setEnabled(c != nullptr);
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
    autoReconnect_ = new QCheckBox(tr("Reconnect automatically"));
    autoReconnect_->setToolTip(tr("Automatically reconnect to the last machine you used when you open gSender."));
    revertWorkspace_ = new QCheckBox(tr("Revert workspace"));
    revertWorkspace_->setToolTip(tr("Allow g-code 'job finishing' commands like M2 and M30 to reset your CNCs "
                                    "workspace back to G54 at the end of each job."));
    powerSaving_ = new QCheckBox(tr("Power saving"));
    powerSaving_->setToolTip(tr("Allow screen to blank/sleep."));
    promptExit_ = new QCheckBox(tr("Prompt on exit"));
    promptExit_->setToolTip(tr("Pop up a confirmation window when exiting the program."));
    hideProcessedLines_ = new QCheckBox(tr("Hide processed lines"));
    hideProcessedLines_->setToolTip(tr("Hide processed g-code lines in the visualizer."));
    warnBadFile_ = new QCheckBox(tr("Warn if bad file"));
    warnBadFile_->setToolTip(tr("Report the invalid lines of a file when it loads."));
    for (QCheckBox* box : {autoReconnect_, revertWorkspace_, powerSaving_, promptExit_, hideProcessedLines_,
                           warnBadFile_}) {
        generalForm->addRow(QString(), box);
    }
    // Visualizer options.
    visualizerTheme_ = new QComboBox;
    visualizerTheme_->addItems(visualizerThemeNames());
    visualizerTheme_->setToolTip(tr("Independent colour control for the visualizer."));
    showBoundingBox_ = new QCheckBox(tr("Show bounding box"));
    showBoundingBox_->setToolTip(tr("Draw a wireframe around the extents of the loaded G-code file."));
    boundingBoxLabels_ = new QCheckBox(tr("Show bounding box labels"));
    boundingBoxLabels_->setToolTip(tr("Show X/Y/Z dimension labels on the bounding box."));
    showMachineBed_ = new QCheckBox(tr("Show machine bed indicator"));
    showMachineBed_->setToolTip(tr("Draw an outline of the machine's homed work area once homing is complete."));
    trimGridToBed_ = new QCheckBox(tr("Trim grid to machine bed"));
    trimGridToBed_->setToolTip(tr("When the machine bed indicator is shown, clip the background grid to just past "
                                  "the bed's edges instead of a fixed square."));
    followTool_ = new QCheckBox(tr("Follow tool during runtime"));
    followTool_->setToolTip(tr("While a job is running, pan the camera to track the tool in X/Y, keeping the same "
                               "viewing angle and height."));
    // Settings backups.
    backupFrequency_ = new QComboBox;
    backupFrequency_->addItems({"On Update", "Daily", "Weekly", "Monthly"});
    backupFrequency_->setToolTip(tr("Choose how often gSender will backup your settings. Useful in case you need to "
                                    "revert them in the future."));
    auto* backupRow = new QHBoxLayout;
    backupLocation_ = new QLineEdit;
    backupLocation_->setPlaceholderText(tr("Application data folder"));
    backupLocation_->setToolTip(tr("Choose the location to backup your settings to. Default: your OS's appData "
                                   "location."));
    auto* browse = new QPushButton(tr("Browse..."));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Settings backup location"),
                                                                 backupLocation_->text());
        if (!folder.isEmpty()) {
            backupLocation_->setText(folder);
        }
    });
    backupRow->addWidget(backupLocation_, 1);
    backupRow->addWidget(browse);
    generalForm->addRow(tr("Run settings backup"), backupFrequency_);
    generalForm->addRow(tr("Settings backup location"), backupRow);
    darkMode_ = new QCheckBox(tr("Dark mode"));
    darkMode_->setToolTip(tr("The application in dark colours."));
    generalForm->addRow(QString(), darkMode_);
    generalForm->addRow(tr("Visualizer theme"), visualizerTheme_);
    for (QCheckBox* box : {showBoundingBox_, boundingBoxLabels_, showMachineBed_, trimGridToBed_, followTool_}) {
        generalForm->addRow(QString(), box);
    }
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
    auto* generalScroll = new QScrollArea;
    generalScroll->setWidget(general);
    generalScroll->setWidgetResizable(true);
    generalScroll->setFrameShape(QFrame::NoFrame);
    tabs_->addTab(generalScroll, tr("General"));

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
    // The sections' wizards: test buttons (SpindleWizard, LaserWizard,
    // AccessoryOutputWizard).
    const auto testRow = [this](std::initializer_list<std::pair<QString, const char*>> buttons) {
        auto* row = new QHBoxLayout;
        for (const auto& [label, command] : buttons) {
            if (!command) {
                row->addSpacing(12);
                continue;
            }
            auto* button = new QPushButton(label);
            button->setToolTip(QString::fromLatin1(command));
            connect(button, &QPushButton::clicked, this, [this, command] {
                if (controller::Controller* c = machine_.controller()) {
                    c->gcode(command);
                }
            });
            row->addWidget(button);
        }
        row->addStretch(1);
        return row;
    };
    spindleForm->addRow(tr("Test spindle"),
                        testRow({{tr("For"), "M3 S1000"}, {tr("Rev"), "M4 S1000"}, {tr("Stop"), "M5 S0"}}));
    spindleForm->addRow(tr("Test laser"), testRow({{tr("Laser On"), "G1F1 M3 S1"}, {tr("Laser Off"), "M5 S0"}}));
    spindleForm->addRow(tr("Accessory outputs"), testRow({{"M3", "M3"},
                                                          {"M4", "M4"},
                                                          {"M5", "M5"},
                                                          {QString(), nullptr},
                                                          {"M7", "M7"},
                                                          {"M8", "M8"},
                                                          {"M9", "M9"}}));
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
    motionForm->addRow(tr("Probe pin"), new PinIndicators(machine_, "P"));
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
    autoReconnect_->setChecked(s.autoReconnect);
    revertWorkspace_->setChecked(s.revertWorkspace);
    powerSaving_->setChecked(s.powerSaving);
    promptExit_->setChecked(s.promptExit);
    hideProcessedLines_->setChecked(s.hideProcessedLines);
    warnBadFile_->setChecked(s.warnBadFile);
    visualizerTheme_->setCurrentText(QString::fromStdString(s.visualizerTheme));
    darkMode_->setChecked(s.darkMode);
    showBoundingBox_->setChecked(s.showBoundingBox);
    boundingBoxLabels_->setChecked(s.boundingBoxLabels);
    showMachineBed_->setChecked(s.showMachineBed);
    trimGridToBed_->setChecked(s.trimGridToBed);
    followTool_->setChecked(s.followTool);
    backupFrequency_->setCurrentText(QString::fromStdString(s.backupFrequency));
    backupLocation_->setText(QString::fromStdString(s.backupLocation));
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
    s.autoReconnect = autoReconnect_->isChecked();
    s.revertWorkspace = revertWorkspace_->isChecked();
    s.powerSaving = powerSaving_->isChecked();
    s.promptExit = promptExit_->isChecked();
    s.hideProcessedLines = hideProcessedLines_->isChecked();
    s.warnBadFile = warnBadFile_->isChecked();
    s.visualizerTheme = visualizerTheme_->currentText().toStdString();
    s.darkMode = darkMode_->isChecked();
    s.showBoundingBox = showBoundingBox_->isChecked();
    s.boundingBoxLabels = boundingBoxLabels_->isChecked();
    s.showMachineBed = showMachineBed_->isChecked();
    s.trimGridToBed = trimGridToBed_->isChecked();
    s.followTool = followTool_->isChecked();
    s.backupFrequency = backupFrequency_->currentText().toStdString();
    s.backupLocation = backupLocation_->text().trimmed().toStdString();
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
