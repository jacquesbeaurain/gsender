#include "machine.hpp"

#include "qt_event_loop.hpp"

#include "gs/config/records.hpp"
#include "gs/sim/grbl_simulator.hpp"
#include "gs/transport/asio_link.hpp"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QThreadPool>

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
// with arcs tessellated back out of their plane.
class ToolpathSink final : public gcode::GeometrySink {
public:
    Toolpath path;

    void atLine(std::size_t index) override { line_ = static_cast<std::uint32_t>(index); }

    void addLine(const gcode::Modal& modal, const gcode::Vec4& from, const gcode::Vec4& to) override {
        if (modal.motion == "G0") {
            push(path.rapids, from, to);
            path.rapidLines.push_back(line_);
        } else {
            push(path.feeds, from, to);
            path.feedLines.push_back(line_);
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

    static void push(std::vector<float>& out, const gcode::Vec4& from, const gcode::Vec4& to) {
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
}

void Machine::disconnectFromMachine() {
    teardown();
    Q_EMIT connectionChanged();
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
        probe::makeProbingOptions(settings_.probe, true, axes, type, toolDiameter, facts);
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
    double radius = type == probe::ProbeType::Diameter ? toolDiameter / 2 : 0;
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
                   [this](const SettingsChanged&) { Q_EMIT settingsChanged(); },
                   [this](const WorkflowChanged&) { Q_EMIT workflowChanged(); },
                   [this](const JobStarted&) { Q_EMIT workflowChanged(); },
                   [this](const JobStopped&) { Q_EMIT workflowChanged(); },
                   [this](const SenderStatusChanged&) { Q_EMIT senderStatusChanged(); },
                   [this](const EstimateDataRequested&) { sendEstimates(); },
                   [this](const ErrorReported& e) {
                       const QString title = e.isAlarm ? tr("Alarm %1").arg(QString::fromStdString(e.code))
                                                       : tr("Error %1").arg(QString::fromStdString(e.code));
                       QString detail = QString::fromStdString(e.description);
                       if (!e.line.empty() && e.line != "N/A") {
                           detail += tr("\n\nLine: %1 (%2)")
                                         .arg(QString::fromStdString(e.line), QString::fromStdString(e.origin));
                       }
                       Q_EMIT errorReported(title, detail);
                   },
                   [this](const GcodeError& e) { Q_EMIT notice(QString::fromStdString(e.message)); },
                   [this](const ToolChangeRequested& e) {
                       Q_EMIT notice(tr("Tool change %1 at line %2 - change the tool, then resume the job.")
                                       .arg(QString::fromStdString(e.tool.value_or("")))
                                       .arg(e.line));
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
    loadProgram(QFileInfo(path).fileName(), std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    return true;
}

void Machine::loadProgram(const QString& name, std::string text) {
    programName_ = name;
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
