// Host test for sceFontGetCharGlyphMetrics and sceFontGetKerning on a macOS system font.
// Build and run (from the repository root):
//   clang -std=c11 -I pkg/runtime/core -I pkg/runtime/kernel/vfs -I pkg/runtime/modules/libSceFont \
//     -I $(brew --prefix freetype)/include/freetype2 -L $(brew --prefix freetype)/lib -lfreetype \
//     -Wl,-undefined,dynamic_lookup tests/font_test/main.c \
//     pkg/runtime/modules/libSceFont/ps4_font.c -o /tmp/font_test && /tmp/font_test

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps4_font.h"
#include "recomp_context.h"

// The tested shims run no guest code: an empty dispatch table makes any guest call abort.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

// Arial carries a legacy 'kern' table: A-V kerns by -152 units of 2048, 'A' advances 1366 units.
#define FONT_PATH "/System/Library/Fonts/Supplemental/Arial.ttf"
#define SCALE 64.0f

enum {
    kStack = 0x10000,   // guest stack top
    kLib = 0x20000,     // library handle (opaque)
    kHandle = 0x21000,  // out font handle
    kOut = 0x22000,     // metrics / kerning output
    kFontObj = 0x23000, // caller-allocated font object
    kFont = 0x100000,   // font file bytes
};

static int32_t call(GuestContext *ctx, void (*shim)(GuestContext *), uint64_t a, uint64_t b, uint64_t c,
                    uint64_t d, uint64_t e) {
    ctx->rsp = kStack - 8;
    ctx->rdi = a;
    ctx->rsi = b;
    ctx->rdx = c;
    ctx->rcx = d;
    ctx->r8 = e;
    shim(ctx);
    assert(ctx->rsp == kStack);
    return (int32_t)ctx->rax;
}

static int near(float got, float want) {
    return fabsf(got - want) < 1.0f / 64.0f;
}

int main(void) {
    FILE *f = fopen(FONT_PATH, "rb");
    assert(f && "test font is missing");
    fseek(f, 0, SEEK_END);
    long font_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    GuestContext ctx = {0};
    ctx.mem_size = kFont + (uint64_t)font_size;
    ctx.mem_base = calloc(1, ctx.mem_size);
    assert(ctx.mem_base);
    assert(fread(ctx.mem_base + kFont, 1, (size_t)font_size, f) == (size_t)font_size);
    fclose(f);

    *(uint64_t *)(ctx.mem_base + kHandle) = kFontObj;
    assert(call(&ctx, shim_sceFontOpenFontMemory, kLib, kFont, (uint64_t)font_size, 0, kHandle) == 0);
    uint64_t font = *(uint64_t *)(ctx.mem_base + kHandle);
    assert(font == kFontObj);
    ctx.xmm[0].f32[0] = SCALE;
    ctx.xmm[1].f32[0] = SCALE;
    assert(call(&ctx, shim_sceFontSetScalePixel, font, 0, 0, 0, 0) == 0);

    // sceFontGetCharGlyphMetrics
    const OrbisFontGlyphMetrics *m = (const OrbisFontGlyphMetrics *)(ctx.mem_base + kOut);
    assert(call(&ctx, shim_sceFontGetCharGlyphMetrics, font, 'A', kOut, 0, 0) == 0);
    assert(near(m->Horizontal.advance, 1366.0f * SCALE / 2048.0f));
    assert(m->width > 0.0f && m->height > 0.0f && m->Horizontal.bearingY > 0.0f);
    assert(m->Vertical.advance > 0.0f);
    printf("[+] 'A' at %.0f px: %.4f x %.4f, advance %.4f\n", SCALE, m->width, m->height,
           m->Horizontal.advance);

    assert(call(&ctx, shim_sceFontGetCharGlyphMetrics, font, 'A', 0, 0, 0) == ORBIS_FONT_ERROR_INVALID_PARAMETER);
    assert(call(&ctx, shim_sceFontGetCharGlyphMetrics, font + 8, 'A', kOut, 0, 0) ==
           ORBIS_FONT_ERROR_INVALID_FONT_HANDLE);
    assert(call(&ctx, shim_sceFontGetCharGlyphMetrics, font, 0, kOut, 0, 0) == ORBIS_FONT_ERROR_NO_SUPPORT_CODE);
    assert(call(&ctx, shim_sceFontGetCharGlyphMetrics, font, 0x10FFFD, kOut, 0, 0) ==
           ORBIS_FONT_ERROR_NO_SUPPORT_GLYPH);
    assert(m->width == 0.0f && m->Horizontal.advance == 0.0f);
    printf("[+] sceFontGetCharGlyphMetrics error codes\n");

    // sceFontGetKerning: FT_KERNING_DEFAULT grid-fits -4.75 px to -5 px.
    const OrbisFontKerning *k = (const OrbisFontKerning *)(ctx.mem_base + kOut);
    assert(call(&ctx, shim_sceFontGetKerning, font, 'A', 'V', kOut, 0) == 0);
    assert(k->offsetX == -5.0f && k->offsetY == 0.0f && k->positionX == 0.0f && k->positionY == 0.0f);
    printf("[+] kerning A-V at %.0f px: %.2f\n", SCALE, k->offsetX);
    assert(call(&ctx, shim_sceFontGetKerning, font, 'A', 'A', kOut, 0) == 0);
    assert(k->offsetX == 0.0f);
    assert(call(&ctx, shim_sceFontGetKerning, font, 'A', 0x10FFFD, kOut, 0) == 0);
    assert(k->offsetX == 0.0f);
    assert(call(&ctx, shim_sceFontGetKerning, font, 'A', 'V', 0, 0) == ORBIS_FONT_ERROR_INVALID_PARAMETER);
    assert(call(&ctx, shim_sceFontGetKerning, font + 8, 'A', 'V', kOut, 0) == ORBIS_FONT_ERROR_INVALID_FONT_HANDLE);
    printf("[+] sceFontGetKerning pairs and error codes\n");

    printf("[font-test] all passed\n");
    free(ctx.mem_base);
    return 0;
}
