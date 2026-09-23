#pragma once

// The application's machine service - what gSender's CNCEngine and the UI's
// controller sagas did together: it owns the connection (serial, TCP or the
// simulated board), the controller session, the loaded program and its
// analysis, and turns controller events into Qt signals. Everything runs on
// the UI thread; the transport and the program analysis report back to it.

#include "app_settings.hpp"

#include "gs/config/config_store.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/session.hpp"
#include "gs/job/program_analysis.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gs::transport {
class AsioLink;
}
namespace gs::sim {
class GrblSimulator;
}

namespace gs::app {

class QtEventLoop;

// Toolpath segments for the visualizer: x0,y0,z0, x1,y1,z1 per segment, and
// the sender line each segment belongs to (for progress colouring).
struct Toolpath {
    std::vector<float> rapids;
    std::vector<float> feeds;
    std::vector<std::uint32_t> rapidLines;
    std::vector<std::uint32_t> feedLines;
};

// The toolpath of a program that is not the loaded job (tool previews),
// traced on the calling thread with the default machine limits.
Toolpath traceToolpath(const std::string& program);

class Machine final : public QObject {
    Q_OBJECT

public:
    // The port name that connects to the built-in simulated Grbl board.
    static const QString kSimulatorPort;

    Machine(QtEventLoop& loop, std::filesystem::path configFile, QObject* parent = nullptr);
    ~Machine() override;

    // ---- connection ----
    // A COM port, an IPv4 address (TCP, `networkPort`) or kSimulatorPort.
    void connectTo(const QString& port, int baudRate = 115200, int networkPort = 23);
    void disconnectFromMachine();
    bool isConnecting() const noexcept { return connecting_; }
    bool isConnected() const;  // the firmware is known and a controller runs
    QString port() const { return port_; }
    controller::Controller* controller() const;

    // ---- program ----
    bool loadFile(const QString& path, QString* error = nullptr);
    void loadProgram(const QString& name, std::string text);
    void unloadProgram();
    bool hasProgram() const noexcept { return !programName_.isEmpty(); }
    QString programName() const { return programName_; }
    const std::string& programText() const noexcept { return programText_; }
    bool isAnalyzing() const noexcept { return analyzing_; }
    const job::ProgramAnalysis& analysis() const noexcept { return analysis_; }
    const Toolpath& toolpath() const noexcept { return toolpath_; }

    // ---- commands ----
    void sendConsoleLine(const QString& line);

    controller::Preferences& preferences() noexcept { return *preferences_; }
    config::MacroStore macros() { return config::MacroStore(config_); }
    // The application preferences: applied to the controller (preferences,
    // tool change context - sent to every new controller as gSender's UI did
    // with "toolchange:context") and saved in the config file.
    const AppSettings& settings() const noexcept { return settings_; }
    void setSettings(const AppSettings& settings);
    config::ConfigStore& config() noexcept { return config_; }

    // ---- probing ----
    // The Probe widget's routine: the probe settings, the board's $13, $22
    // and $132 and the machine position (the workspace is metric for now).
    // Empty when disconnected.
    std::vector<std::string> probeRoutine(probe::Axes axes, probe::ProbeType type, double toolDiameter,
                                          int corner) const;
    // Runs a routine as the widget does: gcode:safe in mm, then the distance
    // mode as it was. False when disconnected or not idle.
    bool runProbe(std::vector<std::string> code);
    bool probeTriggered() const;  // the probe pin (Pn:P)
    // Simulator only: puts the plate where the operator would - the bit over
    // it for `corner`, 10 mm above - so the routine has something to touch.
    // AutoZero and BitZero plates are not modelled (their probes miss).
    bool isSimulated() const noexcept { return simulator_ != nullptr; }
    void placeSimulatedPlate(probe::ProbeType type, double toolDiameter, int corner);
    sim::GrblSimulator* simulator() const noexcept { return simulator_.get(); }

Q_SIGNALS:
    void connectionChanged();
    void connectionFailed(const QString& reason);
    void stateChanged();     // machine status / parser state
    void settingsChanged();  // firmware settings
    void workflowChanged();
    void senderStatusChanged();
    void consoleLine(const QString& text, bool fromHost);
    void programChanged();   // loaded, unloaded or analysed
    void errorReported(const QString& title, const QString& detail);
    void notice(const QString& text);  // tool changes, pauses and other prompts
    void macrosChanged();
    void appSettingsChanged();  // setSettings()
    // A "Code" tool change ran its pre-hook: change the tool, then call
    // controller()->toolChangePost() to run the post-hook and resume.
    void toolChangeWaiting(const QString& comment);

private:
    void startSession(controller::DeviceLink& link);
    void teardown();
    void handle(const controller::ControllerEvent& event);
    void attachProgram();
    void sendEstimates();
    void analysisFinished(std::uint64_t generation, job::ProgramAnalysis analysis, Toolpath toolpath);

    QtEventLoop& loop_;
    config::ConfigStore config_;
    std::shared_ptr<controller::Preferences> preferences_;
    std::unique_ptr<transport::AsioLink> link_;
    std::unique_ptr<sim::GrblSimulator> simulator_;
    std::unique_ptr<controller::Session> session_;
    QString port_;
    bool connecting_ = false;
    AppSettings settings_;

    QString programName_;
    std::string programText_;
    job::ProgramAnalysis analysis_;
    Toolpath toolpath_;
    bool analyzing_ = false;
    std::uint64_t analysisGeneration_ = 0;
    std::shared_ptr<std::atomic<bool>> analysisCancel_;
};

}  // namespace gs::app
