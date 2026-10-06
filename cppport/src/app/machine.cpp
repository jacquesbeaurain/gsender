#include "machine.hpp"
#include "firmware_service.hpp"
#include "plugin_service.hpp"
#include "job_service.hpp"
#include "jog_service.hpp"
#include "probe_service.hpp"
#include "tool_change_service.hpp"

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
#include "gs/transport/ftp_upload.hpp"
#include "gs/transport/port_list.hpp"

#include <QDateTime>
#include <QDebug>
#include <QTimeZone>
#include <QStandardPaths>
#include <QDir>
#include <QKeySequence>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QThreadPool>
#include <QTimer>
#include <QCoreApplication>

#include <boost/json.hpp>

#include <cmath>
#include <numbers>

namespace gs::app {

const QString Machine::kSimulatorPort = QStringLiteral("Simulator");
const QString Machine::kSimulatorHalPort = QStringLiteral("Simulator grblHAL");

namespace {

// ACCESSORY_AUTOCONFIG_KEYS: the [NEWOPT:] / Autoconfig keys that stand for
// an accessory being there.
constexpr std::string_view kAccessoryKeys[] = {"AUTOSPIN", "H100", "ATCEXP", "ETHERNET", "PROBE", "TLS"};

bool isAccessoryKey(std::string_view key) {
    return std::find(std::begin(kAccessoryKeys), std::end(kAccessoryKeys), key) != std::end(kAccessoryKeys);
}

}  // namespace

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// Collects the toolpath while the program is analysed: straight segments,
// with arcs tessellated back out of their plane. A turns the stock about X:
// as Visualize.worker feeds gviewer, every point is turned by -A
// (rotateAxis: G-code's negative A turns clockwise), and a move that turns
// A is drawn in 5-degree chords. `zOffset` raises every point first
// ("Visualize non-center zeros": the stock's radius).
class ToolpathSink final : public gcode::GeometrySink {
public:
    Toolpath path;
    double zOffset = 0;

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

