# gSender Plugin Architecture (WebAssembly & QML)

This document details the architecture, design rationale, capability model, and implementation roadmap for the extensible plugin subsystem in the gSender C++ port (`cppport`).

---

## 1. Executive Summary & Design Rationale

As part of modernizing gSender from an Electron/Node.js application to a high-performance C++20 and Qt QML native system, third-party extensibility must be preserved and elevated. Community makers, machinists, and industrial users rely on extensions for custom probing routines, automatic tool changers (ATC), specialized CAM generators, vision/camera alignment, rotary calculations, and shop-floor automation.

In native C++, exposing an extension point via traditional dynamic libraries (`.dll`, `.so`, `.dylib`) introduces severe stability, security, and developer friction hazards. To solve this, gSender adopts a **sandboxed WebAssembly (Wasm) + QML plugin architecture**.

### Why WebAssembly (Wasm) for a CNC Machine Controller?

#### 1. Physical Machine Safety & Crash Isolation
In standard desktop software, an unhandled crash or segmentation fault is a minor user annoyance. In a CNC machine controller, **a crash is a catastrophic physical hazard**:
* A machine running a 24,000 RPM spindle cutting at 3,000 mm/min that suddenly terminates loses serial/USB heartbeat communication. The controller board may enter an unmanaged state, ruin raw stock, break expensive carbide tooling, or cause mechanical collisions against gantry limits.
* **Wasm executes in a strictly bounds-checked, linear memory sandbox**. If a plugin encounters an out-of-bounds memory access, integer division by zero, or panics, the WebAssembly runtime traps the fault cleanly. The host application (`gsender`) intercepts the trap, reports the error to the user interface, and safely maintains machine communication and motion safety.

#### 2. Eliminating C++ ABI Hell & Compiler Lock-In
C++ has no stable Application Binary Interface (ABI):
* Dynamic libraries compiled with different MSVC runtime versions, GCC versions, or Clang flags cannot be safely linked or loaded dynamically.
* Loading native Qt plugins requires that the third-party binary be compiled against the exact same Qt version and configuration as the host. Upgrading Qt in gSender would break every third-party plugin in the ecosystem.
* **Wasm is a standardized binary format (W3C Standard)**. A `.wasm` module compiled today runs reliably across future versions of gSender without recompilation or toolchain lock-in.

#### 3. Single-Binary Cross-Platform Distribution
gSender runs on multiple operating systems and architectures:
* Windows (x86_64, ARM64)
* macOS (Apple Silicon arm64, Intel x86_64)
* Linux & Single-Board Computers (x86_64, aarch64, Raspberry Pi armhf)

With native shared libraries, every plugin author must configure complex cross-compilation CI pipelines to build, test, and sign 5+ platform binaries. With Wasm, plugin authors compile **a single `.wasm` binary file** that runs natively and deterministically across all platforms.

#### 4. Language Agnosticism for the Maker Community
The CNC community comprises woodworkers, makers, and engineers using varied languages. Wasm does not mandate C++:
* Authors can write plugins in **Rust**, **AssemblyScript / TypeScript**, **Zig**, **C/C++**, **TinyGo**, or **Python** (via MicroPython/Pyodide).
* Any language that compiles to WebAssembly can seamlessly integrate with gSender.

