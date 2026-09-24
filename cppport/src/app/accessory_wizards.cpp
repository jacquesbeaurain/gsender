#include "accessory_wizards.hpp"

#include "machine.hpp"

#include "gs/controller/controller.hpp"
#include "gs/controller/locations.hpp"
#include "gs/toolchange/wizards.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>

namespace gs::app {

// ---- the commands --------------------------------------------------------------------------

std::vector<std::string> autoSpinCommands(bool grblHal, bool slbLite) {
    if (!grblHal) {
        return {"G4P0.1", "$31=1", "G4P0.1", "$30=31250", "G4P0.1", "$$"};
    }
    return {"$9 = 1",
            "G4P0.1",
            "$16 = 0",
            "G4P0.1",
            "$30 = 30000",
            "G4P0.1",
            "$31 = 10000",
            "G4P0.1",
            "$33 = 1000",
            "G4P0.1",
            "$34 = 0",
            "G4P0.1",
            std::string("$35 = ") + (slbLite ? "32" : "30"),
            "G4P0.1",
            std::string("$36 = ") + (slbLite ? "96" : "90"),
            "G4P0.1",
            "$395 = 0",
            "G4P0.1",
            ";Flash onboard LED to confirm",
            "M356 P0 Q2",
            "M356 P1 Q2",
            "G4P0.1",
            "M356 P0 Q1",
            "M356 P1 Q1",
            "M356 P0 Q2",
            "M356 P1 Q2",
            "G4P0.1",
            "M356 P0 Q1",
            "M356 P1 Q1",
            "M356 P0 Q0",
            "M356 P1 Q0",
            "(End of Macro 1)",
            "(Reset)",
            "M2",
            "G4P0.1",
            "$$"};
}

std::vector<std::string> sienciSpindleCommands(long long semver) {
    if (semver < kAtciSupportedVersion) {
        // sienciHAL
        return {"$30=24000", "$31=7500", "$340=5", "$374=3", "$375=50", "$392=11", "$395=6", "$476=2", "$$"};
    }
    return {"$30=24000", "$31=7500", "$340=5", "$374=3", "$375=50", "$394=11",
            std::string("$395=") + (semver >= kSpindle395V7Version ? "7" : "2"),
            "$539=11", "$681=0", "$$", "$REBOOT"};
}

std::vector<std::string> modbusCommands(long long semver) {
    std::vector<std::string> code{"$476=2"};
    if (semver >= kAtciSupportedVersion) {
        code.emplace_back("$REBOOT");
    }
    return code;
}

namespace {

// ---- the board, as the wizards ask ------------------------------------------------------------

long long firmwareBuild(Machine& machine) {
    controller::Controller* c = machine.controller();
    return c ? c->runner().settings().semver : -1;
}

// firmwarePastVersion(): the reported build is at least `required`.
bool firmwarePast(Machine& machine, long long required) {
    return firmwareBuild(machine) >= required;
}

std::string boardId(Machine& machine) {
    controller::Controller* c = machine.controller();
    if (!c) {
        return {};
    }
    const auto& info = c->runner().settings().info;
    const auto board = info.find("BOARD");
    return board == info.end() ? std::string() : board->second.text;
}

std::string setting(Machine& machine, const char* key) {
    controller::Controller* c = machine.controller();
    return c ? c->runner().setting(key) : std::string();
}

void send(Machine& machine, const std::vector<std::string>& code) {
    if (controller::Controller* c = machine.controller()) {
        c->gcode(code);
    }
}

// useValidations()
std::function<WizardCheck()> connectedCheck(Machine& machine) {
    return [&machine] {
        return WizardCheck{machine.isConnected(), QObject::tr("Your controller is not connected.  Connect to your "
                                                              "CNC to configure this accessory.")};
    };
}

std::function<WizardCheck()> homedCheck(Machine& machine) {
    return [&machine] {
        controller::Controller* c = machine.controller();
        return WizardCheck{c && c->hasHomed(), QObject::tr("Machine not homed. Please home your machine before "
                                                           "proceeding with accessory configuration.")};
    };
}

std::function<WizardCheck()> grblHalCheck(Machine& machine) {
    return [&machine] {
        controller::Controller* c = machine.controller();
        return WizardCheck{c && c->isGrblHal(),
                           QObject::tr("You must be connected to a grblHAL device to use this wizard.")};
    };
}

// ---- building blocks ---------------------------------------------------------------------------

QLabel* paragraph(const QString& html) {
    auto* label = new QLabel(html);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    return label;
}

// StepActionButton: a button that runs once - running, then done (or an
// error) - with a message below it.
class StepAction final : public QWidget {
public:
    StepAction(const QString& label, const QString& running, QWidget* parent = nullptr)
        : QWidget(parent), label_(label), running_(running) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        button_ = new QPushButton(label);
        button_->setMinimumHeight(40);
        button_->setMinimumWidth(180);
        layout->addWidget(button_, 0, Qt::AlignLeft);
        message_ = new QLabel;
        message_->setWordWrap(true);
        message_->hide();
        layout->addWidget(message_);
        connect(button_, &QPushButton::clicked, this, [this] {
            if (!done_) {
                button_->setText(running_);
            }
        });
    }

    QPushButton* button() const { return button_; }

    void setDone(bool done, const QString& success = {}) {
        done_ = done;
        button_->setText(done ? QObject::tr("✔ Complete") : label_);
        button_->setStyleSheet(done ? "QPushButton { background: #22c55e; color: white; }" : QString());
        showMessage(done ? success : QString(), false);
    }

    void setError(const QString& error) {
        button_->setText(QObject::tr("Error"));
        showMessage(error, true);
    }

private:
    void showMessage(const QString& text, bool error) {
        message_->setText(text);
        message_->setStyleSheet(error ? "QLabel { background: #fee2e2; color: #991b1b; border-radius: 8px; padding: 8px; }"
                                      : "QLabel { background: #dcfce7; color: #166534; border-radius: 8px; padding: 8px; }");
        message_->setVisible(!text.isEmpty());
    }

