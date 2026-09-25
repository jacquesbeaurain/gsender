# AGENTS.md — working on the gSender C++ port

Everything under `cppport/` is a native C++20 / Qt 6 port of gSender. The
JavaScript application in the parent repository (`../src`) is the reference
implementation: when behaviour is in doubt, read the JS source it was ported
from (each C++ file names its origin in its header comment).

`DEV_WALKTHROUGH.md` records the design, step by step. This file holds the
practical knowledge needed to build, test and extend the port.

## Build and test

```powershell
./tools/build.ps1 -Test                    # configure (first run), build, run all tests
./tools/build.ps1 -Filter 'Controller*'    # build, run matching tests (GoogleTest filter)
./tools/build.ps1 -Target gs_core          # compile the library only
./tools/build.ps1 -Reconfigure             # after changing presets/cache options
./tools/build.ps1 -Test -Full              # unfiltered build and test output
./tools/build.ps1 -Config release-nopch -Test  # occasional: without precompiled headers
./tools/build.ps1 -CTest -TestRegex Sender # through CTest (slow: a process per test)
```

**Release is the only working configuration.** The script defaults to
`-Config release` (`ninja-release`, RelWithDebInfo against the Release
LibPack); every build and test run while developing uses it. Do not build or
run tests in Debug - that is reserved for a human investigating a Release
failure (`-Config debug`, Debug LibPack), and only when asked.

- The script enters the Visual Studio developer shell itself; it works from a
  plain PowerShell. The environment is captured once into
  `build/.vsdevenv.json` and replayed afterwards (`-RefreshVsEnv` redoes it;
  a VS update invalidates it automatically). Presets: `ninja-release`,
  `ninja-release-nopch`, `ninja-debug` (need the dev shell) and `vs-release`,
  `vs-debug` (Visual Studio 18 2026 generator).
- Output is brief by default: compiler/linker diagnostics, test failures and a
  `phase: ok (time)` line per step. A failure without recognisable
  diagnostics prints the whole output.
- Build trees live in `build/<preset>/`; binaries in `build/<preset>/bin/`.
- LibPack locations come from `GS_LIBPACK_DEBUG` / `GS_LIBPACK_RELEASE`
  (defaults in `tools/build.ps1`). **Debug builds must use the Debug LibPack**
  (debug CRT, `Qt6*d.dll`); `cmake/GsLibPack.cmake` warns on a mismatch.
- Tests use GoogleTest from the LibPack, discovered with
  `DISCOVERY_MODE PRE_TEST` so discovery runs after the runtime DLLs are
  copied next to the test executable (`gs_copy_runtime_dlls`). CTest starts
  one process per test case (3.6 s for 175 tests and growing); `-Test` runs
  each `*_tests.exe` once instead, all at the same time, with the
  application tests split into GoogleTest shards (`-AppShards`, default 4;
  1 runs them in one process). Output is one summary line per suite plus
  any failures.

### Linux (cloud sessions)

```bash
tools/setup_linux.sh                       # once: Qt 6.11.1, Boost 1.91, GTest into /opt/gs-deps
tools/build.sh -Test                       # same options and output as build.ps1
tools/build.sh -Filter 'Controller*'
tools/build.sh -Config release-nopch -Test
```

- The dependencies are the LibPack's versions, from conda-forge, installed
  with a micromamba that the script also fetches from conda-forge
  (`download.qt.io` and `archives.boost.io` may be blocked by the network policy;
  `conda.anaconda.org` is the source that works). `GS_DEPS_DIR` moves the
  prefix. Presets: `linux-release`, `linux-release-nopch` (GCC; build trees
  in `build/linux-release*`). There is no Debug preset on Linux.
- A clean build takes ~1 min with PCH (4 cores), ~4 min without; the tests
  ~19 s. The offscreen platform finds fonts through fontconfig (DejaVu), which
  is wider than Segoe UI: widget sizes differ from Windows, so tests must not
  assume exact pixel sizes that fonts decide.
- GCC warns where MSVC does not, and warnings are errors: e.g. `-Wshadow`
  catches a lambda parameter named `info` inside GoogleTest's
  `INSTANTIATE_TEST_SUITE_P`. Build on both platforms when you can.
- Windows-only pieces fall back on Linux: serial ports are not listed (the
  simulator and TCP work), power saving is a no-op, audio cues beep.
- Screenshots: `build/linux-release/bin/gsender -platform offscreen ...`
  with the same arguments as on Windows.

## Fast iteration

Measured on the dev machine (8 threads), release: no-op build + full test run
~0.25 s; editing a test file 0.55 s; `controller.cpp` 5.3 s (the optimizer
dominates); a widely included header ~9 s; full rebuild ~15 s. Keep it that
way:

- **Build settings.** Debug info is embedded (`/Z7`, no PDB-server
  contention between parallel compiles). Standard and third-party headers are
  precompiled per target via `gs_precompile_headers()`; never put first-party
  headers in a PCH (every TU would rebuild on each edit). Warnings are errors
  in all presets (`GS_WARNINGS_AS_ERRORS`), so a warning can't scroll by
  unnoticed in an incremental build.
