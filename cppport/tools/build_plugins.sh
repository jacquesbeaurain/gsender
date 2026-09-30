#!/usr/bin/env bash
# Builds each example plugin's bin/plugin.wasm from its src/*.c and the SDK
# runtime, with clang's wasm32 target and no libc. Pass plugin folder names
# to build only those.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk="$root/packages/plugin-sdk"
cd "$root/plugins"
plugins=("$@")
if ((${#plugins[@]} == 0)); then
    for dir in */; do
        [[ -d "$dir/src" ]] && plugins+=("${dir%/}")
    done
fi
for plugin in "${plugins[@]}"; do
    mkdir -p "$plugin/bin"
    clang --target=wasm32 -O2 -ffreestanding -nostdlib -Wall -Wextra -Werror \
        -I"$sdk/include" -Wl,--no-entry -Wl,--strip-debug \
        -o "$plugin/bin/plugin.wasm" "$plugin"/src/*.c "$sdk/src/runtime.c"
    echo "$plugin: $(wc -c < "$plugin/bin/plugin.wasm") bytes"
done
