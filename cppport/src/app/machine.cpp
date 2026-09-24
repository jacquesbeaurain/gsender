#include "machine.hpp"

#include "qt_event_loop.hpp"
#include "shortcuts.hpp"

#include "gs/calibration/calibration.hpp"
#include "gs/config/history.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/actions.hpp"
#include "gs/controller/spindle.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"
#include "gs/sim/grbl_simulator.hpp"
#include "gs/transport/asio_link.hpp"

#include <QDateTime>
#include <QTimeZone>
#include <QStandardPaths>
#include <QDir>
#include <QKeySequence>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QThreadPool>

#include <boost/json.hpp>

#include <cmath>
#include <numbers>

namespace gs::app {

const QString Machine::kSimulatorPort = QStringLiteral("Simulator");

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// Collects the toolpath while the program is analysed: straight segments,
// with arcs tessellated back out of their plane. A turns the stock about X:
// as gviewer draws it, every point is turned by its A (y cos a - z sin a,
// y sin a + z cos a), and a move that turns A is drawn in 5-degree chords.
class ToolpathSink final : public gcode::GeometrySink {
public:
    Toolpath path;

    void atLine(std::size_t index) override { line_ = static_cast<std::uint32_t>(index); }

    void addLine(const gcode::Modal& modal, const gcode::Vec4& from, const gcode::Vec4& to) override {
        const bool rapid = modal.motion == "G0";
        std::vector<float>& out = rapid ? path.rapids : path.feeds;
        std::vector<std::uint32_t>& lines = rapid ? path.rapidLines : path.feedLines;
        const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(to.a - from.a) / 5.0)), 1, 20000);
        gcode::Vec4 previous = from;
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const gcode::Vec4 current = i == steps ? to
                                                   : gcode::Vec4{from.x + (to.x - from.x) * t,
                                                                 from.y + (to.y - from.y) * t,
                                                                 from.z + (to.z - from.z) * t,
                                                                 from.a + (to.a - from.a) * t};
            push(out, previous, current);
            lines.push_back(line_);
            previous = current;
        }
    }

    void addArc(const gcode::Modal& modal, const gcode::Vec4& from, const gcode::Vec4& to,
                const gcode::Vec4& center) override {
        // In-plane coordinates: x/y in the arc plane, z along its normal.
        const bool clockwise = modal.motion == "G2";
        const double radius = std::hypot(from.x - center.x, from.y - center.y);
        const double start = std::atan2(from.y - center.y, from.x - center.x);
        const double end = std::atan2(to.y - center.y, to.x - center.x);
        double sweep = end - start;
        if (start == end) {
            sweep = clockwise ? -2 * std::numbers::pi : 2 * std::numbers::pi;
        } else if (clockwise && sweep > 0) {
            sweep -= 2 * std::numbers::pi;
        } else if (!clockwise && sweep < 0) {
            sweep += 2 * std::numbers::pi;
        }
        // 5-degree steps, finer for large radii.
        const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / (std::numbers::pi / 36) *
                                                                 std::max(1.0, radius / 50.0))),
                                     2, 2000);
        gcode::Vec4 previous = unplane(modal, from);
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const double angle = start + sweep * t;
            const gcode::Vec4 point{center.x + radius * std::cos(angle), center.y + radius * std::sin(angle),
                                    from.z + (to.z - from.z) * t, to.a};
            const gcode::Vec4 current = i == steps ? unplane(modal, to) : unplane(modal, point);
            push(path.feeds, previous, current);
            path.feedLines.push_back(line_);
            previous = current;
        }
    }

private:
    static gcode::Vec4 unplane(const gcode::Modal& modal, const gcode::Vec4& v) {
        if (modal.plane == "G18") {
            return {v.y, v.z, v.x, v.a};
        }
        if (modal.plane == "G19") {
            return {v.z, v.x, v.y, v.a};
        }
        return v;
    }

    std::uint32_t line_ = 0;

    static gcode::Vec4 wrap(const gcode::Vec4& v) {
        if (v.a == 0) {
            return v;
        }
        const double angle = v.a * std::numbers::pi / 180;
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        return {v.x, v.y * c - v.z * s, v.y * s + v.z * c, v.a};
    }

    void extend(const gcode::Vec4& p) {
        gcode::BoundingBox& box = path.bounds;
        if (!path.bounded) {
            box = {p, p};
            path.bounded = true;
            return;
        }
        box.min = {std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z), 0};
        box.max = {std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z), 0};
    }

    void push(std::vector<float>& out, const gcode::Vec4& fromPoint, const gcode::Vec4& toPoint) {
        const gcode::Vec4 from = wrap(fromPoint);
        const gcode::Vec4 to = wrap(toPoint);
        extend(from);
        extend(to);
        out.insert(out.end(), {static_cast<float>(from.x), static_cast<float>(from.y), static_cast<float>(from.z),
                               static_cast<float>(to.x), static_cast<float>(to.y), static_cast<float>(to.z)});
    }
};

}  // namespace

Toolpath traceToolpath(const std::string& program) {
    ToolpathSink sink;
    job::analyzeProgram(program, {}, &sink);
    return std::move(sink.path);
}

Machine::Machine(QtEventLoop& loop, std::filesystem::path configFile, QObject* parent)
    : QObject(parent), loop_(loop), preferences_(std::make_shared<controller::Preferences>()) {
    config::validateAndRepair(configFile);
    config_.onError = [this](const std::string& message) {
        Q_EMIT errorReported(tr("Configuration"), QString::fromStdString(message));
    };
    config_.load(std::move(configFile));
    settings_ = loadAppSettings(config_);
    *preferences_ = settings_.preferences;
}

Machine::~Machine() {
    if (analysisCancel_) {
        *analysisCancel_ = true;
    }
    QThreadPool::globalInstance()->waitForDone();
    teardown();
}

// ---- connection -------------------------------------------------------------------

controller::Controller* Machine::controller() const {
    return session_ ? session_->controller() : nullptr;
}

