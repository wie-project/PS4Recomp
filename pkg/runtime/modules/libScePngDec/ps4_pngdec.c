// libScePngDec without a decoder backend. ParseHeader is implemented for real
// (it only reads the IHDR chunk); Decode fails with UNSUPPORT_DATA so callers
// fall back gracefully. Layouts / error codes follow shadPS4 pngdec.
#include "recomp_runtime.h"
#include <stdio.h>
#include <string.h>

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
