#!/usr/bin/env bash
# Installs the Linux build's dependencies - the same versions as the FreeCAD
# LibPack (Qt 6.11.1, Boost 1.91, GoogleTest) - from conda-forge into a
# prefix (GS_DEPS_DIR, default /opt/gs-deps), with a micromamba fetched from
# conda-forge itself. Needs only curl, tar (bzip2) and a C++20 compiler.
#
#   tools/setup_linux.sh            # install, or keep an existing prefix
#   tools/setup_linux.sh --force    # recreate the prefix
#
# Then: tools/build.sh -Test
set -euo pipefail

deps="${GS_DEPS_DIR:-/opt/gs-deps}"
tools="${GS_MAMBA_DIR:-$HOME/.cache/gs-micromamba}"
mamba_version=2.9.0
channel=https://conda.anaconda.org/conda-forge

if [[ "${1:-}" != "--force" && -f "$deps/lib/cmake/Qt6/Qt6Config.cmake" \
      && -d "$deps/lib/cmake/boost_json-1.91.0" && -d "$deps/lib/cmake/GTest" ]]; then
    echo "dependencies: present in $deps (--force to recreate)"
    exit 0
fi

mamba="$tools/bin/micromamba"
if [[ ! -x "$mamba" ]]; then
    mkdir -p "$tools"
    curl -fsSL "$channel/linux-64/micromamba-$mamba_version-0.tar.bz2" | tar -xj -C "$tools" bin/micromamba
fi

export MAMBA_ROOT_PREFIX="$tools/root"
[[ "${1:-}" == "--force" ]] && rm -rf "$deps"
"$mamba" create -y -q -p "$deps" -c conda-forge --override-channels \
    "qt6-main=6.11.1" "libboost-devel=1.91" "gtest"
echo "dependencies: installed in $deps"
