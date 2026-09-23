#include "controls.hpp"

#include "machine.hpp"

#include "gs/config/records.hpp"

#include <boost/json.hpp>
#include "gs/controller/spindle.hpp"
#include "gs/util/jsnumber.hpp"

#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace gs::app {
namespace {

bool canCommand(Machine& machine) {
    controller::Controller* c = machine.controller();
    return c && !c->workflow().isRunning();
}

}  // namespace

// ---- spindle, laser and coolant ------------------------------------------------------

SpindlePanel::SpindlePanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QVBoxLayout(this);

    // The mode switch (and grblHAL's spindles).
    auto* modeRow = new QHBoxLayout;
    spindleMode_ = new QPushButton(tr("Spindle"));
    laserMode_ = new QPushButton(tr("Laser"));
    for (QPushButton* button : {spindleMode_, laserMode_}) {
        button->setCheckable(true);
        modeRow->addWidget(button);
    }
    modeRow->setSpacing(0);
    connect(spindleMode_, &QPushButton::clicked, this, [this] {
        if (machine_.laserMode()) {
            toggleMode();
        }
        refresh();
    });
    connect(laserMode_, &QPushButton::clicked, this, [this] {
        if (!machine_.laserMode()) {
            toggleMode();
        }
        refresh();
    });
    spindleSelect_ = new QComboBox;
    spindleSelect_->setToolTip(tr("grblHAL spindle (M104)"));
    connect(spindleSelect_, &QComboBox::activated, this,
            [this](int index) { machine_.selectSpindle(spindleSelect_->itemData(index).toInt()); });
    modeRow->addSpacing(12);
    modeRow->addWidget(spindleSelect_, 1);
    layout->addLayout(modeRow);

    const auto button = [this](QGridLayout* grid, const QString& text, int row, int column, auto action,
                               bool isStop = false) {
        auto* b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, action);
        grid->addWidget(b, row, column);
        (isStop ? stops_ : buttons_).append(b);
        return b;
    };

    // Spindle: speed within $31..$30, CW / CCW / stop.
    spindleBox_ = new QGroupBox(tr("Spindle"));
    auto* spindleGrid = new QGridLayout(spindleBox_);
    speedSlider_ = new QSlider(Qt::Horizontal);
    speed_ = new QDoubleSpinBox;
    speed_->setDecimals(0);
    speed_->setSingleStep(100);
    speed_->setSuffix(tr(" rpm"));
    spindleGrid->addWidget(new QLabel(tr("Speed")), 0, 0);
    spindleGrid->addWidget(speedSlider_, 0, 1, 1, 2);
    spindleGrid->addWidget(speed_, 0, 3);
    button(spindleGrid, tr("CW (M3)"), 1, 1, [this] { startClockwise(); });
    button(spindleGrid, tr("CCW (M4)"), 1, 2, [this] { startCounterClockwise(); });
    button(spindleGrid, tr("Stop (M5)"), 1, 3, [this] { stopSpindle(); }, true);
    layout->addWidget(spindleBox_);

    // Laser: power in % of its maximum, focus (on), test, off.
    laserBox_ = new QGroupBox(tr("Laser"));
    auto* laserGrid = new QGridLayout(laserBox_);
    powerSlider_ = new QSlider(Qt::Horizontal);
    powerSlider_->setRange(0, 100);
    power_ = new QDoubleSpinBox;
    power_->setRange(0, 100);
    power_->setDecimals(0);
    power_->setSuffix(" %");
    duration_ = new QDoubleSpinBox;
    duration_->setRange(0, 60);
    duration_->setDecimals(1);
    duration_->setSuffix(tr(" s"));
    duration_->setToolTip(tr("How long the laser test fires"));
    laserGrid->addWidget(new QLabel(tr("Power")), 0, 0);
    laserGrid->addWidget(powerSlider_, 0, 1, 1, 2);
    laserGrid->addWidget(power_, 0, 3);
    laserGrid->addWidget(new QLabel(tr("Test duration")), 1, 0);
    laserGrid->addWidget(duration_, 1, 1);
    button(laserGrid, tr("Laser On"), 2, 1, [this] { startClockwise(); })
        ->setToolTip(tr("Lights the laser at the set power to focus it (G1 F1 M3)"));
    button(laserGrid, tr("Laser Test"), 2, 2, [this] { startCounterClockwise(); })
        ->setToolTip(tr("Fires the laser for the test duration"));
    button(laserGrid, tr("Laser Off"), 2, 3, [this] { stopSpindle(); }, true);
    layout->addWidget(laserBox_);

    auto* coolant = new QGroupBox(tr("Coolant"));
    auto* coolantGrid = new QGridLayout(coolant);
    button(coolantGrid, tr("Mist (M7)"), 0, 0, [this] { command("M7"); });
    button(coolantGrid, tr("Flood (M8)"), 0, 1, [this] { command("M8"); });
    button(coolantGrid, tr("Off (M9)"), 0, 2, [this] { command("M9"); }, true);
    layout->addWidget(coolant);

    state_ = new QLabel;
    state_->setStyleSheet("color:#555");
    layout->addWidget(state_);
    layout->addStretch();

    // Changes reach the machine (and the settings) 300 ms after the last one.
    speedTimer_ = new QTimer(this);
    speedTimer_->setSingleShot(true);
    speedTimer_->setInterval(300);
    connect(speedTimer_, &QTimer::timeout, this, &SpindlePanel::applySpeed);
    powerTimer_ = new QTimer(this);
    powerTimer_->setSingleShot(true);
    powerTimer_->setInterval(300);
    connect(powerTimer_, &QTimer::timeout, this, &SpindlePanel::applyPower);
    connect(speedSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (!syncing_) {
            setSpeed(value);
        }
    });
    connect(speed_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!syncing_) {
            setSpeed(value);
        }
    });
    connect(powerSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (!syncing_) {
            setLaserPower(value);
        }
    });
    connect(power_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!syncing_) {
            setLaserPower(value);
        }
    });
    connect(duration_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!syncing_) {
            AppSettings settings = machine_.settings();
            settings.spindle.laser.duration = value;
            machine_.setSettings(settings);
        }
    });

    for (auto signal : {&Machine::stateChanged, &Machine::connectionChanged, &Machine::workflowChanged,
                        &Machine::settingsChanged, &Machine::spindlesChanged, &Machine::appSettingsChanged}) {
        connect(&machine_, signal, this, &SpindlePanel::refresh);
    }
    connect(&machine_, &Machine::spindlesChanged, this, &SpindlePanel::fillSpindles);
    connect(&machine_, &Machine::connectionChanged, this, [this] {
        if (!machine_.isConnected()) {
            spindleOn_ = laserOn_ = false;
        }
    });
    refresh();
}

