// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_font.h"
#include "ps4_vfs.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FONTS 128
#define MAX_STRINGS 128
#define MAX_WRITINGS 64

typedef struct FontBacking {
    uint8_t *data;
    size_t size;
    uint32_t subfont_index;
    int refs;
} FontBacking;

typedef struct InternalFontHandle {
    int in_use;
    uint64_t guest_addr;
    uint32_t magic; // 0x0F02
    uint32_t scale_unit; // 0 = pixel, 1 = point
    float scale_w;
    float scale_h;
    float render_scale_w;
    float render_scale_h;
    float weight_x;
    float weight_y;
    float slant;
    float render_weight_x;
    float render_weight_y;
    float render_slant;
    uint32_t dpi_x;
    uint32_t dpi_y;
    uint64_t renderer_guest;
    FT_Face ft_face;
    FontBacking *backing;
    uint64_t library_guest;
} InternalFontHandle;

typedef struct InternalStringNode {
    OrbisFontTextCharacter guest_node;
} InternalStringNode;

typedef struct InternalString {
    int in_use;
    uint64_t guest_addr;
    uint64_t memory_guest;
    uint64_t source_guest;
    uint64_t default_font_guest;
    uint32_t writing_form;
    uint32_t terminate_code;
    uint64_t terminate_order;
    uint32_t character_count;
    uint64_t head_node_guest;
} InternalString;

typedef struct InternalWriting {
    int in_use;
    uint64_t guest_addr;
    uint64_t string_guest;
    uint64_t current_node_guest;
    uint64_t step_char_node_guest;
    uint32_t current_index;
    float cursor_x;
    float cursor_y;
    OrbisFontWritingStep current_step;
} InternalWriting;

static pthread_mutex_t g_font_mutex = PTHREAD_MUTEX_INITIALIZER;
static FT_Library g_ft_library = NULL;
static InternalFontHandle g_fonts[MAX_FONTS];
static InternalString g_strings[MAX_STRINGS];
static InternalWriting g_writings[MAX_WRITINGS];

static const char *g_fallback_font_candidates[] = {
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Geneva.dfont",
    "/System/Library/Fonts/SFNS.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Tahoma.ttf",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    "/System/Library/Fonts/AppleSDGothicNeo.ttc",
    NULL
};

static FT_Face load_system_fallback_face(FT_Library ft_lib) {
    for (int i = 0; g_fallback_font_candidates[i] != NULL; i++) {
        const char *candidate = g_fallback_font_candidates[i];
        FT_Face face = NULL;
        if (FT_New_Face(ft_lib, candidate, 0, &face) == 0) {
            fprintf(stderr, "[ps4-font] Loaded system fallback font: %s\n", candidate);
            return face;
        }
    }
    return NULL;
}

static FontBacking *backing_retain(FontBacking *b) {
    if (b) b->refs++;
    return b;
}

static void backing_release(FontBacking *b) {
    if (!b) return;
    b->refs--;
    if (b->refs <= 0) {
        free(b->data);
        free(b);
    }
}

static void font_compute_pixel_scale(const InternalFontHandle *h, int use_render,
                                     float *out_w, float *out_h) {
    float w = use_render ? h->render_scale_w : h->scale_w;
    float ht = use_render ? h->render_scale_h : h->scale_h;
    if (!use_render && h->scale_unit != 0) {
        if (h->dpi_x != 0) w *= (float)h->dpi_x / 72.0f;
        if (h->dpi_y != 0) ht *= (float)h->dpi_y / 72.0f;
    }
    if (w <= 0.0f) w = 16.0f;
    if (ht <= 0.0f) ht = 16.0f;
    if (out_w) *out_w = w;
    if (out_h) *out_h = ht;
}

static void font_apply_ft_size(InternalFontHandle *h, int use_render) {
    if (!h || !h->ft_face) return;
    float pw = 16.0f, ph = 16.0f;
    font_compute_pixel_scale(h, use_render, &pw, &ph);
    FT_Set_Pixel_Sizes(h->ft_face, (FT_UInt)(pw + 0.5f), (FT_UInt)(ph + 0.5f));
}

static InternalFontHandle *find_or_create_font(uint64_t guest_addr) {
    for (int i = 0; i < MAX_FONTS; i++) {
        if (g_fonts[i].in_use && g_fonts[i].guest_addr == guest_addr) {
            return &g_fonts[i];
        }
    }
    for (int i = 0; i < MAX_FONTS; i++) {
        if (!g_fonts[i].in_use) {
            memset(&g_fonts[i], 0, sizeof(InternalFontHandle));
            g_fonts[i].in_use = 1;
            g_fonts[i].guest_addr = guest_addr;
            g_fonts[i].magic = 0x0F02;
            g_fonts[i].scale_unit = 0;
            g_fonts[i].scale_w = 16.0f;
            g_fonts[i].scale_h = 16.0f;
            g_fonts[i].render_scale_w = 16.0f;
            g_fonts[i].render_scale_h = 16.0f;
            g_fonts[i].weight_x = 1.0f;
            g_fonts[i].weight_y = 1.0f;
            g_fonts[i].render_weight_x = 1.0f;
            g_fonts[i].render_weight_y = 1.0f;
            g_fonts[i].dpi_x = 72;
            g_fonts[i].dpi_y = 72;
            return &g_fonts[i];
        }
    }
    return NULL;
}

static InternalFontHandle *get_font(uint64_t guest_addr) {
    if (!guest_addr) return NULL;
    for (int i = 0; i < MAX_FONTS; i++) {
        if (g_fonts[i].in_use && g_fonts[i].guest_addr == guest_addr) {
            return &g_fonts[i];
        }
    }
    return NULL;
}

static InternalString *find_or_create_string(uint64_t guest_addr) {
    for (int i = 0; i < MAX_STRINGS; i++) {
        if (g_strings[i].in_use && g_strings[i].guest_addr == guest_addr) {
            return &g_strings[i];
        }
    }
    for (int i = 0; i < MAX_STRINGS; i++) {
        if (!g_strings[i].in_use) {
            memset(&g_strings[i], 0, sizeof(InternalString));
            g_strings[i].in_use = 1;
            g_strings[i].guest_addr = guest_addr;
            return &g_strings[i];
        }
    }
    return NULL;
}

static InternalString *get_string(uint64_t guest_addr) {
    if (!guest_addr) return NULL;
    for (int i = 0; i < MAX_STRINGS; i++) {
        if (g_strings[i].in_use && g_strings[i].guest_addr == guest_addr) {
            return &g_strings[i];
        }
    }
    return NULL;
}

static InternalWriting *find_or_create_writing(uint64_t guest_addr) {
    for (int i = 0; i < MAX_WRITINGS; i++) {
        if (g_writings[i].in_use && g_writings[i].guest_addr == guest_addr) {
            return &g_writings[i];
        }
    }
    for (int i = 0; i < MAX_WRITINGS; i++) {
        if (!g_writings[i].in_use) {
            memset(&g_writings[i], 0, sizeof(InternalWriting));
            g_writings[i].in_use = 1;
            g_writings[i].guest_addr = guest_addr;
            return &g_writings[i];
        }
    }
    return NULL;
}

static InternalWriting *get_writing(uint64_t guest_addr) {
    if (!guest_addr) return NULL;
    for (int i = 0; i < MAX_WRITINGS; i++) {
        if (g_writings[i].in_use && g_writings[i].guest_addr == guest_addr) {
            return &g_writings[i];
        }
    }
    return NULL;
}

