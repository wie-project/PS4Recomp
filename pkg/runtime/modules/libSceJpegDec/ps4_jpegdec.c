// libSceJpegDec without a decoder: handles can be created (so init code
// succeeds) but header parsing / decoding fail with UNSUPPORT_DATA, letting
// callers fall back gracefully. Error codes follow the SCE image-decoder
// layout (same scheme as libScePngDec, base 0x80650000).
#include "recomp_runtime.h"
#include <stdio.h>

#define JPEG_DEC_ERROR_INVALID_ADDR    ((int32_t)0x80650001)
#define JPEG_DEC_ERROR_INVALID_SIZE    ((int32_t)0x80650002)
#define JPEG_DEC_ERROR_INVALID_HANDLE  ((int32_t)0x80650004)
#define JPEG_DEC_ERROR_UNSUPPORT_DATA  ((int32_t)0x80650011)

#define JPEG_DEC_WORK_MEMORY_SIZE 0x10000u

#define JPEGDEC_LOG_ONCE(name)                                                 \
    do {                                                                       \
        static int warned_;                                                    \
        if (!warned_) {                                                        \
            warned_ = 1;                                                       \
            fprintf(stderr, "[libSceJpegDec] %s -> UNSUPPORT_DATA (no host decoder)\n", name); \
        }                                                                      \
    } while (0)

// int sceJpegDecQueryMemorySize(const SceJpegDecCreateParam *param)
void shim_sceJpegDecQueryMemorySize(GuestContext *ctx) {
    ctx->rax = ctx->rdi ? (uint64_t)JPEG_DEC_WORK_MEMORY_SIZE
                        : (uint64_t)(int64_t)JPEG_DEC_ERROR_INVALID_ADDR;
    SHIM_RETURN();
}

// int sceJpegDecCreate(const SceJpegDecCreateParam *param, void *memoryAddr,
//                      uint32_t memorySize, SceJpegDecHandle *handle)
void shim_sceJpegDecCreate(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi || !ctx->rcx) {
        ctx->rax = (uint64_t)(int64_t)JPEG_DEC_ERROR_INVALID_ADDR;
        SHIM_RETURN();
    }
    if ((uint32_t)ctx->rdx < JPEG_DEC_WORK_MEMORY_SIZE) {
        ctx->rax = (uint64_t)(int64_t)JPEG_DEC_ERROR_INVALID_SIZE;
        SHIM_RETURN();
    }
    // The handle is the caller-provided work memory (guest address).
    *(uint64_t *)(ctx->mem_base + ctx->rcx) = ctx->rsi;
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceJpegDecDelete(SceJpegDecHandle handle)
void shim_sceJpegDecDelete(GuestContext *ctx) {
    ctx->rax = ctx->rdi ? 0 : (uint64_t)(int64_t)JPEG_DEC_ERROR_INVALID_HANDLE;
    SHIM_RETURN();
}

// int sceJpegDecParseHeader(const SceJpegDecParseParam *param, SceJpegDecImageInfo *info)
void shim_sceJpegDecParseHeader(GuestContext *ctx) {
    JPEGDEC_LOG_ONCE("sceJpegDecParseHeader");
    ctx->rax = (uint64_t)(int64_t)JPEG_DEC_ERROR_UNSUPPORT_DATA;
    SHIM_RETURN();
}

// int sceJpegDecDecode(SceJpegDecHandle handle, const SceJpegDecDecodeParam *param,
//                      SceJpegDecImageInfo *info)
void shim_sceJpegDecDecode(GuestContext *ctx) {
    JPEGDEC_LOG_ONCE("sceJpegDecDecode");
    ctx->rax = (uint64_t)(int64_t)JPEG_DEC_ERROR_UNSUPPORT_DATA;
    SHIM_RETURN();
}
