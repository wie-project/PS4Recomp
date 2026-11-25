// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_JPEGENC_H
#define PS4_JPEGENC_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_JPEG_ENC_ERROR_INVALID_ADDR ((int32_t)0x80650101)
#define ORBIS_JPEG_ENC_ERROR_INVALID_SIZE ((int32_t)0x80650102)
#define ORBIS_JPEG_ENC_ERROR_INVALID_PARAM ((int32_t)0x80650103)
#define ORBIS_JPEG_ENC_ERROR_INVALID_HANDLE ((int32_t)0x80650104)

#define ORBIS_JPEG_ENC_MINIMUM_MEMORY_SIZE 0x800
#define ORBIS_JPEG_ENC_ATTRIBUTE_NONE 0

#define ORBIS_JPEG_ENC_PIXEL_FORMAT_R8G8B8A8 0
#define ORBIS_JPEG_ENC_PIXEL_FORMAT_B8G8R8A8 1
#define ORBIS_JPEG_ENC_PIXEL_FORMAT_Y8U8Y8V8 10
#define ORBIS_JPEG_ENC_PIXEL_FORMAT_Y8 11

#define ORBIS_JPEG_ENC_ENCODE_MODE_NORMAL 0
#define ORBIS_JPEG_ENC_ENCODE_MODE_MJPEG 1

#define ORBIS_JPEG_ENC_COLOR_SPACE_YCC 1
#define ORBIS_JPEG_ENC_COLOR_SPACE_GRAYSCALE 2

#define ORBIS_JPEG_ENC_SAMPLING_TYPE_FULL 0
#define ORBIS_JPEG_ENC_SAMPLING_TYPE_422 1
#define ORBIS_JPEG_ENC_SAMPLING_TYPE_420 2

// Guest structures: the pointers in them are guest addresses.
typedef struct OrbisJpegEncHandleInternal {
    uint64_t handle; // the handle's own address while it is valid
    uint32_t handle_size;
    uint32_t reserved;
} OrbisJpegEncHandleInternal;
_Static_assert(sizeof(OrbisJpegEncHandleInternal) == 0x10, "OrbisJpegEncHandleInternal layout");

typedef struct OrbisJpegEncCreateParam {
    uint32_t size;
    uint32_t attr;
} OrbisJpegEncCreateParam;
_Static_assert(sizeof(OrbisJpegEncCreateParam) == 0x8, "OrbisJpegEncCreateParam layout");

typedef struct OrbisJpegEncEncodeParam {
    uint64_t image;
    uint64_t jpeg;
    uint32_t image_size;
    uint32_t jpeg_size;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t image_pitch;
    uint16_t pixel_format;
    uint16_t encode_mode;
    uint16_t color_space;
    uint8_t sampling_type;
    uint8_t compression_ratio;
    int32_t restart_interval;
} OrbisJpegEncEncodeParam;
_Static_assert(sizeof(OrbisJpegEncEncodeParam) == 0x30, "OrbisJpegEncEncodeParam layout");

typedef struct OrbisJpegEncOutputInfo {
    uint32_t size;
    uint32_t height;
} OrbisJpegEncOutputInfo;
_Static_assert(sizeof(OrbisJpegEncOutputInfo) == 0x8, "OrbisJpegEncOutputInfo layout");

// C API. `mem` is the guest memory base: guest addresses (handles and the addresses inside the
// parameter structures) are offsets from it. Parameter structures are host pointers.
int32_t sceJpegEncQueryMemorySize(const OrbisJpegEncCreateParam *param);
int32_t sceJpegEncCreate(uint8_t *mem, const OrbisJpegEncCreateParam *param, uint64_t memory, uint32_t memory_size,
                         uint64_t *handle);
int32_t sceJpegEncDelete(uint8_t *mem, uint64_t handle);
int32_t sceJpegEncEncode(uint8_t *mem, uint64_t handle, const OrbisJpegEncEncodeParam *param,
                         OrbisJpegEncOutputInfo *output_info);

void shim_sceJpegEncQueryMemorySize(GuestContext *ctx);
void shim_sceJpegEncCreate(GuestContext *ctx);
void shim_sceJpegEncDelete(GuestContext *ctx);
void shim_sceJpegEncEncode(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_JPEGENC_H
