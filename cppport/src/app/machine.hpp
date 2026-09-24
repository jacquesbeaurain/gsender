#pragma once

// The application's machine service - what gSender's CNCEngine and the UI's
// controller sagas did together: it owns the connection (serial, TCP or the
// simulated board), the controller session, the loaded program and its
// analysis, and turns controller events into Qt signals. Everything runs on
// the UI thread; the transport and the program analysis report back to it.

#include "app_settings.hpp"

#include "gs/config/config_store.hpp"
#include "gs/config/history.hpp"
#include "gs/config/machine_profiles.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/locations.hpp"
#include "gs/controller/session.hpp"
#include "gs/job/program_analysis.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gs::transport {
class AsioLink;
class FtpUploader;
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
    // The drawn points' extent (a rotary job's wrapped path differs from the
    // analysis' bounds).
    gcode::BoundingBox bounds;
    bool bounded = false;
};

// The toolpath of a program that is not the loaded job (tool previews),
// traced on the calling thread with the default machine limits. As every
// toolpath, A turns the stock about X: moves are wrapped around it.
Toolpath traceToolpath(const std::string& program);

class Machine final : public QObject {
    Q_OBJECT

public:
    // The port names that connect to the built-in simulated boards: Grbl,
    // and grblHAL with an SD card.
    static const QString kSimulatorPort;
    static const QString kSimulatorHalPort;
    static bool isSimulatorPort(const QString& port) { return port == kSimulatorPort || port == kSimulatorHalPort; }

    Machine(QtEventLoop& loop, std::filesystem::path configFile, QObject* parent = nullptr);
    ~Machine() override;

    // ---- connection ----
    // A COM port, an IPv4 address (TCP, `networkPort`) or a simulator.
    void connectTo(const QString& port, int baudRate = 115200, int networkPort = 23);
    void disconnectFromMachine();
    bool isConnecting() const noexcept { return connecting_; }
    bool isConnected() const;  // the firmware is known and a controller runs
    // grblHAL runs a file from its SD card (its status names the file); no
    // job may start meanwhile.
    bool isRunningSdFile() const;
    QString port() const { return port_; }
    controller::Controller* controller() const;

    // ---- program ----
    // Loads a file from disk and lists it among the recent files.
    bool loadFile(const QString& path, QString* error = nullptr);
    void forgetRecentFile(const QString& path);
    void clearRecentFiles();
    void loadProgram(const QString& name, std::string text, const QString& path = {});
    void unloadProgram();
    bool hasProgram() const noexcept { return !programName_.isEmpty(); }
    QString programName() const { return programName_; }
    QString programPath() const { return programPath_; }  // the file it came from, if any
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
    // The selected machine profile (the default one when none is chosen).
    const config::MachineProfile& machineProfile() const;
    // What the profile's defaults depend on: grblHAL or not, its build and
    // board (nothing while disconnected).
    config::BoardContext boardContext() const;
    // On start (isTimeToBackup / backupPreviousState): when the backup
    // frequency says so - "On Update" when `appVersion` differs from the one
    // that last backed up - the settings file as it is is copied to
    // preferences-backup-<ISO time>.json in the backup folder. The file
    // written, or none.
    QString backupSettingsIfDue(const QString& appVersion, std::int64_t nowMs);
    // The maintenance tasks whose hours have reached their range (the alert
    // at a job's end), and "Reset Timers" for them.
    std::vector<config::MaintenanceTask> dueMaintenanceTasks();
    void resetMaintenanceTimers(const std::vector<int>& ids);
    // Settings files (Settings > Export / Import / Restore Defaults). Export
    // writes the settings and event hooks; Import takes such a file or a
    // gSender one (its export or its store file) and replaces the settings
    // and the hooks it carries - `report` says what was taken, or why not.
    bool exportSettings(const QString& path, QString* error) const;
    bool importSettings(const QString& path, QString* report);
    void restoreDefaultSettings();
    runtime::EventLoop& eventLoop() noexcept;  // the UI thread's

    // ---- tool change wizards ----
    // True for the strategies that run a wizard at M6.
    static bool isWizardStrategy(const std::string& option);
    // Builds the strategy's wizard from the settings and machine, and sends
    // its start-up G-code; `fullFirstWizard` decides the first tool with a
    // fixed sensor when the settings leave it to the operator.
    std::optional<toolchange::Wizard> startToolChangeWizard(const std::string& option, int count,
                                                            bool fullFirstWizard = true);
    // Whether the running wizard's start-up G-code has gone out; actions
    // wait for it (it stores the position they return to).
    bool isToolChangeWizardReady() const noexcept { return wizardReady_; }
    // An action of the wizard: wizard:step, then its G-code; the controller
    // answers with wizardNext once the lines are through.
    void runWizardAction(int step, int substep, const std::vector<std::string>& gcode);

