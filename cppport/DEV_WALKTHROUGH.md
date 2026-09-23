# Development walkthrough — gSender C++ port

This is a running log of how the native port is designed and built, in the
order the work happened. Each step says what was built, why, and how it maps
to the JavaScript original. Operational notes (build commands, LibPack facts,
rules for contributors) live in `AGENTS.md`.

---

## Step 0 — Understanding the original

gSender is an Electron application in three layers:

| Layer | Location | Role |
|---|---|---|
| Server (Node) | `src/server` | Serial/TCP connection, Grbl and grblHAL controllers, G-code streaming (`Sender`), command queue (`Feeder`), job state (`Workflow`), macros, event triggers, config store, firmware flashing |
| Client (React) | `src/app` | UI: DRO, jogging, visualizer (web worker + `@sienci/gviewer`), probing, tools (surfacing, squaring, ...), config editor, stats |
| Transport | socket.io | Every UI action is a socket event; every state change comes back as one |

The heart of the machine-control logic is the controller pair
(`GrblController.js`, `GrblHalController.js`, ~6k lines), built on:

- `*LineParser` + `*Runner`: parse each firmware response line into typed
  results and fold them into a state object (status report, parser state,
  settings, alarms, ...).
- `Sender`: character-counting streaming of a loaded program (never overflow
  the firmware's RX buffer; count `ok`s).
- `Feeder`: one-line-at-a-time queue for console commands and macros, with
  holds for `M0`/`M6`.
- `Workflow`: idle / running / paused.
- `JogStreamer`: continuous jogging as a stream of short `$J=` segments.
- Macro expressions: `%var=expr` assignments and `[expr]` substitutions,
  evaluated with a JavaScript-subset evaluator.

## Step 1 — Architecture of the port

**No client/server split.** In C++ the UI and the machine logic run in one
process, so the socket.io protocol disappears: the UI calls the controller API
directly and receives typed events. (Remote pendant access can be added later
as an optional adapter.)

**Three layers:**

1. `gs_core` — Qt-free C++20 + Boost. Everything that decides what bytes go to
   the machine and how its answers are interpreted. Time and I/O come in
   through interfaces, so the whole streaming/controller state machine can be
   tested with simulated time instead of sleeps and real ports.
2. Transport — Boost.Asio serial and TCP. The LibPack has no QtSerialPort, and
   Asio keeps the transport testable without Qt.
3. `src/app` — Qt 6 Widgets application (FreeCAD's stack), with a
   `QOpenGLWidget` toolpath visualizer. Adapters turn core events into Qt
   signals and run the core's timers on the Qt event loop.

Qt Widgets rather than QML: it matches FreeCAD, `QOpenGLWidget` makes a
custom toolpath renderer straightforward (the LibPack has no Qt Quick 3D), and
widgets can be rendered off-screen for automated screenshots. This is a
decision point worth revisiting if a touch-first UI becomes the priority.

## Step 2 — Build system

- `CMakeLists.txt` + `CMakePresets.json` with Ninja and Visual Studio presets
  for Debug (Debug LibPack) and RelWithDebInfo (Release LibPack).
- `cmake/GsLibPack.cmake` adds a LibPack to `CMAKE_PREFIX_PATH`, detects a
  Debug/Release mismatch, and maps RelWithDebInfo onto the LibPack's Release
  imported configuration.
- `cmake/GsCompilerOptions.cmake`: one interface target with the warning level
  and MSVC conformance flags every first-party target uses.
- `cmake/GsRuntimeDeps.cmake`: copies imported DLLs (Boost, GTest, Qt) next
  to executables via `$<TARGET_RUNTIME_DLLS>`, so tests and the app run from
  the build tree.
- `cmake/GsEmbed.cmake`: compiles data files (firmware tables, machine
  profiles) into `gs_core` so the core has no runtime file dependencies.
- `tools/build.ps1`: enters the VS developer shell, configures, builds, tests.

## Step 3 — JavaScript semantics helpers (`gs/util`)

G-code that gSender generates is built with JavaScript string conversions,
e.g. `` `G0 X${x.toFixed(3)}` ``, and macro expressions print numbers with
`Number.prototype.toString`. To produce byte-identical output the port has
exact ports of:

- `numberToString` — ECMAScript `Number::toString` (shortest round-trip
  digits via `std::to_chars`, exponent form outside `[1e-7, 1e21)`).
- `toFixed` — ECMAScript `toFixed`: ties go away from zero on the *exact*
  binary value (`(2.5).toFixed(0) === "3"`, `(1.005).toFixed(2) === "1.00"`),
  which differs from `printf`'s round-half-even. Ties are detected exactly
  with a cheap pre-filter followed by a full-precision expansion.
- `stringToNumber` (`Number(str)`), `parseFloat`, `parseInt`, `mathRound`.

`gs/util/strings` provides JS-compatible `trim` (strips the UTF-8 BOM and NBSP
too) and one line splitter used everywhere.

## Step 4 — G-code tokenization (`gs/gcode/parser`)

Two tokenizers existed in gSender and both are ported onto one shared
comment/whitespace stripping step:

- `parseLine` (from the `gcode-parser` package): words with "flat" forms
  (`M06` → `M6`), `$`/`%`/`{` commands, `N` line numbers, `*` checksums. The
  controllers use it to spot `M0`, `M1`, `M6`, `M3`/`M4` in streamed lines.
- `scanLine` (from the app's `scanLineFast`): raw letter/value tokens plus an
  "invalid token" flag, used by the interpreter.

## Step 5 — One G-code interpreter (`gs/gcode/interpreter`)

gSender interpreted programs twice, with different code: the server's
`GcodeToolpath` (old cncjs interpreter; used to recover modal state for
start-from-line) and the app's `GCodeVirtualizer` (visualizer geometry, file
statistics, time estimates). The old one silently dropped the move in
`G0 G90 X10` (the `X10` attached to the `G90` group). The port has a single
`Interpreter` with the virtualizer's dispatch rules, used for both.

It reports geometry to a `GeometrySink` (lines, rotary curves, arcs with their
centre and plane), tracks modal state and position (with G92 offsets), and
collects: total time and per-line time, bounding box, tools, feed rates,
spindle speeds, used axes and file type (3-axis / rotary / 4-axis), invalid
lines, tool-change lines and per-line S/T/M events for the tool timeline.

### Deliberate differences from the JavaScript

| Behaviour | gSender | Port | Why |
|---|---|---|---|
| Unclosed `(` | words after it are still parsed | rest of line is a comment | matches what Grbl/grblHAL execute |
| `G4 P` | milliseconds | seconds | Grbl semantics; gSender itself emits `G4 P<seconds>` |
| `G4` | overwrites the motion modal | leaves it | G4 is non-modal |
| Arc time | chord length (a full circle took 0 s) | arc length | accurate estimates |
| Arc bounds | end point only | includes the arc's extreme points | outline/limit checks |
| Bounding box frame | G92-shifted frame | same frame as the drawn geometry | consistency |
| Used axes | only words on `G` lines | also modal-continuation lines | correct 3/4-axis detection |
| `G92 A..` | ignored | offsets A too | rotary jobs commonly reset A with G92 |
| Bad `F` word | feed becomes NaN, estimates break | ignored | robustness |
| Per-line estimates | pushed only for lines with tokens (drifts from the sender's line numbering) | kept per line; aligned to sender lines by the caller | exact remaining-time countdown |

Tests: `tests/core/test_gcode_parser.cpp`, `tests/core/test_gcode_interpreter.cpp`.

## Step 6 — Macro expressions (`gs/expr`)

gSender macros use two constructs, both evaluated with an esprima-based
JavaScript-subset evaluator:

- `%` lines are assignments: `%X0=posx, global.state.wcs=modal.wcs`.
- `[...]` inside G-code lines is substituted: `G0 X[posx - 8] Y[ymax]`.

The port has its own small JavaScript expression engine rather than embedding
a JS runtime:

- `Value` — undefined/null/boolean/number/string/object/array/function with
  JavaScript reference semantics for objects (the per-macro context object and
  the controller's persistent `global` object are shared and mutated).
- A lexer/parser for the expression grammar (templates, member/call chains,
  object/array literals, all unary/binary/logical/conditional operators) and a
  tree-walking evaluator.
- Anything unsupported or any runtime error yields *unresolved*, mirroring the
  JS evaluator that caught exceptions and returned `undefined`. This is
  load-bearing: grblHAL has its own `[...]` expression syntax
  (`G0 X[#<_x>+1]`), and unresolved brackets must pass through to the firmware
  untouched.
- Built-ins exposed to macros (the JS spread `Math`, `Number`, `String`,
  `Boolean`, `JSON`, `Object`, `Date`, `parseFloat`, `parseInt` into the
  context) plus string/number/array methods such as `toFixed`.

Faithful quirk kept: an identifier whose value converts to a number is used
as that number (`posx` holds the string `"12.345"` but `posx + 1` is `13.345`).

| Behaviour | gSender | Port |
|---|---|---|
| `a[b]` read access | used the literal key `"b"` (evaluator bug) | uses the value of `b` |
| `x += 1` in a `%` line | treated as `x = 1` | compound assignment |
| `&&` / `||` | evaluated both sides | short-circuit |

## Step 7 — Firmware data tables and response parsing (`gs/protocol`)

**Data tables.** Error/alarm/setting descriptions, the settings metadata used
by the configuration UI and the machine profiles are large tables in the JS
sources. `tools/extract_data.mjs` bundles each source module with the
repository's esbuild, evaluates it and writes `resources/data/*.json`, which
CMake embeds into gs_core. Regenerate when gSender updates the tables.

**Line parsers.** `parseGrblResponse` / `parseGrblHalResponse` classify one
line into a `std::variant` of typed results (`OkLine`, `StatusLine`,
`ParserStateLine`, `SettingLine`, `AtciLine`, ...). They try the parsers in
the same order as `GrblLineParser` / `GrblHalLineParser` and port the regexes
verbatim with Boost.Regex, so each line is routed exactly as in gSender — the
routing tests from `lineParserRouting.test.js` and friends are ported
directly (`tests/core/test_protocol_parser.cpp`), including the documented
differences between the two firmwares (`[MSG:...]` is feedback on Grbl but an
info line on grblHAL; Grbl accepts mangled `ok`s).

Status reports use a hand-written tokenizer that reproduces the JS token
regex (including its v0.9 comma-format behaviour) and produce typed optional
fields (`mpos`, `wco`, `buf`, `overrides`, ...), so the runner can merge
reports the way the JS spread-merge did.

| Behaviour | gSender | Port |
|---|---|---|
| Hidden `._*` SD files | fell through to the generic info parser | ignored |

## Step 8 — Runners: accumulated machine state (`gs/protocol/runner`)

`Runner` ports `GrblRunner` / `GrblHalRunner`: it feeds each line through the
right parser and folds the result into `RunnerState` (merged status, parser
state, grblHAL axes and SD card) and `FirmwareSettings` (`$` settings,
parameters, version, grblHAL setting descriptions/groups/alarm and error
tables, tool table, ATCI values).

- Status reports merge like the JS spread-merge: absent fields keep their
  previous value, `Pn` is always refreshed, the `WCO` is sticky and work/machine
  positions are derived from each other, rounded to the decimals the firmware
  reported (the JS kept positions as strings and used `toFixed(digits)`).
- The JS signalled "changed" by swapping the state object and letting the
  controller compare identities; the port bumps `stateRevision()` /
  `settingsRevision()` only when a value really changes.
- `parse()` returns the typed line plus flags (`enteredAlarm`, `semver`) in
  place of the extra events the JS runners emitted.

| Behaviour | gSender | Port |
|---|---|---|
| `probeActive` | raw line contains `Pn:P` (missed `Pn:XP`) | pin list contains `P` |
| Grbl accessory state (`A:`) | sticky forever once seen | cleared when `Ov:` arrives without `A:` (Grbl only sends `A:` with `Ov:` while an accessory is on) |
| Alarm code for `ALARM:<text>` | `NaN` | the text |

Firmware reference tables (error/alarm descriptions, setting metadata) are
exposed by `FirmwareTables::get(firmware)`, parsed once from the embedded JSON.

## Step 9 — Time, and the streaming trio (`gs/runtime`, `gs/controller/streaming`)

**Event loop.** Every timer in the JS (`setTimeout`, `setInterval`,
`await delay()`) goes through `runtime::EventLoop`. The Qt app will implement
it with `QTimer`; tests use `ManualEventLoop`, which advances simulated time
deterministically (timers fire in time order, ties in scheduling order). Its
clock starts at 1,000,000 ms because ported code uses `timestamp > 0` as a
flag, as `Date.now()` never returns 0. `TimerScope` owns timers and cancels
them on destruction, so no callback can outlive the object that scheduled it.

**Sender** (character-counting streaming): lines are kept as views into the
program text; the pending line that did not fit is cached and never filtered
twice (the filter has side effects); only an `ok` frees buffer bytes; a line
that filters to nothing is acknowledged locally. The remaining-time countdown
(`fakeCountdown`) is ported with its timers.

**Feeder** and **Workflow** are straightforward; the feeder shares one context
object across a batch so `%` assignments carry between a macro's lines, and
tolerates re-entrant calls from its data filter.

| Behaviour | gSender | Port |
|---|---|---|
| `isCountdownRunning()` | returned the *paused* flag, so the countdown never paused | returns `!paused` |
| Countdown restart interval | leaked one interval per job start | replaced on restart, cleared on unload |

## Step 10 — Continuous jogging (`gs/controller/jog_streamer`, `jog_limits`)

`JogStreamer` streams short `$J=G21G91...` segments paced by wall-clock time
(acks are only backpressure), keeping enough motion queued that the planner
never decelerates. Velocity mode (keyboard/gamepad), displacement mode
(handwheel), soft-limit travel budgets, backpressure gates (RX budget, planner
low-water, in-flight count), draining after stop and the 30 s watchdog are all
ported, along with `jog-limits.js` and `homing.js`.

The 40-test JS suite is ported to `tests/core/test_jog.cpp`. Running the
upstream suite shows **2 of its tests fail on gSender itself**: they were
written when the assumed acceleration for unreported axes was 200 mm/s²;
`ASSUMED_ACCEL` is now 1000. The port reproduces the JS numbers exactly, so
those two tests assert the same intent against the current constant.

## Step 11 — The machine controller (`gs/controller/controller`)

One `Controller` class ports both `GrblController.js` and
`GrblHalController.js`: they share most of their logic and differ in dozens of
small ways, each an `isGrbl()`/`isGrblHal()` branch, so the differences stay
visible side by side. There is no socket layer: the controller is driven by
`receiveLine()` (lines from the board), its command methods (what the UI
calls), a `DeviceLink` (bytes to the board, tagged `Write` - through the write
filter - or `Immediate`) and the `EventLoop`; it reports through typed
`ControllerEvent`s (`events.hpp` maps each to the socket.io event it replaces).
Configuration comes in through `ControllerHooks` (macros, event triggers,
preferences, system commands).

Ported with it (`helpers.hpp`): override byte sequences (`runOverride.js`),
A-to-Y rotary translation (`gcode-translation.js`), `[\xNN]` realtime tokens,
`EventTrigger` and the idle-waiting `ToolChanger`.

The upstream Jest suites for commands, file loading/start-from-line, runner
events, workflow, grblHAL `[AXS:]` probing and reply echo are ported to
`tests/core/test_controller.cpp` (136 cases over both firmwares), asserting
on the wire and on events rather than on spies. Upstream quirks the tests pin
as-is: the feeder reports completion once its last line is *written* (so a
start/resume hook's job starts before the hook's final `ok`), and Grbl's
appended `%wait ; ...` line is not recognized as a wait (the sender filter
keeps comments on `%` lines).

| Behaviour | gSender | Port |
|---|---|---|
| Grbl job-line comment stripping | greedy `/\s*\(.*\)*\)/` - `G1 (a) X1 (b)` lost `X1` | `/\([^)]*\)/` for both firmwares (grblHAL's) |
| grblHAL errors/alarms before `$EE`/`$EA` arrive | error text `undefined`; alarm printed raw with no error report (its intended static-table fallback was unreachable) | both fall back to the static grblHAL tables |
| Realtime bytes | JS strings, UTF-8 encoded on the wire (`C2 85`) | single raw bytes |
| grblHAL `toolchange:context` | shallow-merged partial objects | contexts are complete structs; tool `mappings` survive a context that carries none |
| Jog streamer's status/settings source | controller snapshot (up to 250 ms old) | the runner's current state |
| Settings/state descriptions debounce | lodash `debounce` | cancel-and-rearm timer (same 150 ms) |
| Background polling | always on | `setPollingEnabled()` (tests; later firmware transfers) |

## Step 12 — Connection session (`gs/controller/session`)

`Session` is what `Connection.js` plus the engine's `firmwareFound` handler
did: frame the byte stream into lines (split on `\n`, partial lines wait),
poll `$I` every 800 ms up to 7 times until a line names the firmware
(`/grblhal/i`, else `/grbl|fluidnc/i` - the Grbl startup banner or grblHAL's
`[FIRMWARE:grblHAL]`), fall back to the configured default, then create and
open the matching `Controller` and hand it every later line. The identifying
line itself is not passed on, as upstream (the controller did not exist yet).
The transport stays outside the core: the owner calls `opened()`,
`receive(bytes)` and `closed()`.

`ConnectionFirmwareDetect.test.js` is ported to `tests/core/test_session.cpp`
together with framing and hand-over tests. The upstream "never emits an
undefined firmware" case has no C++ equivalent (the firmware is an enum).

## Step 13 — Configuration file (`gs/config`)

gSender keeps macros, event hooks, commands, job statistics and maintenance
tasks in one JSON file (`~/.sender_rc`). The port's `ConfigStore` behaves like
`services/configstore/index.js`: lodash-style paths (`json_path.hpp`), every
change re-reads the file and writes the whole document atomically (temp file,
flush, rename), unreadable files are reported and never overwritten,
`validateAndRepair()` backs a corrupt file up as `.corrupt-<ms>.bak`, the old
list form of `events` is migrated to an object keyed by event, and defaults
are backfilled (state, job statistics and the maintenance tasks - extracted
into `resources/data/config_defaults.json` by `tools/extract_data.mjs`).
Members the port does not know are preserved, so the file stays compatible.

`MacroStore` and `EventStore` port the record handling of `api.macros.js` and
`api.events.js` (repair of macros without ids, column assignment, a hook
without commands is disabled), and `makeControllerHooks()` connects them to
the controller the way `macro:run` and `EventTrigger.js` read them.

| Behaviour | gSender | Port |
|---|---|---|
| Non-integer numbers written to the file | `1.5` | `1.5E0` (Boost.JSON); same value when read back, and ids, times and indexes are integers |

**Open decision:** whether the port reads and writes gSender's own
`~/.sender_rc` (users keep their macros and hooks, but both applications may
write it) or uses its own file, importing `~/.sender_rc` once on first start.
The store takes any path; the application picks one.

## Step 14 — Transport (`src/transport`, `gs_transport`)

`AsioLink` is the `DeviceLink` to a real board, ported from
`SerialConnection.js` onto Boost.Asio (the LibPack has no QtSerialPort):

- **Serial:** 8N1 at the chosen baud rate (115200 by default); Asio's
  flow-control option asserts DTR and RTS - or RTS/CTS handshaking with
  `rtscts` - as node-serialport did, so Arduino-based Grbl boards reset on
  connect and print their banner.
- **TCP:** `host:port` (port 23 by default) with a 2 s connect timeout; a
  path that looks like an IPv4 address means network, with upstream's regex
  quirk kept (`looksLikeIpAddress`).
- One I/O thread per link. Writes and immediate writes share one queue, so
  the board sees them in call order, as before. Received bytes and completions
  go back to the owner thread through a `Dispatcher` (in the app, the event
  loop's `post`); nothing is delivered after the link is destroyed. A link
  lost to the peer or an I/O error reports `onClosed`; `close()` is silent.
- `listSerialPorts()` enumerates COM ports with SetupAPI (manufacturer,
  friendly name, PnP id, USB vendor/product ids); `isRecognizedPort()` applies
  the engine's vendor/product allow-lists. Linux/macOS enumeration is not
  written yet.

Tests (`tests/transport`, their own executable) run the link over loopback
TCP, including feeding a `Session` that identifies Grbl from its banner.

**Needs a human with hardware:** serial connections to real Grbl and grblHAL
boards (DTR reset, banner detection, streaming a job) have not been exercised.

## Step 15 — Program analysis (`gs/job/program_analysis`)

When a file loads, gSender's visualizer worker interpreted it and the UI sent
the per-line time estimates to the controller (`updateEstimateData`) for the
remaining-time countdown, while the file panel showed its statistics.
`analyzeProgram()` does both with the port's interpreter: estimates are kept
on the sender's numbering (one per non-blank line - gSender's drifted, see
Step 5), plus bounds, tools, feeds, spindle speeds, used axes, invalid lines,
tool changes, the final units and the file type. `interpreterOptionsFor()`
reads the machine limits from the firmware settings as the UI did
(accelerations `$120`-`$123`, rates `$110`-`$113`, ATC from `NEWOPT`).

Faithful quirk: the estimator's trapezoid formula (from Slic3r) assumes a
move reaches its programmed speed; for moves too short to do so it returns
distance / speed, ignoring acceleration.

## Step 16 — A simulated Grbl board (`gs/sim/grbl_simulator`)

Not a port - new: `GrblSimulator` is a `DeviceLink` that behaves like a Grbl
1.1 board, so the application can be developed, demonstrated and tested
without hardware, and the whole stack can be tested end to end.

It prints the startup banner on open, answers `?` (MPos/WPos per `$10`, FS,
Ov, WCO), `$$`, `$#`, `$G`, `$I`, `$X`, `$H`, `$C`, `$J=`, settings writes,
and executes a G-code subset (G0-G3 - arcs along their chord -, G4, G10 L2/L20,
G17-G21, G28/G30, G38.x (always succeeding), G53-G59, G90/G91, G92/G92.1,
M0-M9, M30) with time-based motion from distance and feed (overrides apply),
a 15-block planner that delays the `ok` when full, feed hold/resume, jog
cancel, soft reset (ALARM:3 when interrupting motion) and the usual error
codes (2, 3, 5, 8, 9, 20, 22). Output is always delivered from the event
loop, as a real device's would be.

`tests/core/test_simulator.cpp` covers it and runs a square job through
`Session` + `Controller` to completion: detection from the banner, the
controller's soft reset and `$$` initialization, streaming, and the job-end
detection after half a second of idle.

## Step 17 — The Qt application (`src/app`, first cut)

The UI talks to the core directly (no socket layer):

- `QtEventLoop` implements the core's `EventLoop` with `QTimer`s (precise
  timers - the jog streamer ticks every 10 ms); `post()` is thread-safe and is
  the transport's dispatcher.
- `Machine` is the application service - gSender's CNCEngine plus the UI's
  controller sagas: it owns the config store, the link (`AsioLink`, or the
  simulated board for the "Simulator" port), the `Session`, the loaded program
  and its background analysis (a `QThreadPool` task with cancellation; the
  toolpath is collected with arcs tessellated), sends the estimates when the
  sender asks for them, sends the tool change context (gSender's default:
  Ignore) to each new controller, and re-emits controller events as Qt
  signals. A link is never destroyed inside its own callback: teardown is
  queued.
- Panels (`panels.hpp`): connection bar (ports, recognized boards first, the
  simulator, or an IP address), position readout with zeroing/home/unlock/
  reset, jogging (click = step via `$J=`, hold = continuous through the jog
  streamer), console with history, and the job panel (load/close, start/
  pause/resume/stop, progress, remaining time, file statistics).
- `gsender --simulator --load demo.nc --start --screenshot out.png --wait 9000
  -platform offscreen` runs the real application without a display and saves
  a screenshot - used to check the UI in automation.

The toolpath view is a placeholder in this step.

## Step 18 — Toolpath view (`src/app/toolpath_view`)

The visualizer draws the analysed program with QPainter: rapids dashed,
cutting moves in blue, moves the job has passed (by acknowledged sender line)
dimmed, a 10 mm grid, the work origin's axes and the tool at the work
position. Orthographic camera with Top/3D presets, orbit (drag), pan
(right/middle drag or Shift+drag), zoom about the cursor (wheel) and fit
(double-click). The analysis sink records the sender line of every segment
(`GeometrySink::atLine`, called by `analyzeProgram`) for the progress colours.

QPainter rather than OpenGL for now: it also renders on the offscreen
platform, so screenshots of the running application can check it
(`--view 3d` selects the 3D preset). The toolpath data is renderer-neutral;
an OpenGL view can replace this one if very large files need it.

## Step 19 — Overrides, spindle/coolant and macros (`src/app/controls`)

- Overrides bar under the job progress: feed and spindle sliders (10-200 %)
  sent on release through the controller's override commands (realtime
  bytes, 25 ms apart), 100 % resets, rapid presets 25/50/100 %; the controls
  follow the overrides the firmware reports.
- Spindle & coolant tab: M3/M4 at a speed, M5, M7/M8/M9, with the modal state
  shown.
- Macros tab: the macros of the config file (`MacroStore`) - run (also by
  double-click), create, edit, delete; runs go through the controller's
  `runMacro`, so `%` assignments and `[expressions]` work as in gSender.

## Step 20 — Settings (`src/app/app_settings`, `settings_dialog`)

The application's preferences - what gSender's UI kept in its own store and
pushed to the server - are saved under `"app"` in the port's config file and
applied to the controller: spindle delay, line warnings, Grbl A-axis
passthrough, default firmware (for boards that do not identify themselves),
network port, last port/baud, and the tool change context (strategy Ignore -
gSender's default -, Pause or Code with pre/post hooks, M6 passthrough, skip
dialog). A "Code" tool change prompts after the pre-hook; Continue runs the
post-hook and resumes (`toolchange:post`). The re-zero and tool-sensor
strategies (wizards with probing) are not ported yet.

The Firmware tab lists the board's `$` settings with units and descriptions
(grblHAL's own `$ES` descriptions first, then the extracted Grbl/grblHAL
tables); changed values are written as `$n=value` followed by `$$`.
Menus: File (load/close/quit), Machine (settings, firmware settings), Help.

## Step 21 — Touch plate probing (`gs/probe/probing`, simulator contact)

`Probing.ts` is a pure generator - plate and machine settings in, G-code out
- so it lives in the core. The routines rely on the controller's `%NAME=`
assignments and `[expression]` words (`X_LEFT=posx`, `X[posx/2]`, ...) and
run through `Controller::gcodeSafe(code, "G21")`, as the Probe widget does,
with the modal distance mode appended to restore it.

Upstream has no tests for it, so `tools/gen_probe_fixtures.mjs` bundles the
real `Probing.ts` with esbuild (the Redux store that `SoftLimits` reads is
stubbed per case) and records 168 cases - every plate (Standard Block, Z
Probe, 3D Probe, BitZero, AutoZero Auto/Tip/Diameter) x axes x corner, with
units, firmware, homing/soft limits, `$13`, feeds and thicknesses varied by
a seeded generator - in `tests/data/probing_golden.json`. The C++ output
matches every case byte for byte, and `makeProbingOptions` (the widget's
mm-to-inch conversion) reproduces the recorded options.

Upstream quirks kept for identical output:

| Quirk | Effect |
| --- | --- |
| AutoZero diameter routine tests `axes.z && axes.y && axes.z` | Y+Z without X runs the XYZ routine |
| Imperial options drop the BitZero thicknesses | BitZero falls back to 13 / 15.5 mm in inch workspaces |
| `getZDownTravel` compares the probe distance in workspace units with `$132` (mm) | inch workspaces cap Z probing by a mm figure |
| `${prependUnits} G0 ...` with no G20 | lines start with a space |
| Auto/BitZero centring reads `posx`/`posy` from the last status report | the centre can lag the touch by one status poll (at most about 0.1 mm at the slow feed) |

The simulator now probes for real: conductive boxes (`touchPlateOnCorner`
builds a standard plate hooked over a stock corner) and a bit radius; G38.2/.3
stop where the bit first touches, G38.4/.5 where it leaves; wrong initial
state raises ALARM:4, a miss ALARM:5 (G38.2/.4) or `[PRB:...:0]` (.3/.5);
`Pn:P` shows while touching. G4 and G38.x now hold the input until they
complete, as Grbl's buffer synchronisation does, so the lines after a dwell or
probe are parsed at the settled position. An end-to-end test runs the widget's standard
block XYZ routine on each corner and checks that work zero lands on the
stock corner.

## Step 22 — Probe tab (`src/app/probe_panel`)

The Probe widget as a tab beside Jog/Spindle/Macros: the plate type, the
routines the plate offers (Z, XYZ, XY, X, Y - only Z for a Z probe), the bit
(common diameters, or typed; AutoZero adds Auto and Tip) shown only for
routines that compensate for it, and the corner - a small plan view of the
stock with the plate; a click moves it clockwise. Upstream keeps the corner in
`widgets.probe.direction` without a control (its UI no longer calls
`nextProbeDirection`); the port shows it.

"Probe" opens the run step: gSender's instructions, a circuit light fed by
the probe pin (`Pn:P`), and Start, enabled once the pin has triggered while
the dialog is open, or when the circuit check is off in the settings, or
after "Confirm manually" (upstream's CONFIRM_PROBE shortcut). Start builds the
routine from the settings, `$13`/`$22`/`$132` and the machine position
(`Machine::probeRoutine`) and runs it with `gcode:safe` in mm, restoring the
distance mode (`Machine::runProbe`).

On the simulator the dialog first places a plate where the operator would:
a standard block with the bit 5 mm in from its outer faces and 10 mm above,
a Z probe puck under the bit, or - for a 3D probe - the stock corner itself.
AutoZero and BitZero plates are not modelled yet, so their probes miss
(ALARM:5). `GrblSimulator::setSpeed` runs motion faster than real time; the
application test runs the XYZ routine at 200x and checks the zeroed corner.

The Settings dialog has a Probe page for the plate profile and the probe
feeds, retractions and distances (stored in mm under `app.probe`). The
workspace is metric for now; the imperial conversions exist in the core
(`makeProbingOptions`) for when inch workspaces are added.

## Step 23 — Surfacing (`gs/surfacing`, `src/app/surfacing_dialog`)

gSender's Surfacing generator is another pure function - stock rectangle,
depths, bit and cutting settings in, a program out - so it is ported to the
core with the same golden approach: `tools/gen_surfacing_fixtures.mjs` runs
the upstream generator (the UI store and controller stubbed) over 41 cases -
spiral and zig-zag, all five start positions, both cut directions, mm and
inch workspaces, multi-layer depths, tool numbers, dwell and coolant - and
the C++ output matches every line. Upstream's own
`surfacing-output.test.ts` still expects an older ramp (`G1 X28.36 Z-4`)
than the generator now writes, so its golden lines are not used.

Two upstream details worth knowing: the generator's result contains `"\n"`
elements (spacers that become blank lines when joined), and the spiral's
centre-start variant needs the spiral's last X/Y, which upstream finds by
running the lines through a toolpath - the port tracks the values it writes.
Inputs that never finish upstream (zero stepover or cut depth, a zero ramp
length) end after one pass here.

The two golden generators now share `tools/lib/bundle.mjs`: esbuild loading
with stubs for the Redux store, the settings store and the socket.io
controller; a seeded chooser; and one-case-per-line output for readable
diffs.

In the application, Tools > Surfacing opens the form (defaults and last
values from `app.surfacing`, stored in mm), a plan-view preview
(`ToolpathPreview`, fed by `traceToolpath`) and the G-code with its line
count. "Load as Job" makes it the job as `gSender_Surfacing.gcode`; as
upstream, both actions are disabled unless the machine is idle or jogging.

## Step 24 — Jogging presets and keyboard shortcuts

The widget logic that buttons and shortcuts share moved into the core, where
it is testable with simulated time:

- `gs/controller/jogging`: upstream's step-jog command
  (`$J=G21 G91 X5 F3000` - the old panel sent `$J=G21G91X10F3000`), the
  prevent-jogging-past-limits filter (X-, Y-, A- and Z+ blocked while their
  switch is triggered), the Rapid/Normal/Precise presets (20/10 mm at 5000,
  5/2 mm at 3000, 0.5/0.1 mm at 1000 mm/min) and `JogHelper`: released within
  the threshold (250 ms) a key or button steps; held, it starts a continuous
  jog that stops on release; auto-repeat is ignored; upstream's throttles
  (150 ms, threshold - 25 ms) are kept.
- `gs/controller/actions`: the job control rules (`canRun` idle/hold/check
  and not running, `canPause` running and moving, `canStop` running or
  paused; run resumes a paused or held job, stop leaves check mode, the Stop
  shortcut without a job cancels a jog or resets), zeroing, go-to-zero with
  the safe retract height (machine Z with homing, relative lift otherwise,
  via `gcode:safe` in mm) and the workspace controller shortcuts with
  upstream's state gating. Upstream quirk kept: in an alarm other than 1 or
  2, every allowed controller shortcut (reset, homing) just unlocks.

In the application a `Jogger` owns the presets and the helper; the Jog tab
(preset buttons, XY/Z steps, speed, tap/hold buttons) and the shortcuts use
it. `ShortcutManager` holds gSender's action table - command ids, titles,
categories and default keys (`~`/`!`/`@` start/pause/stop, Shift+arrows and
Shift+PgUp/PgDn jog, Shift+W/E/R zero, Shift+S/D/F/A go to zero, Shift+V/C/X/Z
presets, Shift+B/N/P/O/I view, `^` toggles all) - with the user's changes
from `app.shortcuts`, and an application event filter: shortcuts run while
the main window is active and the focus is not in a text field (Mousetrap's
rule); symbols bind without Shift as Mousetrap binds characters; jog keys
jog while held and stop on release or when the window loses focus.
Tools > Keyboard Shortcuts edits keys (one chord, conflicts refused) and
on/off states, and stores only what differs from the defaults. The
visualizer gained Front/Right/Left views, view cycling and zoom for the
shortcuts.

Not ported yet: macro shortcuts, gamepads, the Toggle Rotary Mode and
Lightweight Mode shortcuts (no rotary mode or lightweight view yet), and
editing the presets outside the Jog tab. (The go-to-corner and park
shortcuts came with Step 29.)

## Step 25 — Start From Line

The controller already rebuilt the machine state for a mid-file start (modal
state, position, feed and speed from the skipped lines, a rise to the safe
height above the file's top, the spindle and its delay); the application now
offers it. `Machine` remembers the line to offer, as upstream's JobControl
does: where a stopped job was, or where a lost connection cut a running one
off (`serialport:closeController` with the current line, which also raises a
notice to reconnect and resume); 1 once a job completes. The line is read at
the workflow's stop, before the sender rewinds - upstream uses its last
sender status, which can be a poll (250 ms) old. "From Line..." in the job
panel opens gSender's dialog: the job's size and stop line, the suggestion to
resume about 10 lines earlier, the line and the safe height (the safe
retract height, else 10 mm). An application test stops a simulated job and
resumes it from a later line: the rise to Z10 comes first, the skipped
lines never reach the board.

The simulator no longer raises ALARM:3 for a soft reset during a feed hold:
Grbl keeps the position after a completed hold, which is why a forced stop
holds before resetting.

## Step 26 — Run outline (`gs/job/outline`)

gSender traces a job's footprint above the stock from a web worker
(`Outline.worker.ts`): Detailed (default) takes the hull of the visualizer's
vertices, Square the file's box (`[xmin]`... evaluated in the file context,
or the numbers when there are no vertices), Rapidless Square the box of the
cutting moves only (arcs by their axis extremes). The program lifts by the Z
travel, traces with G0 (or G1 at the outline speed, or the laser at S1),
returns to where it started (`%X0=posx...`, `X[X0] Y[Y0]`) and restores the
distance mode (`[MM]`).

Detailed calls `concaveman(points, Infinity)`. With infinite concavity
nothing is ever dug in, so the result is concaveman's own convex hull, as a
ring starting from its last point - which gSender then orients (reversing
when sum((x2-x1)(y2+y1)) > 0) and rotates to begin at the vertex nearest the
origin. The port reproduces that hull exactly: the four-extremes cull with
point-in-polygon's ray casting, the monotone chain with collinear points
dropped, and robust-predicates' `orient2d` (Shewchuk's adaptive exact
predicate - toolpaths are full of collinear points, where a rounded
determinant would keep or drop the wrong ones). `tools/gen_outline_fixtures.mjs`
runs the real worker (with a fake `self`/`postMessage`) over random,
collinear-heavy, circular, symmetric and degenerate point sets, boxes and
programs with arcs and inches; the C++ output matches all of them.

In the application the Outline button and the RUN_OUTLINE shortcut run it
with the settings' style and speed (General page). The vertices are the
port's own toolpath in program order, rapids included, so where arcs are
tessellated differently from upstream's visualizer the hull can differ
slightly. The Z travel is upstream's: 5 mm, or with homing what is left
above the machine Z less 1 mm - which at machine Z 0 is -1, a dip of 1 mm and
a final invalid `Z--1` (kept, see the tests). The file context (the
toolpath's box, as the visualizer sets `controller.context`) now also goes to
macros, so `[xmin]`... work in them as upstream.

## Step 27 — Inch workspaces (`gs/util/units`)

gSender stores lengths in mm and shows an inch workspace converted, with
its own rounding in each place; `gs/util/units` ports those rules once
(`convertToImperial` 3 decimals, `convertToMetric` 2, the jogging widget's
`convertValue` multiplying by 1/25.4, and `mapPositionToUnits`: the DRO shows
mm with 2 decimals - a negative zero as 0.00 - and inches with 3, keeping
"-0.000" as upstream does; a custom number of decimals overrides both).
Probing and surfacing now share them.

The workspace units (`app.units`, "mm"/"in", and the position decimals)
switch from the DRO's units badge or the General settings, and:

- the DRO converts (A stays in degrees);
- the jog presets (stored in mm) are shown converted, step jogs are sent
  in G20 (`$J=G20 G91 X0.197 F118.11`) and continuous jogs in inches;
- the Probe widget offers the inch tool diameters and builds its options
  with the imperial conversions (`makeProbingOptions`), the simulated plate
  converting the diameter back to mm;
- the Surfacing dialog edits the converted values, writes a G20 program and
  stores mm again;
- Start From Line takes the safe height in inches (default 0.4 in).

The settings dialog's probe values, the outline speed and the visualizer
stay in mm for now.

## Step 28 — Tool change wizards (`gs/toolchange`, `src/app/toolchange_dialog`)

The remaining tool change strategies are UI wizards in gSender
(`src/app/src/wizards`): when the controller reports `gcode:toolChange` for
"Standard Re-zero", "Flexible Re-zero" or "Fixed Tool Sensor", the UI sends
start-up G-code (storing the position, modals and spindle in
`global.toolchange.*`), then walks the operator through steps whose actions
run G-code; each action is `wizard:step` + `gcode`, the controller answers
`wizard:next` when the lines are through, and the last action ends in
`%toolchange_complete`, which resumes the job.

The four wizard definitions are ported to the core as data builders
(start-up G-code, steps, instruction text, actions) from the probe settings
(`getProbeSettings`: the plate's Z thickness, BitZero flat on the surface)
and the machine's `$13`/`$20`/`$132`, Z position and tool.
`tools/gen_toolchange_fixtures.mjs` runs the upstream wizard modules - JSX
instructions rendered to text with react-dom/server - over 40 settings
combinations; the port matches every start line, instruction and action.
Upstream quirks kept: the Flexible Re-zero hint always says "0.4in" (it
tests the `$13` string, and "0" is truthy); the Fixed Tool Sensor start goes
out with a plain `gcode` command while the re-zero ones go through
`wizard:start` (queued until the board is idle); stored positions come back
as plain numbers (`X5`, not `X5.000`), because evaluate-expression converts
numeric identifiers - pinned by a controller test.

In the application `Machine::startToolChangeWizard` builds the wizard
(asking, for the first tool with a fixed sensor and "Prompt for first tool",
whether to run the full wizard or only measure the loaded tool) and the
wizard dialog shows the steps, the instruction, the tool to load and the
actions; it advances on `wizard:next` and closes after the resume. One
deviation: actions stay disabled until the start-up G-code has actually gone
out (`Controller::wizardStart` gained an optional "started" callback) -
upstream relies on the operator being slower than its 200 ms idle poll, and
an action run earlier finds no stored position. The tool change settings
gained the strategies, the fixed sensor location, the first-tool behaviour
and the optional tool change location (with "Use current" buttons). Tests:
a core end-to-end run and an application run take a job through M6 with
the Standard Re-zero wizard on the simulator and on to its last line.

## Step 29 — The DRO's places and offsets (`gs/controller/locations`, `src/app/dro_panel`)

The position panel now does what gSender's DRO does. The G-code builders
are in the core, with upstream's `RapidPosition.test.ts` and
`Parking.test.ts` ported and a table of every corner from every homing
corner:

- Corners (`getMovementGCode`): up to 1 mm below the top of Z - or the
  pull-off when homing does not set the origin ($22 bit 3) - then, in
  machine coordinates, the pull-off in from the switches at the homing
  corner ($23), max travel ($130/$131) less the pull-off at the far sides.
  Unhomed (no homing flag) everything is computed as if homing were back
  right; grblHAL takes the flag from $22 bit 3. Nothing is sent when the
  limits are missing, or when $23 inverts Z (upstream's "Other").
- Park (`workspace.park`, machine coordinates) with the same lift, and the
  settings' "Go to" for a stored position.
- Go To Location: ABS and INC in work coordinates with the safe retract
  (machine Z with homing, else a relative lift - lowered again for INC),
  MCS as one `G53 G0` of X, Y (and A).
- Typed work positions (`G10 P0 L20 X12.5` in the workspace units) and
  single-axis homing (`$HX`, offered for $22 bit 1).

Upstream behaviour kept: the corner and park lines carry `G21`, which stays
modal; an INC move leaves G91 modal (gcode:safe restores only the units);
MCS neither lifts nor moves Z (the port disables its Z field); the corner
shortcuts need homing enabled but not a homed machine, their buttons both.

Deviations:

| Behaviour | gSender | Port | Why |
|---|---|---|---|
| Go To retract in an inch workspace | the mm height written into G20 lines (10 mm became 10 in) | converted to inches | a 254 mm lift or plunge |
| INC after a lift, inch workspace | adds the mm work Z to an inch target | work Z in inches | same unit mix |
| MCS prefill | raw mm figures, also in inches | the machine position in the workspace units | consistent fields |
| Typed position, empty or invalid | sent as 0 (`Number("")`) | ignored | an Enter on an empty field zeroed the axis |
| Typed position while the machine moves | the field re-mounts on every report | kept while it has the focus | typing is not interrupted |

In the application the DRO (`dro_panel`, split out of `panels.cpp`) has the
workspace selector (G54-G59 with upstream's colours, following the modal
state, disabled while running), per-axis zero - or home, with the
"Single axis" switch - buttons, editable work positions (Enter applies,
Escape or leaving the field restores), machine positions, go-to-zero
buttons, Go To..., the corner arrows and Park (shown with homing enabled,
enabled once homed), Zero All, Home, Go to XY, Unlock and Reset. "Warn when
setting zero" (General settings) makes the zero buttons ask first; the
park location is set there too, with "Use current" and "Go to". The
buttons follow upstream's canClick: connected, no job running, idle or
jogging.

The simulated board now reads $22 as a bitmask and homes single axes
(`$HX`, with bit 1); its homing time scales with `setSpeed`. An application
test selects G55, types a position, homes, goes to a corner and the park
position, runs Go To in all three modes and homes X alone.

## Step 30 — Status area and Machine Information (`src/app/status_area`)

gSender shows the machine state as a coloured pill over the visualizer
(MachineStatus): the state's name ("Running", "Jogging", "Tool Change"...;
"Disconnected" without one), in an alarm its code with a "?" for the
description (grblHAL's own `$EA` text first, then the firmware tables), a
lock icon beside it, and under it in an alarm a button that unlocks - or
runs homing for the homing lock. The port overlays the same on its
visualizer, with upstream's colours.

The two buttons follow different rules upstream, both kept (core
`actions`, with upstream's `isHomingFailureAlarm` tests ported):

| | Alarm button (MachineStatus) | Lock icon (UnlockButton) |
|---|---|---|
| ALARM 1, 2, 14 | reset (soft reset + `$X`) | `$X` |
| ALARM 10, 17 | reset | reset |
| Homing lock, ALARM 11 | homing | `$X`, then the configuration is read again |
| ALARM 6-9 (homing failed) | ask: Rehome or Unlock Anyway | ask |
| other alarms | `$X` | `$X` |
| Hold | cycle start | cycle start |
| any other state | `$X` | cycle start |

The homing-failure question explains that the position is unknown (and for
ALARM 8/9 that a switch or its wiring is at fault, with how to disable
homing meanwhile). Deviation: closing it does nothing - upstream's dialog
treated any close as "Unlock Anyway".

Machine Information ("i" beside the state, or the DISPLAY_MACHINE_INFO
shortcut) lists the firmware version, the CNC modals (probe style,
coordinate system, plane, units, distance, feed, spindle, coolant), the
input pins (limits, probe, door, cycle start, hold, reset) and the tool,
and offers "Lock stepper motors": `$1=255` keeps the motors powered, the
previous idle delay is remembered and restored (50 when none was).

The DRO's Home button now follows upstream (idle or jogging): in an alarm
the status area's button homes. The simulated board can raise any alarm
(`triggerAlarm`); the application test runs ALARM 3 and 9 through the
buttons, and the stepper lock round trip.

## Step 31 — Calibration tools (`gs/calibration`, `src/app/calibration_dialogs`)

gSender's Tools include two calibration wizards; their arithmetic and G-code
are in the core, with upstream's `movement_tuning.test.tsx` and
`XY_Squaring.test.tsx` ported:

- **Movement Tuning**: mark where an axis is, move it (100 mm, 4 in; Z
  -50 mm downwards) with a `$J=` jog at 1000 mm/min, measure how far it
  really went, and scale its steps/mm: `$100 x moved / measured`, to two
  decimals (0 when nothing was measured), written as `$100=98.04` and `$$`.
- **XY Squaring**: mark point 1, move X (300 mm, 12 in), mark 2, move Y,
  mark 3 (`G91 G21 G0 X300` - left in G91, as upstream), then measure the
  triangle's sides. The corner's deviation from 90 degrees comes from the law
  of cosines; square within 0.1 degree, "slightly out" when the diagonal is
  within 2 mm (0.079 in) of a square one, else "needs adjustment" - with the
  rail correction. Where the measured X or Y side differs from the move by
  more than 0.1 %, the new steps/mm are offered (`$100=`/`$101=` to three
  decimals, both written).

Deviations: in an inch workspace the tuning jog's feed is 1000 mm/min in
inches (upstream wrote F1000 into the G20 jog); a tuning move that is not
sent (machine busy, or towards a triggered limit with the protection on)
does not advance the wizard; the squaring threshold follows the current
units (upstream fixed them when its module loaded).

In the application both wizards (Tools menu) stay open beside the main
window, whose jog controls position the machine between steps. Rows show
pending, current and done; moves need an idle machine, measurements a
positive value. XY Squaring draws its triangle - the marked points, the move
being made, the measured sides - and ends with the verdict and the
recommended steps/mm, written after a confirmation. An application test
runs both on the simulator and checks the rewritten `$100`/`$101`.

## Step 32 — Macro shortcuts and automations

**Macro shortcuts.** Upstream adds every macro to its shortcut table
(`commandKeys`, keyed by the macro id, category "Macros") unbound and
switched off; binding keys in the editor switches a shortcut on; the
shortcut runs the macro only when the machine is idle, with the loaded
file as its context. The port's shortcut list is now the fixed table plus
one action per macro (`shortcutActions(machine)`, rebuilt when macros
change), actions carry their default on/off state, the editor switches a
shortcut on when keys are assigned, and only differences from the defaults
are stored - a macro that is on counts as one.

**Automations.** The controller already ran the config file's event hooks
(`events`: `gcode:start`, `gcode:pause`, `gcode:resume`, `gcode:stop` with
trigger "gcode"); the Settings dialog now has gSender's Automations page to
edit them: per event its description, an Enabled switch and the G-code. As
upstream's EventInput, the first commands create the hook (enabled), later
edits update it, and a hook left without commands is disabled. Tests: a
macro bound and triggered through the shortcut manager, and a start hook
that reaches the simulated board before the job's first line.

## Step 33 — Spindle/Laser (`gs/controller/spindle`, the Spindle/Laser tab)

gSender's Spindle widget switches the board between spindle and laser mode
and drives either. The mode switch is in the core (tested line by line):

- to the laser: M5 first if the spindle turns, the workspace units, a
  `G10 L20 P<wcs>` shift making the current position read the work position
  plus the laser's offset from the spindle (so the laser works where the
  spindle was), `$30`/`$31` set to the laser's power range, `$32=1`, the
  device units back;
- back to the spindle: the shift reversed, the spindle's range restored,
  `$32=0`.

Offsets round as upstream (2 decimals in mm, 3 in inches). On Grbl the
laser's range and offset are the app's settings and the spindle's range is
remembered while the laser has `$30`/`$31`; on grblHAL the laser has its
own settings (`$730`/`$731`, offset `$770`/`$771` or `$741`/`$742`) and
only `$32` changes - going back writes the spindle's range unless the board
lists an SLB laser spindle (`SLB_LASER`/`PWM2`). As upstream's store, the
new `$30`-`$32` count at once (no `$$` follows). Deviation: upstream also
divided the position by 25.4 for `$13=1` though its positions are already
mm.

The tab (renamed Spindle/Laser) shows the mode switch and either the
spindle's speed (within `$31`..`$30`) with CW/CCW/Stop, or the laser's power
(% of its maximum) with Laser On (`G1F1 M3 S<power>` to focus), Laser Test
(fires for the set duration, then off) and Laser Off; speed and power
changes reach a running spindle or lit laser 300 ms after the last one
(`S<value>`). grblHAL boards with several spindles get a selector (`M104 Q`,
then the list again). The settings dialog has a Spindle/Laser page (ranges,
laser offset, "Laser on during outline" - which makes Run Outline trace with
the laser lit in laser mode); the TOGGLE_SPINDLE_LASER_MODE shortcut and the
CW/CCW/stop shortcuts follow the mode. An application test switches the
simulated board to laser mode and back, checking the shift, the ranges,
focus and a live power change.

## Step 34 — Statistics: jobs, maintenance, alarms (`gs/config/history`, `src/app/stats_dialog`)

gSender keeps the machine's history for its Stats page; the port records
the same, in upstream's JSON shapes (the config file's `jobStats`,
`maintenance` and `alarmList`; upstream splits the first and last into
`.sender_jobrc` and an errors file, the port keeps one file):

- **Jobs** (`updateJobStats`): when a job ends the Machine records its file
  and path, lines, port, firmware, start and end (ISO dates, the end null
  for a stopped job), elapsed time and status, and the totals - jobs,
  completed, stopped, running time. The sender's times come from the
  monotonic event-loop clock; the records convert them to wall-clock dates.
- **Maintenance** (`updateMaintenanceTasks`): every task's hours grow by the
  job's running time. Tasks are due within their range (rangeStart..End
  hours); the list shows the hours left, "Due", or "Urgent!" past the range
  (Soon: within 10 h, as upstream's preview). Upstream's four default tasks
  come with the config defaults.
- **Alarms and errors** (`updateAlarmsErrors`): each reported alarm or error
  with its code, message, line and source; numeric codes stay numbers in
  the file. Upstream's `isHomingRequiredAlarm` now applies: the homing
  prompt a board raises on connecting (Grbl's "Homing", grblHAL's ALARM:11)
  is neither recorded nor reported as an error - the status area offers
  homing instead.

Tools > Statistics shows the three: the job list (newest first) with the
totals and "Clear Job History"; the maintenance tasks with Add, Edit, Mark
Done (hours back to 0) and Delete; the alarm and error log (newest first)
with Clear. Tests cover the stores (dates, counters, due states) and a job
and an alarm on the simulator reaching the open dialog.

## Step 35 — Recent files, macro import/export

- **Recent files** (FileControl's recentfiles.ts): every file loaded from
  disk is listed (`app.recentFiles`, upstream's `{fileName, filePath,
  fileSize, timeUploaded}`), newest first, at most 8; loading one again moves
  it up. File > Recent Files reloads one; a file that has gone gives
  upstream's message and leaves the list (upstream kept it); Clear Recent
  Files empties it.
- **Macro import/export** (the Macros widget): Export writes gSender's JSON
  - `[{name, content, description (trimmed), id}]` - and Import reads it
  back: an entry whose id is already here updates that macro, others are
  added with new ids, entries without a name or content are skipped, and the
  counts are reported as upstream words them. The rules are in the core
  (`exportMacros`/`importMacros`, tested); files move between gSender and
  the port either way.
