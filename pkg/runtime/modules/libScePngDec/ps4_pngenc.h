// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_PNGENC_H
#define PS4_PNGENC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_PNG_ENC_ERROR_INVALID_ADDR   ((int32_t)0x80690101)
#define ORBIS_PNG_ENC_ERROR_INVALID_SIZE   ((int32_t)0x80690102)
#define ORBIS_PNG_ENC_ERROR_INVALID_PARAM  ((int32_t)0x80690103)
#define ORBIS_PNG_ENC_ERROR_INVALID_HANDLE ((int32_t)0x80690104)
#define ORBIS_PNG_ENC_ERROR_DATA_OVERFLOW  ((int32_t)0x80690110)
#define ORBIS_PNG_ENC_ERROR_FATAL          ((int32_t)0x80690120)

typedef struct OrbisPngEncCreateParam {
    uint32_t this_size;
    uint32_t attribute;
    uint32_t max_image_width;
    uint32_t max_filter_number;
} OrbisPngEncCreateParam;

typedef struct OrbisPngEncEncodeParam {
    const uint8_t *image_mem_addr;
    uint8_t *png_mem_addr;
    uint32_t image_mem_size;
    uint32_t png_mem_size;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t image_pitch;
    uint16_t pixel_format;
    uint16_t color_space;
    uint16_t bit_depth;
    uint16_t clut_number;
    uint16_t filter_type;
    uint16_t compression_level;
} OrbisPngEncEncodeParam;

typedef struct OrbisPngEncOutputInfo {
    uint32_t data_size;
    uint32_t processed_height;
} OrbisPngEncOutputInfo;

int32_t scePngEncQueryMemorySize(const OrbisPngEncCreateParam *param);
int32_t scePngEncCreate(const OrbisPngEncCreateParam *param, void *memoryAddress, uint32_t memorySize, void **handle);
int32_t scePngEncDelete(void *handle);
int32_t scePngEncEncode(void *handle, const OrbisPngEncEncodeParam *param, OrbisPngEncOutputInfo *outputInfo);

#ifdef __cplusplus
}
#endif

#endif // PS4_PNGENC_H
