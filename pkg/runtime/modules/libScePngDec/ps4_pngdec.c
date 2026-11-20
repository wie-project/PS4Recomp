// libScePngDec without a decoder backend. ParseHeader is implemented for real
// (it only reads the IHDR chunk); Decode fails with UNSUPPORT_DATA so callers
// fall back gracefully. Layouts / error codes follow shadPS4 pngdec.
#include "recomp_runtime.h"
#include "ps4_pngenc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <zlib.h>

#define PNG_DEC_ERROR_INVALID_ADDR     ((int32_t)0x80690001)
#define PNG_DEC_ERROR_INVALID_SIZE     ((int32_t)0x80690002)
#define PNG_DEC_ERROR_INVALID_PARAM    ((int32_t)0x80690003)
#define PNG_DEC_ERROR_INVALID_HANDLE   ((int32_t)0x80690004)
#define PNG_DEC_ERROR_INVALID_DATA     ((int32_t)0x80690010)
#define PNG_DEC_ERROR_UNSUPPORT_DATA   ((int32_t)0x80690011)

#define PNG_DEC_WORK_MEMORY_SIZE 0x4000u

typedef struct PngDecParseParam {
    uint64_t pngMemAddr;
    uint32_t pngMemSize;
    uint32_t reserved;
} PngDecParseParam;

typedef struct PngDecImageInfo {
    uint32_t imageWidth;
    uint32_t imageHeight;
    uint16_t colorSpace;
    uint16_t bitDepth;
    uint32_t imageFlag;
} PngDecImageInfo;

enum { PNG_CS_GRAYSCALE = 2, PNG_CS_RGB = 3, PNG_CS_CLUT = 4,
       PNG_CS_GRAYSCALE_ALPHA = 18, PNG_CS_RGBA = 19 };
#define PNG_FLAG_ADAM7_INTERLACE 1u
#define PNG_FLAG_TRNS_CHUNK_EXIST 2u

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

typedef struct PngEncState {
    uint32_t magic; // 0x504E4745 "PNGE"
    uint32_t max_width;
    uint32_t max_filter;
} PngEncState;

static void png_write_be32(uint8_t *dst, uint32_t val) {
    dst[0] = (uint8_t)(val >> 24);
    dst[1] = (uint8_t)(val >> 16);
    dst[2] = (uint8_t)(val >> 8);
    dst[3] = (uint8_t)val;
}

static uint32_t png_write_chunk(uint8_t *dst, const char *type, const uint8_t *data, uint32_t length) {
    png_write_be32(dst, length);
    memcpy(dst + 4, type, 4);
    if (length > 0 && data) {
        memcpy(dst + 8, data, length);
    }
    uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, dst + 4, 4 + length);
    png_write_be32(dst + 8 + length, crc);
    return 12 + length;
}

int32_t scePngEncQueryMemorySize(const OrbisPngEncCreateParam *param) {
    if (!param) return ORBIS_PNG_ENC_ERROR_INVALID_ADDR;
    if (param->attribute != 0 || param->max_filter_number > 5) return ORBIS_PNG_ENC_ERROR_INVALID_PARAM;
    if (param->max_image_width == 0 || param->max_image_width > 1000000) return ORBIS_PNG_ENC_ERROR_INVALID_SIZE;
    return (int32_t)sizeof(PngEncState);
}

int32_t scePngEncCreate(const OrbisPngEncCreateParam *param, void *memoryAddress, uint32_t memorySize, void **handle) {
    if (!param || param->attribute != 0) return ORBIS_PNG_ENC_ERROR_INVALID_PARAM;
    if (!memoryAddress || !handle) return ORBIS_PNG_ENC_ERROR_INVALID_ADDR;
    if (param->max_image_width == 0 || param->max_image_width > 1000000) return ORBIS_PNG_ENC_ERROR_INVALID_SIZE;
    if (memorySize < sizeof(PngEncState)) return ORBIS_PNG_ENC_ERROR_INVALID_SIZE;

    PngEncState *s = (PngEncState *)memoryAddress;
    s->magic = 0x504E4745;
    s->max_width = param->max_image_width;
    s->max_filter = param->max_filter_number;
    *handle = memoryAddress;
    return 0;
}

