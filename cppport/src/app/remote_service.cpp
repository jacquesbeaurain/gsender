#include "remote_service.hpp"

#include "jogger.hpp"
#include "machine.hpp"

#include "gs/controller/actions.hpp"
#include "gs/controller/controller.hpp"
#include "gs/core/resources.hpp"
#include "gs/transport/network_list.hpp"
#include "gs/transport/remote_server.hpp"
#include "gs/util/units.hpp"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gs::app {
namespace {

// Changes within this window reach the pendants as one state message.
constexpr int kStateDelayMs = 50;
// A held jog with no word from its pendant for this long is released.
constexpr qint64 kJogSilenceMs = 1000;

}  // namespace

RemoteService::RemoteService(Machine& machine, Jogger& jogger, QObject* parent)
    : QObject(parent),
      machine_(machine),
      jogger_(jogger),
      server_(std::make_unique<transport::RemoteServer>([this](std::function<void()> fn) {
          // Any thread; dropped if this service is gone by then.
          QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
      })),
      stateTimer_(new QTimer(this)),
      watchdogTimer_(new QTimer(this)) {
    if (const auto page = resources::find("remote/pendant.html")) {
        server_->setResource("/", {"text/html; charset=utf-8", std::string(*page)});
        server_->setResource("/index.html", {"text/html; charset=utf-8", std::string(*page)});
    }
    server_->onClientConnected = [this](transport::RemoteServer::ClientId id) {
        server_->send(id, remote::stateMessage(state()));
        Q_EMIT changed();
    };
    server_->onClientDisconnected = [this](transport::RemoteServer::ClientId id) {
        if (id == jogClient_) {
            releaseJog();
        }
        Q_EMIT changed();
    };
    server_->onMessage = [this](transport::RemoteServer::ClientId id, const std::string& text) {
        if (id == jogClient_) {
            jogHeard_.restart();
        }
        if (const auto command = remote::parseCommand(text)) {
            handle(*command, id);
        }
    };

    stateTimer_->setSingleShot(true);
    stateTimer_->setInterval(kStateDelayMs);
    connect(stateTimer_, &QTimer::timeout, this, &RemoteService::pushState);
    watchdogTimer_->setInterval(250);
    connect(watchdogTimer_, &QTimer::timeout, this, &RemoteService::checkJogWatchdog);

    for (auto signal : {&Machine::stateChanged, &Machine::settingsChanged, &Machine::workflowChanged,
                        &Machine::senderStatusChanged, &Machine::connectionChanged, &Machine::programChanged,
                        &Machine::appSettingsChanged}) {
        connect(&machine_, signal, this, &RemoteService::scheduleState);
    }
    connect(&jogger_, &Jogger::changed, this, &RemoteService::scheduleState);
}

RemoteService::~RemoteService() {
    releaseJog();
    server_->stop();
}

remote::RemoteSettings RemoteService::settings() const {
    return remote::settingsFromJson(machine_.config().get(remote::kSettingsKey));
}

QString RemoteService::apply(const remote::RemoteSettings& requested) {
    const std::string invalid = remote::validateSettings(requested.ip, requested.port);
    if (!invalid.empty()) {
        return QString::fromStdString(invalid);
    }
    remote::RemoteSettings saved = requested;
    saved.error = false;
    QString error;
    if (saved.headlessStatus) {
        error = startServer(QString::fromStdString(saved.ip), saved.port);
        if (!error.isEmpty()) {
            // server/index.js: an address it cannot bind switches remote
            // mode off until it is enabled again, and flags the error.
            saved.headlessStatus = false;
            saved.error = true;
        }
    } else {
        stopServer();
    }
    machine_.config().set(remote::kSettingsKey, remote::settingsToJson(saved));
    return error;
}

QString RemoteService::startFromSettings() {
    const remote::RemoteSettings saved = settings();
    // server/index.js ignores the setting without an address, or with 0.0.0.0.
    if (!saved.headlessStatus || saved.ip.empty() || saved.ip == "0.0.0.0") {
        return {};
    }
    const QString error = startServer(QString::fromStdString(saved.ip), saved.port);
    if (!error.isEmpty()) {
        remote::RemoteSettings off = saved;
        off.headlessStatus = false;
        off.error = true;
        machine_.config().set(remote::kSettingsKey, remote::settingsToJson(off));
    }
    return error;
}