bool Machine::isConnected() const {
    return controller() != nullptr;
}

void Machine::startSession(controller::DeviceLink& link) {
    controller::ControllerHooks hooks = config::makeControllerHooks(config_);
    hooks.preferences = preferences_;
    session_ = std::make_unique<controller::Session>(
        loop_, link, controller::SessionOptions{settings_.defaultFirmware}, std::move(hooks),
        [this](const controller::ControllerEvent& event) { handle(event); });
    session_->onController = [this](controller::Controller& c, bool) {
        connecting_ = false;
        c.setToolChangeContext(settings_.toolChange);
        // A grblHAL board learns whether the workspace is in rotary mode.
        if (c.isGrblHal()) {
            c.setRotaryMode(settings_.rotary.rotaryMode);
        }
        attachProgram();
        Q_EMIT connectionChanged();
    };
}

void Machine::connectTo(const QString& port, int baudRate, int networkPort) {
    teardown();
    port_ = port;
    connecting_ = true;
    Q_EMIT connectionChanged();

    if (port == kSimulatorPort) {
        simulator_ = std::make_unique<sim::GrblSimulator>(loop_);
        startSession(*simulator_);
        simulator_->onData = [this](std::string_view bytes) {
            if (session_) {
                session_->receive(bytes);
            }
        };
        simulator_->open();
        session_->opened();
        return;
    }

    link_ = std::make_unique<transport::AsioLink>([this](std::function<void()> fn) { loop_.post(std::move(fn)); });
    startSession(*link_);
    link_->onData = [this](std::string_view bytes) {
        if (session_) {
            session_->receive(bytes);
        }
    };
    // Never destroy a link from inside one of its own callbacks: defer.
    link_->onClosed = [this](const std::string& reason) {
        const QString why = QString::fromStdString(reason);
        QMetaObject::invokeMethod(
            this,
            [this, why] {
                teardown();
                Q_EMIT connectionFailed(tr("Connection lost: %1").arg(why));
                Q_EMIT connectionChanged();
            },
            Qt::QueuedConnection);
    };
    const auto opened = [this](const std::string& error) {
        if (error.empty()) {
            if (session_) {
                session_->opened();
            }
            return;
        }
        const QString why = QString::fromStdString(error);
        QMetaObject::invokeMethod(
            this,
            [this, why] {
                teardown();
                Q_EMIT connectionFailed(why);
                Q_EMIT connectionChanged();
            },
            Qt::QueuedConnection);
    };
    const std::string path = port.toStdString();
    if (transport::looksLikeIpAddress(path)) {
        link_->openNetwork({path, static_cast<std::uint16_t>(networkPort), 2000}, opened);
    } else {
        link_->openSerial({path, static_cast<unsigned>(baudRate), false}, opened);
    }
}

void Machine::teardown() {
    if (session_) {
        session_->closed();
        session_.reset();
    }
    if (link_) {
        link_->close();
        link_.reset();
    }
    if (simulator_) {
        simulator_->close();
        simulator_.reset();
    }
    connecting_ = false;
    spindles_.clear();
}

runtime::EventLoop& Machine::eventLoop() noexcept {
    return loop_;
}

void Machine::disconnectFromMachine() {
    teardown();
    Q_EMIT connectionChanged();
}

// ---- tool change wizards ---------------------------------------------------------------

bool Machine::isWizardStrategy(const std::string& option) {
    return option == "Standard Re-zero" || option == "Flexible Re-zero" || option == "Fixed Tool Sensor";
}

std::optional<toolchange::Wizard> Machine::startToolChangeWizard(const std::string& option, int count,
                                                                 bool fullFirstWizard) {
    controller::Controller* c = controller();
    if (!c || !isWizardStrategy(option)) {
        return std::nullopt;
    }
    const toolchange::ProbeSettings probe = toolchange::toolChangeProbeSettings(settings_.probe);
    toolchange::MachineFacts facts;
    facts.reportInches = c->runner().setting("$13");
    facts.reportInches = facts.reportInches.empty() ? "0" : facts.reportInches;
    facts.softLimits = c->runner().setting("$20");
    facts.zMaxTravel = c->runner().setting("$132");
    facts.machineZ = c->runner().machinePosition()[2];
    facts.tool = c->runner().modal().tool;

    toolchange::Wizard wizard;
    if (option == "Standard Re-zero") {
        wizard = toolchange::standardRezero(probe, facts);
    } else if (option == "Flexible Re-zero") {
        wizard = toolchange::flexibleRezero(count, probe, facts);
    } else {
        // determineFixedSensorInstructions(): later tools always get the full
        // wizard; the first one as the settings (or the operator) say.
        const std::string& first = settings_.firstToolBehaviour;
        const bool full = count > 1 || first == toolchange::kFirstToolBehaviours[0] ||
                          (first == toolchange::kFirstToolBehaviours[1] && fullFirstWizard);
        wizard = full ? toolchange::fixedToolSensor(count, probe, facts, settings_.toolChangePosition,
                                                    settings_.manualPosition, settings_.moveToManualPosition)
                      : toolchange::probeToolLength(probe, facts, settings_.toolChangePosition);
    }
    wizardReady_ = false;
    if (wizard.startDirect) {
        c->gcode(wizard.start);
        wizardReady_ = true;
    } else {
        std::string text;
        for (const std::string& line : wizard.start) {
            text += line + "\n";
        }
        c->wizardStart(text, [this] {
            wizardReady_ = true;
            Q_EMIT toolChangeWizardReady();
        });
    }
    return wizard;
}

void Machine::runWizardAction(int step, int substep, const std::vector<std::string>& gcode) {
    if (controller::Controller* c = controller()) {
        c->wizardStep(step, substep);
        c->gcode(gcode);
    }
}

// ---- file context and outline -------------------------------------------------------------