bool SpindlePanel::canClick() const {
    controller::Controller* c = machine_.controller();
    return c && !c->workflow().isRunning() && c->state().status.activeState == "Idle";
}

void SpindlePanel::startClockwise() {
    if (!canClick()) {
        return;
    }
    if (machine_.laserMode()) {
        // sendLaserM3(): focus at the set power.
        laserOn_ = true;
        command(controller::laserOnCommand(power_->value(), machine_.laserMaxPower()));
    } else {
        spindleOn_ = true;
        command("M3 S" + js::numberToString(speed_->value()));
    }
}

void SpindlePanel::startCounterClockwise() {
    if (!canClick()) {
        return;
    }
    if (machine_.laserMode()) {
        // runLaserTest(): the controller fires for the duration, the widget
        // turns the laser off once it has passed.
        if (controller::Controller* c = machine_.controller()) {
            c->laserTestOn(power_->value(), duration_->value());
        }
        QTimer::singleShot(static_cast<int>(duration_->value() * 1000), this, [this] { stopSpindle(); });
    } else {
        spindleOn_ = true;
        command("M4 S" + js::numberToString(speed_->value()));
    }
}

void SpindlePanel::stopSpindle() {
    spindleOn_ = laserOn_ = false;
    command("M5 S0");
}

void SpindlePanel::toggleMode() {
    if (canClick()) {
        spindleOn_ = laserOn_ = false;
        machine_.setLaserMode(!machine_.laserMode());
    }
}

void SpindlePanel::setSpeed(double rpm) {
    syncing_ = true;
    speed_->setValue(rpm);
    speedSlider_->setValue(static_cast<int>(rpm));
    syncing_ = false;
    speedTimer_->start();
}

void SpindlePanel::setLaserPower(double percent) {
    syncing_ = true;
    power_->setValue(percent);
    powerSlider_->setValue(static_cast<int>(percent));
    syncing_ = false;
    powerTimer_->start();
}

void SpindlePanel::applySpeed() {
    AppSettings settings = machine_.settings();
    settings.spindle.speed = speed_->value();
    machine_.setSettings(settings);
    controller::Controller* c = machine_.controller();
    if (c && spindleOn_) {
        c->spindleSpeedChange(speed_->value());
    }
}

