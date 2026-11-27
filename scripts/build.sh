#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Standalone build script for PS4Recomp.
# Automatically updates submodules, compiles required 3rdparty static libraries,
# stages assets, and builds a completely autonomous ps4-recomp binary.
#
# Usage: scripts/build.sh [--debug]
#   --debug (or PS4RECOMP_DEBUG=1) keeps debug symbols: ps4-recomp is linked without -s -w and
#   gcn-recompiler is built as RelWithDebInfo. Games get debug symbols from `ps4-recomp -g`.

set -euo pipefail

DEBUG="${PS4RECOMP_DEBUG:-0}"
for arg in "$@"; do
    case "$arg" in
    --debug) DEBUG=1 ;;
    -h | --help)
        sed -n '4,10p' "$0"
        exit 0
        ;;
    *)
        echo "unknown option: $arg (see --help)" >&2
        exit 2
        ;;
    esac
done
GCN_BUILD_TYPE=Release
GO_LDFLAGS="-s -w"
if [ "$DEBUG" = 1 ]; then
    GCN_BUILD_TYPE=RelWithDebInfo
    GO_LDFLAGS=""
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

NCPU="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

echo "=== [ps4-recomp] 1/5: Checking 3rdparty submodules ==="
if [ -f .gitmodules ] && command -v git &>/dev/null; then
    if [ ! -f "3rdparty/spirv-cross/CMakeLists.txt" ] || [ ! -f "3rdparty/libatrac9/C/src/libatrac9.h" ] || [ ! -f "3rdparty/minimp3/minimp3.h" ] || [ ! -f "3rdparty/gcn-recompiler/CMakeLists.txt" ]; then
        echo "Updating git submodules..."
        git submodule update --init --recursive
    fi
fi

echo "=== [ps4-recomp] 2/5: Building 3rdparty static libraries ==="
# 1. SPIRV-Cross
if [ -f "3rdparty/spirv-cross/libspirv-cross.a" ] && nm "3rdparty/spirv-cross/libspirv-cross.a" 2>/dev/null | grep -q "spv_private"; then
    echo "Cleaning stale spirv-cross static library with spv_private override..."
    make -C 3rdparty/spirv-cross clean 2>/dev/null || true
    rm -f "3rdparty/spirv-cross/libspirv-cross.a"
fi
if [ ! -f "3rdparty/spirv-cross/libspirv-cross.a" ]; then
    echo "Building spirv-cross static library..."
    make -C 3rdparty/spirv-cross libspirv-cross.a CXXFLAGS="-std=c++11 -Wall -Wextra -Wshadow -Wno-deprecated-declarations -O2 -DNDEBUG" -j"$NCPU"
fi

# 2. LibAtrac9
if [ ! -f "3rdparty/libatrac9/libatrac9.a" ]; then
    echo "Building libatrac9 static library..."
    mkdir -p 3rdparty/libatrac9/C/obj_static 3rdparty/libatrac9/C/bin
    make -C 3rdparty/libatrac9/C bin/libatrac9.a -j"$NCPU"
    cp 3rdparty/libatrac9/C/bin/libatrac9.a 3rdparty/libatrac9/libatrac9.a
fi

# 3. GCN Recompiler
echo "Building gcn-recompiler static library..."
GCN_BUILD_DIR="${TMPDIR:-/tmp}/gcn_recompiler_build"
# A CMake cache generated from another checkout makes configure fail, so start over in that case.
GCN_SRC="$(cd 3rdparty/gcn-recompiler && pwd -P)"
if [ -f "$GCN_BUILD_DIR/CMakeCache.txt" ] && ! grep -qF "CMAKE_HOME_DIRECTORY:INTERNAL=$GCN_SRC" "$GCN_BUILD_DIR/CMakeCache.txt"; then
    echo "Discarding gcn-recompiler CMake cache left by a different source directory..."
    rm -rf "$GCN_BUILD_DIR"
fi
mkdir -p "$GCN_BUILD_DIR"
CMAKE_GEN=()
if command -v ninja &>/dev/null; then
    CMAKE_GEN=(-G Ninja)
fi
cmake -B "$GCN_BUILD_DIR" "${CMAKE_GEN[@]}" -DCMAKE_BUILD_TYPE="$GCN_BUILD_TYPE" -DGCN_BUILD_TOOLS=OFF 3rdparty/gcn-recompiler
cmake --build "$GCN_BUILD_DIR" --target gcn_recompiler -j"$NCPU"
cp "$GCN_BUILD_DIR/libgcn_recompiler.a" 3rdparty/gcn-recompiler/libgcn_recompiler.a

echo "=== [ps4-recomp] 3/5: Staging assets for embedding ==="
STAGING_DIR="$REPO_ROOT/pkg/thirdparty/assets"
mkdir -p "$STAGING_DIR/gcn-recompiler/include"
mkdir -p "$STAGING_DIR/spirv-cross"
mkdir -p "$STAGING_DIR/libatrac9/C/src"
mkdir -p "$STAGING_DIR/minimp3"

cp 3rdparty/gcn-recompiler/libgcn_recompiler.a "$STAGING_DIR/gcn-recompiler/"
cp 3rdparty/gcn-recompiler/include/*.h "$STAGING_DIR/gcn-recompiler/include/"
cp 3rdparty/spirv-cross/libspirv-cross.a "$STAGING_DIR/spirv-cross/"
cp 3rdparty/spirv-cross/spirv_*.hpp "$STAGING_DIR/spirv-cross/"
cp 3rdparty/spirv-cross/GLSL.std.450.h "$STAGING_DIR/spirv-cross/"
cp 3rdparty/libatrac9/libatrac9.a "$STAGING_DIR/libatrac9/"
cp 3rdparty/libatrac9/C/src/libatrac9.h "$STAGING_DIR/libatrac9/C/src/"
# libSceNgs2 resets a decoder in place: it needs the Atrac9Handle layout.
cp 3rdparty/libatrac9/C/src/structures.h "$STAGING_DIR/libatrac9/C/src/"
cp 3rdparty/minimp3/minimp3.h "$STAGING_DIR/minimp3/"

echo "=== [ps4-recomp] 4/5: Compiling standalone binary ==="
mkdir -p bin
go build -ldflags="$GO_LDFLAGS" -o bin/ps4-recomp ./cmd/ps4-recomp

echo "=== [ps4-recomp] 5/5: Cleaning temporary staging area ==="
# Clean staged files, preserving .gitkeep
find "$STAGING_DIR" -mindepth 1 -not -name ".gitkeep" -delete 2>/dev/null || true

echo "=== [ps4-recomp] SUCCESS: Built standalone binary at $REPO_ROOT/bin/ps4-recomp ==="
ls -lh "$REPO_ROOT/bin/ps4-recomp"
