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
#include <functional>
#include <map>
#include <memory>
#include <set>
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
    // On start: serve when the saved settings say so. Empty, or why it
    // could not (remote mode is then saved off, flagged).
    QString startFromSettings();

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

    // The tool pages (Tools, Config) are the application's own view models
    // shown on the phone: a model is bound under a name with the methods and
    // properties a pendant may use - nothing else is reachable. The model is
    // made on first use. A pendant that subscribes gets the model's
    // properties as JSON now and whenever one changes; it calls the allowed
    // methods (arguments and the result as JSON) and sets the allowed
    // properties.
    struct ModelBinding {
        std::function<QObject*()> create;
        std::set<std::string> methods;
        std::set<std::string> writableProperties;
    };
    void bindModel(const std::string& name, ModelBinding binding);

    // What a pendant is told about a model now (tests); empty for an unknown one.
    std::string modelProperties(const std::string& name);

Q_SIGNALS:
    void changed();  // running, clients or error

private Q_SLOTS:
    void modelChanged();  // any bound model's property notification

private:
    struct BoundModel {
        ModelBinding binding;
        QObject* object = nullptr;
        std::set<std::uint64_t> subscribers;
        std::string lastSent;
        bool dirty = false;
    };

    void scheduleState();
    void pushState();
    void releaseJog();
    void checkJogWatchdog();
    BoundModel* boundModel(const std::string& name);
    void subscribe(const std::string& name, std::uint64_t client);
    void callModel(const remote::PendantCommand& command, std::uint64_t client);
    void setModelProperty(const remote::PendantCommand& command);
    void pushModels();
    void dropClient(std::uint64_t client);

    Machine& machine_;
    Jogger& jogger_;
    std::unique_ptr<transport::RemoteServer> server_;
    QTimer* stateTimer_ = nullptr;     // coalesces bursts of changes
    QTimer* watchdogTimer_ = nullptr;  // a held jog's liveness
    QTimer* modelTimer_ = nullptr;     // coalesces the models' changes
    std::map<std::string, BoundModel> models_;
    std::string lastState_;
    QString host_;
    QString lastError_;
    std::uint64_t jogClient_ = 0;  // the pendant holding a jog
    QElapsedTimer jogHeard_;       // since that pendant last spoke
};

}  // namespace gs::app
