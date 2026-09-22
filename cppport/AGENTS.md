# AGENTS.md — working on the gSender C++ port

Everything under `cppport/` is a native C++20 / Qt 6 port of gSender. The
JavaScript application in the parent repository (`../src`) is the reference
implementation: when behaviour is in doubt, read the JS source it was ported
from (each C++ file names its origin in its header comment).

`DEV_WALKTHROUGH.md` records the design, step by step. This file holds the
practical knowledge needed to build, test and extend the port.

## Build and test

```powershell
./tools/build.ps1 -Config debug -Test          # configure (first run), build, run all tests
./tools/build.ps1 -Config debug -Test -TestRegex Sender
./tools/build.ps1 -Config release -Target gs_core_tests
./tools/build.ps1 -Config debug -Reconfigure   # after adding source files / changing CMake
```

- The script enters the Visual Studio developer shell itself; it works from a
  plain PowerShell. Presets: `ninja-debug`, `ninja-release` (need the dev
  shell) and `vs-debug`, `vs-release` (Visual Studio 18 2026 generator).
- Build trees live in `build/<preset>/`; binaries in `build/<preset>/bin/`.
- LibPack locations come from `GS_LIBPACK_DEBUG` / `GS_LIBPACK_RELEASE`
  (defaults in `tools/build.ps1`). **Debug builds must use the Debug LibPack**
  (debug CRT, `Qt6*d.dll`); `cmake/GsLibPack.cmake` warns on a mismatch.
- Tests use GoogleTest from the LibPack, discovered with
  `DISCOVERY_MODE PRE_TEST` so discovery runs after the runtime DLLs are
  copied next to the test executable (`gs_copy_runtime_dlls`).

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
- Tool-call environment note: in the Bash tool, avoid `cd` into
  subdirectories (it changes the session's working directory); use absolute
  paths instead.
- The repository normalizes to LF (`.gitattributes`). Scripted edits must not
  write CRLF: in Python use `open(path, 'w', newline='\n')` (or
  `write_bytes`), not `Path.write_text` on Windows.
