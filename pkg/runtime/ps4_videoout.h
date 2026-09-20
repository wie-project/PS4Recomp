#ifndef PS4_VIDEOOUT_H
#define PS4_VIDEOOUT_H

#include "recomp_runtime.h"
#include "ps4_equeue.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OrbisVideoOutBufferAttribute {
    int32_t format;
    int32_t tmode;
    int32_t aspect;
    uint32_t width;
    uint32_t height;
    uint32_t pixelPitch;
    uint64_t reserved[2];
} OrbisVideoOutBufferAttribute;

typedef struct OrbisVideoOutFlipStatus {
    uint64_t num;
    uint64_t ptime;
    uint64_t stime;
    int64_t flipArg;
    uint64_t reserved[2];
    int32_t numGpuFlipPending;
    int32_t numFlipPending;
    int32_t currentBuffer;
    uint32_t reserved1;
} OrbisVideoOutFlipStatus;

int32_t sceVideoOutOpen(int32_t userId, int32_t busType, int32_t index, const void *param);
int32_t sceVideoOutClose(int32_t handle);
void sceVideoOutSetBufferAttribute(OrbisVideoOutBufferAttribute *attr, uint32_t pixelFormat, uint32_t tilingMode, uint32_t aspectRatio, uint32_t width, uint32_t height, uint32_t pitch);
int32_t sceVideoOutRegisterBuffers(int32_t handle, int32_t startIndex, void * const *bufferArray, int32_t bufferCount, const OrbisVideoOutBufferAttribute *attr);
int32_t sceVideoOutSetFlipRate(int32_t handle, int32_t fliprate);
int32_t sceVideoOutAddFlipEvent(OrbisKernelEqueue eq, int32_t handle, void *flipArg);
int32_t sceVideoOutSubmitFlip(GuestContext *ctx, int32_t handle, int32_t bufferIndex, uint32_t flipMode, int64_t flipArg);
int32_t sceVideoOutGetFlipStatus(int32_t handle, OrbisVideoOutFlipStatus *status);

void shim_sceVideoOutOpen(GuestContext *ctx);
void shim_sceVideoOutClose(GuestContext *ctx);
void shim_sceVideoOutSetBufferAttribute(GuestContext *ctx);
void shim_sceVideoOutRegisterBuffers(GuestContext *ctx);
void shim_sceVideoOutSetFlipRate(GuestContext *ctx);
void shim_sceVideoOutAddFlipEvent(GuestContext *ctx);
void shim_sceVideoOutSubmitFlip(GuestContext *ctx);
void shim_sceVideoOutGetFlipStatus(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_VIDEOOUT_H
