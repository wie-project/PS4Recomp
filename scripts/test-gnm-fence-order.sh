#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Reuse the real runtime objects of an existing native game build; no replacement runtime stubs.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(cd "${1:?usage: test-gnm-fence-order.sh NATIVE_BUILD_DIR}" && pwd)"
cd "$build"
test -f build.ninja
if ! grep -q '^build ps4_app: link_bin main.o ' build.ninja; then
    echo "Unsupported native build.ninja: expected ps4_app linked with main.o" >&2
    exit 1
fi
perl -pe 's/^build ps4_app: link_bin main.o /build fence_order_test: link_bin fence_order_test.o /; s/^default ps4_app/default fence_order_test/;' \
    build.ninja > fence-order-test.ninja
# Escape the repository path for Ninja, including spaces and literal dollar signs.
source_path="$(printf '%s' "$root/tests/gnm_metal_test/fence_order.mm" | sed 's/\$/$$/g; s/ /$ /g; s/:/$:/g')"
printf '\nbuild fence_order_test.o: compile_mm %s\n' "$source_path" >> fence-order-test.ninja
ninja -f fence-order-test.ninja fence_order_test
cache="$(mktemp -d "${TMPDIR:-/tmp}/ps4-fence-cache.XXXXXX")"
trap 'rm -rf "$cache"' EXIT
for packet in 47 49 48; do
    for bits in 32 64; do
        PS4_BACKGROUND_WINDOW=1 PS4_GNM_METAL="${PS4_GNM_METAL:-3}" \
            MTL_DEBUG_LAYER=1 PS4RECOMP_CACHE_DIR="$cache" ./fence_order_test "$packet" "$bits"
    done
done
