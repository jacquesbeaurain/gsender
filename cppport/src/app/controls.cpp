#include "controls.hpp"

#include "machine.hpp"

#include <QDialogButtonBox>
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
#include <QVBoxLayout>

namespace gs::app {
namespace {

bool canCommand(Machine& machine) {
    controller::Controller* c = machine.controller();
    return c && !c->workflow().isRunning();
}

}  // namespace

// ---- spindle and coolant ------------------------------------------------------------

SpindlePanel::SpindlePanel(Machine& machine, QWidget* parent) : QWidget(parent), machine_(machine) {
    auto* layout = new QVBoxLayout(this);
    auto* spindle = new QGroupBox(tr("Spindle"));
    auto* grid = new QGridLayout(spindle);
    speed_ = new QDoubleSpinBox;
    speed_->setRange(0, 100000);
    speed_->setDecimals(0);
    speed_->setSingleStep(500);
    speed_->setValue(10000);
    speed_->setSuffix(" RPM");
    grid->addWidget(new QLabel(tr("Speed")), 0, 0);
    grid->addWidget(speed_, 0, 1, 1, 2);
    const auto add = [this](QGridLayout* to, const QString& text, int row, int column, auto gcode) {
        auto* button = new QPushButton(text);
        connect(button, &QPushButton::clicked, this, [this, gcode] { command(gcode()); });
        to->addWidget(button, row, column);
        buttons_.append(button);
    };
    add(grid, tr("On CW (M3)"), 1, 0, [this] { return QString("M3 S%1").arg(speed_->value(), 0, 'f', 0); });
    add(grid, tr("On CCW (M4)"), 1, 1, [this] { return QString("M4 S%1").arg(speed_->value(), 0, 'f', 0); });
    add(grid, tr("Off (M5)"), 1, 2, [] { return QString("M5"); });
    layout->addWidget(spindle);

    auto* coolant = new QGroupBox(tr("Coolant"));
    auto* coolantGrid = new QGridLayout(coolant);
    add(coolantGrid, tr("Mist (M7)"), 0, 0, [] { return QString("M7"); });
    add(coolantGrid, tr("Flood (M8)"), 0, 1, [] { return QString("M8"); });
    add(coolantGrid, tr("Off (M9)"), 0, 2, [] { return QString("M9"); });
    layout->addWidget(coolant);

    state_ = new QLabel;
    state_->setStyleSheet("color:#555");
    layout->addWidget(state_);
    layout->addStretch();

    for (auto signal : {&Machine::stateChanged, &Machine::connectionChanged, &Machine::workflowChanged}) {
        connect(&machine_, signal, this, &SpindlePanel::refresh);
    }
    refresh();
}

void SpindlePanel::startClockwise() {
    command(QString("M3 S%1").arg(speed_->value(), 0, 'f', 0));
}

void SpindlePanel::startCounterClockwise() {
    command(QString("M4 S%1").arg(speed_->value(), 0, 'f', 0));
}

void SpindlePanel::stopSpindle() {
    command("M5");
}

void SpindlePanel::command(const QString& gcode) {
    if (auto* c = machine_.controller()) {
        c->gcode(gcode.toStdString());
    }
}

void SpindlePanel::refresh() {
    const bool enabled = canCommand(machine_);
    for (QPushButton* button : buttons_) {
        button->setEnabled(enabled);
    }
    controller::Controller* c = machine_.controller();
    if (!c) {
        state_->setText(tr("Not connected"));
        return;
    }
    const auto& modal = c->state().parserState.modal;
    QStringList coolant;
    for (const std::string& code : modal.coolant) {
        coolant << QString::fromStdString(code);
    }
    state_->setText(tr("Spindle %1 at %2 RPM - coolant %3")
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