    QString label_;
    QString running_;
    QPushButton* button_;
    QLabel* message_;
    bool done_ = false;
};

// A step's side widget that follows the machine.
class LiveWidget : public QWidget {
public:
    explicit LiveWidget(Machine& machine) : machine_(machine) {
        for (auto signal : {&Machine::connectionChanged, &Machine::settingsChanged, &Machine::stateChanged}) {
            connect(&machine_, signal, this, [this] { refresh(); });
        }
    }

protected:
    virtual void refresh() = 0;
    Machine& machine_;
};

// "Commands to be sent": the lines, numbered, under which firmware.
class CommandPreview final : public LiveWidget {
public:
    CommandPreview(Machine& machine, std::function<std::pair<QString, std::vector<std::string>>()> lines)
        : LiveWidget(machine), lines_(std::move(lines)) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        label_ = new QLabel;
        label_->setStyleSheet("QLabel { background: #dbeafe; color: #1e40af; border-radius: 6px; padding: 2px 8px; "
                              "font-weight: 600; }");
        layout->addWidget(label_, 0, Qt::AlignLeft);
        code_ = new QLabel;
        code_->setObjectName("commandPreview");
        code_->setTextFormat(Qt::RichText);
        code_->setStyleSheet("QLabel { background: palette(base); padding: 8px; font-family: monospace; }");
        layout->addWidget(code_);
        refresh();
    }

    void refresh() override {
        const auto [label, lines] = lines_();
        label_->setText(label);
        QString html = "<table cellspacing=0 cellpadding=1>";
        for (std::size_t i = 0; i < lines.size(); ++i) {
            html += QString("<tr><td style='color:gray; padding-right:10px' align=right>%1</td><td>%2</td></tr>")
                        .arg(i + 1)
                        .arg(QString::fromStdString(lines[i]).toHtmlEscaped());
        }
        code_->setText(html + "</table>");
    }

private:
    std::function<std::pair<QString, std::vector<std::string>>()> lines_;
    QLabel* label_;
    QLabel* code_;
};

// A completion page: "Setup Complete!", what was done, what next.
QWidget* completionPage(const QString& done, const QStringList& nextSteps, const QString& warning = {}) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 24, 0, 0);
    auto* title = new QLabel(QObject::tr("✔ Setup Complete!"));
    title->setStyleSheet("font-size: 22pt; font-weight: 700; color: #16a34a;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto* text = paragraph(done);
    text->setAlignment(Qt::AlignCenter);
    layout->addWidget(text);
    if (!nextSteps.isEmpty()) {
        QString html = "<b>" + QObject::tr("Next Steps:") + "</b><ul>";
        for (const QString& step : nextSteps) {
            html += "<li>" + step + "</li>";
        }
        auto* next = paragraph(html + "</ul>");
        next->setStyleSheet("QLabel { background: #eff6ff; border-radius: 8px; padding: 12px; }");
        layout->addWidget(next);
    }
    if (!warning.isEmpty()) {
        auto* caution = paragraph("⚠ " + warning);
        caution->setStyleSheet("QLabel { background: #fef9c3; border-radius: 8px; padding: 12px; }");
        layout->addWidget(caution);
    }
    return page;
}

WizardSideItem helpLink(const QString& url) {
    WizardSideItem item;
    item.kind = WizardSideItem::Kind::Link;
    item.title = QObject::tr("Need help?");
    item.text = QObject::tr("Follow along in our");
    item.url = url;
    return item;
}

WizardSideItem image(const QString& resource) {
    WizardSideItem item;
    item.kind = WizardSideItem::Kind::Image;
    item.image = resource;
    return item;
}

WizardSideItem jogging() {
    WizardSideItem item;
    item.kind = WizardSideItem::Kind::Jogging;
    return item;
}

WizardSideItem widget(std::function<QWidget*()> make, const QString& title = {}) {
    WizardSideItem item;
    item.kind = WizardSideItem::Kind::Widget;
    item.widget = std::move(make);
    item.title = title;
    return item;
}

// ---- Vacuum Table ---------------------------------------------------------------------------

class VacuumZeroPage final : public WizardPage {
public:
    explicit VacuumZeroPage(Machine& machine) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(tr("Jog to the front-left corner of your vacuum table.")));
        layout->addWidget(paragraph(tr("Once you're in position, zero the X and Y axes so the mounting and grid "
                                       "files line up with the table.")));
        auto* zero = new StepAction(tr("Zero X/Y"), tr("Zeroing..."));
        zero->button()->setObjectName("zeroXY");
        connect(zero->button(), &QPushButton::clicked, this, [this, zero, &machine] {
            send(machine, {"G10 L20 P0 X0 Y0"});
            zero->setDone(true);
            setComplete(true);
        });
        layout->addWidget(zero);
    }
};

struct VacuumTableSize {
    QString value;
    QString label;
    QString program;  // the mounting holes' program
    QString name;
};

// VACUUM_TABLE_SIZES / MOUNTING_GCODE_BY_SIZE
const std::vector<VacuumTableSize>& vacuumTableSizes() {
    static const std::vector<VacuumTableSize> sizes{
        {"4x8", QObject::tr("4' x 8' Vacuum Table"), ":/accessories/4x8HoleMounts.gcode",
         "gSender_Vacuum_Table_Mounting_4x8"},
    };
    return sizes;
}

class VacuumSizePage final : public WizardPage {
public:
    explicit VacuumSizePage(std::shared_ptr<QString> size) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(tr("Choose the size of your vacuum table.")));
        auto* choice = new QComboBox;
        choice->setObjectName("tableSize");
        for (const VacuumTableSize& option : vacuumTableSizes()) {
            choice->addItem(option.label, option.value);
        }
        choice->setCurrentIndex(std::max(0, choice->findData(*size)));
        connect(choice, &QComboBox::currentIndexChanged, this,
                [choice, size] { *size = choice->currentData().toString(); });
        layout->addWidget(choice);
        setComplete(true);  // a size is always chosen
    }
};