QString RemoteService::startServer(const QString& host, int port) {
    releaseJog();
    if (port < 0 || port > 65535) {
        lastError_ = tr("Invalid port %1").arg(port);
        Q_EMIT changed();
        return lastError_;
    }
    const std::string error = server_->start(host.toStdString(), static_cast<std::uint16_t>(port));
    host_ = error.empty() ? host : QString();
    lastError_ = error.empty() ? QString() : tr("Could not serve on %1:%2 - %3").arg(host).arg(port).arg(QString::fromStdString(error));
    lastState_.clear();
    Q_EMIT changed();
    return lastError_;
}

void RemoteService::stopServer() {
    releaseJog();
    const bool was = server_->running();
    server_->stop();
    host_.clear();
    if (was) {
        Q_EMIT changed();
    }
}

bool RemoteService::running() const {
    return server_->running();
}

int RemoteService::port() const {
    return server_->port();
}

QString RemoteService::url() const {
    return running() ? QString::fromStdString(remote::pendantUrl(host_.toStdString(), port())) : QString();
}

int RemoteService::clientCount() const {
    return static_cast<int>(server_->clientCount());
}

std::vector<remote::NetworkAddress> RemoteService::addresses() const {
    return transport::listNetworkAddresses();
}

remote::PendantState RemoteService::state() const {
    remote::PendantState s;
    const AppSettings& settings = machine_.settings();
    s.metric = settings.metric;
    s.decimals = settings.customDecimalPlaces != 0 ? settings.customDecimalPlaces : (settings.metric ? 2 : 3);
    switch (jogger_.preset()) {
        case controller::JogPreset::Rapid: s.jogPreset = "Rapid"; break;
        case controller::JogPreset::Precise: s.jogPreset = "Precise"; break;
        default: s.jogPreset = "Normal"; break;
    }
    s.xyStep = jogger_.speeds().xyStep;
    s.zStep = jogger_.speeds().zStep;
    s.aStep = jogger_.speeds().aStep;
    s.jogFeed = jogger_.speeds().feedrate;

    controller::Controller* c = machine_.controller();
    s.connected = c && machine_.isConnected();
    s.port = machine_.port().toStdString();
    s.fileName = machine_.programName().toStdString();
    if (!s.connected) {
        s.statusLabel = controller::statusLabel({});
        return s;
    }
    const auto& status = c->state().status;
    s.activeState = status.activeState;
    s.statusLabel = controller::statusLabel(status.activeState);
    s.alarmCode = status.alarmCode;
    s.workflow = std::string(controller::workflowStateName(c->workflow().state()));
    s.wcs = c->runner().modal().wcs;

    const bool rotary = machine_.rotaryMode();
    std::array<double, 4> wpos = machine_.workPositionMm();
    std::array<double, 4> mpos = machine_.machinePositionMm();
    if (rotary) {
        // The rotary drives Y: its degrees show as A (the DRO's A row).
        wpos[3] = wpos[1];
        mpos[3] = mpos[1];
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (!s.metric) {
            wpos[i] = units::mm2in(wpos[i]);
            mpos[i] = units::mm2in(mpos[i]);
        }
    }
    s.wpos = wpos;
    s.mpos = mpos;
    s.hasA = rotary || status.mpos.count >= 4 || c->state().axes.letters.find('A') != std::string::npos ||
             settings.preferences.useAaxisForGrbl;

    // The board reports feed in its $13 units; the page shows the workspace's.
    const bool boardInches = c->runner().setting("$13", "0") == "1";
    s.feedrate = boardInches == s.metric ? (boardInches ? units::in2mm(status.feedrate) : units::mm2in(status.feedrate))
                                          : status.feedrate;
    s.spindle = status.spindle;
    if (c->sender().hasProgram()) {
        const auto& sender = c->sender().status();
        s.sent = sender.sent;
        s.received = std::max<std::int64_t>(0, c->sender().currentLineRunning());
        s.total = sender.total;
        s.remainingMs = static_cast<std::int64_t>(std::llround(std::max(0.0, sender.remainingTime) * 1000));
    }

    // JobModel's rules: a job can run when a file is loaded and analysed and
    // the card is not running one of its own.
    const bool ready = machine_.hasProgram() && !machine_.isAnalyzing() && !machine_.isRunningSdFile();
    const controller::WorkflowState workflow = c->workflow().state();
    s.canRun = ready && controller::canRun(status.activeState, workflow);
    s.canPause = machine_.hasProgram() && controller::canPause(status.activeState, workflow);
    s.canStop = machine_.hasProgram() && controller::canStop(workflow);
    s.canJog = jogger_.canJog();
    s.homingEnabled = machine_.homingEnabled();
    return s;
}

