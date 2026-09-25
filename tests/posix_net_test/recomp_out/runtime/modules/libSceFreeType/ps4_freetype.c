#include "ps4_freetype.h"
#include "ps4_vfs.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#define MAX_FT_FACES 16
#define GLYPH_BITMAP_MAX (512 * 512)

typedef struct {
    FT_FaceRec guest_face;
    FT_SizeRec guest_size;
    FT_GlyphSlotRec guest_slot;
    uint8_t bitmap_buffer[GLYPH_BITMAP_MAX];
} GuestFaceSlot;

typedef struct {
    int in_use;
    uint64_t guest_addr;
    FT_Face host_face;
} FaceHandleEntry;

static FT_Library g_ft_lib = NULL;
static FaceHandleEntry g_faces[MAX_FT_FACES];
static pthread_mutex_t g_ft_mutex = PTHREAD_MUTEX_INITIALIZER;

static FT_Face find_host_face_locked(uint64_t faceGuest) {
    for (int i = 0; i < MAX_FT_FACES; i++) {
        if (g_faces[i].in_use && g_faces[i].guest_addr == faceGuest) {
            return g_faces[i].host_face;
        }
    }
    return NULL;
}

static FaceHandleEntry *find_entry_by_slot_locked(uint64_t slotGuest) {
    for (int i = 0; i < MAX_FT_FACES; i++) {
        if (g_faces[i].in_use) {
            uint64_t expected_slot = g_faces[i].guest_addr + offsetof(GuestFaceSlot, guest_slot);
            if (expected_slot == slotGuest) {
                return &g_faces[i];
            }
        }
    }
    // Fallback: slotGuest might be faceGuest directly in some caller patterns
    for (int i = 0; i < MAX_FT_FACES; i++) {
        if (g_faces[i].in_use && g_faces[i].guest_addr == slotGuest) {
            return &g_faces[i];
        }
    }
    return NULL;
}

int ps4_ft_init_freetype(GuestContext *ctx, uint64_t alibGuest) {
    pthread_mutex_lock(&g_ft_mutex);
    if (!g_ft_lib) {
        int error = FT_Init_FreeType(&g_ft_lib);
        if (error) {
            pthread_mutex_unlock(&g_ft_mutex);
            return error;
        }
    }
    if (alibGuest && ctx && ctx->mem_base) {
        *(uint64_t *)(ctx->mem_base + alibGuest) = 0x1000;
    }
    pthread_mutex_unlock(&g_ft_mutex);
    return 0;
}

