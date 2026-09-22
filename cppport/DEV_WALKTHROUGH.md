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