expr::Value Machine::fileContext() const {
    // min/max over every vertex, rapids included (GcodeViewer.computeBBox).
    double min[3] = {0, 0, 0};
    double max[3] = {0, 0, 0};
    bool any = false;
    for (const std::vector<float>* segments : {&toolpath_.rapids, &toolpath_.feeds}) {
        for (std::size_t i = 0; i + 2 < segments->size(); i += 3) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const double v = (*segments)[i + axis];
                min[axis] = any ? std::min(min[axis], v) : v;
                max[axis] = any ? std::max(max[axis], v) : v;
            }
            any = true;
        }
    }
    expr::Value context = expr::Value::object();
    context.set("xmin", expr::Value(min[0]));
    context.set("xmax", expr::Value(max[0]));
    context.set("ymin", expr::Value(min[1]));
    context.set("ymax", expr::Value(max[1]));
    context.set("zmin", expr::Value(min[2]));
    context.set("zmax", expr::Value(max[2]));
    return context;
}

bool Machine::runOutline(QString* error) {
    const auto fail = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    controller::Controller* c = controller();
    if (!c || !hasProgram() || analyzing_ || !c->workflow().isIdle() || c->state().status.activeState != "Idle") {
        return fail(tr("Load a file and wait for an idle machine."));
    }
    job::OutlineInput input;
    input.mode = settings_.outlineMode;
    // "Laser on during outline": in laser mode the trace runs lit at S1.
    input.isLaser = settings_.spindle.laser.onOutline && laserMode();
    input.outlineSpeed = settings_.outlineSpeed;
    input.bbox = analysis_.bounds;
    input.content = programText_;
    // The toolpath's vertices in program order, rapids included.
    std::vector<std::pair<std::uint32_t, const float*>> segments;
    for (std::size_t i = 0; i < toolpath_.rapidLines.size(); ++i) {
        segments.emplace_back(toolpath_.rapidLines[i], &toolpath_.rapids[i * 6]);
    }
    for (std::size_t i = 0; i < toolpath_.feedLines.size(); ++i) {
        segments.emplace_back(toolpath_.feedLines[i], &toolpath_.feeds[i * 6]);
    }
    std::stable_sort(segments.begin(), segments.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [line, segment] : segments) {
        input.vertices.insert(input.vertices.end(), segment, segment + 6);
    }
    // Lift 5 mm, or what is left above the machine position with homing.
    const std::string homing = c->runner().setting("$22");
    const bool homingEnabled = !homing.empty() && homing != "0";
    const double zMpos = std::fabs(c->runner().machinePosition()[2]);
    input.zTravel = homingEnabled ? std::min(zMpos - 1, 5.0) : 5.0;
    const auto program = job::outlineProgram(input);
    if (!program) {
        return fail(tr("The file has no toolpath to outline."));
    }
    c->gcode(*program, fileContext());
    Q_EMIT successNotice(tr("Running file outline"));
    return true;
}

// ---- start from line ---------------------------------------------------------------------

bool Machine::startFromLine(std::size_t line, double safeHeight) {
    controller::Controller* c = controller();
    if (!c || !hasProgram() || analyzing_ || !c->workflow().isIdle()) {
        return false;
    }
    controller::StartOptions options;
    options.lineToStartFrom = line;
    options.zMax = analysis_.bounds.max.z;
    options.safeHeight = safeHeight;
    options.spindleDelay = settings_.preferences.spindleDelay;
    c->start(options);
    return true;
}

// ---- positions ------------------------------------------------------------------------

void Machine::zeroAxis(char axis) {
    if (controller::Controller* c = controller()) {
        c->gcode(controller::zeroAxisCommand(axis));
    }
}

void Machine::zeroAllAxes() {
    controller::Controller* c = controller();
    if (!c) {
        return;
    }
    const bool hasA = c->state().axes.letters.find('A') != std::string::npos;
    for (const std::string& command : controller::zeroAllCommands(c->isGrblHal(), hasA)) {
        c->gcode(command);
    }
}

void Machine::goToZero(std::string_view axes) {
    controller::Controller* c = controller();
    if (!c) {
        return;
    }
    const std::string homing = c->runner().setting("$22");
    const bool homingEnabled = !homing.empty() && js::stringToNumber(homing) != 0;
    c->gcodeSafe(controller::goToZeroCommands(axes, homingEnabled, settings_.safeRetractHeight,
                                              c->runner().machinePosition()[2]),
                 "G21");
}

std::array<double, 4> Machine::workPositionMm() const {
    std::array<double, 4> out{};
    if (const controller::Controller* c = controller()) {
        const bool inches = c->runner().setting("$13") == "1";
        for (std::size_t i = 0; i < 4; ++i) {
            const double value = c->state().status.wpos.axis("xyza"[i]);
            out[i] = inches && i < 3 ? units::in2mm(value) : value;
        }
    }
    return out;
}

std::array<double, 4> Machine::machinePositionMm() const {
    std::array<double, 4> out{};
    if (const controller::Controller* c = controller()) {
        const bool inches = c->runner().setting("$13") == "1";
        for (std::size_t i = 0; i < 4; ++i) {
            const double value = c->state().status.mpos.axis("xyza"[i]);
            out[i] = inches && i < 3 ? units::in2mm(value) : value;
        }
    }
    return out;
}

bool Machine::canMove() const {
    controller::Controller* c = controller();
    if (!c || c->workflow().isRunning()) {
        return false;
    }
    const std::string& state = c->state().status.activeState;
    return state == "Idle" || state == "Jog";
}

bool Machine::homingEnabled() const {
    const controller::Controller* c = controller();
    return c && js::stringToNumber(c->runner().setting("$22", "0")) > 0;
}

bool Machine::singleAxisHoming() const {
    const controller::Controller* c = controller();
    return c && controller::singleAxisHomingEnabled(c->runner().setting("$22", "0"));
}

void Machine::selectWorkspace(const QString& wcs) {
    if (controller::Controller* c = controller()) {
        c->gcode(wcs.toStdString());
    }
}

void Machine::setWorkPosition(char axis, double value) {
    if (controller::Controller* c = controller()) {
        c->gcodeSafe({controller::manualOffsetCommand(axis, value)}, settings_.metric ? "G21" : "G20");
    }
}