// Loads one of the bundled programs as the job ("Load to Visualizer"),
// then the installer closes.
class VacuumLoadPage final : public WizardPage {
public:
    VacuumLoadPage(Machine& machine, const QString& text, const QString& resource, const QString& name,
                   const QString& error) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(text));
        auto* load = new StepAction(tr("Load to Visualizer"), tr("Loading..."));
        load->button()->setObjectName("loadToVisualizer");
        connect(load->button(), &QPushButton::clicked, this, [this, load, &machine, resource, name, error] {
            QFile file(resource);
            if (!file.open(QIODevice::ReadOnly)) {
                load->setError(error);
                return;
            }
            machine.loadProgram(name, file.readAll().toStdString());
            load->setDone(true);
            setComplete(true);
            Q_EMIT finish();
        });
        layout->addWidget(load);
    }
};

AccessoryWizard vacuumTableWizard(Machine& machine) {
    auto size = std::make_shared<QString>(vacuumTableSizes().front().value);
    AccessoryWizard wizard;
    wizard.id = "vacuum-table";
    wizard.title = QObject::tr("Vacuum Table");
    wizard.validations = {connectedCheck(machine), homedCheck(machine)};

    SubWizard mounting;
    mounting.id = "mounting-setup";
    mounting.title = QObject::tr("Mounting Setup");
    mounting.description = QObject::tr("Zero your table and carve the mounting holes for your vacuum table.");
    mounting.estimatedTime = QObject::tr("10 - 20 minutes");
    mounting.steps = {
        {"zero-position", QObject::tr("Zero Position"), [&machine] { return new VacuumZeroPage(machine); },
         {jogging()}, {}},
        {"select-size", QObject::tr("Table Size"), [size] { return new VacuumSizePage(size); }, {}, {}},
        {"load-mounting-gcode", QObject::tr("Load to Carve"),
         [&machine, size] {
             const std::vector<VacuumTableSize>& sizes = vacuumTableSizes();
             auto chosen = std::find_if(sizes.begin(), sizes.end(),
                                        [&](const VacuumTableSize& s) { return s.value == *size; });
             const VacuumTableSize& table = chosen != sizes.end() ? *chosen : sizes.front();
             return new VacuumLoadPage(machine,
                                       QObject::tr("Load the mounting-hole pattern for your vacuum table. This will "
                                                   "open the file in the main visualizer."),
                                       table.program, table.name,
                                       QObject::tr("Unable to load the mounting file. Please try again."));
         },
         {}, {}},
    };

    SubWizard grid;
    grid.id = "grid-setup";
    grid.title = QObject::tr("Grid Setup");
    grid.description = QObject::tr("Carve an optional alignment grid onto your vacuum table.");
    grid.estimatedTime = QObject::tr("5 minutes");
    grid.steps = {
        {"load-grid-gcode", QObject::tr("Load Grid to Carve"),
         [&machine] {
             return new VacuumLoadPage(machine,
                                       QObject::tr("Load the alignment grid pattern for your vacuum table. This will "
                                                   "open the file in the main visualizer."),
                                       ":/accessories/Grids.gcode", "gSender_Vacuum_Table_Grid",
                                       QObject::tr("Unable to load the grid file. Please try again."));
         },
         {}, {}},
    };
    wizard.subWizards = {mounting, grid};
    return wizard;
}

// ---- Sienci TLS -------------------------------------------------------------------------------

const QString kTlsHelp = QStringLiteral("https://resources.sienci.com/view/addons-tls/");

class TlsOptionsPage final : public WizardPage {
public:
    explicit TlsOptionsPage(Machine& machine) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(tr("Configure how gSender should handle tool changes with your Tool Length "
                                       "Sensor (TLS).")));
        layout->addWidget(paragraph(tr("<b>First tool behaviour</b>")));
        auto* first = new QComboBox;
        first->setObjectName("firstToolBehaviour");
        for (const char* option : toolchange::kFirstToolBehaviours) {
            first->addItem(QString::fromLatin1(option));
        }
        first->setCurrentText("Prompt for first tool");
        auto* explanation = paragraph(QString());
        const auto explain = [first, explanation] {
            static const char* const kExplanations[] = {
                QT_TR_NOOP("Runs the complete tool change process every time, including for the first tool."),
                QT_TR_NOOP("Asks whether to run the full wizard or just probe the current tool length for the first "
                           "tool change."),
                QT_TR_NOOP("Skips the tool change prompt and only measures the current tool for the first tool "
                           "change.")};
            explanation->setText(tr(kExplanations[std::clamp(first->currentIndex(), 0, 2)]));
        };
        connect(first, &QComboBox::currentIndexChanged, this, explain);
        explain();
        layout->addWidget(first);
        layout->addWidget(explanation);
        auto* custom = new QCheckBox(tr("Set manual tool change location"));
        custom->setObjectName("customLocation");
        custom->setChecked(true);
        layout->addWidget(custom);
        layout->addWidget(paragraph(tr("Move the CNC to a more convenient location for manual tool changes instead "
                                       "of prompting to change over the sensor.")));
        layout->addWidget(paragraph(tr("Select <b>\"Apply\"</b> to set your tool change strategy to Fixed Tool "
                                       "Sensor and save these options.")));
        auto* apply = new StepAction(tr("Apply"), tr("Applying..."));
        apply->button()->setObjectName("applyOptions");
        connect(apply->button(), &QPushButton::clicked, this, [this, apply, first, custom, &machine] {
            std::vector<std::string> code{"$6=1"};
            if (!firmwarePast(machine, kAtciSupportedVersion)) {
                code.emplace_back("$668=0");
            }
            if (boardId(machine) == "SLB Lite") {
                const double value = js::stringToNumber(setting(machine, "$65"));
                const long long current = std::isfinite(value) ? static_cast<long long>(value) : 0;
                const long long updated = current | 8;
                if (updated != current) {
                    code.push_back("$65=" + std::to_string(updated));
                }
                code.emplace_back("G65 P5 Q1");
            }
            code.emplace_back("$$");
            send(machine, code);
            AppSettings settings = machine.settings();
            settings.toolChange.option = "Fixed Tool Sensor";
            settings.moveToManualPosition = custom->isChecked();
            settings.firstToolBehaviour = first->currentText().toStdString();
            settings.toolChange.passthrough = false;
            settings.probe.probeFastFeedrate = 1000;
            machine.setSettings(settings);  // updateToolchangeContext()
            apply->setDone(true, tr("Tool change options configured."));
            setComplete(true);
        });
        layout->addWidget(apply);
    }
};