void RemoteService::handle(const remote::PendantCommand& command, std::uint64_t client) {
    using Kind = remote::PendantCommand::Kind;
    controller::Controller* c = machine_.controller();
    switch (command.kind) {
        case Kind::Ping:
            return;
        case Kind::Preset:
            jogger_.selectPreset(command.text == "Rapid"     ? controller::JogPreset::Rapid
                                 : command.text == "Precise" ? controller::JogPreset::Precise
                                                             : controller::JogPreset::Normal);
            return;
        case Kind::JogRelease:
            if (client == jogClient_) {
                releaseJog();
            }
            return;
        case Kind::JogStop:
            // The jog pad's Stop (JogModel::stop): whoever's jog it is.
            releaseJog();
            jogger_.release();
            if (c) {
                controller::stopWithoutJob(*c);
            }
            return;
        default:
            break;
    }
    if (!c || !machine_.isConnected()) {
        return;
    }
    const std::string& activeState = c->state().status.activeState;
    const controller::WorkflowState workflow = c->workflow().state();
    switch (command.kind) {
        case Kind::JogPress: {
            if (!jogger_.canJog() || jogger_.isPressed()) {
                return;  // one hold at a time, whoever holds it
            }
            const auto& d = command.directions;
            const bool rotary = machine_.rotaryMode();
            controller::JogAxes axes;
            if (d[0] != 0) {
                axes.emplace_back('X', d[0]);
            }
            if (d[1] != 0 && !rotary) {
                axes.emplace_back('Y', d[1]);
            }
            if (d[2] != 0) {
                axes.emplace_back('Z', d[2]);
            }
            if (!axes.empty()) {
                jogger_.press(axes);
            } else if (d[3] != 0) {
                jogger_.pressRotary(d[3]);
            } else {
                return;
            }
            jogClient_ = client;
            jogHeard_.restart();
            watchdogTimer_->start();
            return;
        }
        case Kind::Start:
            if (machine_.hasProgram() && !machine_.isAnalyzing() && !machine_.isRunningSdFile() &&
                controller::canRun(activeState, workflow)) {
                controller::runJob(*c);
            }
            return;
        case Kind::Pause:
            if (machine_.hasProgram() && controller::canPause(activeState, workflow)) {
                controller::pauseJob(*c);
            }
            return;
        case Kind::Stop:
            if (machine_.hasProgram() && controller::canStop(workflow)) {
                controller::stopJob(*c);
            }
            return;
        case Kind::Unlock:
            c->unlock();
            return;
        case Kind::Reset:
            c->reset();
            return;
        case Kind::Home:
            if (machine_.canMove() && machine_.homingEnabled()) {
                c->home();
            }
            return;
        case Kind::ZeroAxis:
            if (machine_.canMove()) {
                const char axis = command.text[0] == 'A' && machine_.rotaryMode() ? 'Y' : command.text[0];
                machine_.zeroAxis(axis);
            }
            return;
        case Kind::ZeroAll:
            if (machine_.canMove()) {
                machine_.zeroAllAxes();
            }
            return;
        case Kind::GoToZero:
            if (machine_.canMove()) {
                machine_.goToZero(command.text);
            }
            return;
        case Kind::Workspace:
            // The DRO's workspace selector: not while a job runs.
            if (activeState != "Run" && !c->workflow().isRunning()) {
                machine_.selectWorkspace(QString::fromStdString(command.text));
            }
            return;
        default:
            return;
    }
}

void RemoteService::scheduleState() {
    if (server_->running() && server_->clientCount() > 0 && !stateTimer_->isActive()) {
        stateTimer_->start();
    }
}

void RemoteService::pushState() {
    if (!server_->running() || server_->clientCount() == 0) {
        return;
    }
    std::string message = remote::stateMessage(state());
    if (message == lastState_) {
        return;
    }
    lastState_ = message;
    server_->broadcast(std::move(message));
}

void RemoteService::releaseJog() {
    watchdogTimer_->stop();
    // Only a pendant's own hold: the desktop's jog buttons share the Jogger.
    if (jogClient_ != 0) {
        jogClient_ = 0;
        jogger_.release();
    }
}

void RemoteService::checkJogWatchdog() {
    if (!jogger_.isPressed()) {
        jogClient_ = 0;
        watchdogTimer_->stop();
        return;
    }
    if (jogHeard_.elapsed() > kJogSilenceMs) {
        releaseJog();
    }
}

}  // namespace gs::app
