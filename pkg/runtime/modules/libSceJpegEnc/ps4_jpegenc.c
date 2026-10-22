// libSceJpegEnc without an encoder: create/delete succeed, Encode fails so
// screenshot/share paths are skipped. Error codes follow shadPS4 jpegenc.
#include "recomp_runtime.h"
#include <stdio.h>

#define JPEG_ENC_ERROR_INVALID_ADDR    ((int32_t)0x80650101)
#define JPEG_ENC_ERROR_INVALID_SIZE    ((int32_t)0x80650102)
#define JPEG_ENC_ERROR_INVALID_PARAM   ((int32_t)0x80650103)
#define JPEG_ENC_ERROR_INVALID_HANDLE  ((int32_t)0x80650104)

#define JPEG_ENC_MINIMUM_MEMORY_SIZE 0x800u

// int sceJpegEncQueryMemorySize(const SceJpegEncCreateParam *param)
void shim_sceJpegEncQueryMemorySize(GuestContext *ctx) {
    ctx->rax = ctx->rdi ? (uint64_t)JPEG_ENC_MINIMUM_MEMORY_SIZE
                        : (uint64_t)(int64_t)JPEG_ENC_ERROR_INVALID_ADDR;
    SHIM_RETURN();
}

// int sceJpegEncCreate(const SceJpegEncCreateParam *param, void *memory,
//                      uint32_t memorySize, SceJpegEncHandle *handle)
void shim_sceJpegEncCreate(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi || !ctx->rcx) {
        ctx->rax = (uint64_t)(int64_t)JPEG_ENC_ERROR_INVALID_ADDR;
        SHIM_RETURN();
    }
    if ((uint32_t)ctx->rdx < JPEG_ENC_MINIMUM_MEMORY_SIZE) {
        ctx->rax = (uint64_t)(int64_t)JPEG_ENC_ERROR_INVALID_SIZE;
        SHIM_RETURN();
    }
    *(uint64_t *)(ctx->mem_base + ctx->rcx) = ctx->rsi;
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceJpegEncDelete(SceJpegEncHandle handle)
void shim_sceJpegEncDelete(GuestContext *ctx) {
    ctx->rax = ctx->rdi ? 0 : (uint64_t)(int64_t)JPEG_ENC_ERROR_INVALID_HANDLE;
    SHIM_RETURN();
}

// int sceJpegEncEncode(SceJpegEncHandle handle, const SceJpegEncEncodeParam *param,
//                      SceJpegEncOutputInfo *outputInfo)
void shim_sceJpegEncEncode(GuestContext *ctx) {
    static int warned;
    if (!warned) {
        warned = 1;
        fprintf(stderr, "[libSceJpegEnc] sceJpegEncEncode -> error (no host encoder)\n");
    }
    ctx->rax = ctx->rdi ? (uint64_t)(int64_t)JPEG_ENC_ERROR_INVALID_PARAM
                        : (uint64_t)(int64_t)JPEG_ENC_ERROR_INVALID_HANDLE;
    SHIM_RETURN();
}