// PositionSetter with the machine's position: the fields follow the
// machine until edited; a set position is undone when the machine moves
// away from it.
class PositionPage : public WizardPage {
public:
    PositionPage(Machine& machine, const QString& intro, const QString& detail, bool goTo)
        : machine_(machine) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(intro));
        layout->addWidget(paragraph(detail));
        const bool metric = machine_.settings().metric;
        layout->addWidget(paragraph(tr("<b>Position (%1)</b>").arg(metric ? "mm" : "in")));
        auto* fields = new QHBoxLayout;
        for (int axis = 0; axis < 3; ++axis) {
            fields->addWidget(new QLabel(QString(QChar("XYZ"[axis]))));
            edits_[axis] = new QLineEdit;
            edits_[axis]->setObjectName(QString("position%1").arg(QChar("XYZ"[axis])));
            connect(edits_[axis], &QLineEdit::textEdited, this, [this] {
                editing_ = true;
                if (isComplete()) {
                    action_->setDone(false);
                    setComplete(false);
                }
            });
            fields->addWidget(edits_[axis], 1);
        }
        layout->addLayout(fields);
        auto* buttons = new QHBoxLayout;
        action_ = new StepAction(tr("Set Position"), tr("Setting..."));
        action_->button()->setObjectName("setPosition");
        connect(action_->button(), &QPushButton::clicked, this, [this] { set(); });
        buttons->addWidget(action_);
        if (goTo) {
            auto* go = new QPushButton(tr("Go To"));
            go->setObjectName("goToPosition");
            go->setMinimumHeight(40);
            connect(go, &QPushButton::clicked, this, [this] { goToPosition(); });
            buttons->addWidget(go, 0, Qt::AlignTop);
        }
        buttons->addStretch(1);
        layout->addLayout(buttons);
        connect(&machine_, &Machine::stateChanged, this, [this] { follow(); });
    }

    // The fields in mm.
    controller::MachineLocation position() const {
        const bool metric = machine_.settings().metric;
        const auto mm = [&](int axis) {
            const double value = js::stringToNumber(edits_[axis]->text().toStdString());
            return metric ? value : units::in2mm(value);
        };
        return {mm(0), mm(1), mm(2)};
    }

protected:
    virtual void apply(const controller::MachineLocation& at) = 0;
    virtual QString success() const = 0;
    virtual void goToPosition() {}

    void show(const controller::MachineLocation& at) {
        const AppSettings& settings = machine_.settings();
        const double values[] = {at.x, at.y, at.z};
        for (int axis = 0; axis < 3; ++axis) {
            edits_[axis]->setText(
                QString::fromStdString(units::positionText(values[axis], settings.metric, settings.customDecimalPlaces)));
        }
    }

    std::optional<std::array<double, 3>> machinePosition() const {
        controller::Controller* c = machine_.controller();
        if (!c) {
            return std::nullopt;
        }
        const auto mpos = c->runner().machinePosition();
        return std::array<double, 3>{mpos[0], mpos[1], mpos[2]};
    }

    // Whether the fields follow a move now (ManualToolChangePosition waits
    // for the first real jog).
    virtual bool followsMove(const std::array<double, 3>&) const { return true; }

    Machine& machine_;

private:
    void follow() {
        const auto mpos = machinePosition();
        if (editing_ || !mpos || !followsMove(*mpos)) {
            return;
        }
        if (isComplete() && setAt_ && *setAt_ != *mpos) {
            action_->setDone(false);
            setComplete(false);
        }
        show({(*mpos)[0], (*mpos)[1], (*mpos)[2]});
    }

    void set() {
        apply(position());
        setAt_ = machinePosition();
        action_->setDone(true, success());
        setComplete(true);
    }

    QLineEdit* edits_[3];
    StepAction* action_;
    bool editing_ = false;
    std::optional<std::array<double, 3>> setAt_;
};

class TlsLocationPage final : public PositionPage {
public:
    explicit TlsLocationPage(Machine& machine)
        : PositionPage(machine,
                       tr("Install the tallest bit you own in your spindle or router. Jog until it's positioned "
                          "just above (10-20mm) the Tool Length Sensor, then set the position using the "
                          "<b>\"Set Position\"</b> button."),
                       tr("Using your tallest tool gives the most Z-axis clearance above the sensor, so its "
                          "measured position ends up negative - this is what lets gSender accurately probe tools "
                          "of any length during a tool change without running out of travel."),
                       false) {
        if (const auto mpos = machinePosition()) {
            show({(*mpos)[0], (*mpos)[1], (*mpos)[2]});
        } else {
            show({});
        }
    }

protected:
    void apply(const controller::MachineLocation& at) override {
        AppSettings settings = machine_.settings();
        settings.toolChangePosition = {at.x, at.y, at.z};
        machine_.setSettings(settings);
        // G10 L2 P9: the sensor's X/Y as the ninth work offset; $# shows it.
        send(machine_, {"G21 G10 L2 P9 X" + js::numberToString(at.x) + " Y" + js::numberToString(at.y), "$#"});
    }
    QString success() const override { return tr("TLS location set."); }
};

