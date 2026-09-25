#!/usr/bin/env bash
# Configures, builds and optionally tests the C++ port on Linux - build.ps1's
# counterpart, with the same options and the same brief output.
#
# Dependencies come from GS_DEPS_DIR (default /opt/gs-deps; tools/setup_linux.sh
# installs them). Tests run each *_tests executable directly and all at once,
# the application tests split into GoogleTest shards.
#
#   tools/build.sh -Test                        # release build, run every test
#   tools/build.sh -Filter 'Controller*'        # release build, run matching tests
#   tools/build.sh -Target gs_core              # compile the library only
#   tools/build.sh -Config release-nopch -Test  # without precompiled headers
#   tools/build.sh -CTest -TestRegex Sender     # through CTest
#
# Options: -Config release|release-nopch, -Target <t> (repeatable), -Test,
# -Filter <gtest filter>, -AppShards <n>, -CTest, -TestRegex <re>, -Full,
# -Reconfigure, -Clean.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export GS_DEPS_DIR="${GS_DEPS_DIR:-/opt/gs-deps}"
# Qt wants a UTF-8 locale and says so, once per process, under the "C" one.
[[ "$(locale charmap 2>/dev/null)" == UTF-8 ]] || export LC_ALL=C.UTF-8

config=release
targets=()
test=0
filter=
app_shards=4
ctest=0
test_regex=
full=0
reconfigure=0
clean=0
while (($#)); do
    case "$1" in
        -Config) config="$2"; shift ;;
        -Target) targets+=("$2"); shift ;;
        -Test) test=1 ;;
        -Filter) filter="$2"; test=1; shift ;;
        -AppShards) app_shards="$2"; shift ;;
        -CTest) ctest=1 ;;
        -TestRegex) test_regex="$2"; ctest=1; shift ;;
        -Full) full=1 ;;
        -Reconfigure) reconfigure=1 ;;
        -Clean) clean=1 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
case "$config" in
    release|release-nopch) ;;
    *) echo "-Config: release or release-nopch" >&2; exit 2 ;;
esac

if [[ ! -f "$GS_DEPS_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    echo "No dependencies in $GS_DEPS_DIR: run tools/setup_linux.sh first" >&2
    exit 1
fi

preset="linux-$config"
build_dir="$root/build/$preset"
diagnostics='(error|warning):|^FAILED:|CMake (Error|Warning)|ninja: build stopped'

# Runs a command. Brief mode prints only lines matching the pattern (all
# output when the command fails without any); -Full passes everything.
step() {
    local name="$1" pattern="$2"
    shift 2
    local start=$SECONDS code output
    if ((full)); then
        "$@"
        code=$?
    else
        output="$("$@" 2>&1)"
        code=$?
        if grep -qE "$pattern" <<<"$output"; then
            grep -E "$pattern" <<<"$output"
        elif ((code)); then
            printf '%s\n' "$output"
        fi
    fi
    if ((code)); then
        echo "$name: FAILED ($code) ($((SECONDS - start))s)"
        exit "$code"
    fi
    echo "$name: ok ($((SECONDS - start))s)"
}

cd "$root"
((clean)) && rm -rf "$build_dir"
if ((reconfigure)) || [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    step configure 'CMake (Error|Warning)' cmake --preset "$preset"
fi

build_args=(--build --preset "$preset")
((${#targets[@]})) && build_args+=(--target "${targets[@]}")
step build "$diagnostics" cmake "${build_args[@]}"

if ((test)); then
    gtest_args=(--gtest_brief=1)
    [[ -n "$filter" ]] && gtest_args+=("--gtest_filter=$filter")
    start=$SECONDS
    logs="$(mktemp -d)"
    trap 'rm -rf "$logs"' EXIT
    declare -A shards_of=()
    pids=()
    for exe in "$build_dir"/bin/*_tests; do
        name="$(basename "$exe")"
        shards=1
        [[ "$name" == gs_app_tests ]] && shards=$((app_shards > 1 ? app_shards : 1))
        shards_of[$name]=$shards
        for ((i = 0; i < shards; i++)); do
            if ((shards > 1)); then
                GTEST_TOTAL_SHARDS=$shards GTEST_SHARD_INDEX=$i "$exe" "${gtest_args[@]}" \
                    >"$logs/$name.$i" 2>&1 &
            else
                "$exe" "${gtest_args[@]}" >"$logs/$name.$i" 2>&1 &
            fi
            pids+=("$!:$name")
        done
    done
    declare -A code_of=()
    for entry in "${pids[@]}"; do
        wait "${entry%%:*}"
        code=$?
        ((code)) && code_of[${entry#*:}]=$code
    done
    failed=0
    for name in $(printf '%s\n' "${!shards_of[@]}" | sort); do
        cat "$logs/$name".* | if ((full)); then cat; else
            # Brief output is failures plus the summary; drop the banner,
            # the per-shard summaries and "filter matched nothing" notes.
            grep -vE '^(Running main\(\) from|\[==========\]|\[  PASSED  \]|WARNING: filter|Note: This is test shard|This plugin does not support)' || true
        fi
        tests=$(cat "$logs/$name".* | awk '/^\[==========\] [0-9]+ tests? from/ { n += $2 } END { print n + 0 }')
        note=
        ((shards_of[$name] > 1)) && note=", ${shards_of[$name]} shards"
        if [[ -n "${code_of[$name]:-}" ]]; then
            echo "$name: FAILED (${code_of[$name]}) (${tests:-0} tests$note)"
            failed=1
        else
            echo "$name: ok (${tests:-0} tests$note)"
        fi
    done
    echo "tests: $( ((failed)) && echo FAILED || echo ok) ($((SECONDS - start))s)"
    ((failed)) && exit 1
fi

if ((ctest)); then
    ctest_args=(--preset "$preset")
    [[ -n "$test_regex" ]] && ctest_args+=(-R "$test_regex")
    step ctest '(Failed|\*\*\*|tests passed|tests failed)' ctest "${ctest_args[@]}"
fi
