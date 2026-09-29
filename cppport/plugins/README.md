# gSender Plugins (C++ / Wasm Examples)

This directory contains example plugins demonstrating WebAssembly logic modules and native QML user interface contributions for gSender.

## Example Plugins

| Directory | Type | What it Demonstrates |
| :--- | :--- | :--- |
| `example-hello/` | Wasm + QML | Minimal plugin showing capability requests, DRO queries, and Tools Page UI contribution. |
| `storage-test/` | QML + Storage Bridge | Exercising per-plugin isolated persistent storage (`storage:get`, `storage:set`, `storage:clear`). |
| `corner-finder/` | Wasm + Overlay | Visualizer 3D overlay markers, probing motion generator, and coordinate calculation. |

## Plugin Structure

```
plugin-folder/
├── gsender-plugin.json      # Plugin manifest, capabilities, and contribution slots
├── bin/
│   └── plugin.wasm          # Compiled WebAssembly binary module
└── ui/
    └── Main.qml             # QML user interface mounted into host slots
```