// 1. sceFontMemoryInit
void shim_sceFontMemoryInit(GuestContext *ctx) {
    uint64_t memGuest = ctx->rdi;
    uint64_t regionGuest = ctx->rsi;
    uint32_t regionSize = (uint32_t)ctx->rdx;
    uint64_t ifaceGuest = ctx->rcx;
    uint64_t mspaceGuest = ctx->r8;
    uint64_t destroyCbGuest = ctx->r9;
    uint64_t destroyCtx = 0;
    if (ctx->rsp && ctx->mem_base) {
        destroyCtx = *(uint64_t *)(ctx->mem_base + ctx->rsp + 8);
    }

    if (!memGuest || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    OrbisFontMem *mem = (OrbisFontMem *)(ctx->mem_base + memGuest);
    if (!ifaceGuest && (!regionGuest || regionSize == 0)) {
        mem->mem_kind = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    mem->mem_kind = 0x0F00;
    mem->attr_bits = 0;
    mem->region_size = regionSize;
    mem->region_base = (void *)(uintptr_t)regionGuest;
    mem->mspace_handle = (void *)(uintptr_t)mspaceGuest;
    mem->iface = (const OrbisFontMemInterface *)(uintptr_t)ifaceGuest;
    mem->on_destroy = (OrbisFontMemDestroyCb)(uintptr_t)destroyCbGuest;
    mem->destroy_ctx = (void *)(uintptr_t)destroyCtx;
    mem->some_ctx1 = NULL;
    mem->some_ctx2 = (void *)(uintptr_t)mspaceGuest;

    fprintf(stderr, "[ps4-font] sceFontMemoryInit: mem=0x%llx region=0x%llx size=0x%x mspace=0x%llx iface=0x%llx\n",
            (unsigned long long)memGuest, (unsigned long long)regionGuest, regionSize,
            (unsigned long long)mspaceGuest, (unsigned long long)ifaceGuest);

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 2. sceFontMemoryTerm
void shim_sceFontMemoryTerm(GuestContext *ctx) {
    uint64_t memGuest = ctx->rdi;
    if (!memGuest || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }
    OrbisFontMem *mem = (OrbisFontMem *)(ctx->mem_base + memGuest);
    if (mem->mem_kind != 0x0F00) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_MEMORY;
        SHIM_RETURN();
    }
    if (mem->on_destroy) {
        mem->mem_kind = 0;
        uint64_t cb = (uint64_t)(uintptr_t)mem->on_destroy;
        GuestContext sub = *ctx;
        sub.rdi = memGuest;
        sub.rsi = (uint64_t)(uintptr_t)mem->mspace_handle;
        sub.rdx = (uint64_t)(uintptr_t)mem->destroy_ctx;
        recomp_call_guest(&sub, cb);
    }
    memset(mem, 0, sizeof(*mem));
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 3. sceFontCreateLibraryWithEdition
void shim_sceFontCreateLibraryWithEdition(GuestContext *ctx) {
    uint64_t memGuest = ctx->rdi;
    uint64_t createParamsGuest = ctx->rsi;
    uint64_t edition = ctx->rdx;
    uint64_t pLibraryGuest = ctx->rcx;

    (void)edition;
    if (!memGuest || !createParamsGuest || !pLibraryGuest || !ctx->mem_base) {
        if (pLibraryGuest && ctx->mem_base) {
            *(uint64_t *)(ctx->mem_base + pLibraryGuest) = 0;
        }
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_font_mutex);
    if (!g_ft_library) {
        if (FT_Init_FreeType(&g_ft_library) != 0) {
            pthread_mutex_unlock(&g_font_mutex);
            ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_ALLOCATION_FAILED;
            SHIM_RETURN();
        }
    }
    pthread_mutex_unlock(&g_font_mutex);

    uint64_t libGuest = recomp_vm_alloc_named(ctx, 0x1000, PROT_READ | PROT_WRITE, 0, "font_lib");
    if (!libGuest) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_ALLOCATION_FAILED;
        SHIM_RETURN();
    }

    uint8_t *libPtr = (uint8_t *)(ctx->mem_base + libGuest);
    memset(libPtr, 0, 0x1000);
    *(uint16_t *)(libPtr + 0x00) = 0x0F01; // magic

    *(uint64_t *)(ctx->mem_base + pLibraryGuest) = libGuest;

    fprintf(stderr, "[ps4-font] sceFontCreateLibraryWithEdition: created lib=0x%llx\n", (unsigned long long)libGuest);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 4. sceFontDestroyLibrary
void shim_sceFontDestroyLibrary(GuestContext *ctx) {
    uint64_t pLibraryGuest = ctx->rdi;
    if (pLibraryGuest && ctx->mem_base) {
        *(uint64_t *)(ctx->mem_base + pLibraryGuest) = 0;
    }
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 5. sceFontSupportSystemFonts
void shim_sceFontSupportSystemFonts(GuestContext *ctx) {
    uint64_t libGuest = ctx->rdi;
    if (!libGuest || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_LIBRARY;
        SHIM_RETURN();
    }
    uint8_t *libPtr = (uint8_t *)(ctx->mem_base + libGuest);
    *(uint64_t *)(libPtr + 0xA0) = 0x1; // sysfonts_ctx non-null
    fprintf(stderr, "[ps4-font] sceFontSupportSystemFonts: enabled system fonts for lib=0x%llx\n", (unsigned long long)libGuest);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 6. sceFontSupportExternalFonts
void shim_sceFontSupportExternalFonts(GuestContext *ctx) {
    uint64_t libGuest = ctx->rdi;
    if (!libGuest || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_LIBRARY;
        SHIM_RETURN();
    }
    uint8_t *libPtr = (uint8_t *)(ctx->mem_base + libGuest);
    *(uint64_t *)(libPtr + 0xA8) = 0x1; // external_fonts_ctx non-null
    fprintf(stderr, "[ps4-font] sceFontSupportExternalFonts: enabled external fonts (max=%u, formats=0x%x)\n",
            (unsigned int)ctx->rsi, (unsigned int)ctx->rdx);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 7. sceFontCreateRendererWithEdition
void shim_sceFontCreateRendererWithEdition(GuestContext *ctx) {
    uint64_t pRendererGuest = ctx->rcx;
    if (!pRendererGuest || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }
    uint64_t rendGuest = recomp_vm_alloc_named(ctx, 0x1000, PROT_READ | PROT_WRITE, 0, "font_renderer");
    if (!rendGuest) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_ALLOCATION_FAILED;
        SHIM_RETURN();
    }
    uint8_t *rendPtr = (uint8_t *)(ctx->mem_base + rendGuest);
    memset(rendPtr, 0, 0x1000);
    *(uint16_t *)(rendPtr + 0x00) = 0x0F07; // renderer magic
    *(uint64_t *)(ctx->mem_base + pRendererGuest) = rendGuest;
    fprintf(stderr, "[ps4-font] sceFontCreateRendererWithEdition: created renderer=0x%llx\n", (unsigned long long)rendGuest);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 8. sceFontDestroyRenderer
void shim_sceFontDestroyRenderer(GuestContext *ctx) {
    uint64_t pRendererGuest = ctx->rdi;
    if (pRendererGuest && ctx->mem_base) {
        *(uint64_t *)(ctx->mem_base + pRendererGuest) = 0;
    }
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 9. sceFontAttachDeviceCacheBuffer
void shim_sceFontAttachDeviceCacheBuffer(GuestContext *ctx) {
    uint64_t libGuest = ctx->rdi;
    uint64_t bufGuest = ctx->rsi;
    uint32_t size = (uint32_t)ctx->rdx;
    if (libGuest && ctx->mem_base) {
        uint8_t *libPtr = (uint8_t *)(ctx->mem_base + libGuest);
        *(uint64_t *)(libPtr + 0xB0) = bufGuest;
    }
    fprintf(stderr, "[ps4-font] sceFontAttachDeviceCacheBuffer: lib=0x%llx buf=0x%llx size=0x%x\n",
            (unsigned long long)libGuest, (unsigned long long)bufGuest, size);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 10. sceFontOpenFontSet
void shim_sceFontOpenFontSet(GuestContext *ctx) {
    uint64_t libGuest = ctx->rdi;
    uint32_t fontSetType = (uint32_t)ctx->rsi;
    uint32_t openMode = (uint32_t)ctx->rdx;
    uint64_t openParamsGuest = ctx->rcx;
    uint64_t pFontHandleGuest = ctx->r8;

    (void)openMode;
    (void)openParamsGuest;

    if (!libGuest || !pFontHandleGuest || !ctx->mem_base) {
        if (pFontHandleGuest && ctx->mem_base) {
            *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        }
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_font_mutex);
    if (!g_ft_library) {
        FT_Init_FreeType(&g_ft_library);
    }
    FT_Face face = load_system_fallback_face(g_ft_library);

    uint64_t fontGuest = recomp_vm_alloc_named(ctx, 0x1000, PROT_READ | PROT_WRITE, 0, "font_handle");
    if (!fontGuest) {
        pthread_mutex_unlock(&g_font_mutex);
        *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_ALLOCATION_FAILED;
        SHIM_RETURN();
    }

    uint8_t *hPtr = (uint8_t *)(ctx->mem_base + fontGuest);
    memset(hPtr, 0, 0x1000);
    *(uint16_t *)(hPtr + 0x00) = 0x0F02; // native font magic
    *(uint64_t *)(hPtr + 0x28) = libGuest; // library

    InternalFontHandle *h = find_or_create_font(fontGuest);
    if (h) {
        h->library_guest = libGuest;
        h->ft_face = face;
    }
    pthread_mutex_unlock(&g_font_mutex);

    *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = fontGuest;
    fprintf(stderr, "[ps4-font] sceFontOpenFontSet: fontSetType=0x%x -> handle=0x%llx\n",
            fontSetType, (unsigned long long)fontGuest);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 11. sceFontCloseFont
void shim_sceFontCloseFont(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (h) {
        if (h->ft_face) {
            FT_Done_Face(h->ft_face);
            h->ft_face = NULL;
        }
        if (h->backing) {
            backing_release(h->backing);
            h->backing = NULL;
        }
        h->in_use = 0;
    }
    if (fontGuest && ctx->mem_base) {
        *(uint16_t *)(ctx->mem_base + fontGuest) = 0;
    }
    pthread_mutex_unlock(&g_font_mutex);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 12. sceFontBindRenderer
void shim_sceFontBindRenderer(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    uint64_t rendGuest = ctx->rsi;
    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (h) {
        h->renderer_guest = rendGuest;
    }
    if (fontGuest && ctx->mem_base) {
        uint8_t *hPtr = (uint8_t *)(ctx->mem_base + fontGuest);
        *(uint64_t *)(hPtr + 0x30) = rendGuest;
    }
    pthread_mutex_unlock(&g_font_mutex);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 13. sceFontUnbindRenderer
void shim_sceFontUnbindRenderer(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (h) {
        h->renderer_guest = 0;
    }
    if (fontGuest && ctx->mem_base) {
        uint8_t *hPtr = (uint8_t *)(ctx->mem_base + fontGuest);
        *(uint64_t *)(hPtr + 0x30) = 0;
    }
    pthread_mutex_unlock(&g_font_mutex);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 14. sceFontRebindRenderer
void shim_sceFontRebindRenderer(GuestContext *ctx) {
    shim_sceFontBindRenderer(ctx);
}

// 15. sceFontSetupRenderScalePixel
void shim_sceFontSetupRenderScalePixel(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    float scale_w = ctx->xmm[0].f32[0];
    float scale_h = ctx->xmm[1].f32[0];
    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (h) {
        h->render_scale_w = scale_w > 0.0f ? scale_w : 16.0f;
        h->render_scale_h = scale_h > 0.0f ? scale_h : 16.0f;
        font_apply_ft_size(h, 1);
    }
    pthread_mutex_unlock(&g_font_mutex);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 16. sceFontSetScalePixel
void shim_sceFontSetScalePixel(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    float scale_w = ctx->xmm[0].f32[0];
    float scale_h = ctx->xmm[1].f32[0];
    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (h) {
        h->scale_unit = 0;
        h->scale_w = scale_w > 0.0f ? scale_w : 16.0f;
        h->scale_h = scale_h > 0.0f ? scale_h : 16.0f;
        font_apply_ft_size(h, 0);
    }
    pthread_mutex_unlock(&g_font_mutex);
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 17. sceFontRenderSurfaceInit
void shim_sceFontRenderSurfaceInit(GuestContext *ctx) {
    uint64_t surfGuest = ctx->rdi;
    uint64_t bufGuest = ctx->rsi;
    int32_t bufWidthByte = (int32_t)ctx->rdx;
    int8_t pixelSizeByte = (int8_t)ctx->rcx;
    int32_t widthPixel = (int32_t)ctx->r8;
    int32_t heightPixel = (int32_t)ctx->r9;

    if (!surfGuest || !ctx->mem_base) {
        SHIM_RETURN();
    }

    OrbisFontRenderSurface *surf = (OrbisFontRenderSurface *)(ctx->mem_base + surfGuest);
    memset(surf, 0, sizeof(*surf));
    surf->buffer = (void *)(uintptr_t)bufGuest;
    surf->widthByte = bufWidthByte;
    surf->pixelSizeByte = pixelSizeByte;
    surf->width = widthPixel >= 0 ? widthPixel : 0;
    surf->height = heightPixel >= 0 ? heightPixel : 0;
    surf->sc_x0 = 0;
    surf->sc_y0 = 0;
    surf->sc_x1 = (uint32_t)surf->width;
    surf->sc_y1 = (uint32_t)surf->height;

    SHIM_RETURN();
}

// 18. sceFontTextSourceInit
void shim_sceFontTextSourceInit(GuestContext *ctx) {
    uint64_t sourceGuest = ctx->rdi;
    uint64_t textAddress = ctx->rsi;
    uint32_t textSizeByte = (uint32_t)ctx->rdx;
    uint64_t textParser = ctx->rcx;
    uint64_t textObject = ctx->r8;

    if (!sourceGuest || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    OrbisFontTextSource *src = (OrbisFontTextSource *)(ctx->mem_base + sourceGuest);
    memset(src, 0, sizeof(*src));
    src->systemUse0 = (1ULL << 32) | 0x54585352ULL; // 'TXSR'
    src->start = (const void *)(uintptr_t)textAddress;
    src->end = textSizeByte ? (const void *)(uintptr_t)(textAddress + textSizeByte) : NULL;
    src->current = src->start;
    src->textParser = (OrbisFontTextParseFunction)(uintptr_t)textParser;
    src->textObject = (void *)(uintptr_t)textObject;

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 19. sceFontTextSourceSetDefaultFont
void shim_sceFontTextSourceSetDefaultFont(GuestContext *ctx) {
    uint64_t sourceGuest = ctx->rdi;
    uint64_t fontGuest = ctx->rsi;
    if (sourceGuest && ctx->mem_base) {
        OrbisFontTextSource *src = (OrbisFontTextSource *)(ctx->mem_base + sourceGuest);
        src->defaultFont = (OrbisFontHandle)(uintptr_t)fontGuest;
    }
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 20. sceFontTextSourceSetWritingForm
void shim_sceFontTextSourceSetWritingForm(GuestContext *ctx) {
    uint64_t sourceGuest = ctx->rdi;
    uint32_t form = (uint32_t)ctx->rsi;
    if (sourceGuest && ctx->mem_base) {
        OrbisFontTextSource *src = (OrbisFontTextSource *)(ctx->mem_base + sourceGuest);
        src->systemUse0 = ((uint64_t)form << 32) | (src->systemUse0 & 0xFFFFFFFFULL);
    }
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 21. sceFontCreateString
void shim_sceFontCreateString(GuestContext *ctx) {
    uint64_t memGuest = ctx->rdi;
    uint64_t srcGuest = ctx->rsi;
    uint64_t detailGuest = ctx->rdx;
    uint64_t pStringGuest = ctx->rcx;

    (void)detailGuest;
    if (!memGuest || !srcGuest || !pStringGuest || !ctx->mem_base) {
        if (pStringGuest && ctx->mem_base) {
            *(uint64_t *)(ctx->mem_base + pStringGuest) = 0;
        }
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    OrbisFontTextSource *src = (OrbisFontTextSource *)(ctx->mem_base + srcGuest);
    uint64_t strGuest = recomp_vm_alloc_named(ctx, 0x10000, PROT_READ | PROT_WRITE, 0, "font_string");
    if (!strGuest) {
        *(uint64_t *)(ctx->mem_base + pStringGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_ALLOCATION_FAILED;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_font_mutex);
    InternalString *s = find_or_create_string(strGuest);
    if (s) {
        s->memory_guest = memGuest;
        s->source_guest = srcGuest;
        s->default_font_guest = (uint64_t)(uintptr_t)src->defaultFont;
        s->writing_form = (uint32_t)(src->systemUse0 >> 32);
        s->character_count = 0;
        s->head_node_guest = 0;

        // Parse text if parser is provided
        if (src->textParser && src->start) {
            uint64_t parserFn = (uint64_t)(uintptr_t)src->textParser;
            uint64_t nodeAllocGuest = strGuest + 0x200;
            uint64_t prevNodeGuest = 0;
            uint64_t orderPtrGuest = strGuest + 0x80;
            uint64_t parseResultGuest = strGuest + 0x100;

            *(uint64_t *)(ctx->mem_base + orderPtrGuest) = 0;
            memset(ctx->mem_base + parseResultGuest, 0, sizeof(OrbisFontTextParseResult));

            pthread_mutex_unlock(&g_font_mutex);

            for (uint32_t i = 0; i < 256; i++) {
                memset(ctx->mem_base + parseResultGuest, 0, sizeof(OrbisFontTextParseResult));

                GuestContext sub = *ctx;
                sub.rdi = srcGuest;
                sub.rsi = orderPtrGuest;
                sub.rdx = parseResultGuest;
                recomp_call_guest(&sub, parserFn);

                int32_t state = (int32_t)sub.rax;
                if (state == 1) { // kTextParserResultFontCode
                    OrbisFontTextParseResult *res = (OrbisFontTextParseResult *)(ctx->mem_base + parseResultGuest);
                    uint64_t order = *(uint64_t *)(ctx->mem_base + orderPtrGuest);
                    uint64_t nodeGuest = nodeAllocGuest + (i * sizeof(OrbisFontTextCharacter));

                    OrbisFontTextCharacter *node = (OrbisFontTextCharacter *)(ctx->mem_base + nodeGuest);
                    memset(node, 0, sizeof(*node));
                    node->prev = prevNodeGuest ? (OrbisFontTextCharacter *)(uintptr_t)prevNodeGuest : NULL;
                    node->next = NULL;
                    node->textOrder = (void *)(uintptr_t)order;
                    node->font = res->FontCode.font ? res->FontCode.font : (OrbisFontHandle)(uintptr_t)s->default_font_guest;
                    node->shapeEntry = NULL;
                    node->characterCode = res->FontCode.code;
                    node->clusterSpan = 1;
                    node->clusterIndex = 0;
                    node->clusterKind = 0;
                    node->synthetic = 0;
                    uint32_t code = res->FontCode.code;
                    bool is_ws = (code == 0x20 || code == 0x09 || code == 0x0a || code == 0x0b || code == 0x0c || code == 0x0d || code == 0x3000 || (code >= 0x2000 && code <= 0x200a));
                    node->flags = (is_ws ? 0x0EULL : 0ULL) << 8;

                    if (prevNodeGuest) {
                        OrbisFontTextCharacter *prev = (OrbisFontTextCharacter *)(ctx->mem_base + prevNodeGuest);
                        prev->next = (OrbisFontTextCharacter *)(uintptr_t)nodeGuest;
                    } else {
                        s->head_node_guest = nodeGuest;
                    }
                    prevNodeGuest = nodeGuest;
                    s->character_count++;
                } else if (state == 0) { // kTextParserResultTerminate
                    OrbisFontTextParseResult *res = (OrbisFontTextParseResult *)(ctx->mem_base + parseResultGuest);
                    s->terminate_code = res->Terminate.terminateCode;
                    s->terminate_order = *(uint64_t *)(ctx->mem_base + orderPtrGuest);
                    break;
                } else {
                    break;
                }
            }

            pthread_mutex_lock(&g_font_mutex);
        }
        fprintf(stderr, "[ps4-font] sceFontCreateString: src=0x%llx parser=0x%llx count=%u\n",
                (unsigned long long)srcGuest, (unsigned long long)(src ? (uintptr_t)src->textParser : 0),
                s ? s->character_count : 0);
    }
    pthread_mutex_unlock(&g_font_mutex);

    *(uint64_t *)(ctx->mem_base + pStringGuest) = strGuest;
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 22. sceFontDestroyString
void shim_sceFontDestroyString(GuestContext *ctx) {
    uint64_t pStringGuest = ctx->rdi;
    if (pStringGuest && ctx->mem_base) {
        uint64_t strGuest = *(uint64_t *)(ctx->mem_base + pStringGuest);
        pthread_mutex_lock(&g_font_mutex);
        InternalString *s = get_string(strGuest);
        if (s) {
            s->in_use = 0;
        }
        pthread_mutex_unlock(&g_font_mutex);
        if (strGuest) {
            recomp_vm_free(ctx, strGuest, 0x10000);
        }
        *(uint64_t *)(ctx->mem_base + pStringGuest) = 0;
    }
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 23. sceFontStringRefersTextCharacters
void shim_sceFontStringRefersTextCharacters(GuestContext *ctx) {
    uint64_t strGuest = ctx->rdi;
    uint64_t pCountGuest = ctx->rsi;

    pthread_mutex_lock(&g_font_mutex);
    InternalString *s = get_string(strGuest);
    uint64_t head = 0;
    uint32_t count = 0;
    if (s) {
        head = s->head_node_guest;
        count = s->character_count;
    }
    pthread_mutex_unlock(&g_font_mutex);

    if (pCountGuest && ctx->mem_base) {
        *(uint32_t *)(ctx->mem_base + pCountGuest) = count;
    }
    ctx->rax = head;
    SHIM_RETURN();
}

// 24. sceFontStringGetTerminateOrder
void shim_sceFontStringGetTerminateOrder(GuestContext *ctx) {
    uint64_t strGuest = ctx->rdi;
    pthread_mutex_lock(&g_font_mutex);
    InternalString *s = get_string(strGuest);
    uint64_t order = s ? s->terminate_order : 0;
    pthread_mutex_unlock(&g_font_mutex);
    ctx->rax = order;
    SHIM_RETURN();
}

// 25. sceFontStringGetTerminateCode
void shim_sceFontStringGetTerminateCode(GuestContext *ctx) {
    uint64_t strGuest = ctx->rdi;
    pthread_mutex_lock(&g_font_mutex);
    InternalString *s = get_string(strGuest);
    uint32_t code = s ? s->terminate_code : 0;
    pthread_mutex_unlock(&g_font_mutex);
    ctx->rax = code;
    SHIM_RETURN();
}

// 26. sceFontStringRefersRenderCharacters
void shim_sceFontStringRefersRenderCharacters(GuestContext *ctx) {
    uint64_t strGuest = ctx->rdi;
    uint64_t startCharGuest = ctx->rsi;
    uint64_t lastCharGuest = ctx->rdx;
    uint64_t pCountGuest = ctx->rcx;

    pthread_mutex_lock(&g_font_mutex);
    InternalString *s = get_string(strGuest);
    uint64_t head = s ? s->head_node_guest : 0;
    uint64_t retNode = startCharGuest ? startCharGuest : head;
    uint32_t count = 0;

    if (s && retNode && ctx->mem_base) {
        uint64_t curr = retNode;
        while (curr) {
            OrbisFontTextCharacter *node = (OrbisFontTextCharacter *)(ctx->mem_base + curr);
            if (node->clusterIndex == 0) {
                count++;
            }
            if (curr == lastCharGuest) {
                break;
            }
            curr = (uint64_t)(uintptr_t)node->next;
        }
        if (lastCharGuest && curr != lastCharGuest) {
            count = 0;
            retNode = 0;
        }
    }
    pthread_mutex_unlock(&g_font_mutex);

    if (pCountGuest && ctx->mem_base) {
        *(uint32_t *)(ctx->mem_base + pCountGuest) = count;
    }
    fprintf(stderr, "[ps4-font] sceFontStringRefersRenderCharacters: str=0x%llx count=%u ret=0x%llx start=0x%llx last=0x%llx\n",
            (unsigned long long)strGuest, count, (unsigned long long)retNode,
            (unsigned long long)startCharGuest, (unsigned long long)lastCharGuest);
    ctx->rax = retNode;
    SHIM_RETURN();
}

// 27. sceFontStringGetWritingForm
void shim_sceFontStringGetWritingForm(GuestContext *ctx) {
    uint64_t strGuest = ctx->rdi;
    pthread_mutex_lock(&g_font_mutex);
    InternalString *s = get_string(strGuest);
    uint32_t form = s ? s->writing_form : 0;
    pthread_mutex_unlock(&g_font_mutex);
    ctx->rax = form;
    SHIM_RETURN();
}

// 28. sceFontCreateWritingLine
void shim_sceFontCreateWritingLine(GuestContext *ctx) {
    uint64_t pWritingLineGuest = ctx->rdx;
    if (pWritingLineGuest && ctx->mem_base) {
        uint64_t lineGuest = recomp_vm_alloc_named(ctx, 0x1000, PROT_READ | PROT_WRITE, 0, "font_writing_line");
        *(uint64_t *)(ctx->mem_base + pWritingLineGuest) = lineGuest;
    }
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 29. sceFontDestroyWritingLine
void shim_sceFontDestroyWritingLine(GuestContext *ctx) {
    uint64_t pWritingLineGuest = ctx->rdi;
    if (pWritingLineGuest && ctx->mem_base) {
        *(uint64_t *)(ctx->mem_base + pWritingLineGuest) = 0;
    }
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 30. sceFontWritingLineClear
void shim_sceFontWritingLineClear(GuestContext *ctx) {
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 31. sceFontWritingLineGetOrderingSpace
void shim_sceFontWritingLineGetOrderingSpace(GuestContext *ctx) {
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 32. sceFontWritingLineGetRenderMetrics
void shim_sceFontWritingLineGetRenderMetrics(GuestContext *ctx) {
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 33. sceFontWritingLineWritesOrder
void shim_sceFontWritingLineWritesOrder(GuestContext *ctx) {
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 34. sceFontWritingLineRefersRenderStep
void shim_sceFontWritingLineRefersRenderStep(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// 35. sceFontWritingInit
void shim_sceFontWritingInit(GuestContext *ctx) {
    uint64_t writingGuest = ctx->rdi;
    uint64_t stringGuest = ctx->rsi;
    uint64_t startCharGuest = ctx->rdx;

    pthread_mutex_lock(&g_font_mutex);
    InternalWriting *w = find_or_create_writing(writingGuest);
    if (w) {
        w->string_guest = stringGuest;
        InternalString *s = get_string(stringGuest);
        w->current_node_guest = startCharGuest ? startCharGuest : (s ? s->head_node_guest : 0);
        w->step_char_node_guest = 0;
        w->current_index = 0;
        w->cursor_x = 0.0f;
        w->cursor_y = 0.0f;
    }
    pthread_mutex_unlock(&g_font_mutex);

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 36. sceFontWritingGetRenderMetrics
void shim_sceFontWritingGetRenderMetrics(GuestContext *ctx) {
    uint64_t writingGuest = ctx->rdi;
    uint64_t metricsGuest = ctx->rsi;

    if (metricsGuest && ctx->mem_base) {
        OrbisFontWritingMetrics *m = (OrbisFontWritingMetrics *)(ctx->mem_base + metricsGuest);
        memset(m, 0, sizeof(*m));

        pthread_mutex_lock(&g_font_mutex);
        InternalWriting *w = get_writing(writingGuest);
        if (w) {
            InternalString *s = get_string(w->string_guest);
            float totalAdvance = (s ? (float)s->character_count : 0.0f) * 16.0f;
            m->advanceX = totalAdvance;
            m->advanceY = 0.0f;
            m->Extent.left = 0.0f;
            m->Extent.right = totalAdvance;
            m->Extent.top = 16.0f;
            m->Extent.bottom = 0.0f;
        }
        pthread_mutex_unlock(&g_font_mutex);
    }

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 37. sceFontWritingRefersRenderStep
void shim_sceFontWritingRefersRenderStep(GuestContext *ctx) {
    uint64_t writingGuest = ctx->rdi;
    uint64_t retStepGuest = 0;

    pthread_mutex_lock(&g_font_mutex);
    InternalWriting *w = get_writing(writingGuest);
    if (w && w->current_node_guest && ctx->mem_base) {
        OrbisFontTextCharacter *node = (OrbisFontTextCharacter *)(ctx->mem_base + w->current_node_guest);
        InternalFontHandle *h = get_font((uint64_t)(uintptr_t)node->font);

        float adv = h ? h->scale_w : 16.0f;
        memset(&w->current_step, 0, sizeof(w->current_step));
        w->current_step.x = w->cursor_x;
        w->current_step.y = w->cursor_y;
        w->current_step.advanceX = adv;
        w->current_step.advanceY = 0.0f;
        w->current_step.font = node->font;
        w->current_step.glyphCode = node->characterCode;
        w->current_step.Profile.characterCount = 1;
        w->current_step.Positioning.x = w->cursor_x;
        w->current_step.Positioning.y = w->cursor_y;
        w->current_step.GlyphMetrics.width = adv;
        w->current_step.GlyphMetrics.height = h ? h->scale_h : 16.0f;
        w->current_step.GlyphMetrics.Horizontal.advance = adv;
        w->current_step.GlyphMetrics.Horizontal.bearingX = 0.0f;
        w->current_step.GlyphMetrics.Horizontal.bearingY = 0.0f;

        w->cursor_x += adv;
        w->step_char_node_guest = w->current_node_guest;
        w->current_node_guest = (uint64_t)(uintptr_t)node->next;
        w->current_index++;

        // Step buffer inside writing guest memory at offset 0x80
        retStepGuest = writingGuest + 0x80;
        memcpy(ctx->mem_base + retStepGuest, &w->current_step, sizeof(OrbisFontWritingStep));
    }
    pthread_mutex_unlock(&g_font_mutex);

    ctx->rax = retStepGuest;
    SHIM_RETURN();
}

// 38. sceFontWritingRefersRenderStepCharacter
void shim_sceFontWritingRefersRenderStepCharacter(GuestContext *ctx) {
    uint64_t writingGuest = ctx->rdi;
    uint64_t letterStepGuest = ctx->rsi;
    uint64_t retCharGuest = 0;

    pthread_mutex_lock(&g_font_mutex);
    InternalWriting *w = get_writing(writingGuest);
    if (w && ctx->mem_base) {
        retCharGuest = w->step_char_node_guest;
        if (letterStepGuest) {
            OrbisFontWritingLetterStep *ls = (OrbisFontWritingLetterStep *)(ctx->mem_base + letterStepGuest);
            memset(ls, 0, sizeof(*ls));
            ls->x = w->current_step.x;
            ls->y = w->current_step.y;
            ls->advanceX = w->current_step.advanceX;
            ls->advanceY = w->current_step.advanceY;
            ls->Components.textsCount = 1;
            ls->Components.textsIndex = w->current_index > 0 ? w->current_index - 1 : 0;
            ls->Components.glyphsCount = 1;
            ls->Components.glyphsIndex = ls->Components.textsIndex;
            ls->Components.characterTextCount = 1;
            ls->Components.baseTextCount = 1;
        }
    }
    pthread_mutex_unlock(&g_font_mutex);

    ctx->rax = retCharGuest;
    SHIM_RETURN();
}

// 39. sceFontCharacterGetBidiLevel
void shim_sceFontCharacterGetBidiLevel(GuestContext *ctx) {
    uint64_t bidiGuest = ctx->rsi;
    if (bidiGuest && ctx->mem_base) {
        *(int32_t *)(ctx->mem_base + bidiGuest) = 0;
    }
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 40. sceFontCharacterGetSyllableStringState
void shim_sceFontCharacterGetSyllableStringState(GuestContext *ctx) {
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 41. sceFontCharacterGetTextFontCode
void shim_sceFontCharacterGetTextFontCode(GuestContext *ctx) {
    uint64_t nodeGuest = ctx->rdi;
    uint64_t pFontGuest = ctx->rsi;
    uint64_t pCodeGuest = ctx->rdx;

    if (!nodeGuest || !pFontGuest || !pCodeGuest || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    OrbisFontTextCharacter *node = (OrbisFontTextCharacter *)(ctx->mem_base + nodeGuest);
    *(uint64_t *)(ctx->mem_base + pFontGuest) = (uint64_t)(uintptr_t)node->font;
    *(uint32_t *)(ctx->mem_base + pCodeGuest) = node->characterCode;

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 42. sceFontCharacterGetTextOrder
void shim_sceFontCharacterGetTextOrder(GuestContext *ctx) {
    uint64_t nodeGuest = ctx->rdi;
    uint64_t pOrderGuest = ctx->rsi;

    if (!nodeGuest || !pOrderGuest || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    OrbisFontTextCharacter *node = (OrbisFontTextCharacter *)(ctx->mem_base + nodeGuest);
    *(uint64_t *)(ctx->mem_base + pOrderGuest) = (uint64_t)(uintptr_t)node->textOrder;

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 43. sceFontCharacterLooksWhiteSpace
void shim_sceFontCharacterLooksWhiteSpace(GuestContext *ctx) {
    uint64_t nodeGuest = ctx->rdi;
    uint32_t ret = 0;
    if (nodeGuest && ctx->mem_base) {
        OrbisFontTextCharacter *node = (OrbisFontTextCharacter *)(ctx->mem_base + nodeGuest);
        if (node->characterCode == ' ' || node->characterCode == '\t' || node->characterCode == 0x3000) {
            ret = node->characterCode;
        }
    }
    ctx->rax = ret;
    SHIM_RETURN();
}

// 44. sceFontCharacterRefersTextNext
void shim_sceFontCharacterRefersTextNext(GuestContext *ctx) {
    uint64_t nodeGuest = ctx->rdi;
    uint64_t next = 0;
    if (nodeGuest && ctx->mem_base) {
        OrbisFontTextCharacter *node = (OrbisFontTextCharacter *)(ctx->mem_base + nodeGuest);
        next = (uint64_t)(uintptr_t)node->next;
    }
    ctx->rax = next;
    SHIM_RETURN();
}

// 45. sceFontCharactersRefersTextCodes
void shim_sceFontCharactersRefersTextCodes(GuestContext *ctx) {
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 46. sceFontTextCodesStepNext
void shim_sceFontTextCodesStepNext(GuestContext *ctx) {
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 47. sceFontWordsFindWordCharacters
void shim_sceFontWordsFindWordCharacters(GuestContext *ctx) {
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

static int32_t font_register_opened_handle(GuestContext *ctx, uint64_t libGuest,
                                           uint64_t preallocFontGuest, FT_Face face,
                                           FontBacking *backing, const InternalFontHandle *copy_from,
                                           uint64_t *out_handle_guest) {
    uint64_t fontGuest = preallocFontGuest;
    if (!fontGuest) {
        fontGuest = recomp_vm_alloc_named(ctx, 0x1000, PROT_READ | PROT_WRITE, 0, "font_handle");
        if (!fontGuest) {
            return ORBIS_FONT_ERROR_ALLOCATION_FAILED;
        }
    }
    uint8_t *hPtr = (uint8_t *)(ctx->mem_base + fontGuest);
    memset(hPtr, 0, 0x100);
    *(uint16_t *)(hPtr + 0x00) = 0x0F02;
    *(uint64_t *)(hPtr + 0x28) = libGuest;

    InternalFontHandle *h = find_or_create_font(fontGuest);
    if (!h) {
        return ORBIS_FONT_ERROR_ALLOCATION_FAILED;
    }
    h->library_guest = libGuest;
    h->ft_face = face;
    h->backing = backing;
    if (copy_from) {
        h->scale_unit = copy_from->scale_unit;
        h->scale_w = copy_from->scale_w;
        h->scale_h = copy_from->scale_h;
        h->render_scale_w = copy_from->render_scale_w;
        h->render_scale_h = copy_from->render_scale_h;
        h->weight_x = copy_from->weight_x;
        h->weight_y = copy_from->weight_y;
        h->slant = copy_from->slant;
        h->render_weight_x = copy_from->render_weight_x;
        h->render_weight_y = copy_from->render_weight_y;
        h->render_slant = copy_from->render_slant;
        h->dpi_x = copy_from->dpi_x;
        h->dpi_y = copy_from->dpi_y;
    }
    font_apply_ft_size(h, 0);
    *out_handle_guest = fontGuest;
    return ORBIS_FONT_OK;
}

static void font_render_glyph_common(GuestContext *ctx, int add_baseline) {
    uint64_t fontGuest = ctx->rdi;
    uint32_t code = (uint32_t)ctx->rsi;
    uint64_t surfGuest = ctx->rdx;
    float x = ctx->xmm[0].f32[0];
    float y = ctx->xmm[1].f32[0];
    uint64_t metricsGuest = ctx->rcx;
    uint64_t resultGuest = ctx->r8;

    (void)resultGuest;

    if (!fontGuest || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    if (code == 0) {
        if (metricsGuest) memset(ctx->mem_base + metricsGuest, 0, sizeof(OrbisFontGlyphMetrics));
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_NO_SUPPORT_CODE;
        SHIM_RETURN();
    }
    if (!surfGuest) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (!h) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }

    float adv = h->render_scale_w;
    float hgt = h->render_scale_h;
    float gw = adv;
    float gh = hgt;
    float bx = 0.0f;
    float by = hgt;

    if (h->ft_face) {
        font_apply_ft_size(h, 1);
        float active_slant = h->render_slant != 0.0f ? h->render_slant : h->slant;
        if (active_slant != 0.0f) {
            FT_Matrix mat;
            mat.xx = 0x10000L;
            mat.xy = (FT_Fixed)(active_slant * 65536.0f);
            mat.yx = 0;
            mat.yy = 0x10000L;
            FT_Set_Transform(h->ft_face, &mat, NULL);
        } else {
            FT_Set_Transform(h->ft_face, NULL, NULL);
        }

        float y_used = y;
        if (add_baseline) {
            float asc = (float)(h->ft_face->size->metrics.ascender >> 6);
            if (asc <= 0.0f) asc = hgt * 0.8f;
            y_used = y + asc;
        }

        FT_UInt glyph_idx = FT_Get_Char_Index(h->ft_face, code);
        if (glyph_idx != 0 && FT_Load_Glyph(h->ft_face, glyph_idx, FT_LOAD_DEFAULT) == 0) {
            FT_Render_Glyph(h->ft_face->glyph, FT_RENDER_MODE_NORMAL);
            FT_GlyphSlot slot = h->ft_face->glyph;
            adv = (float)(slot->advance.x >> 6);
            if (adv <= 0.0f) adv = h->render_scale_w;
            gw = (float)(slot->metrics.width >> 6);
            gh = (float)(slot->metrics.height >> 6);
            bx = (float)slot->bitmap_left;
            by = (float)slot->bitmap_top;

            OrbisFontRenderSurface *surf = (OrbisFontRenderSurface *)(ctx->mem_base + surfGuest);
            if (surf && surf->buffer && slot->bitmap.buffer) {
                int sx0 = (int)surf->sc_x0;
                int sy0 = (int)surf->sc_y0;
                int sx1 = (int)surf->sc_x1;
                int sy1 = (int)surf->sc_y1;
                if (sx0 < 0) sx0 = 0;
                if (sy0 < 0) sy0 = 0;
                if (sx1 > surf->width) sx1 = surf->width;
                if (sy1 > surf->height) sy1 = surf->height;

                int dst_x0 = (int)x + slot->bitmap_left;
                int dst_y0 = (int)y_used - slot->bitmap_top;
                uint8_t *dst_buf = (uint8_t *)(ctx->mem_base + (uintptr_t)surf->buffer);
                int bpp = surf->pixelSizeByte > 0 ? surf->pixelSizeByte : 4;

                for (unsigned int row = 0; row < slot->bitmap.rows; row++) {
                    int py = dst_y0 + (int)row;
                    if (py < sy0 || py >= sy1) continue;
                    for (unsigned int col = 0; col < slot->bitmap.width; col++) {
                        int px = dst_x0 + (int)col;
                        if (px < sx0 || px >= sx1) continue;
                        uint8_t alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                        if (alpha == 0) continue;

                        uint8_t *dst_pixel = dst_buf + py * surf->widthByte + px * bpp;
                        if (bpp == 4) {
                            dst_pixel[0] = alpha;
                            dst_pixel[1] = alpha;
                            dst_pixel[2] = alpha;
                            dst_pixel[3] = alpha;
                        } else if (bpp == 1) {
                            dst_pixel[0] = alpha;
                        }
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&g_font_mutex);

    if (metricsGuest) {
        OrbisFontGlyphMetrics *m = (OrbisFontGlyphMetrics *)(ctx->mem_base + metricsGuest);
        memset(m, 0, sizeof(*m));
        m->width = gw;
        m->height = gh;
        m->Horizontal.advance = adv;
        m->Horizontal.bearingX = bx;
        m->Horizontal.bearingY = by;
    }

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 48. sceFontRenderCharGlyphImageHorizontal
void shim_sceFontRenderCharGlyphImageHorizontal(GuestContext *ctx) {
    font_render_glyph_common(ctx, 0);
}

// 49. sceFontRenderCharGlyphImage
void shim_sceFontRenderCharGlyphImage(GuestContext *ctx) {
    font_render_glyph_common(ctx, 1);
}

// 50. sceFontOpenFontFile
void shim_sceFontOpenFontFile(GuestContext *ctx) {
    uint64_t libGuest = ctx->rdi;
    uint64_t pathGuest = ctx->rsi;
    uint32_t openMode = (uint32_t)ctx->rdx;
    uint64_t openDetailGuest = ctx->rcx;
    uint64_t pFontHandleGuest = ctx->r8;
    (void)openMode;

    if (!libGuest || !ctx->mem_base) {
        if (pFontHandleGuest && ctx->mem_base) *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_LIBRARY;
        SHIM_RETURN();
    }
    if (!pFontHandleGuest || !pathGuest) {
        if (pFontHandleGuest) *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    const char *guestPath = (const char *)(ctx->mem_base + pathGuest);
    uint32_t subfont_index = 0;
    if (openDetailGuest) {
        const OrbisFontOpenParams *params = (const OrbisFontOpenParams *)(ctx->mem_base + openDetailGuest);
        subfont_index = params->subfont_index;
    }

    uint64_t prealloc = *(uint64_t *)(ctx->mem_base + pFontHandleGuest);
    uint8_t *fileBuf = NULL;
    size_t fileSize = 0;

    int fd = ps4_vfs_open(guestPath, O_RDONLY, 0);
    if (fd >= 0) {
        struct stat st;
        if (ps4_vfs_fstat(fd, &st) == 0 && st.st_size > 0) {
            fileSize = (size_t)st.st_size;
            fileBuf = (uint8_t *)malloc(fileSize);
            if (fileBuf) {
                size_t got = 0;
                while (got < fileSize) {
                    ssize_t r = ps4_vfs_read(fd, fileBuf + got, fileSize - got);
                    if (r <= 0) break;
                    got += (size_t)r;
                }
                if (got != fileSize) {
                    free(fileBuf);
                    fileBuf = NULL;
                }
            }
        }
        ps4_vfs_close(fd);
    }

    pthread_mutex_lock(&g_font_mutex);
    if (!g_ft_library) {
        FT_Init_FreeType(&g_ft_library);
    }
    FT_Face face = NULL;
    FontBacking *backing = NULL;
    if (fileBuf) {
        if (FT_New_Memory_Face(g_ft_library, fileBuf, (FT_Long)fileSize, (FT_Long)subfont_index, &face) == 0) {
            backing = (FontBacking *)calloc(1, sizeof(FontBacking));
            if (backing) {
                backing->data = fileBuf;
                backing->size = fileSize;
                backing->subfont_index = subfont_index;
                backing->refs = 1;
            } else {
                FT_Done_Face(face);
                face = NULL;
                free(fileBuf);
            }
        } else {
            free(fileBuf);
        }
    }
    if (!face) {
        pthread_mutex_unlock(&g_font_mutex);
        *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        fprintf(stderr, "[ps4-font] sceFontOpenFontFile failed: path=%s\n", guestPath);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_FS_OPEN_FAILED;
        SHIM_RETURN();
    }

    uint64_t outHandle = 0;
    int32_t rc = font_register_opened_handle(ctx, libGuest, prealloc, face, backing, NULL, &outHandle);
    if (rc != ORBIS_FONT_OK) {
        FT_Done_Face(face);
        backing_release(backing);
        outHandle = 0;
    }
    pthread_mutex_unlock(&g_font_mutex);

    *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = outHandle;
    fprintf(stderr, "[ps4-font] sceFontOpenFontFile: path=%s sub=%u -> handle=0x%llx (rc=0x%x)\n",
            guestPath, subfont_index, (unsigned long long)outHandle, (unsigned int)rc);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// 51. sceFontOpenFontMemory
void shim_sceFontOpenFontMemory(GuestContext *ctx) {
    uint64_t libGuest = ctx->rdi;
    uint64_t fontAddr = ctx->rsi;
    uint32_t fontSize = (uint32_t)ctx->rdx;
    uint64_t openParamsGuest = ctx->rcx;
    uint64_t pFontHandleGuest = ctx->r8;

    if (!libGuest || !ctx->mem_base) {
        if (pFontHandleGuest && ctx->mem_base) *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_LIBRARY;
        SHIM_RETURN();
    }
    if (!fontAddr || fontSize == 0 || !pFontHandleGuest || fontAddr >= ctx->mem_size ||
        fontSize > ctx->mem_size - fontAddr) {
        if (pFontHandleGuest) *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    uint32_t subfont_index = 0;
    if (openParamsGuest) {
        const OrbisFontOpenParams *params = (const OrbisFontOpenParams *)(ctx->mem_base + openParamsGuest);
        subfont_index = params->subfont_index;
    }

    uint8_t *memCopy = (uint8_t *)malloc(fontSize);
    if (!memCopy) {
        *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_ALLOCATION_FAILED;
        SHIM_RETURN();
    }
    memcpy(memCopy, ctx->mem_base + fontAddr, fontSize);

    uint64_t prealloc = *(uint64_t *)(ctx->mem_base + pFontHandleGuest);

    pthread_mutex_lock(&g_font_mutex);
    if (!g_ft_library) {
        FT_Init_FreeType(&g_ft_library);
    }
    FT_Face face = NULL;
    FontBacking *backing = NULL;
    if (FT_New_Memory_Face(g_ft_library, memCopy, (FT_Long)fontSize, (FT_Long)subfont_index, &face) == 0) {
        backing = (FontBacking *)calloc(1, sizeof(FontBacking));
        if (backing) {
            backing->data = memCopy;
            backing->size = fontSize;
            backing->subfont_index = subfont_index;
            backing->refs = 1;
        } else {
            FT_Done_Face(face);
            face = NULL;
            free(memCopy);
        }
    } else {
        free(memCopy);
    }

    if (!face) {
        pthread_mutex_unlock(&g_font_mutex);
        *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_FONT_OPEN_FAILED;
        SHIM_RETURN();
    }

    uint64_t outHandle = 0;
    int32_t rc = font_register_opened_handle(ctx, libGuest, prealloc, face, backing, NULL, &outHandle);
    if (rc != ORBIS_FONT_OK) {
        FT_Done_Face(face);
        backing_release(backing);
        outHandle = 0;
    }
    pthread_mutex_unlock(&g_font_mutex);

    *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = outHandle;
    fprintf(stderr, "[ps4-font] sceFontOpenFontMemory: addr=0x%llx size=%u sub=%u -> handle=0x%llx\n",
            (unsigned long long)fontAddr, fontSize, subfont_index, (unsigned long long)outHandle);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// 52. sceFontOpenFontInstance
void shim_sceFontOpenFontInstance(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    uint64_t setupFontGuest = ctx->rsi;
    uint64_t pFontHandleGuest = ctx->rdx;

    if (!fontGuest || !ctx->mem_base) {
        if (pFontHandleGuest && ctx->mem_base) *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    if (!setupFontGuest && !pFontHandleGuest) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *src = get_font(fontGuest);
    if (!src) {
        pthread_mutex_unlock(&g_font_mutex);
        if (pFontHandleGuest) *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = 0;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }

    InternalFontHandle copy_cfg = *src;
    FT_Face new_face = NULL;
    FontBacking *backing = NULL;
    if (src->backing && g_ft_library) {
        if (FT_New_Memory_Face(g_ft_library, src->backing->data, (FT_Long)src->backing->size,
                               (FT_Long)src->backing->subfont_index, &new_face) == 0) {
            backing = backing_retain(src->backing);
        }
    }
    if (!new_face && src->ft_face) {
        FT_Reference_Face(src->ft_face);
        new_face = src->ft_face;
    }

    uint64_t outHandle = 0;
    int32_t rc = font_register_opened_handle(ctx, copy_cfg.library_guest, setupFontGuest,
                                             new_face, backing, &copy_cfg, &outHandle);
    if (rc != ORBIS_FONT_OK) {
        if (new_face) FT_Done_Face(new_face);
        backing_release(backing);
        outHandle = 0;
    }
    pthread_mutex_unlock(&g_font_mutex);

    if (pFontHandleGuest) {
        *(uint64_t *)(ctx->mem_base + pFontHandleGuest) = outHandle;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// 53. sceFontSetResolutionDpi
void shim_sceFontSetResolutionDpi(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    uint32_t h_dpi = (uint32_t)ctx->rsi;
    uint32_t v_dpi = (uint32_t)ctx->rdx;

    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (!h) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    h->dpi_x = h_dpi ? h_dpi : 72;
    h->dpi_y = v_dpi ? v_dpi : 72;
    font_apply_ft_size(h, 0);
    pthread_mutex_unlock(&g_font_mutex);

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 54. sceFontSetScalePoint
void shim_sceFontSetScalePoint(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    float w = ctx->xmm[0].f32[0];
    float h_pt = ctx->xmm[1].f32[0];

    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (!h) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    h->scale_unit = 1;
    h->scale_w = w > 0.0f ? w : 16.0f;
    h->scale_h = h_pt > 0.0f ? h_pt : 16.0f;
    font_apply_ft_size(h, 0);
    pthread_mutex_unlock(&g_font_mutex);

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 55. sceFontGetScalePixel
void shim_sceFontGetScalePixel(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    uint64_t outWGuest = ctx->rsi;
    uint64_t outHGuest = ctx->rdx;

    if (!fontGuest || !ctx->mem_base) {
        if (outWGuest && ctx->mem_base) *(float *)(ctx->mem_base + outWGuest) = 0.0f;
        if (outHGuest && ctx->mem_base) *(float *)(ctx->mem_base + outHGuest) = 0.0f;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    if (!outWGuest && !outHGuest) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (!h) {
        pthread_mutex_unlock(&g_font_mutex);
        if (outWGuest) *(float *)(ctx->mem_base + outWGuest) = 0.0f;
        if (outHGuest) *(float *)(ctx->mem_base + outHGuest) = 0.0f;
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    float px_w = 0.0f, px_h = 0.0f;
    font_compute_pixel_scale(h, 0, &px_w, &px_h);
    pthread_mutex_unlock(&g_font_mutex);

    if (outWGuest) *(float *)(ctx->mem_base + outWGuest) = px_w;
    if (outHGuest) *(float *)(ctx->mem_base + outHGuest) = px_h;
    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 56. sceFontSetEffectWeight
void shim_sceFontSetEffectWeight(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    float weightX = ctx->xmm[0].f32[0];
    float weightY = ctx->xmm[1].f32[0];
    uint32_t mode = (uint32_t)ctx->rsi;

    if (mode != 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }
    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (!h) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    h->weight_x = weightX;
    h->weight_y = weightY;
    pthread_mutex_unlock(&g_font_mutex);

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 57. sceFontSetEffectSlant
void shim_sceFontSetEffectSlant(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    float slantRatio = ctx->xmm[0].f32[0];

    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (!h) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    h->slant = slantRatio;
    pthread_mutex_unlock(&g_font_mutex);

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 58. sceFontSetupRenderEffectWeight
void shim_sceFontSetupRenderEffectWeight(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    float weightX = ctx->xmm[0].f32[0];
    float weightY = ctx->xmm[1].f32[0];
    uint32_t mode = (uint32_t)ctx->rsi;

    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (!h) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    if (!h->renderer_guest) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_NOT_BOUND_RENDERER;
        SHIM_RETURN();
    }
    if (mode != 0) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }
    h->render_weight_x = weightX;
    h->render_weight_y = weightY;
    pthread_mutex_unlock(&g_font_mutex);

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 59. sceFontSetupRenderEffectSlant
void shim_sceFontSetupRenderEffectSlant(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    float slantRatio = ctx->xmm[0].f32[0];

    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (!h) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    if (!h->renderer_guest) {
        pthread_mutex_unlock(&g_font_mutex);
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_NOT_BOUND_RENDERER;
        SHIM_RETURN();
    }
    h->render_slant = slantRatio;
    pthread_mutex_unlock(&g_font_mutex);

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 60. sceFontGetHorizontalLayout
void shim_sceFontGetHorizontalLayout(GuestContext *ctx) {
    uint64_t fontGuest = ctx->rdi;
    uint64_t layoutGuest = ctx->rsi;

    if (!fontGuest || !ctx->mem_base) {
        if (layoutGuest && ctx->mem_base) memset(ctx->mem_base + layoutGuest, 0, sizeof(OrbisFontHorizontalLayout));
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }
    if (!layoutGuest) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_font_mutex);
    InternalFontHandle *h = get_font(fontGuest);
    if (!h) {
        pthread_mutex_unlock(&g_font_mutex);
        memset(ctx->mem_base + layoutGuest, 0, sizeof(OrbisFontHorizontalLayout));
        ctx->rax = (uint64_t)(int64_t)ORBIS_FONT_ERROR_INVALID_FONT_HANDLE;
        SHIM_RETURN();
    }

    float px_w = 16.0f, px_h = 16.0f;
    font_compute_pixel_scale(h, 0, &px_w, &px_h);
    float asc = px_h * 0.8f;
    float desc = -px_h * 0.2f;
    float line_adv = px_h;

    if (h->ft_face) {
        font_apply_ft_size(h, 0);
        if (h->ft_face->size) {
            float ft_asc = (float)h->ft_face->size->metrics.ascender / 64.0f;
            float ft_desc = (float)h->ft_face->size->metrics.descender / 64.0f;
            float ft_hgt = (float)h->ft_face->size->metrics.height / 64.0f;
            if (ft_asc > 0.0f) asc = ft_asc;
            if (ft_desc <= 0.0f) desc = ft_desc;
            line_adv = (ft_hgt > 0.0f) ? ft_hgt : (asc - desc);
        }
    }
    pthread_mutex_unlock(&g_font_mutex);

    OrbisFontHorizontalLayout *layout = (OrbisFontHorizontalLayout *)(ctx->mem_base + layoutGuest);
    layout->baselineOffset = asc;
    layout->lineAdvance = line_adv;
    layout->decorationExtent = (desc < 0.0f) ? -desc : 0.0f;

    ctx->rax = ORBIS_FONT_OK;
    SHIM_RETURN();
}

// 61. sceFontRenderSurfaceSetScissor
void shim_sceFontRenderSurfaceSetScissor(GuestContext *ctx) {
    uint64_t surfGuest = ctx->rdi;
    int32_t x0 = (int32_t)ctx->rsi;
    int32_t y0 = (int32_t)ctx->rdx;
    int32_t w = (int32_t)ctx->rcx;
    int32_t h = (int32_t)ctx->r8;

    if (!surfGuest || !ctx->mem_base) {
        SHIM_RETURN();
    }

    OrbisFontRenderSurface *surf = (OrbisFontRenderSurface *)(ctx->mem_base + surfGuest);
    const uint32_t surface_w = (uint32_t)surf->width;
    if (surface_w != 0) {
        uint32_t x1, x0_out;
        uint32_t w_u = (uint32_t)w;
        if (x0 < 0) {
            x1 = w_u + (uint32_t)x0;
            if (surface_w < x1) x1 = surface_w;
            if (w_u <= (uint32_t)(-x0)) x1 = 0;
            x0_out = 0;
        } else {
            x1 = surface_w;
            x0_out = surface_w;
            if ((uint32_t)x0 <= surface_w) {
                if (surface_w < w_u) w_u = surface_w;
                x1 = w_u + (uint32_t)x0;
                x0_out = (uint32_t)x0;
                if (surface_w < x1) x1 = surface_w;
            }
        }
        surf->sc_x0 = x0_out;
        surf->sc_x1 = x1;
    }

    const uint32_t surface_h = (uint32_t)surf->height;
    if (surface_h == 0) {
        SHIM_RETURN();
    }

    uint32_t y0_out;
    uint32_t y1 = surface_h;
    uint32_t h_u = (uint32_t)h;
    if (y0 < 0) {
        y0_out = 0;
        if (h_u <= (uint32_t)(-y0)) {
            surf->sc_y0 = 0;
            surf->sc_y1 = 0;
            SHIM_RETURN();
        }
    } else {
        y0_out = surface_h;
        if (surface_h < (uint32_t)y0) {
            surf->sc_y0 = y0_out;
            surf->sc_y1 = y1;
            SHIM_RETURN();
        }
        y0_out = (uint32_t)y0;
        if (surface_h < h_u) h_u = surface_h;
    }

    const uint32_t y1_candidate = h_u + (uint32_t)y0;
    if (y1_candidate <= surface_h) {
        y1 = y1_candidate;
    }
    surf->sc_y0 = y0_out;
    surf->sc_y1 = y1;
    SHIM_RETURN();
}