class ManualPositionPage final : public PositionPage {
public:
    explicit ManualPositionPage(Machine& machine)
        : PositionPage(machine,
                       tr("Jog to the location you'd like the machine to move to for manual tool changes, then set "
                          "the position using the <b>\"Set Position\"</b> button."),
                       tr("The fields below are already filled with a recommended position. Hit <b>\"Go To\"</b> to "
                          "send the machine there, then jog to fine-tune it from that starting point before setting "
                          "the position."),
                       true) {
        atStart_ = machinePosition();
        controller::MachineLocation recommended;
        if (const auto at = defaultPosition()) {
            recommended = *at;
        }
        show(recommended);
    }

protected:
    void apply(const controller::MachineLocation& at) override {
        AppSettings settings = machine_.settings();
        settings.manualPosition = {at.x, at.y, at.z};
        machine_.setSettings(settings);
    }
    QString success() const override { return tr("Tool change location set."); }

    void goToPosition() override {
        send(machine_, controller::parkCommands(position(), locationSettings()));
    }

    // No real jog since the step opened: keep the recommendation.
    bool followsMove(const std::array<double, 3>& mpos) const override { return !atStart_ || *atStart_ != mpos; }

private:
    controller::LocationSettings locationSettings() const {
        controller::LocationSettings s;
        s.homing = setting(machine_, "$22");
        s.homingDirMask = setting(machine_, "$23");
        s.pullOff = setting(machine_, "$27");
        s.xMaxTravel = setting(machine_, "$130");
        s.yMaxTravel = setting(machine_, "$131");
        return s;
    }

    std::optional<controller::MachineLocation> defaultPosition() const {
        controller::Controller* c = machine_.controller();
        return controller::defaultToolChangePosition(locationSettings(), c && c->homingFlag());
    }

    std::optional<std::array<double, 3>> atStart_;
};

// ContinuityIndicator's phases.
enum class Continuity { Checking, Waiting, Success, StuckOn };

class ContinuityPage final : public WizardPage {
public:
    explicit ContinuityPage(Machine& machine) : machine_(machine) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(tr("Let's confirm your Tool Length Sensor is wired correctly. Press the TLS down "
                                       "when prompted below.")));
        indicator_ = new QLabel;
        indicator_->setObjectName("continuity");
        indicator_->setAlignment(Qt::AlignCenter);
        indicator_->setMinimumHeight(160);
        layout->addWidget(indicator_);
        prompt_ = paragraph(tr("Firmly press the TLS sensor to verify the connection."));
        prompt_->setAlignment(Qt::AlignCenter);
        layout->addWidget(prompt_);
        result_ = paragraph(QString());
        layout->addWidget(result_);
        retry_ = new QPushButton(tr("Try Again"));
        retry_->setObjectName("retryContinuity");
        connect(retry_, &QPushButton::clicked, this, [this] { setPhase(Continuity::Checking); });
        layout->addWidget(retry_, 0, Qt::AlignLeft);
        succeeded_ = new QTimer(this);
        succeeded_->setSingleShot(true);
        succeeded_->setInterval(1500);  // CONTINUITY_CHECK_SUCCESS_DELAY_MS
        connect(succeeded_, &QTimer::timeout, this, [this] { setComplete(true); });
        connect(&machine_, &Machine::stateChanged, this, [this] { update(); });
        setPhase(Continuity::Checking);
    }

private:
    bool pinOn() const {
        controller::Controller* c = machine_.controller();
        return c && c->state().status.probeActive;
    }

    void update() {
        // An idle pin already on is a short; else wait for the press.
        if (phase_ == Continuity::Checking) {
            setPhase(pinOn() ? Continuity::StuckOn : Continuity::Waiting);
        } else if (phase_ == Continuity::Waiting && pinOn()) {
            setPhase(Continuity::Success);
        }
    }

    void setPhase(Continuity phase) {
        phase_ = phase;
        static const char* const kCopy[] = {QT_TR_NOOP("Checking continuity…"),
                                            QT_TR_NOOP("Waiting for probe contact…"),
                                            QT_TR_NOOP("Continuity confirmed"),
                                            QT_TR_NOOP("Sensor triggered immediately")};
        static const char* const kColour[] = {"#6b7280", "#2563eb", "#16a34a", "#dc2626"};
        const int index = static_cast<int>(phase);
        indicator_->setText(QString("<div style='font-size:40pt; color:%1'>%2</div><div>%3</div>")
                                .arg(kColour[index], phase == Continuity::Success   ? "✔"
                                                     : phase == Continuity::StuckOn ? "⚠"
                                                                                    : "◎",
                                     tr(kCopy[index])));
        prompt_->setVisible(phase == Continuity::Waiting);
        retry_->setVisible(phase == Continuity::StuckOn);
        if (phase == Continuity::Success) {
            result_->setText(tr("<b>Success</b><br>Continuity check passed. Your TLS is working correctly."));
            result_->setStyleSheet("QLabel { background: #dcfce7; color: #166534; border-radius: 8px; padding: 8px; }");
            succeeded_->start();
        } else {
            succeeded_->stop();
            setComplete(false);
            result_->setText(phase == Continuity::StuckOn
                                 ? tr("<b>Error</b><br>Probe pin immediately asserted. Check your wiring or probe for "
                                      "a short and confirm $6 (Invert Probe Pin) is set correctly.")
                                 : QString());
            result_->setStyleSheet("QLabel { background: #fee2e2; color: #991b1b; border-radius: 8px; padding: 8px; }");
        }
        result_->setVisible(!result_->text().isEmpty());
        if (phase == Continuity::Checking) {
            update();
        }
    }

    Machine& machine_;
    Continuity phase_ = Continuity::Checking;
    QLabel* indicator_;
    QLabel* prompt_;
    QLabel* result_;
    QPushButton* retry_;
    QTimer* succeeded_;
};

QString badge(bool ok, const QString& text) {
    return QString("<span style='background:%1; color:%2'>&nbsp;%3&nbsp;</span>")
        .arg(ok ? "#dcfce7" : "#fee2e2", ok ? "#15803d" : "#b91c1c", text);
}

