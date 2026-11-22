#ifndef PS4_ULT_H
#define PS4_ULT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
#define ORBIS_ULT_OK                  0
#define ORBIS_ULT_ERROR_INVALID_ARG   ((int32_t)0x80540001)
#define ORBIS_ULT_ERROR_NULL_POINTER  ((int32_t)0x80540002)
#define ORBIS_ULT_ERROR_OUT_OF_MEMORY ((int32_t)0x80540003)

typedef struct SceUltMutexOptParam {
    uint32_t size;
    uint32_t attribute;
    uint64_t reserved;
} SceUltMutexOptParam;

typedef struct SceUltWaitingQueueResourcePoolOptParam {
    uint32_t size;
    uint32_t attribute;
    uint64_t reserved;
} SceUltWaitingQueueResourcePoolOptParam;

// C API
int32_t sceUltInitialize(void *param);
int32_t sceUltFinalize(void);
int32_t _sceUltMutexOptParamInitialize(SceUltMutexOptParam *opt);
int32_t _sceUltMutexCreate(void *mutex, const char *name, const SceUltMutexOptParam *opt, void *workArea);
int32_t sceUltMutexLock(void *mutex);
int32_t sceUltMutexUnlock(void *mutex);
int32_t sceUltMutexDestroy(void *mutex);
size_t sceUltWaitingQueueResourcePoolGetWorkAreaSize(uint32_t numResources);
int32_t _sceUltWaitingQueueResourcePoolCreate(void *pool, const char *name, uint32_t numResources, void *workArea, const void *opt);
int32_t sceUltWaitingQueueResourcePoolDestroy(void *pool);

// Shims
void shim_sceUltInitialize(GuestContext *ctx);
void shim_sceUltFinalize(GuestContext *ctx);
void shim__sceUltMutexOptParamInitialize(GuestContext *ctx);
void shim__sceUltMutexCreate(GuestContext *ctx);
void shim_sceUltMutexLock(GuestContext *ctx);
void shim_sceUltMutexUnlock(GuestContext *ctx);
void shim_sceUltMutexDestroy(GuestContext *ctx);
void shim_sceUltWaitingQueueResourcePoolGetWorkAreaSize(GuestContext *ctx);
void shim__sceUltWaitingQueueResourcePoolCreate(GuestContext *ctx);
void shim_sceUltWaitingQueueResourcePoolDestroy(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_ULT_H