void Machine::homeAxis(char axis) {
    if (controller::Controller* c = controller()) {
        c->gcode(controller::homeAxisCommand(axis));
    }
}

namespace {

controller::LocationSettings locationSettings(const controller::Controller& c) {
    const protocol::Runner& runner = c.runner();
    return {runner.setting("$22"), runner.setting("$23"), runner.setting("$27"), runner.setting("$130"),
            runner.setting("$131")};
}

}  // namespace

void Machine::goToCorner(controller::MachineCorner corner) {
    controller::Controller* c = controller();
    if (!c) {
        return;
    }
    const controller::LocationSettings settings = locationSettings(*c);
    const std::vector<std::string> gcode =
        controller::cornerCommands(corner, settings, c->homingFlag(), settings.pullOffDistance(), c->isGrblHal());
    if (gcode.empty()) {
        Q_EMIT notice(tr("Unable to find machine limits - make sure they're set in preferences"));
        return;
    }
    c->gcode(gcode);
}

void Machine::goToPark() {
    if (controller::Controller* c = controller()) {
        const toolchange::MachinePosition& park = settings_.park;
        c->gcode(controller::parkCommands({park.x, park.y, park.z}, locationSettings(*c)));
    }
}

void Machine::goToMachinePosition(const toolchange::MachinePosition& position) {
    if (controller::Controller* c = controller()) {
        c->gcode(controller::locationCommands({position.x, position.y, position.z}, locationSettings(*c)));
    }
}

void Machine::goToLocation(controller::GoToMode mode, double x, double y, double z, double a) {
    controller::Controller* c = controller();
    if (!c) {
        return;
    }
    controller::GoToLocation location;
    location.mode = mode;
    location.x = x;
    location.y = y;
    location.z = z;
    location.a = a;
    // A goes along in rotary mode (the rotary replaces Y) or on a grblHAL
    // board that reports one.
    location.yAvailable = !settings_.rotary.rotaryMode;
    location.aAvailable =
        settings_.rotary.rotaryMode || (c->isGrblHal() && c->state().axes.letters.find('A') != std::string::npos);
    location.metric = settings_.metric;
    location.homingEnabled = js::stringToNumber(c->runner().setting("$22", "0")) != 0;
    location.safeRetractHeight = settings_.safeRetractHeight;
    location.machineZ = machinePositionMm()[2];
    const double workZ = workPositionMm()[2];
    location.workZ = settings_.metric ? workZ : units::convertToImperial(workZ);
    c->gcodeSafe(controller::goToLocationCommands(location), settings_.metric ? "G21" : "G20");
}

// ---- status and machine information ------------------------------------------------------

QString Machine::alarmDescription(const std::string& code) const {
    if (const controller::Controller* c = controller()) {
        if (const auto alarm = c->alarmInfo(code)) {
            return QString::fromStdString(alarm->description);
        }
    }
    return tr("No matching description found");
}

bool Machine::stepperLocked() const {
    const controller::Controller* c = controller();
    return c && c->runner().setting("$1") == "255";
}

void Machine::setStepperLock(bool lock) {
    controller::Controller* c = controller();
    if (!c) {
        return;
    }
    AppSettings settings = settings_;
    if (lock) {
        settings.stepperRestoreValue = c->runner().setting("$1");
        c->gcode(std::vector<std::string>{"$1=255", "$$"});
    } else {
        const std::string value = settings.stepperRestoreValue.empty() ? "50" : settings.stepperRestoreValue;
        c->gcode(std::vector<std::string>{"$1=" + value, "$$"});
        settings.stepperRestoreValue.clear();
    }
    setSettings(settings);
}

// ---- spindle and laser ----------------------------------------------------------------------

bool Machine::laserMode() const {
    const controller::Controller* c = controller();
    const std::string mode = c ? c->runner().setting("$32") : std::string();
    return mode.empty() ? settings_.spindle.laserMode : js::stringToNumber(mode) != 0;
}

double Machine::laserMaxPower() const {
    const controller::Controller* c = controller();
    if (c && c->isGrblHal()) {
        const std::string max = c->runner().setting("$730", "255");
        return js::stringToNumber(max);
    }
    return settings_.spindle.laser.maxPower;
}

void Machine::setLaserMode(bool laser) {
    controller::Controller* c = controller();
    if (!c) {
        return;
    }
    protocol::Runner& runner = c->runner();
    const bool hal = c->isGrblHal();
    AppSettings s = settings_;
    controller::ModeSwitch change;
    change.toLaser = laser;
    change.metric = s.metric;
    change.deviceUnits = runner.modal().units;
    change.spindleOn = runner.modal().spindle != "M5";
    change.wcs = runner.modal().wcs;
    const std::array<double, 4> work = workPositionMm();
    change.workX = work[0];
    change.workY = work[1];
    if (hal) {
        // The SLB's laser offset lives in the firmware: $770/$771 (older $741/$742).
        const auto offset = [&runner](const char* key, const char* older) {
            std::string value = runner.setting(key);
            if (value.empty()) {
                value = runner.setting(older);
            }
            return value.empty() ? 0.0 : js::stringToNumber(value);
        };
        change.offset = {offset("$770", "$741"), offset("$771", "$742")};
    } else {
        change.offset = {s.spindle.laser.xOffset, s.spindle.laser.yOffset};
    }
    const double currentMax = js::stringToNumber(runner.setting("$30", "30000"));
    const double currentMin = js::stringToNumber(runner.setting("$31", "1000"));
    if (laser) {
        if (!hal) {  // grblHAL's laser has its own range
            s.spindle.spindleMax = currentMax;
            s.spindle.spindleMin = currentMin;
            change.range = std::pair{s.spindle.laser.maxPower, s.spindle.laser.minPower};
        }
    } else {
        const bool laserSpindle = hal && std::any_of(spindles_.begin(), spindles_.end(), [](const auto& spindle) {
                                      return spindle.label == "SLB_LASER" || spindle.label == "PWM2";
                                  });
        if (!laserSpindle) {
            s.spindle.laser.maxPower = currentMax;
            s.spindle.laser.minPower = currentMin;
            change.range = std::pair{s.spindle.spindleMax, s.spindle.spindleMin};
        }
    }
    s.spindle.laserMode = laser;
    setSettings(s);
    c->gcode(controller::modeSwitchCommands(change));
    // As upstream's store, the new values count at once (no $$ follows).
    if (change.range) {
        runner.setSetting("$30", js::numberToString(change.range->first));
        runner.setSetting("$31", js::numberToString(change.range->second));
    }
    runner.setSetting("$32", laser ? "1" : "0");
    Q_EMIT settingsChanged();
}