// TLSContinuitySidebar: the settings the sensor needs.
class TlsSettingsSide final : public LiveWidget {
public:
    explicit TlsSettingsSide(Machine& machine) : LiveWidget(machine) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        text_ = paragraph(QString());
        text_->setObjectName("tlsSettings");
        layout->addWidget(text_);
        refresh();
    }

    void refresh() override {
        const std::string invert = setting(machine_, "$6");
        QString html = "<b>" + tr("Related Settings") + "</b><br>" + tr("$6 - Invert Probe Pin") + ": " +
                       QString::fromStdString(invert.empty() ? "-" : invert) + " " +
                       badge(js::stringToNumber(invert) == 1, js::stringToNumber(invert) == 1 ? tr("OK") : tr("Expected 1"));
        if (!firmwarePast(machine_, kAtciSupportedVersion)) {
            const std::string legacy = setting(machine_, "$668");
            html += "<br>" + tr("$668 - Legacy Tool Sensor") + ": " +
                    QString::fromStdString(legacy.empty() ? "-" : legacy) + " " +
                    badge(js::stringToNumber(legacy) == 0, js::stringToNumber(legacy) == 0 ? tr("OK") : tr("Expected 0"));
        }
        text_->setText(html);
    }

private:
    QLabel* text_;
};

// TLSInputEnable: an SLB Lite with the TLS input switched on in $65.
class TlsInputSide final : public LiveWidget {
public:
    explicit TlsInputSide(Machine& machine) : LiveWidget(machine) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        text_ = paragraph(QString());
        layout->addWidget(text_);
        enable_ = new QPushButton(tr("Enable TLS Input"));
        enable_->setObjectName("enableTlsInput");
        connect(enable_, &QPushButton::clicked, this, [this] {
            send(machine_, {"G65 P5 Q1"});
            sent_->show();
        });
        layout->addWidget(enable_, 0, Qt::AlignLeft);
        sent_ = new QLabel(tr("TLS input enable command sent."));
        sent_->hide();
        layout->addWidget(sent_);
        refresh();
    }

    void refresh() override {
        const double inputs = js::stringToNumber(setting(machine_, "$65"));
        const bool enabled = std::isfinite(inputs) && (static_cast<long long>(inputs) & 8) != 0;
        setVisible(boardId(machine_) == "SLB Lite" && enabled);
        const std::string invert = setting(machine_, "$6");
        controller::Controller* c = machine_.controller();
        const int probeType = c && c->state().status.probe ? c->state().status.probe->type : 0;
        text_->setText("<b>" + tr("TLS Input") + "</b><br>" + tr("$6 - Invert Probe Pin") + ": " +
                       QString::fromStdString(invert.empty() ? "-" : invert) + " " +
                       badge(js::stringToNumber(invert) == 1, js::stringToNumber(invert) == 1 ? tr("OK") : tr("Expected 1")) +
                       "<br>" + tr("Probe Type") + ": " + QString::number(probeType) + " " +
                       badge(probeType == 1, probeType == 1 ? tr("OK") : tr("Not Ready")));
        enable_->setEnabled(probeType != 1);
    }

private:
    QLabel* text_;
    QPushButton* enable_;
    QLabel* sent_;
};

AccessoryWizard tlsWizard(Machine& machine) {
    AccessoryWizard wizard;
    wizard.id = "sienci-tls";
    wizard.title = QObject::tr("Sienci TLS");
    wizard.image = ":/accessories/TLS_Step_01.png";
    wizard.helpUrl = kTlsHelp;
    wizard.validations = {connectedCheck(machine), grblHalCheck(machine), homedCheck(machine)};
    SubWizard setup;
    setup.id = "tls-setup";
    setup.title = QObject::tr("TLS Setup Wizard");
    setup.description = QObject::tr("Configure your Tool Length Sensor and tool change behaviour");
    setup.estimatedTime = QObject::tr("5 - 15 minutes");
    setup.steps = {
        {"options", QObject::tr("Tool Change Options"), [&machine] { return new TlsOptionsPage(machine); },
         {image(":/accessories/TLS_Step_01.png"), helpLink(kTlsHelp)}, {}},
        {"tls-location", QObject::tr("Set TLS Location"), [&machine] { return new TlsLocationPage(machine); },
         {image(":/accessories/TLS_Step_02.png"), jogging(), helpLink(kTlsHelp)}, {}},
        {"manual-position", QObject::tr("Set Tool Change Location"),
         [&machine] { return new ManualPositionPage(machine); },
         {image(":/accessories/TLS_Step_03_Pin.png"), jogging(), helpLink(kTlsHelp)},
         [&machine] { return !machine.settings().moveToManualPosition; }},
        {"continuity-check", QObject::tr("Verify TLS Continuity"), [&machine] { return new ContinuityPage(machine); },
         {widget([&machine] { return new TlsSettingsSide(machine); }),
          widget([&machine] { return new TlsInputSide(machine); }), helpLink(kTlsHelp)},
         {}},
    };
    setup.completion = [&machine] {
        QStringList next;
        if (!firmwarePast(machine, kAtciSupportedVersion)) {
            next << QObject::tr("Restart your controller using the power switch (power cycle)")
                 << QObject::tr("Reconnect in gSender to finish setting up your Tool Length Sensor.");
        }
        return completionPage(QObject::tr("Your Tool Length Sensor and tool change behaviour have been configured."),
                              next,
                              QObject::tr("If you change your spindle or router's physical position (e.g. "
                                          "reinstalling it or a mounting bracket), you may need to update these "
                                          "settings in <b>Config</b> or run this installation wizard again."));
    };
    wizard.subWizards = {setup};
    return wizard;
}

// ---- AutoSpin ---------------------------------------------------------------------------------

const QString kAutoSpinHelp = QStringLiteral("https://resources.sienci.com/view/as-er-collets/");

std::pair<QString, std::vector<std::string>> autoSpinPreview(Machine& machine) {
    controller::Controller* c = machine.controller();
    const bool grblHal = c && c->isGrblHal();
    const bool slbLite = boardId(machine) == "SLB Lite";
    return {grblHal ? (slbLite ? QObject::tr("grblHAL (slb-lite)") : QObject::tr("grblHAL")) : QObject::tr("Other Firmware"),
            autoSpinCommands(grblHal, slbLite)};
}

