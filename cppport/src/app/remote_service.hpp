#pragma once

// Remote mode (features/RemoteMode, api.remote.js, server/index.js): the
// wireless pendant. Upstream saved "remoteSettings" and restarted the whole
// app so its Express server rebound to the chosen address and port; the
// phone then loaded the React app at /#/remote. Here a RemoteServer serves
// the pendant page (resources/remote/pendant.html) and a WebSocket; this
// service keeps the settings, (re)starts the server as they change - no
// restart needed - pushes the machine's state to every pendant and applies
// what they send through the same rules as the desktop's buttons.
//
// Safety: a jog held on a phone stops when the phone lets go, disconnects or
// goes quiet (the page pings while a button is held; a second without word
// from it releases the jog).

#include "gs/remote/network.hpp"
#include "gs/remote/pendant.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class QTimer;

namespace gs::transport {
class RemoteServer;
}

namespace gs::app {

class Machine;
class Jogger;

class RemoteService final : public QObject {
    Q_OBJECT

public:
    RemoteService(Machine& machine, Jogger& jogger, QObject* parent = nullptr);
    ~RemoteService() override;

    // The saved settings (config "remoteSettings").
    remote::RemoteSettings settings() const;
    // onConfirmUpdate(): validates, saves and applies - serving on the
    // address when on, stopping when off. Empty on success, else what went
    // wrong (the validation message, or why the address could not be bound;
    // remote mode is then saved off with `error` set, as upstream does).
    QString apply(const remote::RemoteSettings& settings);
    // On start: serve when the saved settings say so.
    void startFromSettings();

    // Serves on `host`:`port` without touching the settings (port 0: any
    // free one) - tests and diagnostics. Empty on success.
    QString startServer(const QString& host, int port);
    void stopServer();

    bool running() const;
    int port() const;           // the bound port while running
    QString host() const { return host_; }
    QString url() const;        // what the QR code holds, while running
    int clientCount() const;
    QString lastError() const { return lastError_; }

    // The computer's addresses, the recommended one first.
    std::vector<remote::NetworkAddress> addresses() const;

    // What the pendants are shown now.
    remote::PendantState state() const;
    // Applies one pendant's request. `client` (the server's ids start at 1)
    // holds any jog it starts: only its release, its silence or its leaving
    // ends that jog.
    void handle(const remote::PendantCommand& command, std::uint64_t client);

Q_SIGNALS:
    void changed();  // running, clients or error

private:
    void scheduleState();
    void pushState();
    void releaseJog();
    void checkJogWatchdog();

    Machine& machine_;
    Jogger& jogger_;
    std::unique_ptr<transport::RemoteServer> server_;
    QTimer* stateTimer_ = nullptr;     // coalesces bursts of changes
    QTimer* watchdogTimer_ = nullptr;  // a held jog's liveness
    std::string lastState_;
    QString host_;
    QString lastError_;
    std::uint64_t jogClient_ = 0;  // the pendant holding a jog
    QElapsedTimer jogHeard_;       // since that pendant last spoke
};

}  // namespace gs::app
