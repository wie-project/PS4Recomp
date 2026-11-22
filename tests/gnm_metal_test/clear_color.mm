// SPDX-License-Identifier: GPL-2.0-or-later
// Native regression: a fast clear of an 8-bit sRGB attachment must decode. DRIVECLUB accumulates its
// sun shadow mask into an R8 sRGB target that it fast clears to 0 each frame; the clear was taken
// but never applied (ClearColorFor returned false), so the mask kept its old "fully lit" value and
// no cascade ever darkened it: no tree shadows on the track.
#import <Metal/Metal.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "ps4_gnm_clear_color.h"

using ps4gnm_clear::ClearColorFor;

static void Check(bool ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static bool Near(double a, double b) { return fabs(a - b) < 1e-4; }

int main(void) {
    MTLClearColor c{};
    Check(ClearColorFor(MTLPixelFormatR8Unorm_sRGB, 0x00, 0, &c), "R8 sRGB clear 0 decodes");
    Check(Near(c.red, 0.0) && Near(c.alpha, 1.0), "R8 sRGB raw 0 clears to 0");
    Check(ClearColorFor(MTLPixelFormatR8Unorm_sRGB, 0xFF, 0, &c) && Near(c.red, 1.0), "R8 sRGB raw 255 clears to 1");
    // raw 0x80 is the sRGB-encoded value 128/255; Metal wants the linear value (about 0.2159)
    Check(ClearColorFor(MTLPixelFormatR8Unorm_sRGB, 0x80, 0, &c) && Near(c.red, 0.21586), "R8 sRGB raw 128 is linearised");
    Check(ClearColorFor(MTLPixelFormatRG8Unorm_sRGB, 0xFF00, 0, &c) && Near(c.red, 0.0) && Near(c.green, 1.0),
          "RG8 sRGB channels");
    Check(ClearColorFor(MTLPixelFormatR8Unorm, 0x80, 0, &c) && Near(c.red, 128 / 255.0), "R8 unorm stays linear");
    puts("clear colors: 8-bit sRGB fast clears decode and are linearised");
    return 0;
}