int32_t scePngEncDelete(void *handle) {
    if (!handle) return ORBIS_PNG_ENC_ERROR_INVALID_HANDLE;
    PngEncState *s = (PngEncState *)handle;
    if (s->magic != 0x504E4745) return ORBIS_PNG_ENC_ERROR_INVALID_HANDLE;
    s->magic = 0;
    return 0;
}

int32_t scePngEncEncode(void *handle, const OrbisPngEncEncodeParam *param, OrbisPngEncOutputInfo *outputInfo) {
    if (!handle) return ORBIS_PNG_ENC_ERROR_INVALID_HANDLE;
    PngEncState *s = (PngEncState *)handle;
    if (s->magic != 0x504E4745) return ORBIS_PNG_ENC_ERROR_INVALID_HANDLE;
    if (!param) return ORBIS_PNG_ENC_ERROR_INVALID_PARAM;
    if (!param->image_mem_addr || !param->png_mem_addr) return ORBIS_PNG_ENC_ERROR_INVALID_ADDR;
    if (param->image_width == 0 || param->image_height == 0) return ORBIS_PNG_ENC_ERROR_INVALID_PARAM;
    if (param->image_width > s->max_width) return ORBIS_PNG_ENC_ERROR_INVALID_SIZE;

    // Minimum PNG header + chunks is ~68 bytes: signature (8) + IHDR (25) + IDAT (12+) + IEND (12)
    if (param->png_mem_size < 68) return ORBIS_PNG_ENC_ERROR_DATA_OVERFLOW;

    bool has_alpha = (param->color_space == 19 /* RGBA */ || param->color_space == 18 /* GRAYSCALE_ALPHA */);
    uint32_t channels = has_alpha ? 4 : 3;
    uint32_t in_channels = 4;
    uint32_t src_pitch = param->image_pitch ? param->image_pitch : (param->image_width * in_channels);
    bool swap_rb = (param->pixel_format == 1 /* B8G8R8A8 */);

    uint32_t raw_row_size = 1 + param->image_width * channels;
    uint64_t raw_size = (uint64_t)raw_row_size * param->image_height;
    uint8_t *raw_buf = (uint8_t *)malloc((size_t)raw_size);
    if (!raw_buf) return ORBIS_PNG_ENC_ERROR_FATAL;

    for (uint32_t y = 0; y < param->image_height; y++) {
        uint8_t *row_dst = raw_buf + (uint64_t)y * raw_row_size;
        *row_dst++ = 0; // Filter: None
        const uint8_t *row_src = param->image_mem_addr + (uint64_t)y * src_pitch;
        for (uint32_t x = 0; x < param->image_width; x++) {
            uint8_t r = swap_rb ? row_src[x * in_channels + 2] : row_src[x * in_channels + 0];
            uint8_t g = row_src[x * in_channels + 1];
            uint8_t b = swap_rb ? row_src[x * in_channels + 0] : row_src[x * in_channels + 2];
            *row_dst++ = r;
            *row_dst++ = g;
            *row_dst++ = b;
            if (has_alpha) {
                *row_dst++ = row_src[x * in_channels + 3];
            }
        }
    }

    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    uint8_t *out = param->png_mem_addr;
    uint32_t out_capacity = param->png_mem_size;
    uint32_t offset = 0;

    memcpy(out + offset, png_sig, 8);
    offset += 8;

    uint8_t ihdr_data[13];
    png_write_be32(ihdr_data, param->image_width);
    png_write_be32(ihdr_data + 4, param->image_height);
    ihdr_data[8] = 8;
    ihdr_data[9] = has_alpha ? 6 : 2;
    ihdr_data[10] = 0;
    ihdr_data[11] = 0;
    ihdr_data[12] = 0;
    offset += png_write_chunk(out + offset, "IHDR", ihdr_data, 13);

    if (offset + 12 + 12 > out_capacity) {
        free(raw_buf);
        return ORBIS_PNG_ENC_ERROR_DATA_OVERFLOW;
    }
    uint8_t *idat_data = out + offset + 8;
    uLongf idat_capacity = out_capacity - offset - 12 - 12;
    int zrc = compress2(idat_data, &idat_capacity, raw_buf, (uLong)raw_size,
                        param->compression_level > 9 ? 6 : (int)param->compression_level);
    free(raw_buf);
    if (zrc != Z_OK) {
        return ORBIS_PNG_ENC_ERROR_DATA_OVERFLOW;
    }

    png_write_be32(out + offset, (uint32_t)idat_capacity);
    memcpy(out + offset + 4, "IDAT", 4);
    uint32_t idat_crc = (uint32_t)crc32(0L, Z_NULL, 0);
    idat_crc = (uint32_t)crc32(idat_crc, out + offset + 4, 4 + (uint32_t)idat_capacity);
    png_write_be32(out + offset + 8 + (uint32_t)idat_capacity, idat_crc);
    offset += 12 + (uint32_t)idat_capacity;

    offset += png_write_chunk(out + offset, "IEND", NULL, 0);

    if (outputInfo) {
        outputInfo->data_size = offset;
        outputInfo->processed_height = param->image_height;
    }
    return (int32_t)offset;
}

