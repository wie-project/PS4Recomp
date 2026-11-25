// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_PNGDEC_H
#define PS4_PNGDEC_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_PNG_DEC_ERROR_INVALID_ADDR ((int32_t)0x80690001)
#define ORBIS_PNG_DEC_ERROR_INVALID_SIZE ((int32_t)0x80690002)
#define ORBIS_PNG_DEC_ERROR_INVALID_PARAM ((int32_t)0x80690003)
#define ORBIS_PNG_DEC_ERROR_INVALID_HANDLE ((int32_t)0x80690004)
#define ORBIS_PNG_DEC_ERROR_INVALID_WORK_MEMORY ((int32_t)0x80690005)
#define ORBIS_PNG_DEC_ERROR_INVALID_DATA ((int32_t)0x80690010)
#define ORBIS_PNG_DEC_ERROR_UNSUPPORT_DATA ((int32_t)0x80690011)
#define ORBIS_PNG_DEC_ERROR_DECODE_ERROR ((int32_t)0x80690012)
#define ORBIS_PNG_DEC_ERROR_FATAL ((int32_t)0x80690020)

#define ORBIS_PNG_DEC_COLOR_SPACE_GRAYSCALE 2
#define ORBIS_PNG_DEC_COLOR_SPACE_RGB 3
#define ORBIS_PNG_DEC_COLOR_SPACE_CLUT 4
#define ORBIS_PNG_DEC_COLOR_SPACE_GRAYSCALE_ALPHA 18
#define ORBIS_PNG_DEC_COLOR_SPACE_RGBA 19

#define ORBIS_PNG_DEC_IMAGE_FLAG_ADAM7_INTERLACE 1u
#define ORBIS_PNG_DEC_IMAGE_FLAG_TRNS_CHUNK_EXIST 2u

#define ORBIS_PNG_DEC_PIXEL_FORMAT_R8G8B8A8 0
#define ORBIS_PNG_DEC_PIXEL_FORMAT_B8G8R8A8 1

// Guest structures; the addresses are guest addresses.
typedef struct OrbisPngDecParseParam {
    uint64_t png_mem_addr;
    uint32_t png_mem_size;
    uint32_t reserved;
} OrbisPngDecParseParam;

typedef struct OrbisPngDecImageInfo {
    uint32_t image_width;
    uint32_t image_height;
    uint16_t color_space;
    uint16_t bit_depth;
    uint32_t image_flag;
} OrbisPngDecImageInfo;

typedef struct OrbisPngDecCreateParam {
    uint32_t this_size;
    uint32_t attribute;
    uint32_t max_image_width;
} OrbisPngDecCreateParam;

typedef struct OrbisPngDecDecodeParam {
    uint64_t png_mem_addr;
    uint64_t image_mem_addr;
    uint32_t png_mem_size;
    uint32_t image_mem_size;
    uint16_t pixel_format;
    uint16_t alpha_value;
    uint32_t image_pitch;
} OrbisPngDecDecodeParam;

_Static_assert(sizeof(OrbisPngDecParseParam) == 16, "OrbisPngDecParseParam layout");
_Static_assert(sizeof(OrbisPngDecImageInfo) == 16, "OrbisPngDecImageInfo layout");
_Static_assert(sizeof(OrbisPngDecCreateParam) == 12, "OrbisPngDecCreateParam layout");
_Static_assert(sizeof(OrbisPngDecDecodeParam) == 32, "OrbisPngDecDecodeParam layout");

// The work memory a decoder takes (shadPS4: sizeof its handler).
#define PS4_PNG_DEC_WORK_MEMORY_SIZE 16

// C API. `mem` is the guest memory base: guest addresses in the structures are offsets from it.
// `handle` is the decoder's work memory (as a guest address, nonzero once created).
int32_t scePngDecQueryMemorySize(const OrbisPngDecCreateParam *param);
int32_t scePngDecCreate(const OrbisPngDecCreateParam *param, uint64_t memory, uint32_t memory_size,
                        uint64_t *handle);
int32_t scePngDecDelete(uint64_t handle);
int32_t scePngDecParseHeader(uint8_t *mem, const OrbisPngDecParseParam *param, OrbisPngDecImageInfo *info);
// Decodes to 8-bit RGBA or BGRA rows image_pitch bytes apart (4 * width when 0). Returns
// (width << 16) | height, or 0 for images larger than 32767 in either dimension.
int32_t scePngDecDecode(uint8_t *mem, uint64_t handle, const OrbisPngDecDecodeParam *param,
                        OrbisPngDecImageInfo *info);

void shim_scePngDecQueryMemorySize(GuestContext *ctx);
void shim_scePngDecCreate(GuestContext *ctx);
void shim_scePngDecDelete(GuestContext *ctx);
void shim_scePngDecParseHeader(GuestContext *ctx);
void shim_scePngDecDecode(GuestContext *ctx);
void shim_scePngDecDecodeWithInputControl(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_PNGDEC_H
