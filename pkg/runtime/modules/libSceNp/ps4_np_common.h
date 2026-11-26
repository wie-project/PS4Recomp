// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpCommon (and its libSceNpCommonCompat alias), following shadPS4
// src/core/libraries/np/np_common.cpp: NpId / online id helpers, the NP mutex, condition
// variable and thread wrappers over the libkernel pthread objects, and the callout context (a
// worker thread that runs guest handlers at their deadlines).

#ifndef PS4_NP_COMMON_H
#define PS4_NP_COMMON_H

#include <stdint.h>
#include "ps4_np.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_NP_ERROR_INVALID_PLATFORM_TYPE ((int32_t)0x80550004)
#define ORBIS_NP_CALLOUT_ERROR_NOT_INITIALIZED ((int32_t)0x80558001)
#define ORBIS_NP_CALLOUT_ERROR_ALREADY_INITIALIZED ((int32_t)0x80558002)
#define ORBIS_NP_CALLOUT_ERROR_DUPLICATE_ENTRY ((int32_t)0x80558006)
#define ORBIS_NP_LW_COND_ERROR_TIMEDOUT ((int32_t)0x8055800B)
#define ORBIS_NP_LW_MUTEX_ERROR_BUSY ((int32_t)0x8055800F)

// sceNpGetPlatformType results.
#define ORBIS_NP_PLATFORM_TYPE_NONE 0
#define ORBIS_NP_PLATFORM_TYPE_PS3 1
#define ORBIS_NP_PLATFORM_TYPE_VITA 2
#define ORBIS_NP_PLATFORM_TYPE_PS4 3

// Guest layouts. Pointers are guest addresses; thread, mutex and cond are ScePthread,
// ScePthreadMutex and ScePthreadCond variables.
typedef struct OrbisNpCalloutEntry {
    uint64_t next;          // OrbisNpCalloutEntry*
    uint64_t handler;       // void (*)(uint64_t arg)
    int64_t deadline_usec;  // sceNpGetSystemClockUsec time the handler is due
    uint64_t arg;
} OrbisNpCalloutEntry;

typedef struct OrbisNpCalloutContext {
    uint64_t thread;
    uint64_t mutex;
    uint64_t cond;
    int32_t active;
    int32_t stop_requested;
    uint64_t head;          // OrbisNpCalloutEntry*, sorted by deadline
} OrbisNpCalloutContext;

_Static_assert(sizeof(OrbisNpCalloutEntry) == 0x20, "OrbisNpCalloutEntry layout");
_Static_assert(offsetof(OrbisNpCalloutContext, active) == 0x18, "OrbisNpCalloutContext layout");
_Static_assert(offsetof(OrbisNpCalloutContext, head) == 0x20, "OrbisNpCalloutContext layout");
_Static_assert(sizeof(OrbisNpCalloutContext) == 0x28, "OrbisNpCalloutContext layout");

// C API. Mutex, cond, thread and callout arguments are guest addresses; ctx is the calling
// thread's context. Results are SCE codes (kernel errors as 0x80020000 | errno).
int32_t sceNpCmpOnlineId(const OrbisNpOnlineId *id1, const OrbisNpOnlineId *id2);
int32_t sceNpCmpNpId(const OrbisNpId *id1, const OrbisNpId *id2);
int32_t sceNpCmpNpIdInOrder(const OrbisNpId *id1, const OrbisNpId *id2, uint32_t *out_result);
uint32_t sceNpMutexInit(GuestContext *ctx, uint64_t mutex, uint64_t flags);
uint32_t sceNpMutexLock(GuestContext *ctx, uint64_t mutex);
uint32_t sceNpMutexUnlock(GuestContext *ctx, uint64_t mutex);
uint32_t sceNpMutexTryLock(GuestContext *ctx, uint64_t mutex);
int32_t sceNpMutexDestroy(GuestContext *ctx, uint64_t mutex);
int32_t sceNpCondInit(GuestContext *ctx, uint64_t cond);
int32_t sceNpCondDestroy(GuestContext *ctx, uint64_t cond);
int32_t sceNpCondSignal(GuestContext *ctx, uint64_t cond);
int32_t sceNpCondTimedwait(GuestContext *ctx, uint64_t cond, uint64_t mutex, uint32_t usec);
int32_t sceNpCreateThread(GuestContext *ctx, uint64_t thread, uint64_t start_routine, uint64_t arg,
                          int32_t priority, uint64_t stack_size, uint64_t affinity_mask,
                          const char *name);
uint32_t sceNpJoinThread(uint64_t thread, uint64_t *ret);
int32_t sceNpGetSystemClockUsec(int64_t *usec);
int32_t sceNpGetPlatformType(const OrbisNpId *np_id);
int32_t sceNpIntIsValidOnlineId(const OrbisNpOnlineId *id);
void sceNpGetSdkVersion(char *version_buf);
int32_t sceNpCalloutInitCtx(GuestContext *ctx, uint64_t callout_ctx, const char *name,
                            uint64_t stack_size, int32_t priority, uint64_t affinity_mask);
int32_t sceNpCalloutStartOnCtx64(GuestContext *ctx, uint64_t callout_ctx, uint64_t entry,
                                 int64_t delay_usec, uint64_t handler, uint64_t arg);
int32_t sceNpCalloutStopOnCtx(GuestContext *ctx, uint64_t callout_ctx, uint64_t entry,
                              uint32_t *removed);
void sceNpCalloutTermCtx(GuestContext *ctx, uint64_t callout_ctx);

void shim_sceNpCmpOnlineId(GuestContext *ctx);
void shim_sceNpCmpNpId(GuestContext *ctx);
void shim_sceNpCmpNpIdInOrder(GuestContext *ctx);
void shim_sceNpMutexInit(GuestContext *ctx);
void shim_sceNpMutexLock(GuestContext *ctx);
void shim_sceNpMutexUnlock(GuestContext *ctx);
void shim_sceNpMutexTryLock(GuestContext *ctx);
void shim_sceNpMutexDestroy(GuestContext *ctx);
void shim_sceNpLwMutexInit(GuestContext *ctx);
void shim_sceNpLwMutexLock(GuestContext *ctx);
void shim_sceNpLwMutexUnlock(GuestContext *ctx);
void shim_sceNpLwMutexTryLock(GuestContext *ctx);
void shim_sceNpLwMutexDestroy(GuestContext *ctx);
void shim_sceNpCondInit(GuestContext *ctx);
void shim_sceNpCondDestroy(GuestContext *ctx);
void shim_sceNpCondSignal(GuestContext *ctx);
void shim_sceNpCondTimedwait(GuestContext *ctx);
void shim_sceNpCreateThread(GuestContext *ctx);
void shim_sceNpJoinThread(GuestContext *ctx);
void shim_sceNpGetSystemClockUsec(GuestContext *ctx);
void shim_sceNpGetPlatformType(GuestContext *ctx);
void shim_sceNpIntIsValidOnlineId(GuestContext *ctx);
void shim_sceNpGetSdkVersion(GuestContext *ctx);
void shim_sceNpCalloutInitCtx(GuestContext *ctx);
void shim_sceNpCalloutStartOnCtx(GuestContext *ctx);
void shim_sceNpCalloutStartOnCtx64(GuestContext *ctx);
void shim_sceNpCalloutStopOnCtx(GuestContext *ctx);
void shim_sceNpCalloutTermCtx(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NP_COMMON_H
