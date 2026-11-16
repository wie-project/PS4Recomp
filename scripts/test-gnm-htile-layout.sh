#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Check the HTILE slice layout against the real runtime objects of an existing native game build.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(cd "${1:?usage: test-gnm-htile-layout.sh NATIVE_BUILD_DIR}" && pwd)"
cd "$build"
test -f build.ninja
if ! grep -q '^build ps4_app: link_bin main.o ' build.ninja; then
    echo "Unsupported native build.ninja: expected ps4_app linked with main.o" >&2
    exit 1
fi
perl -pe 's/^build ps4_app: link_bin main.o /build htile_layout_test: link_bin htile_layout_test.o /; s/^default ps4_app/default htile_layout_test/;' \
    build.ninja > htile-layout-test.ninja
# Escape the repository path for Ninja, including spaces and literal dollar signs.
source_path="$(printf '%s' "$root/tests/gnm_metal_test/htile_layout.mm" | sed 's/\$/$$/g; s/ /$ /g; s/:/$:/g')"
printf '\nbuild htile_layout_test.o: compile_mm %s\n' "$source_path" >> htile-layout-test.ninja
ninja -f htile-layout-test.ninja htile_layout_test
./htile_layout_test