// int scePngDecQueryMemorySize(const ScePngDecCreateParam *param)
void shim_scePngDecQueryMemorySize(GuestContext *ctx) {
    if (!ctx->rdi) {
        ctx->rax = (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_ADDR;
        SHIM_RETURN();
    }
    uint32_t attribute = *(uint32_t *)(ctx->mem_base + ctx->rdi + 4);
    ctx->rax = attribute > 1 ? (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_PARAM
                             : (uint64_t)PNG_DEC_WORK_MEMORY_SIZE;
    SHIM_RETURN();
}

// int scePngDecCreate(const ScePngDecCreateParam *param, void *memoryAddress,
//                     uint32_t memorySize, ScePngDecHandle *handle)
void shim_scePngDecCreate(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi || !ctx->rcx) {
        ctx->rax = (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_ADDR;
        SHIM_RETURN();
    }
    if ((uint32_t)ctx->rdx < PNG_DEC_WORK_MEMORY_SIZE) {
        ctx->rax = (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_SIZE;
        SHIM_RETURN();
    }
    *(uint64_t *)(ctx->mem_base + ctx->rcx) = ctx->rsi;
    ctx->rax = 0;
    SHIM_RETURN();
}

// int scePngDecDelete(ScePngDecHandle handle)
void shim_scePngDecDelete(GuestContext *ctx) {
    ctx->rax = ctx->rdi ? 0 : (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_HANDLE;
    SHIM_RETURN();
}

// int scePngDecParseHeader(const ScePngDecParseParam *param, ScePngDecImageInfo *imageInfo)
void shim_scePngDecParseHeader(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi) {
        ctx->rax = (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_ADDR;
        SHIM_RETURN();
    }
    const PngDecParseParam *pp = (const PngDecParseParam *)(ctx->mem_base + ctx->rdi);
    if (!pp->pngMemAddr) {
        ctx->rax = (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_ADDR;
        SHIM_RETURN();
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const uint8_t *png = ctx->mem_base + pp->pngMemAddr;
    uint32_t size = pp->pngMemSize;
    if (size < 33 || memcmp(png, sig, 8) != 0 || memcmp(png + 12, "IHDR", 4) != 0) {
        ctx->rax = (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_DATA;
        SHIM_RETURN();
    }
    const uint8_t *ihdr = png + 16;
    PngDecImageInfo *info = (PngDecImageInfo *)(ctx->mem_base + ctx->rsi);
    memset(info, 0, sizeof(*info));
    info->imageWidth = be32(ihdr);
    info->imageHeight = be32(ihdr + 4);
    info->bitDepth = ihdr[8];
    switch (ihdr[9]) {
    case 0: info->colorSpace = PNG_CS_GRAYSCALE; break;
    case 2: info->colorSpace = PNG_CS_RGB; break;
    case 3: info->colorSpace = PNG_CS_CLUT; break;
    case 4: info->colorSpace = PNG_CS_GRAYSCALE_ALPHA; break;
    case 6: info->colorSpace = PNG_CS_RGBA; break;
    default:
        ctx->rax = (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_DATA;
        SHIM_RETURN();
    }
    if (ihdr[12] == 1) info->imageFlag |= PNG_FLAG_ADAM7_INTERLACE;
    // Scan chunk list for tRNS (stop at IDAT/IEND).
    uint32_t off = 8;
    while (off + 12 <= size) {
        uint32_t len = be32(png + off);
        const uint8_t *type = png + off + 4;
        if (memcmp(type, "tRNS", 4) == 0) {
            info->imageFlag |= PNG_FLAG_TRNS_CHUNK_EXIST;
            break;
        }
        if (memcmp(type, "IDAT", 4) == 0 || memcmp(type, "IEND", 4) == 0) break;
        if (len > size - off - 12) break;
        off += 12 + len;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

// int scePngDecDecode(ScePngDecHandle handle, const ScePngDecDecodeParam *param,
//                     ScePngDecImageInfo *imageInfo)
void shim_scePngDecDecode(GuestContext *ctx) {
    static int warned;
    if (!warned) {
        warned = 1;
        fprintf(stderr, "[libScePngDec] scePngDecDecode -> UNSUPPORT_DATA (no host decoder)\n");
    }
    if (!ctx->rdi) {
        ctx->rax = (uint64_t)(int64_t)PNG_DEC_ERROR_INVALID_HANDLE;
        SHIM_RETURN();
    }
    ctx->rax = (uint64_t)(int64_t)PNG_DEC_ERROR_UNSUPPORT_DATA;
    SHIM_RETURN();
}


static inline void *guest_to_host(const GuestContext *ctx, uint64_t addr) {
    if (!addr) return NULL;
    if (ctx && ctx->mem_base && addr < (1ULL << 39)) {
        return (void *)(ctx->mem_base + addr);
    }
    return (void *)addr;
}

void shim_scePngEncQueryMemorySize(GuestContext *ctx) {
    const OrbisPngEncCreateParam *param = (const OrbisPngEncCreateParam *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)scePngEncQueryMemorySize(param);
    SHIM_RETURN();
}

void shim_scePngEncCreate(GuestContext *ctx) {
    const OrbisPngEncCreateParam *param = (const OrbisPngEncCreateParam *)guest_to_host(ctx, ctx->rdi);
    void *mem = guest_to_host(ctx, ctx->rsi);
    uint32_t mem_size = (uint32_t)ctx->rdx;
    void **handle = (void **)guest_to_host(ctx, ctx->rcx);
    ctx->rax = (uint64_t)(int64_t)scePngEncCreate(param, mem, mem_size, handle);
    SHIM_RETURN();
}

void shim_scePngEncDelete(GuestContext *ctx) {
    void *handle = guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)scePngEncDelete(handle);
    SHIM_RETURN();
}

void shim_scePngEncEncode(GuestContext *ctx) {
    void *handle = guest_to_host(ctx, ctx->rdi);
    const OrbisPngEncEncodeParam *param = (const OrbisPngEncEncodeParam *)guest_to_host(ctx, ctx->rsi);
    OrbisPngEncOutputInfo *info = (OrbisPngEncOutputInfo *)guest_to_host(ctx, ctx->rdx);
    if (!param) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_PNG_ENC_ERROR_INVALID_PARAM;
        SHIM_RETURN();
    }
    OrbisPngEncEncodeParam host_param = *param;
    host_param.image_mem_addr = (const uint8_t *)guest_to_host(ctx, (uint64_t)param->image_mem_addr);
    host_param.png_mem_addr = (uint8_t *)guest_to_host(ctx, (uint64_t)param->png_mem_addr);
    ctx->rax = (uint64_t)(int64_t)scePngEncEncode(handle, &host_param, info);
    SHIM_RETURN();
}