- **PCH blind spot.** A PCH can hide a missing `#include`. The
  `ninja-release-nopch` preset builds without one (its own tree, ~17 s full
  build); run `./tools/build.ps1 -Config release-nopch -Test` every few
  commits and at the end of a work session.
- **Split big translation units.** An optimized compile of a 1,500-line file
  takes ~5 s. When one file starts dominating incremental builds, split it
  along its natural seams (e.g. commands / stream filters / event handling)
  so an edit recompiles only its part and the parts build in parallel.
- **Keep headers light.** Heavy headers (`boost/regex.hpp`, `boost/json.hpp`,
  `<regex>`) belong in `.cpp` files. A first-party header that many TUs include
  costs every one of them a recompile per edit.
- **Loop.** While iterating: `-Filter` for the tests at hand, `-Target gs_core`
  to check library code compiles before writing its tests. Before each
  commit: `./tools/build.ps1 -Test` (all suites, ~4.5 s).
- **Commit cadence.** Commit each coherent, tested increment: a ported
  component with the tests that pin its behaviour. Don't hold several
  components back for one big commit, and don't hold a commit back for
  documentation polish. The walkthrough entry can be short and grow with
  later commits; the deviation table must be current for what is committed.
  Large ports (e.g. a controller) can land as "core + first test batch", then
  further test batches.

### Agent workflow

Most wall-clock time goes to reading and writing text, not to compiling:

- Read upstream JS with targeted `grep -n`/`sed -n 'a,bp'` ranges instead of
  whole multi-thousand-line files. Each C++ file names its JS origin; note
  non-obvious upstream facts (and deviations) in comments where they are
  ported, so they survive context resets and need not be re-derived.
- Verify upstream semantics *before* writing a large file, then write it once.
  Draft-then-rewrite of a 1,500-line file costs minutes; later changes go
  through small `Edit`s.
- Port the upstream Jest tests together with the code; they pin down exactly
  the behaviour to match and catch misreadings early.
- Keep tool output small: the build script is already brief; pipe other
  commands through `Select-Object -First N` / `head`.
- Don't modify tracked files temporarily to probe tool behaviour (e.g.
  injecting a failing test); use a scratch copy or wait for a real case.
- Use the `Edit` tool for replacements whose text contains escape sequences
  (`\n`, `\\`); heredoc-fed Python/sed replacements of such text misfire -
  even with a quoted `<<'EOF'` delimiter the Bash tool turns `\\n` into a
  real newline inside C++ string literals. For larger scripted edits, write
  the script with the `Write` tool into the scratchpad and run it. Scripted
  edits are fine for plain text.
- On Linux run `tools/build.sh` from the Bash tool (see "Linux" above).
- Run `tools/build.ps1` through the PowerShell tool (PowerShell 7). Windows
  PowerShell 5 (`powershell.exe` from the Bash tool) refuses to run scripts
  under the default execution policy.

## Qt application notes

- `gs_app` compiles with `QT_NO_KEYWORDS`: write `Q_SIGNALS`, `Q_SLOTS`,
  `Q_EMIT`. Qt's `emit`/`signals`/`slots` macros otherwise break plain C++
  identifiers in included core headers (a method named `emit`, a parameter
  named `signals`).
- Qt needs `libpng16`, `z` and `zstd` from the LibPack beside the executable;
  `$<TARGET_RUNTIME_DLLS>` misses them (they are not imported targets).
  `gs_deploy_qt_plugins()` copies them together with the `platforms/` and
  `styles/` plugins. A test executable that exits with `0xC0000135` is missing
  a DLL: `dumpbin /dependents` (after `tools/build.ps1` has cached the VS
  environment) finds which.
- The offscreen platform needs `QT_QPA_FONTDIR` on Windows (the app and the
  app tests point it at `%WINDIR%\Fonts`), otherwise text renders as boxes.
- Check UI changes with a screenshot of the real application:
  `build/ninja-release/bin/gsender.exe -platform offscreen --config
  <scratch>/rc.json --simulator --load <file> [--start] [--view 3d]
  --screenshot out.png --wait 3000`, then look at it. Always pass `--config`
  with a scratch file: without it the run reads and writes the user's own
  `~/.gsender-cpp_rc` (recent files, settings). Edit that scratch file's
  `app` object to screenshot other settings. `--simulator-hal` connects to
  the simulator's grblHAL personality instead (SD card, YMODEM, complete
  reports) - the way to exercise grblHAL-only features without hardware.
