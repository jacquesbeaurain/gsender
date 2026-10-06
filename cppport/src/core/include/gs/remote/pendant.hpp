#pragma once

// The wireless pendant's protocol. Upstream's remote mode served the whole
// React app to the phone, which talked socket.io to the server like the
// desktop window did (routes /remote, /remote/workflow, ...). The port has no
// client/server split, so the pendant is a small page of its own that talks
// JSON over a WebSocket: the application pushes a state message whenever the
// machine changes and the page sends commands. This header is that
// vocabulary; gs_transport serves it and gs_app applies it.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gs::remote {

// What the page shows: the DRO, the status, the job and what may be pressed.
struct PendantState {
    bool connected = false;
    std::string port;          // the connection ("COM3", "/dev/ttyUSB0", "Simulator")
    std::string activeState;   // "Idle", "Run", "Alarm", ... (empty while disconnected)
    std::string statusLabel;   // as the status area names it ("Running", "Jogging")
    std::string alarmCode;
    std::string workflow = "idle";  // "idle", "running", "paused"
    // Work and machine positions in the workspace units; A in degrees.
    std::array<double, 4> wpos{};
    std::array<double, 4> mpos{};
    bool hasA = false;
    bool metric = true;
    int decimals = 2;
    std::string wcs = "G54";
    std::string fileName;
    std::int64_t sent = 0;
    std::int64_t received = 0;
    std::int64_t total = 0;
    std::int64_t remainingMs = 0;
    double feedrate = 0;
    double spindle = 0;
    // The buttons' rules (ControlButton, canClickShortcut).
    bool canJog = false;
    bool canRun = false;
    bool canPause = false;
    bool canStop = false;
    bool homingEnabled = false;
    // The Jogging widget's preset and its steps in the workspace units.
    std::string jogPreset = "Normal";
    double xyStep = 0;
    double zStep = 0;
    double aStep = 0;
    double jogFeed = 0;
};

// {"type":"state", ...}: the state for the page.
std::string stateMessage(const PendantState& state);

// A page's request, `{"type": "...", ...}`.
struct PendantCommand {
    enum class Kind {
        JogPress,     // hold a jog button: {"type":"jogPress","x":1,"y":0,"z":0,"a":0}
        JogRelease,   // let go (a tap jogs one step, a hold stops)
        JogStop,      // the jog pad's Stop: cancel a jog, else reset
        Start,        // start or resume the loaded job
        Pause,
        Stop,
        Unlock,       // $X
        Home,         // $H
        Reset,        // soft reset
        ZeroAxis,     // {"axis":"X"}: work zero here
        ZeroAll,
        GoToZero,     // {"axes":"XY"}
        Workspace,    // {"wcs":"G55"}
        Preset,       // {"preset":"Rapid" | "Normal" | "Precise"}
        Ping,         // keeps a hold alive; answered with nothing
        // The tool pages (Tools, Config): the application's view models,
        // named, whose properties are pushed to the page that subscribed and
        // whose methods it may call (the application decides which).
        Subscribe,    // {"model":"spindle"}: its properties now, and on every change
        Unsubscribe,
        Call,         // {"model":"spindle","method":"stop","args":[...],"id":7}
        SetProperty,  // {"model":"config","property":"search","value":"..."}
        // A G-code file chosen on the phone: {"name":"part.nc","content":"G21..."}.
        // Upstream's remote page loaded files the same way (a file input);
        // nothing names a path on the computer.
        LoadProgram,
    };
    Kind kind = Kind::Ping;
    std::array<int, 4> directions{};  // JogPress: X, Y, Z, A as -1, 0 or +1
    std::string text;                 // axis, axes, wcs or preset
    // Subscribe, Unsubscribe, Call and SetProperty.
    std::string model;                // [a-z0-9_]{1,32}
    std::string member;               // the method or property: an identifier
    std::string argsJson;             // Call: a JSON array; SetProperty: the value
    std::int64_t callId = 0;          // Call: echoed in the reply (0: no reply wanted)
    std::string content;              // LoadProgram: the program (text = its name)
};

// {"type":"model","model":"spindle","properties":{...}}: a model's properties.
// `propertiesJson` is a JSON object.
std::string modelMessage(std::string_view model, std::string_view propertiesJson);
// {"type":"result","id":7,"ok":true,"value":...}: a call's answer. `valueJson`
// is any JSON value ("null" for none); `error` (when not empty) turns it into
// {"ok":false,"error":"..."}.
std::string resultMessage(std::int64_t id, std::string_view valueJson, std::string_view error = {});

// Empty for anything malformed or unknown (the page is outside the
// application's control; nothing it sends may throw or reach the machine
// unchecked). Axes are upper-cased and limited to X, Y, Z, A; workspaces to
// G54-G59; presets to the three names.
std::optional<PendantCommand> parseCommand(std::string_view json);

}  // namespace gs::remote
