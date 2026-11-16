// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HTILE metadata layout of depth surfaces: the slice size a metadata (fast) clear covers.
 *
 * The game's HTILE fast clear of the 4-layer 2048x2048 shadow map writes 1048576 bytes (4 x
 * 256 KiB), so a 2048x2048 depth slice's HTILE is exactly 256 KiB on both the base PS4 layout and
 * the Neo alternate tiling. A small surface shows the difference: the Neo 16-pipe layout pads a
 * 256x256 slice to 1024x512 metadata texels (32 KiB), the base 8-pipe layout to 512x512 (16 KiB).
 */

#include "ps4_gnm_resources.h"

#include <assert.h>
#include <stdio.h>

using namespace ps4gnm;

int main(void) {
    // 32 bits per 8x8 depth tile, padded to the metadata macro tile; 2048x2048 is aligned for
    // every pipe count, so its HTILE slice is 2048*2048/64*4 bytes.
    assert(HtileSliceBytes(2048, 2048, 16, false) == 262144);
    assert(HtileSliceBytes(2048, 2048, 8, false) == 262144);

    DepthTarget d;
    d.pitch = 2048;
    d.height = 2048;
    d.tile_mode = TileMode::Depth2DThin64;
    d.htile_linear = false;
    assert(DepthHtileSliceBytes(d, true) == 262144);
    assert(DepthHtileSliceBytes(d, false) == 262144);

    // Neo alternate tiling: 16 pipes (1024x512 metadata macro tile) instead of the base 8 pipes.
    d.pitch = 256;
    d.height = 256;
    assert(DepthHtileSliceBytes(d, true) == 32768);
    assert(DepthHtileSliceBytes(d, false) == 16384);

    printf("[htile-test] HTILE slice sizes OK\n");
    return 0;
}