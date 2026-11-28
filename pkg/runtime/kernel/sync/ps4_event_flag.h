// SPDX-License-Identifier: GPL-2.0-or-later
//
// Orbis event flags, after shadPS4 (src/core/libraries/kernel/threads/event_flag.cpp).
#ifndef PS4_EVENT_FLAG_H
#define PS4_EVENT_FLAG_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Creation attributes: waiter queue order and how many threads may wait.
#define SCE_KERNEL_EVF_ATTR_TH_FIFO 0x01
#define SCE_KERNEL_EVF_ATTR_TH_PRIO 0x02
#define SCE_KERNEL_EVF_ATTR_SINGLE 0x10
#define SCE_KERNEL_EVF_ATTR_MULTI 0x20

// Event Flag wait modes (Orbis OS)
#define SCE_KERNEL_EVF_WAITMODE_AND         0x01
#define SCE_KERNEL_EVF_WAITMODE_OR          0x02
#define SCE_KERNEL_EVF_WAITMODE_CLEAR_ALL   0x10
#define SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT   0x20

#define SCE_KERNEL_ERROR_EPERM              0x80020001
#define SCE_KERNEL_ERROR_ESRCH              0x80020003
#define SCE_KERNEL_ERROR_EACCES             0x8002000d
#define SCE_KERNEL_ERROR_EBUSY              0x80020010
#define SCE_KERNEL_ERROR_EINVAL             0x80020016
#define SCE_KERNEL_ERROR_ETIMEDOUT          0x8002003c
#define SCE_KERNEL_ERROR_ENAMETOOLONG       0x8002003f
#define SCE_KERNEL_ERROR_ECANCELED          0x80020055

// The guest's SceKernelEventFlag is a pointer-sized handle; the runtime hands out table ids.
typedef uint64_t OrbisKernelEventFlag;
typedef uint32_t OrbisKernelUseconds;

typedef struct OrbisKernelEventFlagOptParam {
    size_t size;
} OrbisKernelEventFlagOptParam;

int32_t sceKernelCreateEventFlag(OrbisKernelEventFlag *idOut, const char *name, uint32_t attr, uint64_t initPattern, const OrbisKernelEventFlagOptParam *opt);
int32_t sceKernelDeleteEventFlag(OrbisKernelEventFlag id);
int32_t sceKernelSetEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern);
int32_t sceKernelClearEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern);
int32_t sceKernelWaitEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern, uint32_t waitMode, uint64_t *resultPattern, OrbisKernelUseconds *timeout);
int32_t sceKernelPollEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern, uint32_t waitMode, uint64_t *resultPattern);
int32_t sceKernelCancelEventFlag(OrbisKernelEventFlag id, uint64_t setPattern, int32_t *pNumWaitThreads);

// Guest execution shims
void shim_sceKernelCreateEventFlag(GuestContext *ctx);
void shim_sceKernelDeleteEventFlag(GuestContext *ctx);
void shim_sceKernelOpenEventFlag(GuestContext *ctx);
void shim_sceKernelCloseEventFlag(GuestContext *ctx);
void shim_sceKernelSetEventFlag(GuestContext *ctx);
void shim_sceKernelClearEventFlag(GuestContext *ctx);
void shim_sceKernelWaitEventFlag(GuestContext *ctx);
void shim_sceKernelPollEventFlag(GuestContext *ctx);
void shim_sceKernelCancelEventFlag(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_EVENT_FLAG_H