void Machine::selectSpindle(int id) {
    controller::Controller* c = controller();
    if (!c) {
        return;
    }
    spindles_.clear();  // clearSpindles(): the list comes again
    Q_EMIT spindlesChanged();
    c->gcode(std::vector<std::string>{"M104 Q" + std::to_string(id), c->spindleListCommand()});
}

// ---- rotary ------------------------------------------------------------------------------

bool Machine::setRotaryMode(bool rotaryMode) {
    controller::Controller* c = controller();
    if (!c) {
        return false;
    }
    AppSettings settings = settings_;
    rotary::ModeSwitch change;
    change.enable = rotaryMode;
    change.grblHal = c->isGrblHal();
    if (!change.grblHal) {
        if (rotaryMode) {
            // What Y had, to restore on leaving.
            settings.rotary.defaults = rotary::currentFirmwareValues(c->settings().settings);
            change.grblSettings = settings.rotary.firmware;
        } else {
            change.grblSettings = settings.rotary.defaults;
        }
    }
    c->gcode(rotary::modeSwitchCommands(change, c->settings().settings));
    if (change.grblHal) {
        c->setRotaryMode(rotaryMode);
    }
    settings.rotary.rotaryMode = rotaryMode;
    setSettings(settings);
    return true;
}

bool Machine::runRotaryProbe(bool yAlignment) {
    controller::Controller* c = controller();
    if (!c) {
        return false;
    }
    const bool inches = c->runner().setting("$13") == "1";
    c->gcodeSafe(yAlignment ? rotary::yAxisAlignmentProbing(inches) : rotary::zAxisProbing(inches),
                 inches ? "G20" : "G21");
    return true;
}

// ---- calibration tools --------------------------------------------------------------------

bool Machine::runTuningMove(char axis, double distance) {
    controller::Controller* c = controller();
    if (!c) {
        return false;
    }
    const auto axes = controller::filterAxesForLimits({{axis, distance}}, c->state().status.pinState,
                                                      settings_.jog.preventJoggingPastLimits);
    if (!axes) {
        return false;
    }
    c->gcode(calibration::tuningMove(axis, distance, settings_.metric));
    return true;
}

void Machine::runSquaringMove(char axis, double distance) {
    if (controller::Controller* c = controller()) {
        c->gcode(calibration::squaringMove(axis, distance, settings_.metric));
    }
}

double Machine::settingNumber(const std::string& key) const {
    const controller::Controller* c = controller();
    const std::string value = c ? c->runner().setting(key) : std::string();
    return value.empty() ? std::nan("") : js::stringToNumber(value);
}

void Machine::writeFirmwareSettings(const std::vector<std::string>& lines) {
    if (controller::Controller* c = controller()) {
        c->gcode(lines);
    }
}

// ---- probing ----------------------------------------------------------------------------

std::vector<std::string> Machine::probeRoutine(probe::Axes axes, probe::ProbeType type, double toolDiameter,
                                               int corner) const {
    controller::Controller* c = controller();
    if (!c) {
        return {};
    }
    // The widget reads `settings.$13 ?? '0'` and the like.
    const auto setting = [&](const char* key, const char* fallback) {
        const std::string value = c->runner().setting(key);
        return value.empty() ? std::string(fallback) : value;
    };
    probe::MachineFacts facts;
    facts.grblHal = c->firmware() == protocol::Firmware::GrblHal;
    facts.reportInches = setting("$13", "0");
    facts.homing = setting("$22", "0");
    facts.zMaxTravel = setting("$132", "0");
    facts.machineZ = c->runner().machinePosition()[2];
    const probe::ProbingOptions options =
        probe::makeProbingOptions(settings_.probe, settings_.metric, axes, type, toolDiameter, facts);
    return probe::probeCode(options, corner);
}

bool Machine::runProbe(std::vector<std::string> code) {
    controller::Controller* c = controller();
    if (!c || code.empty() || !c->workflow().isIdle()) {
        return false;
    }
    code.push_back(c->runner().modal().distance);
    c->gcodeSafe(code, "G21");
    return true;
}

bool Machine::probeTriggered() const {
    controller::Controller* c = controller();
    return c && c->state().status.probeActive;
}

void Machine::placeSimulatedPlate(probe::ProbeType type, double toolDiameter, int corner) {
    if (!simulator_) {
        return;
    }
    const sim::SimAxes at = simulator_->machinePosition();
    const probe::ProbeSettings& p = settings_.probe;
    const double sx = corner == probe::kBottomLeft || corner == probe::kTopLeft ? 1 : -1;
    const double sy = corner == probe::kBottomLeft || corner == probe::kBottomRight ? 1 : -1;
    // The tool diameter comes in the workspace units; the simulator is mm.
    const double diameterMm = settings_.metric ? toolDiameter : units::in2mm(toolDiameter);
    double radius = type == probe::ProbeType::Diameter ? diameterMm / 2 : 0;
    std::vector<sim::Solid> solids;
    switch (p.plateType) {
        case probe::PlateType::StandardBlock: {
            // The bit 5 mm in from the plate's outer faces.
            const double thickness = p.zThickness.standardBlock;
            solids = sim::touchPlateOnCorner(corner, at[0] + 5 * sx, at[1] + 5 * sy, at[2] - 10 - thickness, thickness,
                                             p.xyThickness);
            break;
        }
        case probe::PlateType::ZProbe: {
            const double top = at[2] - 10;
            solids = {sim::Solid{{at[0] - 25, at[1] - 25, top - p.zThickness.zProbe}, {at[0] + 25, at[1] + 25, top}}};
            break;
        }
        case probe::PlateType::Probe3D:
            // A touch probe closes its circuit on the stock itself; the tip
            // starts 5 mm in from the corner.
            radius = p.tipDiameter3D / 2;
            solids = sim::touchPlateOnCorner(corner, at[0] - 5 * sx, at[1] - 5 * sy, at[2] - 10, 0, 0, 100, 30);
            break;
        case probe::PlateType::AutoZero:
        case probe::PlateType::BitZero:
            break;
    }
    simulator_->setProbeSolids(std::move(solids));
    simulator_->setToolRadius(radius);
}