class AutoSpinEepromPage final : public WizardPage {
public:
    explicit AutoSpinEepromPage(Machine& machine) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(tr("Your AutoSpin EEPROM settings are applied in this step based on your connected "
                                       "firmware type.")));
        layout->addWidget(paragraph(tr("<ol><li>Press <b>\"Apply Settings\"</b></li><li>Reboot your controller using the "
                                       "power switch and reconnect</li><li>Click <b>\"Next\"</b></li></ol>")));
        auto* apply = new StepAction(tr("Apply Settings"), tr("Configuring..."));
        apply->button()->setObjectName("autospin-apply-settings");
        apply->button()->setEnabled(machine.isConnected());
        connect(&machine, &Machine::connectionChanged, apply,
                [apply, &machine] { apply->button()->setEnabled(machine.isConnected()); });
        connect(apply->button(), &QPushButton::clicked, this, [this, apply, &machine] {
            send(machine, autoSpinPreview(machine).second);
            QTimer::singleShot(500, this, [this, apply] {
                auto* box = new QMessageBox(QMessageBox::Information, tr("Restart your Controller"),
                                            tr("Please manually restart your CNC controller (power cycle) and reconnect "
                                               "to gSender for these settings to take effect."),
                                            QMessageBox::Ok, window());
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->setObjectName("restartController");
                box->open();
                apply->setDone(true);
                setComplete(true);
            });
        });
        layout->addWidget(apply);
    }
};

class AutoSpinTestPage final : public WizardPage {
public:
    explicit AutoSpinTestPage(Machine& machine) : machine_(machine) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(tr("Test your AutoSpin setup by running the spindle at a set speed.")));
        layout->addWidget(paragraph(tr("<ol><li>Turn the AutoSpin dial to \"S\"</li><li>Turn on the spindle using the "
                                       "power toggle</li><li>Choose a speed with the slider</li><li>Press "
                                       "<b>\"Start\"</b> to run the spindle (M3)</li></ol>")));
        slider_ = new QSlider(Qt::Horizontal);
        slider_->setObjectName("autospin-test-speed");
        slider_->setSingleStep(100);
        slider_->setPageStep(100);
        layout->addWidget(slider_);
        range_ = new QLabel;
        layout->addWidget(range_);
        auto* buttons = new QHBoxLayout;
        start_ = new QPushButton(tr("▶ Start"));
        start_->setObjectName("autospin-start");
        start_->setToolTip(tr("Run spindle clockwise (M3)"));
        stop_ = new QPushButton(tr("⛔ Stop"));
        stop_->setObjectName("autospin-stop");
        stop_->setToolTip(tr("Stop spindle (M5)"));
        buttons->addWidget(start_);
        buttons->addWidget(stop_);
        buttons->addStretch(1);
        layout->addLayout(buttons);
        // A running spindle takes a new speed 300 ms after the last change.
        change_ = new QTimer(this);
        change_->setSingleShot(true);
        change_->setInterval(300);
        connect(change_, &QTimer::timeout, this, [this] {
            if (controller::Controller* c = machine_.controller()) {
                c->spindleSpeedChange(slider_->value());
            }
        });
        connect(slider_, &QSlider::valueChanged, this, [this] {
            refresh();
            if (running()) {
                change_->start();
            }
        });
        connect(start_, &QPushButton::clicked, this, [this] {
            send(machine_, {"M3 S" + std::to_string(slider_->value())});
            setComplete(true);
        });
        connect(stop_, &QPushButton::clicked, this, [this] {
            change_->stop();
            send(machine_, {"M5"});
        });
        for (auto signal : {&Machine::connectionChanged, &Machine::settingsChanged, &Machine::stateChanged}) {
            connect(&machine_, signal, this, [this] { refresh(); });
        }
        const auto [min, max] = limits();
        slider_->setRange(min, max);
        slider_->setValue(min);
        refresh();
    }

private:
    std::pair<int, int> limits() const {
        const auto read = [this](const char* key, double fallback) {
            const std::string text = setting(machine_, key);
            const double value = text.empty() ? fallback : js::stringToNumber(text);
            return std::isfinite(value) ? static_cast<int>(value) : static_cast<int>(fallback);
        };
        return {read("$31", 1000), read("$30", 30000)};
    }

    bool running() const {
        controller::Controller* c = machine_.controller();
        return c && c->runner().modal().spindle != "M5";
    }

    void refresh() {
        const bool connected = machine_.isConnected();
        const auto [min, max] = limits();
        if (slider_->minimum() != min || slider_->maximum() != max) {
            slider_->setRange(min, max);  // the value is clamped
        }
        slider_->setEnabled(connected);
        start_->setEnabled(connected);
        stop_->setEnabled(connected);
        QString text = tr("%1 RPM - Range %2 - %3 RPM ($31 - $30)").arg(slider_->value()).arg(min).arg(max);
        controller::Controller* c = machine_.controller();
        if (running() && c) {
            text += tr(" · reporting %1 RPM").arg(c->state().status.spindle);
        }
        range_->setText(text);
    }

    Machine& machine_;
    QSlider* slider_;
    QLabel* range_;
    QPushButton* start_;
    QPushButton* stop_;
    QTimer* change_;
};

