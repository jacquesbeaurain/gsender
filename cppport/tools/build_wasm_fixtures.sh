#!/usr/bin/env bash
# Rebuilds the Wasm interpreter's test modules (tests/data/wasm) from
# engine_test.c with clang's wasm32 target: unoptimized, optimized, and
# optimized with the post-MVP features toolchains emit (bulk memory,
# saturating conversions, sign extension, multi-value, reference types,
# tail calls). SIMD is not supported by the interpreter.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../tests/data/wasm"
features="-mbulk-memory -mnontrapping-fptoint -msign-ext -mmutable-globals -mmultivalue -mreference-types -mtail-call"
build() {
    clang --target=wasm32 -nostdlib -Wl,--no-entry "$@" engine_test.c
}
build -O0 -o engine_test_O0.wasm
build -O2 -o engine_test_O2.wasm
# shellcheck disable=SC2086
build -O2 $features -o engine_test_edge.wasm