// ---- controller events ----------------------------------------------------------------

void Machine::handle(const controller::ControllerEvent& event) {
    using namespace controller;
    std::visit(Overloaded{
                   [this](const ConsoleOutput& e) { Q_EMIT consoleLine(QString::fromStdString(e.text), false); },
                   [this](const ConsoleInput& e) { Q_EMIT consoleLine(QString::fromStdString(e.text), true); },
                   [this](const StateChanged&) { Q_EMIT stateChanged(); },
                   // The DRO's corner, park and MCS moves follow these.
                   [this](const HasHomedChanged&) { Q_EMIT stateChanged(); },
                   [this](const HomingFlagChanged&) { Q_EMIT stateChanged(); },
                   [this](const SettingsChanged&) { Q_EMIT settingsChanged(); },
                   [this](const WorkflowChanged& e) {
                       // A job ending: note where it got to before the sender
                       // rewinds (upstream reads its last, up to 250 ms old,
                       // sender status). A finished job offers line 1 again.
                       controller::Controller* c = controller();
                       if (e.state == WorkflowState::Idle && jobRunning_ && c) {
                           const SenderStatus status = c->sender().status();
                           lastLine_ = status.finishTime != 0
                                           ? 1
                                           : std::max<std::int64_t>(status.currentLineRunning, 1);
                           recordJob(status);
                       }
                       Q_EMIT workflowChanged();
                   },
                   [this](const JobStarted&) {
                       jobRunning_ = true;
                       jobErrors_.clear();
                       Q_EMIT workflowChanged();
                   },
                   [this](const JobStopped&) {
                       jobRunning_ = false;
                       // The workspace the job started in comes back, unless
                       // M2/M30 may leave G54 (workspace.revertWorkspace).
                       if (controller::Controller* c = controller(); c && !settings_.revertWorkspace) {
                           c->gcode(c->state().status.activeState == "Check" ? "[global.state.testWCS]"
                                                                             : "[global.state.workspace]");
                       }
                       Q_EMIT workflowChanged();
                   },
                   [this](const SenderStatusChanged&) { Q_EMIT senderStatusChanged(); },
                   [this](const ControllerClosed& e) {
                       if (jobRunning_ && e.currentLineRunning > 0) {
                           jobRunning_ = false;
                           lastLine_ = e.currentLineRunning;
                           Q_EMIT jobInterrupted(e.currentLineRunning);
                       }
                   },
                   [this](const EstimateDataRequested&) { sendEstimates(); },
                   [this](const ErrorReported& e) {
                       // The homing prompt on connecting is expected: neither
                       // reported nor kept (the status area offers homing).
                       if (isHomingRequiredAlarm(e.isAlarm, e.code, e.firmware == protocol::Firmware::GrblHal)) {
                           return;
                       }
                       // updateAlarmsErrors()
                       config::AlarmRecord record;
                       record.alarm = e.isAlarm;
                       record.code = e.code;
                       record.message = e.description;
                       record.source = e.origin;
                       record.line = e.line;
                       record.lineNumber = e.lineNumber;
                       record.controller = std::string(protocol::firmwareName(e.firmware));
                       record.time = QDateTime::currentMSecsSinceEpoch();
                       config::AlarmHistory(config_).record(std::move(record));
                       Q_EMIT historyChanged();
                       const QString title = e.isAlarm ? tr("Alarm %1").arg(QString::fromStdString(e.code))
                                                       : tr("Error %1").arg(QString::fromStdString(e.code));
                       QString detail = QString::fromStdString(e.description);
                       if (!e.line.empty() && e.line != "N/A") {
                           detail += tr("\n\nLine: %1 (%2)")
                                         .arg(QString::fromStdString(e.line), QString::fromStdString(e.origin));
                       }
                       Q_EMIT errorReported(title, detail);
                   },
                   // Kept for the Job End summary (the error itself pops up
                   // through ErrorReported, as upstream's 'error' toast).
                   [this](const GcodeError& e) {
                       const std::int64_t now = loop_.nowMs();
                       if (lastJobErrorMs_ < 0 || now - lastJobErrorMs_ >= 250) {
                           jobErrors_ << QString::fromStdString(e.message);
                           lastJobErrorMs_ = now;
                       }
                   },
                   [this](const ToolChangeRequested& e) {
                       if (isWizardStrategy(e.option)) {
                           Q_EMIT toolChangeWizardRequested(QString::fromStdString(e.option), e.count,
                                                            QString::fromStdString(e.comment));
                           return;
                       }
                       // "Pause": gSender's toast.
                       Q_EMIT notice(tr("Tool change %1 at line %2 - change the tool, then resume the job.")
                                       .arg(QString::fromStdString(e.tool.value_or("")))
                                       .arg(e.line));
                   },
                   [this](const WizardNext& e) { Q_EMIT wizardNext(e.step, e.substep); },
                   [this](const SpindleAdded& e) {
                       // addSpindle(): one entry per id, the latest wins.
                       const auto same = std::find_if(spindles_.begin(), spindles_.end(), [&e](const auto& s) {
                           return s.id == e.spindle.id;
                       });
                       if (same != spindles_.end()) {
                           *same = e.spindle;
                       } else {
                           spindles_.push_back(e.spindle);
                       }
                       Q_EMIT spindlesChanged();
                   },
                   [this](const ToolChangeStarted&) { Q_EMIT notice(tr("Tool change: running the pre-hook...")); },
                   [this](const ToolChangePreHookComplete& e) {
                       Q_EMIT toolChangeWaiting(QString::fromStdString(e.comment));
                   },
                   [this](const ProgramPaused& e) {
                       Q_EMIT notice(tr("Program paused (%1) %2")
                                       .arg(QString::fromStdString(e.data), QString::fromStdString(e.comment)));
                   },
                   [](const auto&) {},
               },
               event);
}

