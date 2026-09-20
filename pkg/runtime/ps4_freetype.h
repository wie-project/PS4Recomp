#ifndef PS4_FREETYPE_H
#define PS4_FREETYPE_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// FreeType 2 Bridge API
int ps4_ft_init_freetype(GuestContext *ctx, uint64_t alibGuest);
int ps4_ft_new_face(GuestContext *ctx, uint64_t libGuest, uint64_t pathGuest, int64_t faceIndex, uint64_t afaceGuest);
int ps4_ft_set_pixel_sizes(GuestContext *ctx, uint64_t faceGuest, uint32_t width, uint32_t height);
uint32_t ps4_ft_get_char_index(GuestContext *ctx, uint64_t faceGuest, uint64_t code);
int ps4_ft_load_glyph(GuestContext *ctx, uint64_t faceGuest, uint32_t glyphIndex, int32_t loadFlags);
int ps4_ft_render_glyph(GuestContext *ctx, uint64_t slotGuest, int32_t renderMode);

// Guest ABI shims
void shim_FT_Init_FreeType(GuestContext *ctx);
void shim_FT_New_Face(GuestContext *ctx);
void shim_FT_Set_Pixel_Sizes(GuestContext *ctx);
void shim_FT_Get_Char_Index(GuestContext *ctx);
void shim_FT_Load_Glyph(GuestContext *ctx);
void shim_FT_Render_Glyph(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_FREETYPE_H
