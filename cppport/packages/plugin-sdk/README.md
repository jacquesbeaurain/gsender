# gSender Plugin SDK (C / WebAssembly)

Headers and a small freestanding runtime for writing gSender plugin logic in C
(or anything that compiles to `wasm32`). The ABI and the bridge are described
in [`PLUGINS.md`](../../PLUGINS.md).

## Directory Contents

- `include/gsender/plugin.h`: the host imports (`gs_host_request` and the shortcuts) and the exports a plugin defines.
- `include/gsender/runtime.h`, `src/runtime.c`: `malloc`/`free`, `memcpy` and friends, the `gsender_plugin_alloc`/`gsender_plugin_free` exports the host copies strings in with, a growable string (`gs_str_*`), `gs_respond`, and tiny JSON field readers (`gs_json_number`, `gs_json_bool`, `gs_json_string`).
- `include/gsender/capabilities.h`: request type and topic names.
- `include/gsender/types.h`: shared structures (DRO coordinates, overlay markers).
- `manifest.schema.json`: JSON schema for `gsender-plugin.json`, parsers included.

## Building a Plugin (C / clang)

```bash
clang --target=wasm32 -O2 -ffreestanding -nostdlib \
    -I packages/plugin-sdk/include \
    -Wl,--no-entry -Wl,--strip-debug \
    -o bin/plugin.wasm src/main.c packages/plugin-sdk/src/runtime.c
```

`tools/build_plugins.sh` does this for every plugin under `plugins/`. The
interpreter supports WebAssembly 2.0 and tail calls, not SIMD or threads, so
leave `-msimd128` and `-pthread` off.

Place `bin/plugin.wasm` beside `gsender-plugin.json` in a folder of gSender's
plugins directory.