- The Machine/app tests run in real time against the simulator; keep them
  short (wait for a condition, never for a fixed long delay) and speed the
  simulated motion up where the timing is not the point
  (`machine.simulator()->setSpeed(n)`). They are the slowest suite (~17 s
  in one process: waits for the controller's 250 ms status polls, a forced
  stop's 700 ms before its reset, job ends after 500 ms idle); `-Test` runs
  them as parallel shards, so each test must stand alone - its own
  `QTemporaryDir` config and `Machine`, no fixed paths or ports. Filter
  them out while iterating on the core (`-Filter '-AppTest.*'`), or select
  one (`-Filter 'AppTest.Probe*'`).
- Wait on what the controller has heard (`machine.machinePositionMm()`,
  `c.state()`), not on the simulator's own state, before reading anything
  the UI derives from it: the controller's view lags by up to a status poll.
- `GS_TEST_SCREENSHOTS=<dir>` makes the dialog tests save what they render,
  to look at a dialog without a display.

## FreeCAD LibPack facts (26.3.0 / 3.5.5, x64)

- Toolset is MSVC v145 (VS 2026, `cl` 14.51); Boost libraries are named
  `*-vc145-mt[-gd]-x64-1_91`.
- Qt 6.11.1 with Core, Gui, Widgets, Network, OpenGL, OpenGLWidgets, Svg,
  Test, Concurrent, Quick... **No QtSerialPort, QtWebSockets, QtHttpServer,
  QtCharts or Qt3D.** Serial I/O therefore goes through Boost.Asio.
- Boost 1.91 (Asio, Beast, JSON, Regex, ...). Boost headers are under
  `include/boost-1_91`; use the `Boost::headers` / `Boost::json` targets.
- GTest/GMock are shared libraries (`GTEST_LINKED_AS_SHARED_LIBRARY=1` comes
  with the imported target). The Debug LibPack's debug GTest DLLs have no `d`
  suffix.
- `plugins/platforms/qoffscreen.dll` is available for headless UI rendering.

## Architecture rules

- `src/core` (`gs_core`) is **Qt-free**: C++20 + Boost only. G-code parsing and
  interpretation, firmware protocol parsing, streaming, controllers and config
  live here so they can be unit tested deterministically.
- Time and I/O reach the core through interfaces (an event loop/scheduler and
  a transport), never through Qt or real sockets. Tests drive them with fakes.
- There is no client/server split (no socket.io, no HTTP): the Qt UI calls the
  controller API directly on the UI thread and receives typed events.
- The UI (`src/app`) adapts core events to Qt signals; it must not duplicate
  protocol or streaming logic.

## Porting rules and gotchas

- JavaScript number semantics matter wherever numbers become text sent to the
  machine or shown to users: use `gs::js::toFixed`, `numberToString`,
  `stringToNumber` (`Number(...)`), `parseFloat`, `parseInt`, `mathRound`.
  `printf`/`std::format` round ties to even; JS `toFixed` does not.
- JS `trim()` also strips the UTF-8 BOM and non-breaking spaces; use
  `gs::str::trim`.
- Split programs with `gs::str::splitLines` (handles `\r\n`, `\n`, lone `\r`)
  everywhere, so the sender, the visualizer and the estimator agree on line
  numbering.
- A G-code word's "flat" form follows JS concatenation: `M06` -> `M6`,
  `X1.500` -> `X1.5` (`gs::gcode::Word::flat()`).
- Deliberate behaviour differences from the JS are listed in
  `DEV_WALKTHROUGH.md`; add to that list whenever you knowingly deviate.
- Firmware regexes are ported verbatim with Boost.Regex (header-only,
  Perl/ECMAScript syntax) so matching stays identical to the JavaScript.
- C++20 range-for does not extend the lifetime of temporaries nested inside
  the range expression: `for (auto v : str::splitView(makeString(), ','))`
  dangles. Bind the string to a local first.
- Realtime commands are single raw bytes (`0x85`, `0x91`, ...). gSender wrote
  them as JS strings, which node-serialport UTF-8 encodes (`C2 91`); Grbl
  happens to discard the `C2`. The port writes the correct single byte.
- Data tables (errors, alarms, settings metadata, machine profiles) are
  generated from the JS sources by `node tools/extract_data.mjs` into
  `resources/data/*.json` and embedded into gs_core. Do not hand-edit them;
  re-run the script after upstream changes.
- Pure generators (G-code in/out: probing, surfacing, ...) get golden tests:
  a `tools/gen_<name>_fixtures.mjs` loads the upstream module with
  `tools/lib/bundle.mjs` (`loadModule(entry, [stubs.redux, stubs.store,
  stubs.controller])` - stubs read `globalThis.__reduxState` /
  `__storeValues`), sweeps a seeded matrix, and `writeCases()` writes
  `tests/data/<name>_golden.json`; the C++ test compares every line. Upstream
  Jest expectations can be stale - the running JS is the reference. Keep
  fixtures small (tens of KB) by bounding sizes in the matrix.
- Tool-call environment note: in the Bash tool, avoid `cd` into
  subdirectories (it changes the session's working directory); use absolute
  paths instead.
- The repository normalizes to LF (`.gitattributes`). Scripted edits must not
  write CRLF: in Python use `open(path, 'w', newline='\n')` (or
  `write_bytes`), not `Path.write_text` on Windows.