void SpindlePanel::applyPower() {
    AppSettings settings = machine_.settings();
    settings.spindle.laser.power = power_->value();
    machine_.setSettings(settings);
    controller::Controller* c = machine_.controller();
    if (c && laserOn_) {
        c->laserPowerChange(power_->value(), machine_.laserMaxPower());
    }
}

void SpindlePanel::fillSpindles() {
    spindleSelect_->clear();
    for (const auto& spindle : machine_.spindles()) {
        const int id = spindle.id.value_or(0);
        spindleSelect_->addItem(QString("%1 - %2").arg(id).arg(QString::fromStdString(spindle.label)), id);
        if (spindle.enabled) {
            spindleSelect_->setCurrentIndex(spindleSelect_->count() - 1);
        }
    }
}

void SpindlePanel::command(const std::string& gcode) {
    if (auto* c = machine_.controller()) {
        c->gcode(gcode);
    }
}

void SpindlePanel::refresh() {
    controller::Controller* c = machine_.controller();
    const bool laser = machine_.laserMode();
    const AppSettings& settings = machine_.settings();
    spindleMode_->setChecked(!laser);
    laserMode_->setChecked(laser);
    spindleBox_->setVisible(!laser);
    laserBox_->setVisible(laser);
    const bool idle = canClick();
    spindleMode_->setEnabled(idle);
    laserMode_->setEnabled(idle);
    for (QPushButton* b : buttons_) {
        b->setEnabled(idle);
    }
    for (QPushButton* b : stops_) {
        b->setEnabled(c != nullptr && !c->workflow().isRunning());
    }

    // The speed range: the board's $31..$30 (upstream's defaults 1000..30000).
    const double min = c ? js::stringToNumber(c->runner().setting("$31", "1000")) : settings.spindle.spindleMin;
    const double max = c ? js::stringToNumber(c->runner().setting("$30", "30000")) : settings.spindle.spindleMax;
    syncing_ = true;
    if (!laser && std::isfinite(min) && std::isfinite(max) && max >= min) {
        speed_->setRange(min, max);
        speedSlider_->setRange(static_cast<int>(min), static_cast<int>(max));
    }
    if (!speedTimer_->isActive()) {
        speed_->setValue(settings.spindle.speed);  // clamped to the range, as upstream
        speedSlider_->setValue(static_cast<int>(speed_->value()));
    }
    if (!powerTimer_->isActive()) {
        power_->setValue(settings.spindle.laser.power);
        powerSlider_->setValue(static_cast<int>(power_->value()));
    }
    duration_->setValue(settings.spindle.laser.duration);
    syncing_ = false;

    // grblHAL's spindles, when there is a choice.
    spindleSelect_->setVisible(c && c->isGrblHal() && machine_.spindles().size() > 1);
    spindleSelect_->setEnabled(idle);

    if (!c) {
        state_->setText(tr("Not connected"));
        return;
    }
    const auto& modal = c->state().parserState.modal;
    QStringList coolant;
    for (const std::string& code : modal.coolant) {
        coolant << QString::fromStdString(code);
    }
    state_->setText(tr("%1 %2 at S%3 - coolant %4")
                        .arg(laser ? tr("Laser") : tr("Spindle"))
                        .arg(QString::fromStdString(modal.spindle))
                        .arg(c->state().status.spindle, 0, 'f', 0)
                        .arg(coolant.join(' ')));
}

// ---- overrides --------------------------------------------------------------------