// ---- settings files ----------------------------------------------------------------------

QString Machine::backupSettingsIfDue(const QString& appVersion, std::int64_t nowMs) {
    const std::string& frequency = settings_.backupFrequency;
    bool due = false;
    if (frequency == "On Update") {
        due = settings_.lastBackupVersion != appVersion.toStdString();
    } else {
        constexpr std::int64_t kDay = 1000LL * 60 * 60 * 24;
        const std::int64_t interval = frequency == "Daily" ? kDay : frequency == "Weekly" ? 7 * kDay : 30 * kDay;
        due = nowMs - settings_.lastBackupTime >= interval;
    }
    QFile source(QString::fromStdWString(config_.file().wstring()));
    if (!due || !source.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray content = source.readAll();
    QString folder = QString::fromStdString(settings_.backupLocation);
    if (folder.isEmpty() || !QDir(folder).exists()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(folder);
    }
    // new Date().toISOString() with the colons replaced.
    const QString stamp =
        QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::UTC).toString("yyyy-MM-ddTHH-mm-ss.zzzZ");
    const QString path = QDir(folder).filePath("preferences-backup-" + stamp + ".json");
    QFile backup(path);
    if (!backup.open(QIODevice::WriteOnly) || backup.write(content) != content.size()) {
        return {};
    }
    backup.close();
    AppSettings settings = settings_;
    settings.lastBackupTime = nowMs;
    settings.lastBackupVersion = appVersion.toStdString();
    setSettings(settings);
    return path;
}

bool Machine::exportSettings(const QString& path, QString* error) const {
    const boost::json::object out{{"format", "gsender-cpp-settings"},
                                  {"version", 1},
                                  {"app", appSettingsToJson(settings_)},
                                  {"events", config_.get("events", boost::json::object())}};
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = file.errorString();
        return false;
    }
    const std::string text = boost::json::serialize(out);
    file.write(text.data(), static_cast<qint64>(text.size()));
    return true;
}

bool Machine::importSettings(const QString& path, QString* report) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *report = tr("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    boost::system::error_code error;
    const boost::json::value data =
        boost::json::parse(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), error);
    if (error) {
        *report = tr("Failed to import settings. Please check the file format.");
        return false;
    }
    AppSettings imported;
    std::optional<boost::json::object> events;
    const boost::json::object* object = data.if_object();
    if (object && object->contains("app") && object->at("app").is_object()) {
        imported = appSettingsFromJson(object->at("app").as_object());
        if (const boost::json::value* hooks = object->if_contains("events"); hooks && hooks->is_object()) {
            events = hooks->as_object();
        }
        *report = tr("Settings imported.");
    } else if (std::optional<GSenderSettings> gsender = readGSenderSettings(data)) {
        imported = std::move(gsender->settings);
        events = std::move(gsender->events);
        // Shortcuts: the port's actions only, kept where they differ from
        // its defaults (as the shortcut editor stores them).
        const std::vector<ShortcutAction> actions = shortcutActions(*this);
        std::map<std::string, ShortcutBinding> kept;
        for (const auto& [id, binding] : imported.shortcuts) {
            const ShortcutAction* action = findShortcutAction(actions, QString::fromStdString(id));
            if (!action) {
                continue;
            }
            const QKeySequence keys = QKeySequence::fromString(QString::fromStdString(binding.keys));
            const QKeySequence defaults = QKeySequence::fromString(action->defaultKeys);
            if (keys != defaults || binding.active != action->defaultActive) {
                kept[id] = ShortcutBinding{keys.toString(QKeySequence::PortableText).toStdString(), binding.active};
            }
        }
        imported.shortcuts = std::move(kept);
        *report = tr("gSender settings imported; %1 keyboard shortcut(s) differ from the defaults.")
                      .arg(imported.shortcuts.size());
        if (!gsender->unreadableShortcuts.empty()) {
            *report += " " + tr("%1 shortcut(s) had keys the port cannot read.").arg(gsender->unreadableShortcuts.size());
        }
    } else {
        *report = tr("Failed to import settings. Please check the file format.");
        return false;
    }
    setSettings(imported);
    if (events) {
        config_.set("events", *events);
    }
    return true;
}

void Machine::restoreDefaultSettings() {
    setSettings(AppSettings{});
    config::EventStore(config_).clear();  // restoreDefault(): the event hooks go too
}

// ---- history ---------------------------------------------------------------------------

void Machine::recordJob(const controller::SenderStatus& status) {
    const controller::Controller* c = controller();
    // The sender's times are the event loop's (monotonic); the records keep
    // wall-clock dates.
    const std::int64_t wallNow = QDateTime::currentMSecsSinceEpoch();
    const std::int64_t loopNow = loop_.nowMs();
    const auto wall = [&](std::int64_t t) { return wallNow - (loopNow - t); };
    config::JobRecord job;
    job.file = status.name;
    job.path = programPath_.toStdString();
    job.totalLines = status.total;
    job.port = port_.toStdString();
    job.controller = c ? std::string(protocol::firmwareName(c->firmware())) : std::string();
    job.startTime = wall(status.startTime);
    if (status.finishTime > 0) {
        job.endTime = wall(status.finishTime);
    }
    job.duration = status.elapsedTime;
    job.completed = status.finishTime > 0;
    config::JobStatsStore(config_).record(std::move(job), status.timeRunning);
    config::MaintenanceStore(config_).addRunTime(status.timeRunning);
    Q_EMIT historyChanged();
    const QStringList errors = jobErrors_;
    jobErrors_.clear();
    Q_EMIT jobEnded(status.finishTime > 0, static_cast<double>(status.elapsedTime), errors);
}