    gcode::Vec4 wrap(const gcode::Vec4& point) const {
        const gcode::Vec4 v{point.x, point.y, point.z + zOffset, point.a};
        if (v.a == 0) {
            return v;
        }
        const double angle = -v.a * std::numbers::pi / 180;
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
    job::analyzeProgram(program, {}, {}, &sink);
    return std::move(sink.path);
}

Machine::Machine(QtEventLoop& loop, std::filesystem::path configFile, QObject* parent)
    : QObject(parent), loop_(loop), preferences_(std::make_shared<controller::Preferences>()),
      consoleLog_(new ConsoleLog(this)) {
    config::validateAndRepair(configFile);
    config_.onError = [this](const std::string& message) {
        Q_EMIT errorReported(tr("Configuration"), QString::fromStdString(message));
    };
    config_.load(std::move(configFile));
    settings_ = loadAppSettings(config_);
    *preferences_ = settings_.preferences;
    jobService_ = std::make_unique<JobService>(*this);
    jogService_ = std::make_unique<JogService>(*this);
    probeService_ = std::make_unique<ProbeService>(*this);
    toolChangeService_ = std::make_unique<ToolChangeService>(*this);
    firmwareService_ = std::make_unique<FirmwareService>(*this);
    const auto configDir = configFile.empty() ? std::filesystem::current_path() : std::filesystem::absolute(configFile).parent_path();
    const QString storageDir = QString::fromStdString((configDir / "plugins-data").string());
    pluginService_ = std::make_unique<PluginService>(*this, storageDir);
    pluginService_->addSearchPath(QString::fromStdString((configDir / "plugins").string()));
    pluginService_->addSearchPath(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));
    pluginService_->addSearchPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../plugins")));
    pluginService_->addSearchPath(QDir::current().filePath(QStringLiteral("plugins")));
    pluginService_->scanPlugins();
    reestimateTimer_ = new QTimer(this);
    reestimateTimer_->setSingleShot(true);
    reestimateTimer_->setInterval(750);
    connect(reestimateTimer_, &QTimer::timeout, this, &Machine::reestimate);
}

Machine::~Machine() {
    if (analysisCancel_) {
        *analysisCancel_ = true;
    }
    if (reestimateCancel_) {
        *reestimateCancel_ = true;
    }
    QThreadPool::globalInstance()->waitForDone();
    teardown();
}

// ---- connection -------------------------------------------------------------------

bool Machine::isRunningSdFile() const {
    const controller::Controller* c = controller();
    return c && c->state().status.sdProgress.name.has_value();
}

controller::Controller* Machine::controller() const {
    return session_ ? session_->controller() : nullptr;
}

bool Machine::isConnected() const {
    return controller() != nullptr;
}

void Machine::startSession(controller::DeviceLink& link) {
    controller::ControllerHooks hooks = config::makeControllerHooks(config_);
    hooks.preferences = preferences_;
    // SD card uploads to a networked grblHAL (GrblHALFTP).
    hooks.ftpUpload = [this](controller::FtpUploadRequest request, controller::UploadCallbacks callbacks) {
        if (!ftp_) {
            ftp_ = std::make_unique<transport::FtpUploader>(
                [this](std::function<void()> fn) { loop_.post(std::move(fn)); });
        }
        std::vector<transport::FtpFile> files;
        for (protocol::YModemFile& file : request.files) {
            files.push_back({std::move(file.name), std::move(file.data)});
        }
        transport::FtpOptions options;
        options.host = request.host;
        options.port = static_cast<std::uint16_t>(request.port);
        options.user = request.user;
        options.password = request.password;
        if (!ftp_->upload(std::move(options), std::move(files),
                          {callbacks.started, callbacks.progress, callbacks.completed, callbacks.failed}) &&
            callbacks.failed) {
            callbacks.failed("An upload is already running.");
        }
    };
    session_ = std::make_unique<controller::Session>(
        loop_, link, controller::SessionOptions{settings_.defaultFirmware}, std::move(hooks),
        [this](const controller::ControllerEvent& event) { handle(event); });
    session_->onLine = [this](std::string_view line) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.remove_suffix(1);
        }
        Q_EMIT rawLine(QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size())));
    };
    session_->onController = [this](controller::Controller& c, bool) {
        connecting_ = false;
        // The console's banner for the connection.
        consoleLog_->write(QString("gSender - [%1]").arg(QString::fromStdString(std::string(protocol::firmwareName(c.firmware())))),
                           ConsoleType::System);
        consoleLog_->write(QString("Connected to %1 with a baud rate of %2").arg(port_).arg(baudRate_),
                           ConsoleType::System);
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
    baudRate_ = baudRate;
    connecting_ = true;
    Q_EMIT connectionChanged();

    if (isSimulatorPort(port)) {
        simulator_ = std::make_unique<sim::GrblSimulator>(loop_);
        simulator_->setGrblHal(port == kSimulatorHalPort);
        simulator_->setReportAfterDwell(true);
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
    // Each connection starts with a clean console.
    consoleLog_->clear();
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

bool Machine::reconnectAutomatically() {
    const AppSettings& s = settings();
    if (!s.autoReconnect || s.port.empty() || isConnected() || isConnecting()) {
        return false;
    }
    const QString port = QString::fromStdString(s.port);
    bool known = isSimulatorPort(port) || transport::looksLikeIpAddress(s.port);
    for (const transport::SerialPortInfo& info : transport::listSerialPorts()) {
        known = known || info.path == s.port;
    }
    if (!known) {
        return false;
    }
    connectTo(port, s.baudRate, s.networkPort);
    return true;
}

void Machine::disconnectFromMachine() {
    teardown();
    Q_EMIT connectionChanged();
}

// ---- tool change wizards ---------------------------------------------------------------

bool Machine::isWizardStrategy(const std::string& option) {
    return ToolChangeService::isWizardStrategy(option);
}

std::optional<toolchange::Wizard> Machine::startToolChangeWizard(const std::string& option, int count,
                                                                 bool fullFirstWizard) {
    return toolChangeService_->startToolChangeWizard(option, count, fullFirstWizard);
}

bool Machine::isToolChangeWizardReady() const {
    return toolChangeService_->isToolChangeWizardReady();
}

void Machine::runWizardAction(int step, int substep, const std::vector<std::string>& gcode) {
    toolChangeService_->runWizardAction(step, substep, gcode);
}

void Machine::completeToolChangeWizard() {
    toolChangeService_->completeToolChangeWizard();
}

void Machine::cancelToolChangeWizard() {
    toolChangeService_->cancelToolChangeWizard();
}

// ---- file context and outline -------------------------------------------------------------

expr::Value Machine::fileContext() const {
    return jobService_->fileContext();
}

bool Machine::runOutline(QString* error) {
    return jobService_->runOutline(error);
}

// ---- start from line ---------------------------------------------------------------------

bool Machine::startFromLine(std::size_t line, double safeHeight) {
    return jobService_->startFromLine(line, safeHeight);
}

// ---- positions ------------------------------------------------------------------------

void Machine::zeroAxis(char axis) {
    jogService_->zeroAxis(axis);
}

void Machine::zeroAllAxes() {
    jogService_->zeroAllAxes();
}

void Machine::goToZero(std::string_view axes) {
    jogService_->goToZero(axes);
}

std::array<double, 4> Machine::workPositionMm() const {
    return jogService_->workPositionMm();
}

std::array<double, 4> Machine::machinePositionMm() const {
    return jogService_->machinePositionMm();
}

bool Machine::canMove() const {
    return jogService_->canMove();
}

bool Machine::homingEnabled() const {
    return jogService_->homingEnabled();
}

bool Machine::singleAxisHoming() const {
    return jogService_->singleAxisHoming();
}

void Machine::selectWorkspace(const QString& wcs) {
    jogService_->selectWorkspace(wcs);
}

void Machine::setWorkPosition(char axis, double value) {
    jogService_->setWorkPosition(axis, value);
}

void Machine::homeAxis(char axis) {
    jogService_->homeAxis(axis);
}

void Machine::goToCorner(controller::MachineCorner corner) {
    jogService_->goToCorner(corner);
}

void Machine::goToPark() {
    jogService_->goToPark();
}

void Machine::goToMachinePosition(const toolchange::MachinePosition& position) {
    jogService_->goToMachinePosition(position);
}

void Machine::goToLocation(controller::GoToMode mode, double x, double y, double z, double a) {
    jogService_->goToLocation(mode, x, y, z, a);
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
    return firmwareService_->stepperLocked();
}

void Machine::setStepperLock(bool lock) {
    firmwareService_->setStepperLock(lock);
}

// ---- spindle and laser ----------------------------------------------------------------------

bool Machine::laserMode() const {
    return firmwareService_->laserMode();
}

double Machine::laserMaxPower() const {
    return firmwareService_->laserMaxPower();
}

void Machine::setLaserMode(bool laser) {
    firmwareService_->setLaserMode(laser);
}

void Machine::selectSpindle(int id) {
    firmwareService_->selectSpindle(id);
}

// ---- rotary ------------------------------------------------------------------------------

bool Machine::setRotaryMode(bool rotaryMode) {
    return firmwareService_->setRotaryMode(rotaryMode);
}

bool Machine::runRotaryProbe(bool yAlignment) {
    return probeService_->runRotaryProbe(yAlignment);
}

// ---- calibration tools --------------------------------------------------------------------

bool Machine::runTuningMove(char axis, double distance) {
    return firmwareService_->runTuningMove(axis, distance);
}

void Machine::runSquaringMove(char axis, double distance) {
    firmwareService_->runSquaringMove(axis, distance);
}

double Machine::settingNumber(const std::string& key) const {
    return firmwareService_->settingNumber(key);
}

void Machine::writeFirmwareSettings(const std::vector<std::string>& lines) {
    firmwareService_->writeFirmwareSettings(lines);
}

// ---- probing ----------------------------------------------------------------------------

std::vector<std::string> Machine::probeRoutine(probe::Axes axes, probe::ProbeType type, double toolDiameter,
                                               int corner) const {
    return probeService_->probeRoutine(axes, type, toolDiameter, corner);
}

bool Machine::runProbe(std::vector<std::string> code) {
    return probeService_->runProbe(std::move(code));
}

bool Machine::probeTriggered() const {
    return probeService_->probeTriggered();
}

void Machine::placeSimulatedPlate(probe::ProbeType type, double toolDiameter, int corner,
                                  probe::Axes axes) {
    probeService_->placeSimulatedPlate(type, toolDiameter, corner, axes);
}

// ---- controller events ----------------------------------------------------------------

void Machine::handle(const controller::ControllerEvent& event) {
    using namespace controller;
    std::visit(Overloaded{
                   [this](const ConsoleOutput& e) {
                       const QString text = QString::fromStdString(e.text);
                       const QString line = text.trimmed();
                       consoleLog_->write(line, classifyRead(line));
                       Q_EMIT consoleLine(text, false);
                   },
                   [this](const ConsoleInput& e) {
                       const QString text = QString::fromStdString(e.text);
                       // Other than printable ASCII shows as \xNN.
                       QString line;
                       for (const QChar c : text.trimmed()) {
                           line += c.unicode() >= 0x20 && c.unicode() <= 0x7E
                                       ? QString(c)
                                       : QString("\\x%1").arg(static_cast<int>(c.unicode()), 0, 16);
                       }
                       consoleLog_->write(line, classifyWrite(e.source));
                       Q_EMIT consoleLine(text, true);
                   },
                   [this](const StateChanged&) { Q_EMIT stateChanged(); },
                   // The DRO's corner, park and MCS moves follow these.
                   [this](const HasHomedChanged&) { Q_EMIT stateChanged(); },
                   [this](const HomingFlagChanged&) { Q_EMIT stateChanged(); },
                   [this](const SettingsChanged&) {
                       Q_EMIT settingsChanged();
                       scheduleReestimate();
                   },
                   // Real-world data for tuning the estimator (upstream's server log).
                   [](const JobEstimateAccuracy& e) { qInfo().noquote() << QString::fromStdString(e.text); },
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
                   [this](const FeederStatusChanged& e) {
                       // The routine's last line taken: it went through.
                       if (probing_ && e.status.queue == 0 && !e.status.pending && !e.status.hold) {
                           probing_ = false;
                           Q_EMIT probeSucceeded();
                       }
                   },
                   [this](const ControllerClosed& e) {
                       probing_ = false;
                       if (jobRunning_ && e.currentLineRunning > 0) {
                           jobRunning_ = false;
                           lastLine_ = e.currentLineRunning;
                           Q_EMIT jobInterrupted(e.currentLineRunning);
                       }
                   },
                   [this](const EstimateDataRequested&) { sendEstimates(); },
                   [this](const ErrorReported& e) {
                       if (e.isAlarm) {
                           probing_ = false;  // a failed probe alarms
                       }
                       // "Warn on bad line": the Invalid Line helper, for any error.
                       if (!e.isAlarm && settings_.preferences.showLineWarnings) {
                           Q_EMIT lineWarning(QString::fromStdString(e.code), QString::fromStdString(e.line));
                       }
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
                       Q_EMIT toolChangeRequired();
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
                   [this](const GrblHalInfo& e) {
                       // [NEWOPT:] says which accessories are there to begin with.
                       if (e.info.name != "NEWOPT") {
                           return;
                       }
                       for (const auto& [key, value] : e.info.value.options) {
                           if (isAccessoryKey(key)) {
                               accessoryConnected_[key] = value && *value != "0";
                           }
                       }
                   },
                   [this](const GrblHalAutoconfig& e) {
                       for (const auto& [key, value] : e.autoconfig.values) {
                           if (!isAccessoryKey(key)) {
                               continue;
                           }
                           const bool connected = value != "0";
                           const auto known = accessoryConnected_.find(key);
                           if (known != accessoryConnected_.end() && known->second != connected) {
                               Q_EMIT accessoryConnectivityChanged(QString::fromStdString(key), connected);
                           }
                           accessoryConnected_[key] = connected;
                       }
                   },
                   [this](const YModemStarted&) { Q_EMIT sdUploadStarted(); },
                   [this](const YModemProgress& e) { Q_EMIT sdUploadProgress(e.percent); },
                   [this](const YModemCompleted&) { Q_EMIT sdUploadCompleted(); },
                   [this](const YModemFailed& e) { Q_EMIT sdUploadFailed(QString::fromStdString(e.message)); },
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
    return jobService_->loadFile(path, error);
}

void Machine::forgetRecentFile(const QString& path) {
    jobService_->forgetRecentFile(path);
}

void Machine::clearRecentFiles() {
    jobService_->clearRecentFiles();
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
    const gcode::EstimatorSettings estimator = estimatorSettings();
    estimatorSettings_ = estimator;
    auto program = std::make_shared<const std::string>(programText_);
    const bool nonCenterZeros = settings_.rotary.diameterOffset;
    QThreadPool::globalInstance()->start([this, program, estimator, cancel, generation, nonCenterZeros] {
        ToolpathSink sink;
        // "Visualize non-center zeros": a file declaring its cylinder and
        // never moving Y is drawn about the stock's axis.
        if (nonCenterZeros) {
            if (const rotary::RotaryMetadata metadata = rotary::parseRotaryMetadata(*program);
                metadata.radius && !metadata.hasYAxisMoves) {
                sink.zOffset = *metadata.radius;
            }
        }
        job::ProgramAnalysis analysis =
            job::analyzeProgram(*program, {}, estimator, &sink, [&cancel] { return cancel->load(); });
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
        c->updateEstimateData(analysis_.lineTime, analysis_.lineKind, analysis_.estimatedTime);
    }
}

gcode::EstimatorSettings Machine::estimatorSettings() const {
    job::EstimatorInputs inputs;
    inputs.laserMode = settings_.spindle.laserMode;
    inputs.useAaxisForGrbl = settings_.preferences.useAaxisForGrbl;
    inputs.baudRate = baudRate_;
    const controller::Controller* c = controller();
    inputs.grblHal = c && c->isGrblHal();
    return job::estimatorSettingsFor(c ? c->settings() : protocol::FirmwareSettings{}, inputs);
}

void Machine::scheduleReestimate() {
    reestimateTimer_->start();
}

void Machine::reestimate() {
    // Never swap estimates under a running job.
    if (!hasProgram() || analyzing_ || jobRunning_) {
        return;
    }
    const gcode::EstimatorSettings estimator = estimatorSettings();
    if (estimator == estimatorSettings_) {
        return;
    }
    estimatorSettings_ = estimator;

    if (reestimateCancel_) {
        *reestimateCancel_ = true;
    }
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    reestimateCancel_ = cancel;
    const std::uint64_t generation = ++reestimateGeneration_;
    const std::uint64_t program = analysisGeneration_;
    auto text = std::make_shared<const std::string>(programText_);
    QThreadPool::globalInstance()->start([this, text, estimator, cancel, generation, program] {
        std::optional<gcode::EstimateResult> estimate =
            job::estimateProgram(*text, estimator, [&cancel] { return cancel->load(); });
        if (!estimate) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, program, result = std::move(*estimate)]() mutable {
                if (generation != reestimateGeneration_ || program != analysisGeneration_ || analyzing_) {
                    return;
                }
                analysis_.lineTime = std::move(result.lineTime);
                analysis_.lineKind = std::move(result.lineKind);
                analysis_.estimatedTime = result.totalTime;
                sendEstimates();
                Q_EMIT programChanged();
            },
            Qt::QueuedConnection);
    });
}

void Machine::setSettings(const AppSettings& settings) {
    // Spindle/laser controls going off in laser mode go back to the spindle
    // (the setting's onUpdate).
    const bool leaveLaser = settings_.spindleFunctions && !settings.spindleFunctions && settings.spindle.laserMode;
    settings_ = settings;
    *preferences_ = settings_.preferences;  // the controller reads it live
    if (controller::Controller* c = controller()) {
        c->setToolChangeContext(settings_.toolChange);
    }
    saveAppSettings(config_, settings_);
    Q_EMIT appSettingsChanged();
    scheduleReestimate();  // the spindle mode and the A axis feed it too
    if (leaveLaser) {
        setLaserMode(false);
    }
}

void Machine::sendConsoleLine(const QString& line) {
    if (controller::Controller* c = controller()) {
        c->writeConsoleLine(line.toStdString());
    }
}

}  // namespace gs::app