int ps4_ft_new_face(GuestContext *ctx, uint64_t libGuest, uint64_t pathGuest, int64_t faceIndex, uint64_t afaceGuest) {
    (void)libGuest;
    if (!ctx || !ctx->mem_base || !pathGuest || !afaceGuest) return -EINVAL;

    const char *guestPath = (const char *)(ctx->mem_base + pathGuest);
    char resolvedPath[1024];
    if (ps4_vfs_resolve(guestPath, resolvedPath, sizeof(resolvedPath)) != 0) {
        strncpy(resolvedPath, guestPath, sizeof(resolvedPath) - 1);
        resolvedPath[sizeof(resolvedPath) - 1] = '\0';
    }

    pthread_mutex_lock(&g_ft_mutex);
    if (!g_ft_lib) {
        int error = FT_Init_FreeType(&g_ft_lib);
        if (error) {
            pthread_mutex_unlock(&g_ft_mutex);
            return error;
        }
    }

    FaceHandleEntry *entry = NULL;
    for (int i = 0; i < MAX_FT_FACES; i++) {
        if (!g_faces[i].in_use) {
            entry = &g_faces[i];
            break;
        }
    }
    if (!entry) {
        pthread_mutex_unlock(&g_ft_mutex);
        return -ENOMEM;
    }

    FT_Face hostFace = NULL;
    int error = FT_New_Face(g_ft_lib, resolvedPath, (FT_Long)faceIndex, &hostFace);
    if (error) {
        pthread_mutex_unlock(&g_ft_mutex);
        return error;
    }

    uint64_t guest_addr = recomp_vm_alloc(ctx, sizeof(GuestFaceSlot));
    if (guest_addr == (uint64_t)-1) {
        FT_Done_Face(hostFace);
        pthread_mutex_unlock(&g_ft_mutex);
        return -ENOMEM;
    }

    GuestFaceSlot *slot = (GuestFaceSlot *)(ctx->mem_base + guest_addr);
    memset(slot, 0, sizeof(GuestFaceSlot));

    uint64_t slot_guest_addr = guest_addr + offsetof(GuestFaceSlot, guest_slot);
    uint64_t size_guest_addr = guest_addr + offsetof(GuestFaceSlot, guest_size);
    uint64_t bitmap_guest_addr = guest_addr + offsetof(GuestFaceSlot, bitmap_buffer);

    slot->guest_face.glyph = (FT_GlyphSlot)slot_guest_addr;
    slot->guest_face.size = (FT_Size)size_guest_addr;
    slot->guest_slot.bitmap.buffer = (unsigned char *)bitmap_guest_addr;
    slot->guest_slot.face = (FT_Face)guest_addr;
    if (hostFace->size) {
        slot->guest_size = *(hostFace->size);
    }

    entry->in_use = 1;
    entry->guest_addr = guest_addr;
    entry->host_face = hostFace;

    *(uint64_t *)(ctx->mem_base + afaceGuest) = guest_addr;

    pthread_mutex_unlock(&g_ft_mutex);
    return 0;
}

int ps4_ft_set_pixel_sizes(GuestContext *ctx, uint64_t faceGuest, uint32_t width, uint32_t height) {
    pthread_mutex_lock(&g_ft_mutex);
    FT_Face hostFace = find_host_face_locked(faceGuest);
    if (!hostFace) {
        pthread_mutex_unlock(&g_ft_mutex);
        return -EINVAL;
    }
    int error = FT_Set_Pixel_Sizes(hostFace, width, height);
    if (!error && hostFace->size && ctx && ctx->mem_base) {
        GuestFaceSlot *slot = (GuestFaceSlot *)(ctx->mem_base + faceGuest);
        slot->guest_size = *(hostFace->size);
    }
    pthread_mutex_unlock(&g_ft_mutex);
    return error;
}

uint32_t ps4_ft_get_char_index(GuestContext *ctx, uint64_t faceGuest, uint64_t code) {
    (void)ctx;
    pthread_mutex_lock(&g_ft_mutex);
    FT_Face hostFace = find_host_face_locked(faceGuest);
    if (!hostFace) {
        pthread_mutex_unlock(&g_ft_mutex);
        return 0;
    }
    FT_UInt idx = FT_Get_Char_Index(hostFace, (FT_ULong)code);
    pthread_mutex_unlock(&g_ft_mutex);
    return (uint32_t)idx;
}

int ps4_ft_load_glyph(GuestContext *ctx, uint64_t faceGuest, uint32_t glyphIndex, int32_t loadFlags) {
    (void)ctx;
    pthread_mutex_lock(&g_ft_mutex);
    FT_Face hostFace = find_host_face_locked(faceGuest);
    if (!hostFace) {
        pthread_mutex_unlock(&g_ft_mutex);
        return -EINVAL;
    }
    int error = FT_Load_Glyph(hostFace, glyphIndex, loadFlags);
    if (!error && hostFace->glyph && ctx && ctx->mem_base) {
        GuestFaceSlot *slot = (GuestFaceSlot *)(ctx->mem_base + faceGuest);
        slot->guest_slot.metrics = hostFace->glyph->metrics;
        slot->guest_slot.advance = hostFace->glyph->advance;
    }
    pthread_mutex_unlock(&g_ft_mutex);
    return error;
}

