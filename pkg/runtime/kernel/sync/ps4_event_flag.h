#ifndef PS4_EVENT_FLAG_H
#define PS4_EVENT_FLAG_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Event Flag wait modes (Orbis OS)
#define SCE_KERNEL_EVF_WAITMODE_AND         0x01
#define SCE_KERNEL_EVF_WAITMODE_OR          0x02
#define SCE_KERNEL_EVF_WAITMODE_CLEAR_ALL   0x10
#define SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT   0x20

#define SCE_KERNEL_ERROR_EBUSY              0x80020010
#define SCE_KERNEL_ERROR_ETIMEDOUT          0x8002000A
#define SCE_KERNEL_ERROR_EINVAL             0x80020016

typedef int32_t OrbisKernelEventFlag;
typedef uint64_t OrbisKernelUseconds;

typedef struct OrbisKernelEventFlagOptParam {
    size_t size;
} OrbisKernelEventFlagOptParam;

void ps4_event_flag_init(void);
void ps4_event_flag_destroy(void);

int32_t sceKernelCreateEventFlag(OrbisKernelEventFlag *idOut, const char *name, uint32_t attr, uint64_t initPattern, const OrbisKernelEventFlagOptParam *opt);
int32_t sceKernelDeleteEventFlag(OrbisKernelEventFlag id);
int32_t sceKernelSetEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern);
int32_t sceKernelClearEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern);
int32_t sceKernelWaitEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern, uint32_t waitMode, uint64_t *resultPattern, OrbisKernelUseconds *timeout);
int32_t sceKernelPollEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern, uint32_t waitMode, uint64_t *resultPattern);

// Guest execution shims
void shim_sceKernelCreateEventFlag(GuestContext *ctx);
void shim_sceKernelDeleteEventFlag(GuestContext *ctx);
void shim_sceKernelSetEventFlag(GuestContext *ctx);
void shim_sceKernelClearEventFlag(GuestContext *ctx);
void shim_sceKernelWaitEventFlag(GuestContext *ctx);
void shim_sceKernelPollEventFlag(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_EVENT_FLAG_H
