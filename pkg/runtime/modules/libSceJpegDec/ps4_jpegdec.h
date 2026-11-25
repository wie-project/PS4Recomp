// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_JPEGDEC_H
#define PS4_JPEGDEC_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_JPEG_DEC_ERROR_INVALID_ADDR ((int32_t)0x80650001)
#define ORBIS_JPEG_DEC_ERROR_INVALID_SIZE ((int32_t)0x80650002)
#define ORBIS_JPEG_DEC_ERROR_INVALID_PARAM ((int32_t)0x80650003)
#define ORBIS_JPEG_DEC_ERROR_INVALID_HANDLE ((int32_t)0x80650004)
#define ORBIS_JPEG_DEC_ERROR_INVALID_WORK_MEMORY ((int32_t)0x80650005)
#define ORBIS_JPEG_DEC_ERROR_EXCEED_MAX_WIDTH ((int32_t)0x80650006)
#define ORBIS_JPEG_DEC_ERROR_INVALID_DATA ((int32_t)0x80650010)
#define ORBIS_JPEG_DEC_ERROR_UNSUPPORT_DATA ((int32_t)0x80650011)
#define ORBIS_JPEG_DEC_ERROR_DECODE_ERROR ((int32_t)0x80650012)
#define ORBIS_JPEG_DEC_ERROR_FATAL ((int32_t)0x80650020)

#define ORBIS_JPEG_DEC_PIXEL_FORMAT_R8G8B8A8 0
#define ORBIS_JPEG_DEC_PIXEL_FORMAT_B8G8R8A8 1
#define ORBIS_JPEG_DEC_PIXEL_FORMAT_Y8U8Y8V8 10
#define ORBIS_JPEG_DEC_PIXEL_FORMAT_Y8 11

#define ORBIS_JPEG_DEC_COLOR_SPACE_UNKNOWN 0
#define ORBIS_JPEG_DEC_COLOR_SPACE_YCC 1
#define ORBIS_JPEG_DEC_COLOR_SPACE_GRAYSCALE 2

// Guest structures: the pointers in them are guest addresses.
typedef struct OrbisJpegDecHandleInternal {
    uint64_t handle; // the handle's own address while it is valid
    uint32_t handle_size;
    uint32_t decode_mode;
    uint32_t max_width;
    uint32_t stride_or_aux;
} OrbisJpegDecHandleInternal;
_Static_assert(sizeof(OrbisJpegDecHandleInternal) == 0x18, "OrbisJpegDecHandleInternal layout");

typedef struct OrbisJpegDecCreateParam {
    uint32_t size;
    uint32_t decode_mode;
    uint32_t max_width;
} OrbisJpegDecCreateParam;
_Static_assert(sizeof(OrbisJpegDecCreateParam) == 0xc, "OrbisJpegDecCreateParam layout");

typedef struct OrbisJpegDecParseParam {
    uint64_t jpeg_addr;
    uint32_t jpeg_size;
    uint16_t decode_mode;
    uint16_t downscale_ratio;
} OrbisJpegDecParseParam;
_Static_assert(sizeof(OrbisJpegDecParseParam) == 0x10, "OrbisJpegDecParseParam layout");

typedef struct OrbisJpegDecImageInfo {
    uint32_t image_width;
    uint32_t image_height;
    uint16_t color_space;
    uint16_t num_components;
    uint8_t component_id[4];
    uint8_t sampling_factor[4];
    uint32_t decode_mode;
    uint32_t required_decode_mode;
    uint32_t output_width;
    uint32_t output_height;
} OrbisJpegDecImageInfo;
_Static_assert(sizeof(OrbisJpegDecImageInfo) == 0x24, "OrbisJpegDecImageInfo layout");

typedef struct OrbisJpegDecDecodeParam {
    uint64_t jpeg_addr;
    uint64_t image_addr;
    uint64_t aux_addr;
    uint32_t jpeg_size;
    uint32_t image_size;
    uint32_t aux_size;
    uint16_t decode_mode;
    uint16_t downscale_ratio;
    uint16_t pixel_format;
    uint16_t alpha_value;
    uint32_t image_pitch;
} OrbisJpegDecDecodeParam;
_Static_assert(sizeof(OrbisJpegDecDecodeParam) == 0x30, "OrbisJpegDecDecodeParam layout");

// C API. `mem` is the guest memory base: guest addresses (handles and the addresses inside the
// parameter structures) are offsets from it. Parameter structures are host pointers.
int32_t sceJpegDecQueryMemorySize(const OrbisJpegDecCreateParam *param);
int32_t sceJpegDecCreate(uint8_t *mem, const OrbisJpegDecCreateParam *param, uint64_t memory, uint32_t memory_size,
                         uint64_t *handle);
int32_t sceJpegDecDelete(uint8_t *mem, uint64_t handle);
int32_t sceJpegDecParseHeader(uint8_t *mem, const OrbisJpegDecParseParam *param, OrbisJpegDecImageInfo *image_info);
int32_t sceJpegDecDecode(uint8_t *mem, uint64_t handle, const OrbisJpegDecDecodeParam *param,
                         OrbisJpegDecImageInfo *image_info);

void shim_sceJpegDecQueryMemorySize(GuestContext *ctx);
void shim_sceJpegDecCreate(GuestContext *ctx);
void shim_sceJpegDecDelete(GuestContext *ctx);
void shim_sceJpegDecParseHeader(GuestContext *ctx);
void shim_sceJpegDecDecode(GuestContext *ctx);
void shim_sceJpegDecDecodeWithInputControl(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_JPEGDEC_H
