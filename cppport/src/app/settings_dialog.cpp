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
    generalForm->addRow(tr("Spindle delay"), spindleDelay_);
    generalForm->addRow(QString(), lineWarnings_);
    generalForm->addRow(QString(), aAxis_);
    generalForm->addRow(tr("Default firmware"), firmware_);
    generalForm->addRow(tr("Network port"), networkPort_);
    tabs_->addTab(general, tr("General"));

    auto* toolChange = new QWidget;
    auto* toolForm = new QFormLayout(toolChange);
    toolChange_ = new QComboBox;
    for (const char* option : kToolChangeOptions) {
        toolChange_->addItem(option);
    }
    toolChange_->setToolTip(tr("Ignore: comment M6 out. Pause: pause the job at M6. "
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
    tabs_->addTab(toolChange, tr("Tool Change"));

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
    const int option = toolChange_->findText(QString::fromStdString(s.toolChange.option));
    toolChange_->setCurrentIndex(option >= 0 ? option : 0);
    passthrough_->setChecked(s.toolChange.passthrough);
    skipDialog_->setChecked(s.toolChange.skipDialog);
    preHook_->setPlainText(QString::fromStdString(s.toolChange.preHook));
    postHook_->setPlainText(QString::fromStdString(s.toolChange.postHook));
}

void SettingsDialog::save() {
    AppSettings s = machine_.settings();
    s.preferences.spindleDelay = spindleDelay_->value();
    s.preferences.showLineWarnings = lineWarnings_->isChecked();
    s.preferences.useAaxisForGrbl = aAxis_->isChecked();
    s.defaultFirmware = firmware_->currentIndex() == 1 ? protocol::Firmware::GrblHal : protocol::Firmware::Grbl;
    s.networkPort = networkPort_->value();
    s.toolChange.option = toolChange_->currentText().toStdString();
    s.toolChange.passthrough = passthrough_->isChecked();
    s.toolChange.skipDialog = skipDialog_->isChecked();
    s.toolChange.preHook = preHook_->toPlainText().toStdString();
    s.toolChange.postHook = postHook_->toPlainText().toStdString();
    machine_.setSettings(s);
}

}  // namespace gs::app
