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
