#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Standalone build script for PS4Recomp.
# Automatically updates submodules, compiles required 3rdparty static libraries,
# stages assets, and builds a completely autonomous ps4-recomp binary.

set -euo pipefail

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
if [ ! -f "3rdparty/spirv-cross/libspirv-cross.a" ]; then
    echo "Building spirv-cross static library..."
    make -C 3rdparty/spirv-cross libspirv-cross.a -j"$NCPU"
fi

# 2. LibAtrac9
if [ ! -f "3rdparty/libatrac9/libatrac9.a" ]; then
    echo "Building libatrac9 static library..."
    mkdir -p 3rdparty/libatrac9/C/obj_static 3rdparty/libatrac9/C/bin
    make -C 3rdparty/libatrac9/C bin/libatrac9.a -j"$NCPU"
    cp 3rdparty/libatrac9/C/bin/libatrac9.a 3rdparty/libatrac9/libatrac9.a
fi

# 3. GCN Recompiler
if [ ! -f "3rdparty/gcn-recompiler/libgcn_recompiler.a" ]; then
    echo "Building gcn-recompiler static library..."
    GCN_BUILD_DIR="${TMPDIR:-/tmp}/gcn_recompiler_build"
    mkdir -p "$GCN_BUILD_DIR"
    CMAKE_GEN=()
    if command -v ninja &>/dev/null; then
        CMAKE_GEN=(-G Ninja)
    fi
    cmake -B "$GCN_BUILD_DIR" "${CMAKE_GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DGCN_BUILD_TOOLS=OFF 3rdparty/gcn-recompiler
    cmake --build "$GCN_BUILD_DIR" --target gcn_recompiler -j"$NCPU"
    cp "$GCN_BUILD_DIR/libgcn_recompiler.a" 3rdparty/gcn-recompiler/libgcn_recompiler.a
fi

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
cp 3rdparty/minimp3/minimp3.h "$STAGING_DIR/minimp3/"

echo "=== [ps4-recomp] 4/5: Compiling standalone binary ==="
mkdir -p bin
go build -ldflags="-s -w" -o bin/ps4-recomp ./cmd/ps4-recomp

echo "=== [ps4-recomp] 5/5: Cleaning temporary staging area ==="
# Clean staged files, preserving .gitkeep
find "$STAGING_DIR" -mindepth 1 -not -name ".gitkeep" -delete 2>/dev/null || true

echo "=== [ps4-recomp] SUCCESS: Built standalone binary at $REPO_ROOT/bin/ps4-recomp ==="
ls -lh "$REPO_ROOT/bin/ps4-recomp"
