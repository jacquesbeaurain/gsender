# gSender Plugins (C++ / Wasm Examples)

This directory contains reference plugins demonstrating WebAssembly logic modules and native QML user interface contributions for gSender.

## Example Plugins Suite

| Directory | Type | What it Demonstrates |
| :--- | :--- | :--- |
| `example-hello/` | Wasm + QML | Minimal plugin showing capability requests, DRO queries, and Tools Page UI contribution. |
| `storage-test/` | QML + Storage Bridge | Exercising per-plugin isolated persistent storage (`storage:get`, `storage:set`, `storage:clear`). |
| `controller-events-demo/` | Wasm + QML Telemetry | Subscribes to `workspace` and `controller` topics for live DRO and state streaming. |
| `basic-cam/` | Wasm + CAM Toolpath | Generates 2D facing toolpaths and streams G-code via `gcode:load:to:visualizer`. |
| `corner-finder/` | QML Overlay + Wasm | Interactive 3D visualizer overlay (`visualizer-overlay` slot) and camera viewport controls. |
| `parser-demo/` | Regex Parsers + QML | Declares line/block firmware response parsers in manifest (`parsers`) to intercept GRBL replies. |

## Plugin Structure

```
plugin-folder/
├── gsender-plugin.json      # Plugin manifest, capabilities, and contribution slots
├── src/
│   └── main.c               # C implementation using gSender Plugin C SDK
├── bin/
│   └── plugin.wasm          # Compiled WebAssembly binary module
├── ui/
│   └── Main.qml             # QML user interface mounted into host slots
└── README.md                # Plugin overview and documentation
```