OverridesBar::OverridesBar(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    const auto slider = [this, layout](QLabel*& label, const QString& name, auto apply) {
        label = new QLabel(QString("%1 100%").arg(name));
        label->setMinimumWidth(90);
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(10, 200);
        s->setValue(100);
        s->setPageStep(10);
        auto* reset = new QPushButton("100%");
        reset->setMaximumWidth(52);
        connect(s, &QSlider::sliderPressed, this, [this] { dragging_ = true; });
        connect(s, &QSlider::valueChanged, this, [label, name](int value) {
            label->setText(QString("%1 %2%").arg(name).arg(value));
        });
        // Send when released (or on keyboard/page steps), not for every pixel.
        connect(s, &QSlider::sliderReleased, this, [this, s, apply] {
            dragging_ = false;
            apply(s->value());
        });
        connect(s, &QSlider::actionTriggered, this, [this, s, apply](int action) {
            if (action != QAbstractSlider::SliderMove) {
                apply(s->sliderPosition());
            }
        });
        connect(reset, &QPushButton::clicked, this, [apply] { apply(100); });
        layout->addWidget(label);
        layout->addWidget(s, 1);
        layout->addWidget(reset);
        return s;
    };
    feed_ = slider(feedLabel_, tr("Feed"), [this](int value) {
        if (auto* c = machine_.controller()) {
            c->feedOverride(value);
        }
    });
    layout->addSpacing(12);
    spindle_ = slider(spindleLabel_, tr("Spindle"), [this](int value) {
        if (auto* c = machine_.controller()) {
            c->spindleOverride(value);
        }
    });
    layout->addSpacing(12);
    layout->addWidget(new QLabel(tr("Rapids")));
    for (int value : {25, 50, 100}) {
        auto* button = new QPushButton(QString("%1%").arg(value));
        button->setCheckable(true);
        button->setMaximumWidth(52);
        connect(button, &QPushButton::clicked, this, [this, value] {
            if (auto* c = machine_.controller()) {
                c->rapidOverride(value);
            }
        });
        layout->addWidget(button);
        rapid_.append(button);
    }
    connect(&machine_, &Machine::stateChanged, this, &OverridesBar::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &OverridesBar::refresh);
    refresh();
}

void OverridesBar::refresh() {
    controller::Controller* c = machine_.controller();
    setEnabled(c != nullptr);
    if (!c || dragging_) {
        return;
    }
    const auto& overrides = c->state().status.overrides;
    const QSignalBlocker blockFeed(feed_);
    const QSignalBlocker blockSpindle(spindle_);
    feed_->setValue(overrides[0]);
    spindle_->setValue(overrides[2]);
    feedLabel_->setText(tr("Feed %1%").arg(overrides[0]));
    spindleLabel_->setText(tr("Spindle %1%").arg(overrides[2]));
    const int rapids[] = {25, 50, 100};
    for (int i = 0; i < 3; ++i) {
        rapid_[i]->setChecked(overrides[1] == rapids[i]);
    }
}

// ---- macros ---------------------------------------------------------------------------

MacrosPanel::MacrosPanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QVBoxLayout(this);
    list_ = new QListWidget;
    layout->addWidget(list_, 1);
    auto* row = new QHBoxLayout;
    run_ = new QPushButton(tr("Run"));
    add_ = new QPushButton(tr("New..."));
    editButton_ = new QPushButton(tr("Edit..."));
    remove_ = new QPushButton(tr("Delete"));
    for (QPushButton* button : {run_, add_, editButton_, remove_}) {
        row->addWidget(button);
    }
    layout->addLayout(row);
    auto* files = new QHBoxLayout;
    auto* import = new QPushButton(tr("Import..."));
    import->setToolTip(tr("Import macros from a file"));
    auto* exportButton = new QPushButton(tr("Export..."));
    exportButton->setToolTip(tr("Export macros to a file"));
    files->addWidget(import);
    files->addWidget(exportButton);
    files->addStretch(1);
    layout->addLayout(files);
    connect(import, &QPushButton::clicked, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Import Macros"), QString(), tr("Macros (*.json);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QString message;
        if (importFrom(path, &message)) {
            QMessageBox::information(this, tr("Import Macros"), message);
        } else {
            QMessageBox::warning(this, tr("Import Macros"), message);
        }
    });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        if (machine_.macros().list().empty()) {
            QMessageBox::information(this, tr("Export Macros"), tr("No Macros to Export"));
            return;
        }
        const QString name = QString("gSender-macros-%1.json").arg(QDate::currentDate().toString(Qt::ISODate));
        const QString path =
            QFileDialog::getSaveFileName(this, tr("Export Macros"), name, tr("Macros (*.json);;All files (*)"));
        QString message;
        if (!path.isEmpty() && !exportTo(path, &message)) {
            QMessageBox::warning(this, tr("Export Macros"), message);
        }
    });

    connect(run_, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = list_->currentItem();
        controller::Controller* c = machine_.controller();
        if (item && c) {
            // With the file's box ([xmin] ...), as upstream's macro:run.
            c->runMacro(item->data(Qt::UserRole).toString().toStdString(), machine_.fileContext());
        }
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { run_->click(); });
    connect(add_, &QPushButton::clicked, this, [this] { edit(true); });
    connect(editButton_, &QPushButton::clicked, this, [this] { edit(false); });
    connect(remove_, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = list_->currentItem();
        if (!item || QMessageBox::question(this, tr("Delete macro"), tr("Delete \"%1\"?").arg(item->text())) !=
                         QMessageBox::Yes) {
            return;
        }
        machine_.macros().remove(item->data(Qt::UserRole).toString().toStdString());
        Q_EMIT machine_.macrosChanged();
    });
    connect(list_, &QListWidget::currentRowChanged, this, &MacrosPanel::refresh);
    connect(&machine_, &Machine::macrosChanged, this, &MacrosPanel::reload);
    for (auto signal : {&Machine::connectionChanged, &Machine::workflowChanged}) {
        connect(&machine_, signal, this, &MacrosPanel::refresh);
    }
    reload();
}