    // ---- file context and outline ----
    // The loaded file's box as expression context (xmin ... zmax, from the
    // toolpath with its rapids), as the visualizer sets controller.context
    // for macros and outlines; zeros without a file.
    expr::Value fileContext() const;
    // "Run outline": traces the job's outline (the settings' style and
    // speed) above the stock. False, with the reason, when it cannot run.
    bool runOutline(QString* error = nullptr);

    // ---- start from line ----
    // The sender line Start From Line offers: where the last job was stopped
    // or cut off by a lost connection; 1 once a job completes (upstream's
    // lastLine).
    std::int64_t lastLine() const noexcept { return lastLine_; }
    // gcode:start from `line`, first rising to `safeHeight` (mm) above the
    // file's highest Z, with the spindle delay (StartFromLine.tsx). False
    // without a program or an idle machine.
    bool startFromLine(std::size_t line, double safeHeight);

    // ---- positions (DRO) ----
    void zeroAxis(char axis);  // work zero here, "G10 L20 P0 X0"
    void zeroAllAxes();        // X, Y, Z (and A on grblHAL)
    // gotoZero / goXYAxes: `axes` "X", "Y", "Z", "A" or "XY", lifting to the
    // safe retract height first when one is set; gcode:safe in mm.
    void goToZero(std::string_view axes);
    // The reported positions in mm ($13=1 boards report inches), X Y Z A;
    // zeros when disconnected.
    std::array<double, 4> workPositionMm() const;
    std::array<double, 4> machinePositionMm() const;
    // The DRO's canClick: connected, no job running, idle or jogging.
    bool canMove() const;
    bool homingEnabled() const;     // $22 > 0
    bool singleAxisHoming() const;  // $22 bit 1: the DRO offers per-axis homing
    void selectWorkspace(const QString& wcs);  // "G54" ... "G59"
    // Makes the current position read `value` (workspace units) on `axis`:
    // "G10 P0 L20 X..." in the workspace units.
    void setWorkPosition(char axis, double value);
    void homeAxis(char axis);  // "$HX"
    // The DRO's corner buttons and Park (upstream offers them with homing
    // enabled, once the machine has homed). A notice when the machine limits
    // are unknown.
    void goToCorner(controller::MachineCorner corner);
    void goToPark();
    // The settings' "Go to" for a stored machine position.
    void goToMachinePosition(const toolchange::MachinePosition& position);
    // Go To Location: the target (or distances) in the workspace units, A in
    // degrees; gcode:safe in the workspace units.
    void goToLocation(controller::GoToMode mode, double x, double y, double z, double a);

    // ---- status and machine information ----
    // An alarm code's description, as the status area's "?" shows it.
    QString alarmDescription(const std::string& code) const;
    // Machine Info's "Lock stepper motors": $1=255 keeps the motors powered
    // between moves; the previous $1 is kept and restored on unlocking (50
    // when none was kept).
    bool stepperLocked() const;
    void setStepperLock(bool lock);

    // ---- spindle and laser ----
    // Laser mode: the board's $32 when connected, else the mode last chosen.
    bool laserMode() const;
    // The Spindle/Laser toggle (enableLaserMode / enableSpindleMode): the
    // work offset shift between the tools, the power ranges ($30/$31 -
    // except for grblHAL's own laser), $32; the settings keep the range the
    // other mode will want back.
    void setLaserMode(bool laser);
    // The laser's maximum S: $730 on grblHAL, the settings' on Grbl.
    double laserMaxPower() const;
    // grblHAL's spindles ($spindles), as listed since connecting.
    const std::vector<protocol::SpindleLine>& spindles() const noexcept { return spindles_; }
    // grblHAL: switch spindle (M104 Q<id>) and list them again.
    void selectSpindle(int id);

    // ---- rotary ----
    bool rotaryMode() const noexcept { return settings_.rotary.rotaryMode; }
    // Enters or leaves rotary mode (updateWorkspaceMode): the board's
    // commands (on Grbl its Y settings for the rotary, saving the previous
    // ones; on grblHAL the A/Y swap and the controller's rotary mode), then
    // the mode saved. False when disconnected.
    bool setRotaryMode(bool rotary);
    // The Rotary widget's probing routines, run with gcode:safe in $13's
    // units: the rotary's Z, or the Y alignment. False when disconnected.
    bool runRotaryProbe(bool yAlignment);

