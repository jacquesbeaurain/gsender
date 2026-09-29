# gSender Plugin SDK (C / WebAssembly)

The native & WebAssembly Plugin SDK for gSender. Provides headers and interface contracts for building sandboxed, high-performance CNC plugins that run within gSender's native C++ runtime.

## Directory Contents

- `include/gsender/plugin.h`: Main host interface definitions (imports & exports).
- `include/gsender/capabilities.h`: Standard capability strings (`machine:command`, `storage:get`, etc.).
- `include/gsender/types.h`: Shared data structures (DRO, status, 3D overlay markers).
- `manifest.schema.json`: JSON schema for `gsender-plugin.json`.

## Quick Start (C / Rust / Zig)

To compile a plugin to Wasm using Clang or Rust:

### C / Clang
```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export-all -O3 -Iinclude -o plugin.wasm src/main.c
```

### Rust
```bash
cargo build --target wasm32-unknown-unknown --release
```

Place the resulting `plugin.wasm` alongside `gsender-plugin.json` and install into gSender's plugins directory.