bool MacrosPanel::importFrom(const QString& path, QString* message) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *message = tr("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    boost::system::error_code error;
    const boost::json::value macros =
        boost::json::parse(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), error);
    if (error || !macros.is_array()) {
        *message = tr("Error Importing Macros: not a macros file.");
        return false;
    }
    config::MacroStore store = machine_.macros();
    const config::MacroImport result = config::importMacros(store, macros);
    Q_EMIT machine_.macrosChanged();
    if (result.imported > 0) {
        *message = tr("Successfully imported %1 macro(s)").arg(result.imported) +
                   (result.updated > 0 ? tr(", updated %1 existing macro(s)").arg(result.updated) : QString());
    } else if (result.updated > 0) {
        *message = tr("Updated %1 existing macro(s)").arg(result.updated);
    } else {
        *message = tr("No macros found to import.");
    }
    return true;
}

bool MacrosPanel::exportTo(const QString& path, QString* message) {
    config::MacroStore store = machine_.macros();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *message = tr("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    const std::string text = boost::json::serialize(config::exportMacros(store));
    file.write(text.data(), static_cast<qint64>(text.size()));
    *message = tr("Exported %1 macro(s)").arg(store.list().size());
    return true;
}

void MacrosPanel::reload() {
    const QString current = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
    list_->clear();
    for (const config::MacroRecord& macro : machine_.macros().list()) {
        auto* item = new QListWidgetItem(QString::fromStdString(macro.name));
        item->setData(Qt::UserRole, QString::fromStdString(macro.id));
        QString tip = QString::fromStdString(macro.description).trimmed();
        tip += (tip.isEmpty() ? "" : "\n\n") + QString::fromStdString(macro.content);
        item->setToolTip(tip);
        list_->addItem(item);
        if (item->data(Qt::UserRole).toString() == current) {
            list_->setCurrentItem(item);
        }
    }
    refresh();
}

void MacrosPanel::refresh() {
    const bool selected = list_->currentItem() != nullptr;
    run_->setEnabled(selected && canCommand(machine_));
    editButton_->setEnabled(selected);
    remove_->setEnabled(selected);
}

void MacrosPanel::edit(bool create) {
    MacroDialog dialog(this);
    std::optional<config::MacroRecord> existing;
    if (!create) {
        QListWidgetItem* item = list_->currentItem();
        if (!item) {
            return;
        }
        existing = machine_.macros().find(item->data(Qt::UserRole).toString().toStdString());
        if (!existing) {
            return;
        }
        dialog.setValues(QString::fromStdString(existing->name), QString::fromStdString(existing->content),
                         QString::fromStdString(existing->description).trimmed());
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    config::MacroStore store = machine_.macros();
    if (existing) {
        store.update(existing->id, config::MacroChanges{.name = dialog.name().toStdString(),
                                                        .content = dialog.content().toStdString(),
                                                        .description = dialog.description().toStdString()});
    } else {
        store.create(dialog.name().toStdString(), dialog.content().toStdString(), dialog.description().toStdString());
    }
    Q_EMIT machine_.macrosChanged();
}

MacroDialog::MacroDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Macro"));
    auto* form = new QFormLayout(this);
    name_ = new QLineEdit;
    content_ = new QPlainTextEdit;
    content_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    content_->setMinimumSize(420, 220);
    content_->setPlaceholderText(tr("G-code, e.g. G0 Z10\n%X0=posx and [expressions] work as in gSender"));
    description_ = new QLineEdit;
    form->addRow(tr("Name"), name_);
    form->addRow(tr("G-code"), content_);
    form->addRow(tr("Description"), description_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!name().isEmpty() && !content().trimmed().isEmpty()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MacroDialog::setValues(const QString& name, const QString& content, const QString& description) {
    name_->setText(name);
    content_->setPlainText(content);
    description_->setText(description);
}

QString MacroDialog::name() const {
    return name_->text().trimmed();
}

QString MacroDialog::content() const {
    return content_->toPlainText();
}

QString MacroDialog::description() const {
    return description_->text();
}

}  // namespace gs::app