    // ---- calibration tools ----
    // Movement Tuning's move (workspace units): a $J= jog at 1000 mm/min,
    // dropped towards a triggered limit when that protection is on. False
    // when nothing was sent.
    bool runTuningMove(char axis, double distance);
    // XY Squaring's move to the next mark (workspace units, G0 in G91).
    void runSquaringMove(char axis, double distance);
    // A firmware setting as a number (NaN when not reported).
    double settingNumber(const std::string& key) const;
    // Sends lines such as "$100=98.04" and "$$".
    void writeFirmwareSettings(const std::vector<std::string>& lines);

    // ---- probing ----
    // The Probe widget's routine: the probe settings (converted for inch
    // workspaces), the board's $13, $22 and $132 and the machine position;
    // `toolDiameter` in the workspace units. Empty when disconnected.
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
    void successNotice(const QString& text);  // what upstream toasts as a success
    // A job ended - completed, or stopped - with the G-code errors it met
    // (the Job End alert).
    void jobEnded(bool completed, double durationMs, const QStringList& errors);
    // A loaded file has lines the interpreter could not read (with "Warn if
    // bad file" on): how many, and the first five.
    void invalidLinesFound(int count, const QStringList& sample);
    void macrosChanged();
    void appSettingsChanged();  // setSettings()
    // The connection closed while a job ran, around sender line `line`.
    void jobInterrupted(qint64 line);
    // M6 with a wizard strategy: the job is paused for startToolChangeWizard().
    void toolChangeWizardRequested(const QString& option, int count, const QString& comment);
    void wizardNext(int step, int substep);  // an action's G-code is done
    void toolChangeWizardReady();            // the start-up G-code went out
    void spindlesChanged();                  // grblHAL's spindle list
    // A job or an alarm/error was recorded (Stats).
    void historyChanged();
    // A "Code" tool change ran its pre-hook: change the tool, then call
    // controller()->toolChangePost() to run the post-hook and resume.
    void toolChangeWaiting(const QString& comment);
    // An SD card upload (ymodem:*): started, the progress of the file being
    // sent, done - or failed, with why.
    void sdUploadStarted();
    void sdUploadProgress(int percent);
    void sdUploadCompleted();
    void sdUploadFailed(const QString& message);
    // grblHAL: an accessory ("TLS", "ATCEXP", ...) came or went, by its
    // Autoconfig message against what [NEWOPT:] said (the accessory
    // connectivity toasts).
    void accessoryConnectivityChanged(const QString& accessory, bool connected);
    // A probe routine run with runProbe() got through without an alarm (the
    // board took all of it); and a job reached a tool change (M6) - the
    // audio cues' "probe:success" and "toolchange:start".
    void probeSucceeded();
    void toolChangeRequired();

private:
    void startSession(controller::DeviceLink& link);
    void teardown();
    void handle(const controller::ControllerEvent& event);
    void attachProgram();
    void sendEstimates();
    void analysisFinished(std::uint64_t generation, job::ProgramAnalysis analysis, Toolpath toolpath);
    // updateJobStats() / updateMaintenanceTasks() at a job's end.
    void recordJob(const controller::SenderStatus& status);

    QtEventLoop& loop_;
    config::ConfigStore config_;
    std::shared_ptr<controller::Preferences> preferences_;
    std::unique_ptr<transport::AsioLink> link_;
    std::unique_ptr<transport::FtpUploader> ftp_;  // SD card uploads to networked grblHAL
    std::unique_ptr<sim::GrblSimulator> simulator_;
    std::unique_ptr<controller::Session> session_;
    QString port_;
    bool connecting_ = false;
    AppSettings settings_;

    QString programName_;
    QString programPath_;
    std::string programText_;
    job::ProgramAnalysis analysis_;
    Toolpath toolpath_;
    bool analyzing_ = false;
    std::uint64_t analysisGeneration_ = 0;
    bool jobRunning_ = false;
    // The G-code errors of the job running (upstream's saga keeps them,
    // throttled to one per 250 ms).
    QStringList jobErrors_;
    std::int64_t lastJobErrorMs_ = -1;
    bool wizardReady_ = false;
    bool probing_ = false;  // a runProbe() routine is on its way
    std::vector<protocol::SpindleLine> spindles_;
    // Whether each accessory was last known connected (upstream keeps it
    // for the whole session, across connections).
    std::map<std::string, bool> accessoryConnected_;
    std::int64_t lastLine_ = 1;
    std::shared_ptr<std::atomic<bool>> analysisCancel_;
};

}  // namespace gs::app