AccessoryWizard autoSpinWizard(Machine& machine) {
    AccessoryWizard wizard;
    wizard.id = "autospin";
    wizard.title = QObject::tr("AutoSpin");
    wizard.image = ":/accessories/AutoSpin_landing.png";
    wizard.helpUrl = kAutoSpinHelp;
    wizard.validations = {connectedCheck(machine)};
    SubWizard setup;
    setup.id = "autospin-config";
    setup.title = QObject::tr("AutoSpin Setup");
    setup.description = QObject::tr("Configure your AutoSpin for first time use");
    setup.estimatedTime = QObject::tr("5 - 15 minutes");
    setup.configVersion = "1.0";
    setup.steps = {
        {"eeprom-config", QObject::tr("AutoSpin EEPROM Configuration"),
         [&machine] { return new AutoSpinEepromPage(machine); },
         {widget([&machine] { return new CommandPreview(machine, [&machine] { return autoSpinPreview(machine); }); },
                 QObject::tr("Commands to be sent")),
          helpLink(kAutoSpinHelp)},
         {}},
        {"test", QObject::tr("Test AutoSpin"), [&machine] { return new AutoSpinTestPage(machine); },
         {helpLink(kAutoSpinHelp)}, {}},
    };
    setup.completion = [] {
        return completionPage(QObject::tr("Your AutoSpin has been successfully configured and is ready to use."),
                              {QObject::tr("Restart your controller using the power switch"),
                               QObject::tr("Turn the AutoSpin dial to \"S\" and turn on the power toggle"),
                               QObject::tr("Reconnect in gSender and verify your spindle starts and stops from the "
                                           "Carve page.")});
    };
    wizard.subWizards = {setup};
    return wizard;
}

// ---- Sienci Spindle ----------------------------------------------------------------------------

std::pair<QString, std::vector<std::string>> spindlePreview(Machine& machine) {
    const long long build = firmwareBuild(machine);
    return {build >= kAtciSupportedVersion ? QObject::tr("grblHAL (>%1)").arg(kAtciSupportedVersion)
                                           : QObject::tr("sienciHAL (< %1)").arg(kAtciSupportedVersion),
            sienciSpindleCommands(build)};
}

class SpindleConfigPage final : public WizardPage {
public:
    explicit SpindleConfigPage(Machine& machine) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(tr("Your spindle settings are applied in this step and the controller will restart "
                                       "automatically.")));
        QString steps = "<ol><li>" + tr("Press <b>\"Apply And Restart\"</b>") + "</li>";
        if (!firmwarePast(machine, kAtciSupportedVersion)) {
            steps += "<li>" + tr("Reboot your controller using the power switch and reconnect") + "</li>";
        }
        layout->addWidget(paragraph(steps + "<li>" + tr("Click <b>\"Next\"</b>") + "</li></ol>"));
        auto* setup = new StepAction(tr("Setup Spindle"), tr("Configuring..."));
        setup->button()->setObjectName("ss-setup-spindle-reboot");
        setup->button()->setEnabled(machine.isConnected());
        connect(&machine, &Machine::connectionChanged, setup,
                [setup, &machine] { setup->button()->setEnabled(machine.isConnected()); });
        connect(setup->button(), &QPushButton::clicked, this, [this, setup, &machine] {
            send(machine, spindlePreview(machine).second);
            AppSettings settings = machine.settings();
            settings.spindleFunctions = true;  // the Spindle/Laser tab
            machine.setSettings(settings);
            QTimer::singleShot(500, this, [this, setup] {
                setup->setDone(true);
                setComplete(true);
            });
        });
        layout->addWidget(setup);
    }
};

class ModbusPage final : public WizardPage {
public:
    explicit ModbusPage(Machine& machine) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(paragraph(tr("<b>You are able to complete this step while the controller is still "
                                       "alarmed</b>")));
        layout->addWidget(paragraph(tr("Additional spindle settings are applied in this step.")));
        layout->addWidget(paragraph(tr("<ol><li>Reconnect to your controller. Please ignore any alarms that pop-up.</li>"
                                       "<li>Press <b>\"Apply and Restart\"</b></li></ol>")));
        auto* configure = new StepAction(tr("Configure Modbus"), tr("Configuring..."));
        configure->button()->setObjectName("ss-configure-modbus");
        configure->button()->setEnabled(machine.isConnected());
        connect(&machine, &Machine::connectionChanged, configure,
                [configure, &machine] { configure->button()->setEnabled(machine.isConnected()); });
        connect(configure->button(), &QPushButton::clicked, this, [this, configure, &machine] {
            send(machine, modbusCommands(firmwareBuild(machine)));
            QTimer::singleShot(1500, this, [this, configure] {
                configure->setDone(true);
                setComplete(true);
            });
        });
        layout->addWidget(configure);
    }
};

AccessoryWizard spindleWizard(Machine& machine) {
    AccessoryWizard wizard;
    wizard.id = "sienci-spindle";
    wizard.title = QObject::tr("Sienci Spindle");
    wizard.image = ":/accessories/spindle_image.png";
    wizard.validations = {connectedCheck(machine), grblHalCheck(machine)};
    SubWizard config;
    config.id = "spindle-config";
    config.title = QObject::tr("Sienci Spindle Config");
    config.description = QObject::tr("Configure your Sienci Spindle for first time use");
    config.estimatedTime = QObject::tr("5 - 30 minutes");
    config.configVersion = "1.0";
    config.steps = {
        {"spindle-config", QObject::tr("Spindle Config"), [&machine] { return new SpindleConfigPage(machine); },
         {widget([&machine] { return new CommandPreview(machine, [&machine] { return spindlePreview(machine); }); },
                 QObject::tr("Commands to be sent"))},
         {}},
        {"modbus-config", QObject::tr("Modbus Configuration"), [&machine] { return new ModbusPage(machine); }, {}, {}},
    };
    config.completion = [] {
        return completionPage(QObject::tr("Your spindle has been successfully configured and is ready to use."),
                              {QObject::tr("Restart your controller using the power switch"),
                               QObject::tr("Ensure the VFD is turned on before restarting the controller"),
                               QObject::tr("Reconnect in gSender and verify your spindle is working as expected.")});
    };
    wizard.subWizards = {config};
    return wizard;
}

}  // namespace

std::vector<AccessoryWizard> accessoryWizards(Machine& machine) {
    // useAllWizards(), in its order - less the ATC's.
    return {spindleWizard(machine), tlsWizard(machine), autoSpinWizard(machine), vacuumTableWizard(machine)};
}

}  // namespace gs::app
