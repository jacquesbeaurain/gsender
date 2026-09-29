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
* Wasm enforces **capability-based security**. Plugins have zero access to the filesystem, network, or OS APIs by default. The host exposes only explicit, fine-grained capabilities approved by the user (e.g. read DRO coordinates, emit G-code commands, read/write isolated plugin storage).

#### 6. Separation of Concerns: Wasm for Logic, QML for UI
* **Logic & Computation**: Handled in Wasm (e.g. pocketing math, heightmap triangulation, tool changer state machines).
* **User Interface**: Defined declaratively via QML components or structured UI schemas hosted in gSender's native hardware-accelerated rendering pipeline.

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

The bridge follows a **zero-trust capability model**: all requests from a plugin are rejected unless explicitly declared in the manifest's `capabilities.requestTypes` and `capabilities.topics`.

### Request Types (`requestTypes`)

| Capability | Category | Description |
| :--- | :--- | :--- |
| `machine:get:context` | Machine | Query current controller state, modal groups, and firmware type |
| `machine:command` | Machine | Send G-code or real-time commands to the CNC controller |
| `machine:query` | Machine | Execute synchronous query and await response line |
| `machine:busy:set` | Machine | Mark the application as busy (disabling conflicting user actions) |
| `machine:parser:register` | Machine | Register custom runtime firmware response parsers |
| `gcode:load:to:visualizer`| G-code | Feed generated G-code program directly into visualizer & job pipeline |
| `workspace:get:state` | Workspace | Query workspace units, active WCS (G54-G59), tool offset, DRO |
| `storage:get` | Storage | Read a persistent key from the plugin's isolated store |
| `storage:set` | Storage | Write a persistent key to the plugin's isolated store |
| `storage:delete` | Storage | Remove a key from the plugin's isolated store |
| `storage:get:all` | Storage | Retrieve all key/value pairs in the plugin's slice |
| `storage:set:all` | Storage | Bulk write key/value pairs |
| `storage:clear` | Storage | Clear all stored keys for this plugin |
| `viewer:screen-to-world` | Visualizer| Project 2D viewport coordinates to 3D world coordinates |
| `viewer:world-to-screen` | Visualizer| Project 3D world coordinates to 2D viewport coordinates |
| `viewer:camera:set` | Visualizer| Set camera perspective (`top`, `front`, `left`, `right`, `3d`) |
| `viewer:overlay:set` | Visualizer| Submit declarative 3D markers (points, rings, crosses, labels) |

### Event Topics (`topics`)

Plugins can subscribe to reactive push streams from the host:

| Topic | Description |
| :--- | :--- |
| `workspace` | Pushed on WCS change, units change (metric/inch), tool load, or zeroing |
| `controller` | Real-time stream of machine status, DRO coordinates, feed/speed overrides |
| `viewer` | Viewport interaction events (3D click picking, drag gestures, focal target) |
| `parser` | Stream of raw serial lines matching the plugin's registered parser patterns |

---

## 5. Per-Plugin Isolated Storage

Plugins cannot access global machine settings, network profiles, or other plugins' configuration data.
* Every plugin receives a namespaced persistent store keyed by its unique `id` (e.g. `com.sienci.corner-finder`).
* Storage is managed through [`src/app/plugin_storage.hpp`](file:///d:/repos/gh/gsender/cppport/src/app/plugin_storage.hpp) backed by JSON persistence in the user's application configuration directory:
  - Windows: `%APPDATA%\gsender\plugins-data\<plugin-id>.json`
  - Linux: `~/.config/gsender/plugins-data/<plugin-id>.json`
  - macOS: `~/Library/Application Support/gsender/plugins-data/<plugin-id>.json`

---

## 6. Implementation Progress & Roadmap

| Feature Component | Status | Location / Tracking |
| :--- | :---: | :--- |
| **Plugin Architecture Specification** |  Complete | [`PLUGINS.md`](file:///d:/repos/gh/gsender/cppport/PLUGINS.md) |
| **Plugin SDK & C/Wasm Headers** |  Complete | [`packages/plugin-sdk/`](file:///d:/repos/gh/gsender/cppport/packages/plugin-sdk) |
| **Example Plugins Layout** |  Complete | [`plugins/`](file:///d:/repos/gh/gsender/cppport/plugins) |
| **Manifest Parsing & Validation** |  Complete | [`src/app/plugin_manifest.hpp`](file:///d:/repos/gh/gsender/cppport/src/app/plugin_manifest.hpp) |
| **Per-Plugin Isolated Storage** |  Complete | [`src/app/plugin_storage.hpp`](file:///d:/repos/gh/gsender/cppport/src/app/plugin_storage.hpp) |
| **Capability-Enforced Bridge** |  Complete | [`src/app/plugin_bridge.hpp`](file:///d:/repos/gh/gsender/cppport/src/app/plugin_bridge.hpp) |
| **Plugin Discovery & Lifecycle Service** |  Complete | [`src/app/plugin_service.hpp`](file:///d:/repos/gh/gsender/cppport/src/app/plugin_service.hpp) |
| **Machine Integration** |  Complete | [`src/app/machine.hpp`](file:///d:/repos/gh/gsender/cppport/src/app/machine.hpp), [`src/app/machine.cpp`](file:///d:/repos/gh/gsender/cppport/src/app/machine.cpp) |
| **QML Plugins UI & Tools Integration** |  Complete | [`src/ui/plugins_model.hpp`](file:///d:/repos/gh/gsender/cppport/src/ui/plugins_model.hpp), [`src/ui/qml/PluginsTool.qml`](file:///d:/repos/gh/gsender/cppport/src/ui/qml/PluginsTool.qml) |
| **Wasm Runtime Integration** |  Next Milestone | Embedded Wasm execution engine (WAMR / wasmtime) |