int ps4_ft_render_glyph(GuestContext *ctx, uint64_t slotGuest, int32_t renderMode) {
    if (!ctx || !ctx->mem_base) return -EINVAL;

    pthread_mutex_lock(&g_ft_mutex);
    FaceHandleEntry *entry = find_entry_by_slot_locked(slotGuest);
    if (!entry || !entry->host_face) {
        pthread_mutex_unlock(&g_ft_mutex);
        return -EINVAL;
    }

    FT_Face hostFace = entry->host_face;
    int error = FT_Render_Glyph(hostFace->glyph, (FT_Render_Mode)renderMode);
    if (error) {
        pthread_mutex_unlock(&g_ft_mutex);
        return error;
    }

    GuestFaceSlot *slot = (GuestFaceSlot *)(ctx->mem_base + entry->guest_addr);
    FT_GlyphSlot hostSlot = hostFace->glyph;

    slot->guest_slot.metrics = hostSlot->metrics;
    slot->guest_slot.bitmap.rows = hostSlot->bitmap.rows;
    slot->guest_slot.bitmap.width = hostSlot->bitmap.width;
    slot->guest_slot.bitmap.pitch = hostSlot->bitmap.pitch;
    slot->guest_slot.bitmap.num_grays = hostSlot->bitmap.num_grays;
    slot->guest_slot.bitmap.pixel_mode = hostSlot->bitmap.pixel_mode;
    slot->guest_slot.bitmap_left = hostSlot->bitmap_left;
    slot->guest_slot.bitmap_top = hostSlot->bitmap_top;
    slot->guest_slot.advance.x = hostSlot->advance.x;
    slot->guest_slot.advance.y = hostSlot->advance.y;

    if (hostSlot->bitmap.buffer && hostSlot->bitmap.rows > 0 && hostSlot->bitmap.width > 0) {
        size_t copyBytes = hostSlot->bitmap.rows * (size_t)abs(hostSlot->bitmap.pitch);
        if (copyBytes > GLYPH_BITMAP_MAX) copyBytes = GLYPH_BITMAP_MAX;
        memcpy(slot->bitmap_buffer, hostSlot->bitmap.buffer, copyBytes);
    }

    pthread_mutex_unlock(&g_ft_mutex);
    return 0;
}

// Guest ABI shims
void shim_FT_Init_FreeType(GuestContext *ctx) {
    uint64_t alibGuest = ctx->rdi;
    int ret = ps4_ft_init_freetype(ctx, alibGuest);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_FT_New_Face(GuestContext *ctx) {
    uint64_t libGuest = ctx->rdi;
    uint64_t pathGuest = ctx->rsi;
    int64_t faceIndex = (int64_t)ctx->rdx;
    uint64_t afaceGuest = ctx->rcx;

    int ret = ps4_ft_new_face(ctx, libGuest, pathGuest, faceIndex, afaceGuest);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_FT_Set_Pixel_Sizes(GuestContext *ctx) {
    uint64_t faceGuest = ctx->rdi;
    uint32_t width = (uint32_t)ctx->rsi;
    uint32_t height = (uint32_t)ctx->rdx;

    int ret = ps4_ft_set_pixel_sizes(ctx, faceGuest, width, height);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_FT_Get_Char_Index(GuestContext *ctx) {
    uint64_t faceGuest = ctx->rdi;
    uint64_t code = ctx->rsi;

    uint32_t idx = ps4_ft_get_char_index(ctx, faceGuest, code);
    ctx->rax = (uint64_t)idx;
    SHIM_RETURN();
}

void shim_FT_Load_Glyph(GuestContext *ctx) {
    uint64_t faceGuest = ctx->rdi;
    uint32_t glyphIndex = (uint32_t)ctx->rsi;
    int32_t loadFlags = (int32_t)ctx->rdx;

    int ret = ps4_ft_load_glyph(ctx, faceGuest, glyphIndex, loadFlags);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_FT_Render_Glyph(GuestContext *ctx) {
    uint64_t slotGuest = ctx->rdi;
    int32_t renderMode = (int32_t)ctx->rsi;

    int ret = ps4_ft_render_glyph(ctx, slotGuest, renderMode);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}