const config::MachineProfile& Machine::machineProfile() const {
    if (const config::MachineProfile* chosen = config::findMachineProfile(settings_.machineProfileId)) {
        return *chosen;
    }
    return *config::findMachineProfile(config::defaultMachineProfileId());
}

config::BoardContext Machine::boardContext() const {
    config::BoardContext board;
    if (const controller::Controller* c = controller()) {
        board.grblHal = c->isGrblHal();
        board.semver = c->settings().semver;
        if (const auto info = c->settings().info.find("BOARD"); info != c->settings().info.end()) {
            board.boardId = info->second.text;
        }
    }
    return board;
}

std::vector<config::MaintenanceTask> Machine::dueMaintenanceTasks() {
    std::vector<config::MaintenanceTask> due;
    for (const config::MaintenanceTask& task : config::MaintenanceStore(config_).list()) {
        if (task.currentTime >= task.rangeStart) {
            due.push_back(task);
        }
    }
    return due;
}

void Machine::resetMaintenanceTimers(const std::vector<int>& ids) {
    config::MaintenanceStore store(config_);
    std::vector<config::MaintenanceTask> tasks = store.list();
    for (config::MaintenanceTask& task : tasks) {
        if (std::find(ids.begin(), ids.end(), task.id) != ids.end()) {
            task.currentTime = 0;
        }
    }
    store.save(tasks);
    Q_EMIT historyChanged();
}

// ---- program ---------------------------------------------------------------------------

bool Machine::loadFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QFileInfo info(path);
    loadProgram(info.fileName(), std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                info.absoluteFilePath());
    AppSettings settings = settings_;
    addRecentFile(settings.recentFiles, {info.fileName().toStdString(), info.absoluteFilePath().toStdString(),
                                         info.size(), QDateTime::currentMSecsSinceEpoch()});
    setSettings(settings);
    return true;
}

void Machine::forgetRecentFile(const QString& path) {
    AppSettings settings = settings_;
    std::erase_if(settings.recentFiles, [&path](const RecentFile& f) { return f.filePath == path.toStdString(); });
    setSettings(settings);
}

void Machine::clearRecentFiles() {
    AppSettings settings = settings_;
    settings.recentFiles.clear();
    setSettings(settings);
}

void Machine::loadProgram(const QString& name, std::string text, const QString& path) {
    programName_ = name;
    programPath_ = path;
    programText_ = std::move(text);
    analysis_ = {};
    toolpath_ = {};
    attachProgram();

    // Analyse in the background; a newer load supersedes this one.
    if (analysisCancel_) {
        *analysisCancel_ = true;
    }
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    analysisCancel_ = cancel;
    const std::uint64_t generation = ++analysisGeneration_;
    analyzing_ = true;
    const gcode::InterpreterOptions options =
        controller() ? job::interpreterOptionsFor(controller()->settings()) : gcode::InterpreterOptions{};
    auto program = std::make_shared<const std::string>(programText_);
    QThreadPool::globalInstance()->start([this, program, options, cancel, generation] {
        ToolpathSink sink;
        job::ProgramAnalysis analysis = job::analyzeProgram(*program, options, &sink, [&cancel] { return cancel->load(); });
        if (analysis.cancelled) {
            return;
        }
        // Queued to the UI thread; dropped if the Machine is gone (its
        // destructor also waits for this task).
        QMetaObject::invokeMethod(
            this,
            [this, generation, analysis = std::move(analysis), path = std::move(sink.path)]() mutable {
                analysisFinished(generation, std::move(analysis), std::move(path));
            },
            Qt::QueuedConnection);
    });
    Q_EMIT programChanged();
}

void Machine::analysisFinished(std::uint64_t generation, job::ProgramAnalysis analysis, Toolpath toolpath) {
    if (generation != analysisGeneration_) {
        return;
    }
    analysis_ = std::move(analysis);
    toolpath_ = std::move(toolpath);
    analyzing_ = false;
    sendEstimates();
    Q_EMIT programChanged();
    // maybeWarnInvalidLines()
    if (settings_.warnBadFile && !analysis_.invalidLines.empty()) {
        QStringList sample;
        for (std::size_t i = 0; i < analysis_.invalidLines.size() && i < 5; ++i) {
            sample << QString::fromStdString(analysis_.invalidLines[i]);
        }
        Q_EMIT invalidLinesFound(static_cast<int>(analysis_.invalidLines.size()), sample);
    }
}

void Machine::unloadProgram() {
    if (analysisCancel_) {
        *analysisCancel_ = true;
    }
    ++analysisGeneration_;
    analyzing_ = false;
    programName_.clear();
    programText_.clear();
    analysis_ = {};
    toolpath_ = {};
    if (controller::Controller* c = controller()) {
        c->unloadProgram();
    }
    Q_EMIT programChanged();
}

void Machine::attachProgram() {
    controller::Controller* c = controller();
    if (!c || !hasProgram()) {
        return;
    }
    c->loadFile(programName_.toStdString(), programText_);
}

void Machine::sendEstimates() {
    controller::Controller* c = controller();
    if (c && hasProgram() && !analyzing_) {
        c->updateEstimateData(analysis_.estimates, analysis_.estimatedTime);
    }
}

void Machine::setSettings(const AppSettings& settings) {
    settings_ = settings;
    *preferences_ = settings_.preferences;  // the controller reads it live
    if (controller::Controller* c = controller()) {
        c->setToolChangeContext(settings_.toolChange);
    }
    saveAppSettings(config_, settings_);
    Q_EMIT appSettingsChanged();
}

void Machine::sendConsoleLine(const QString& line) {
    if (controller::Controller* c = controller()) {
        c->writeConsoleLine(line.toStdString());
    }
}

}  // namespace gs::app