#### 5. Capability-Based Security (Least Privilege)
CNC operators routinely download macros and community plugins from Discord, GitHub, or maker forums:
* Native C++ or Python plugins have unrestricted operating system access: they can access user files, execute arbitrary shell commands, or open rogue sockets.
* Wasm enforces **capability-based security**. Plugin *logic* has zero access to the filesystem, network, or OS APIs by default. The host exposes only the capabilities the manifest declares (e.g. read DRO coordinates, emit G-code commands, read/write isolated plugin storage).
* This covers the Wasm half only. A plugin's QML UI runs in the application's own QML engine and is **trusted code**; see [section 9](#9-known-risks-plugin-qml-is-trusted).

#### 6. Separation of Concerns: Wasm for Logic, QML for UI
* **Logic & Computation**: Handled in Wasm (e.g. pocketing math, heightmap triangulation, tool changer state machines).
* **User Interface**: QML components hosted in gSender's own QML engine (trusted, with full access; see section 9).

---

## 2. Directory Layout Alignment

To mirror upstream gSender's repository structure and ensure developer familiarity, the C++ port mirrors upstream's layout:

```
gsender/
├── plugins/                        # Upstream Electron example plugins
├── packages/
│   └── plugin-sdk/                 # Upstream TypeScript Plugin SDK
└── cppport/
    ├── PLUGINS.md                  # This architecture specification & guide
    ├── plugins/                    # C++ / Wasm / QML example plugins
    │   ├── basic-cam/              # Reference CAM generator plugin
    │   ├── controller-events-demo/ # Controller state event listener demo
    │   ├── corner-finder/          # Probing & visualizer overlay demo
    │   ├── example-hello/          # Minimal hello-world plugin
    │   ├── parser-demo/            # Custom firmware parser extension
    │   └── storage-test/           # Isolated storage validation plugin
    ├── packages/
    │   └── plugin-sdk/             # Native & Wasm C/C++ Plugin SDK
    │       ├── include/
    │       │   └── gsender/
    │       │       ├── plugin.h    # Core C API / Wasm host imports & exports
    │       │       ├── types.h     # Shared POD structures (DRO, status, events)
    │       │       └── bridge.h    # Bridge message definitions
    │       └── manifest.schema.json# JSON schema for gsender-plugin.json
    └── src/
        └── app/
            ├── plugin_manifest.hpp # Plugin manifest parser & validator
            ├── plugin_manifest.cpp
            ├── plugin_storage.hpp  # Namespaced per-plugin key/value storage
            ├── plugin_storage.cpp
            ├── plugin_bridge.hpp   # Capability-enforced message dispatcher
            ├── plugin_bridge.cpp
            ├── plugin_service.hpp  # Plugin lifecycle & directory discovery
            └── plugin_service.cpp
```

---

## 3. Plugin Manifest Specification (`gsender-plugin.json`)

Every plugin directory must provide a valid `gsender-plugin.json` manifest at its root:

```json
{
  "id": "com.sienci.example-hello",
  "name": "Example Hello",
  "version": "1.0.0",
  "description": "Demonstrates gSender Wasm + QML plugin integration",
  "author": "Sienci Labs",
  "engine": ">=1.5.0",
  "wasm": {
    "entry": "bin/plugin.wasm"
  },
  "ui": {
    "entry": "qml/Main.qml",
    "contributions": [
      {
        "slot": "tools-page",
        "label": "Hello Plugin",
        "icon": "hand"
      }
    ]
  },
  "capabilities": {
    "requestTypes": [
      "machine:get:context",
      "machine:command",
      "storage:get",
      "storage:set"
    ],
    "topics": [
      "workspace",
      "controller"
    ]
  },
  "parsers": []
}
```

### Response Parsers (`parsers`)

A plugin can declare regex parsers that gSender runs against every line the
board sends (the port of upstream's `server/lib/plugin-parsers`). Matches go to
that plugin alone, on the `parser` topic; the chain only observes and never
alters or consumes a line.

```json
"parsers": [
  { "id": "probe", "mode": "line",
    "match": "^\\[PRB:(?<x>[-\\d.]+),(?<y>[-\\d.]+),(?<z>[-\\d.]+):(?<ok>[01])\\]" },
  { "id": "settings", "mode": "block", "begin": "^\\$0=",
    "match": "^\\$(?<key>\\d+)=(?<value>.*)$", "until": "ok", "whenWorkflow": "idle" }
]
```

* `mode: "line"` needs `match`; each matching line is one event (`line`, `groups` by name, `captures` by number).
* `mode: "block"` needs `begin` and either `end` or `until` (`ok`, `error`, `ok-or-error`); the event carries the block's `lines`, the `entries` that matched `match`, `complete` and `reason` (`end`, `until`, `maxLines`, `timeout`, `restart`, `strict`, `close`).
* Patterns are strings or `{ "source", "flags" }` (JavaScript flags `i`, `m`, `s`, `u`; `g`/`y` are ignored). Patterns prone to catastrophic backtracking (nested quantifiers, overlapping alternations under a quantifier, counts above 1000) are rejected.
* Options: `whenWorkflow` (`any`, or `idle` to sit out running jobs), `maxLines` (64), `timeout` ms (2000), `ignore`, `ignoreStatusReports` (true), `strict`, `restartOnBegin`, `emitPartial` (true), `label`.
* Limits: 16 parsers per plugin (manifest and runtime together), 20 events per parser per second (then one `rate-limited` error event), and a parser whose matching is slow is quarantined. Rejected specs and these errors arrive on `parser` with `"error": true` and a `reason` (`invalid-spec`, `rate-limited`, `quarantined`).
* Runtime parsers (`machine:parser:register`) are dropped when the connection closes; manifest parsers stay while the plugin is enabled.

### Contribution Slots

Plugins declare where their UI or functionality hooks into the main application:

| Slot | Description | UI Mount Target |
| :--- | :--- | :--- |
| `tools-page` | Adds a dedicated card or full tool panel | **Tools Page** grid / sub-page |
| `tools-tab` | Embeds a sub-tab into the Carve screen | **Carve Page** tool widget |
| `visualizer-overlay` | Renders 3D overlay markers & picking | **3D Visualizer** viewport |
| `settings-section` | Extends application configuration | **Config Page** plugin category |
| `standalone` | Background headless service / parser | Headless service (no UI card) |

---

## 4. Host Bridge Capabilities & Security Model

Every bridge request is rejected unless the manifest's `capabilities.requestTypes` lists it, and broadcast topics reach only plugins whose `capabilities.topics` list them. These checks bind a plugin's Wasm code; its QML is trusted (section 9).

### Request Types (`requestTypes`)

| Capability | Payload | Result |
| :--- | :--- | :--- |
| `machine:get:context` | none | `connected`, `activeState`, `isGrblHal` |
| `machine:command` | `command` | Sends G-code to the controller |
| `machine:query` | `cmd`, `opts` {`until`, `maxLines`, `timeout`, `includeStatusReports`, `allowDuringJob`} | `queryId` now; the reply's `lines`, `ok`, `error`, `complete`, `reason` later on `query` with that `queryId`. One query at a time, only while no job runs unless `allowDuringJob` (which needs an `until` pattern). |
| `machine:parser:register` | `spec` (one parser) or `specs` | `registered`, `errors`, `warnings` |
| `machine:parser:unregister` | `id` (none: all runtime parsers) | |
| `machine:busy:set` | `busy`, `label` | Holds the status pill at "running" (or `label`) through a line-by-line operation. The host releases it once the machine has moved and then stayed idle 1.5 s, after 15 s without motion, after 15 minutes, or on disconnect. |
| `workspace:get:state` | none | `units`, `wpos`, `mpos` |
| `gcode:load:to:visualizer`| `gcode`, `name` | Loads the program as the current job |
| `storage:get` / `set` / `delete` | `key`, `value` | `found`, `value` |
| `storage:get:all` / `set:all` / `clear` | `entries` for `set:all` | The plugin's keys and values |
| `viewer:screen-to-world` | `px`, `py` (visualizer pixels) | `x`, `y`, `z` on the work plane, or `{}` when it is edge-on |
| `viewer:world-to-screen` | `x`, `y`, `z` | `x`, `y` |
| `viewer:camera:set` | `view`: `top`, `front`, `left`, `right`, `3d` | |
| `viewer:camera:lock-rotate` | `locked` | Stops orbiting while locked |
| `viewer:pick:arm` | `mode`: `click` or `hold` (500 ms) | Picks arrive on `viewer`; needs a connected, idle machine and a non-rotary file |
| `viewer:pick:disarm` | none | |
| `viewer:overlay:set` | `markers`: [{`id`, `x`, `y`, `z`, `shape` (`circle`, `cross`, `ring`), `color`, `size`, `label`}] | Replaces this plugin's markers (at most 256) |

Upstream's `redux:get:state` has no counterpart: the C++ port has no Redux store.

### Event Topics (`topics`)

| Topic | Grant | Description |
| :--- | :--- | :--- |
| `workspace` | yes | Work position on each status change |
| `controller` | yes | The controller's `activeState` on each status change |
| `viewer` | yes | `{kind: "pick", world, screen}` and `{kind: "hold-progress", t}` |
| `parser` | no | This plugin's parser matches and parser errors only |
| `query` | no | Replies to this plugin's `machine:query` only |

---

## 5. Per-Plugin Isolated Storage

Plugins cannot access global machine settings, network profiles, or other plugins' configuration data.
* Every plugin receives a namespaced persistent store keyed by its unique `id` (e.g. `com.sienci.corner-finder`).
* Storage is managed through [`src/app/plugin_storage.hpp`](src/app/plugin_storage.hpp) backed by JSON persistence in the user's application configuration directory:
  - Windows: `%APPDATA%\gsender\plugins-data\<plugin-id>.json`
  - Linux: `~/.config/gsender/plugins-data/<plugin-id>.json`
  - macOS: `~/Library/Application Support/gsender/plugins-data/<plugin-id>.json`

---

## 6. Implementation Progress & Roadmap

| Feature Milestone | Status | Description & Location |
| :--- | :---: | :--- |
| **Plugin Architecture Spec & SDK Headers** |  Complete | [`PLUGINS.md`](PLUGINS.md), [`packages/plugin-sdk/include/gsender/`](packages/plugin-sdk/include/gsender/) |
| **Core Architecture & Manifest Parsing** |  Complete | [`plugin_manifest.hpp`](src/app/plugin_manifest.hpp), [`plugin_storage.hpp`](src/app/plugin_storage.hpp), [`plugin_bridge.hpp`](src/app/plugin_bridge.hpp), [`plugin_service.hpp`](src/app/plugin_service.hpp) |
| **QML Management Tool & Card Grid** |  Complete | [`PluginsModel`](src/ui/plugins_model.hpp), [`PluginsTool.qml`](src/ui/qml/PluginsTool.qml), [`ToolsPage.qml`](src/ui/qml/ToolsPage.qml) |
| **Milestone 1: Dynamic Slot Hosting & Context** |  Complete | [`PluginQmlContext`](src/ui/plugin_qml_context.hpp), [`PluginHost.qml`](src/ui/qml/PluginHost.qml), dynamic slot injection in Tools Hub, Carve Tab, and 3D Visualizer |
| **Milestone 2: Mirrored Upstream Plugin Suite** |  Complete | 6 reference plugins in [`plugins/`](plugins/): `example-hello`, `storage-test`, `controller-events-demo`, `basic-cam`, `corner-finder`, `parser-demo` |
| **Milestone 3: Embedded Wasm Sandbox Runtime** |  Complete | [`WasmEngine`](src/app/wasm_engine.hpp), [`PluginWasmHost`](src/app/plugin_wasm_host.hpp), linear memory isolation, C SDK import bindings, trap isolation |

---

## 7. Embedded WebAssembly Sandbox Engine

The interpreter ([`src/app/wasm_engine.hpp`](src/app/wasm_engine.hpp)) runs what clang, Rust or Zig produce for `wasm32`: WebAssembly 2.0 (multi-value, bulk memory, sign extension, saturating conversions, reference types) plus tail calls. SIMD and threads are not supported; a module using them is rejected at load.

* **Bounded**: every call has a fuel budget (100 million instructions per plugin call), a call-depth limit, and memory is capped (64 MiB for plugins). A runaway loop traps with `OutOfFuel` instead of freezing the UI.
* **Isolated**: every load and store is bounds checked; traps (unreachable, division by zero, bad indirect calls, out-of-bounds access) stop the call and are reported, never crash the host.
* **Validated input**: malformed or truncated modules are rejected at load.

### Plugin ABI

The SDK ([`packages/plugin-sdk`](packages/plugin-sdk)) defines it; `tools/build_plugins.sh` builds every example with clang.

* Exports: `gsender_plugin_init`, `gsender_plugin_shutdown`, `gsender_plugin_handle_request(request, response, size)`, `gsender_plugin_on_topic_event(topic, json)`, and the allocator the host copies strings in with, `gsender_plugin_alloc` / `gsender_plugin_free` (the SDK runtime provides both).
* Imports from `env`: `gs_host_request(type, payload_json, out, size)` for any bridge request (writes `{"ok", "result", "error"}`; a negative return is the size it needs), and the shortcuts `gs_host_emit_gcode`, `gs_host_get_wpos`, `gs_host_storage_get` / `set` / `delete`, `gs_host_load_gcode`, `gs_host_log`.
* Topic events that a plugin's own call causes are queued and delivered after the call returns (the interpreter is not re-entrant).

---

## 8. Dynamic Slot Hosting & QML Context Injection

Plugin QML user interfaces are mounted dynamically via [`PluginHost.qml`](src/ui/qml/PluginHost.qml) and receive a `gsender` bridge object ([`PluginQmlContext`](src/ui/plugin_qml_context.hpp)) through their `property var gsender`:

* **`gsender.send(type, payload, callback)`**: Dispatches permission-checked RPC commands to the host.
* **`gsender.subscribe(topic, callback)`**: Subscribes to granted topics, and to `parser` / `query` for this plugin's own events.
* **`gsender.storageGet(key)`, `storageSet(key, value)`**: Accesses per-plugin persistent storage.
* **Dynamic Slot Injection**:
  * **`tools-page`**: Appears automatically as an interactive card on the Tools Hub and opens into a dedicated tool page with full navigation.
  * **`tools-tab`**: Added dynamically to the tab bar in the Carve screen.
  * **`visualizer-overlay`**: Rendered directly over the 3D viewport without interfering with toolpath rendering.

---

## 9. Known Risks: Plugin QML Is Trusted

Decision (2026-09-30): plugins get full capabilities for now; their UIs are not sandboxed. This section records what that means, for review before gSender accepts community plugins.

Tested: QML cannot be isolated inside the application's process. Even in a separate `QQmlEngine` with a URL interceptor and blocked imports:

* **Application objects are reachable.** `import GSender` resolves the app's C++ types, so a plugin can use the `Backend` singleton, create models such as `JogModel`, and move the machine or change settings without any capability check.
* **Interceptors are bypassed.** `Qt.createQmlObject` compiles QML from a string and skips the URL interceptor, so blocking imports by URL does not hold.
* **The item tree is open.** A plugin item can walk `parent` into the host's screens, read their properties and call their functions.
* **The OS is reachable.** `Qt.openUrlExternally` launches files and URLs; `XMLHttpRequest` reads local files and the network.
* **Crashes and hangs are shared.** A QML binding loop or a heavy JavaScript loop freezes the UI; a crash takes gSender down.

Consequences today:

* The capability checks in section 4 bind Wasm code only. A plugin's QML can do anything the app can.
* The `capabilities` list is a statement of intent, not a guarantee; installing a plugin with a UI means trusting its author.
* Plugin install has no signature or review step yet.

Options when this is revisited:

1. **Declarative UI**: plugins describe forms as JSON from Wasm and gSender draws them from its own components. A real sandbox; plugin UIs limited to standard controls.
2. **Hardened QML**: an offscreen engine with type and URL blocking. Flexible, but defense in depth rather than a sandbox.
3. **QML for bundled plugins only**: community plugins run Wasm logic with no custom UI (or with option 1).
4. **Out of process**: plugin UIs in a separate process rendered into the app. Isolates crashes and objects, at a large cost in complexity.

Until then: show the manifest's capabilities and author before enabling a plugin, and install only plugins from trusted sources.
