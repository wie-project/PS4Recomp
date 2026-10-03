#ifndef PS4_EQUEUE_H
#define PS4_EQUEUE_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_KERNEL_EVFILT_READ     (-1)
#define ORBIS_KERNEL_EVFILT_WRITE    (-2)
#define ORBIS_KERNEL_EVFILT_USER     (-4)
#define ORBIS_KERNEL_EVFILT_TIMER    (-7)
#define ORBIS_KERNEL_EVFILT_VIDEO_OUT 1

#define ORBIS_KERNEL_ERROR_EBADF      0x80020009
#define ORBIS_KERNEL_ERROR_EINVAL     0x80020016
#define ORBIS_KERNEL_ERROR_ETIMEDOUT   0x8002003c
#define ORBIS_KERNEL_ERROR_EFAULT     0x8002000e

typedef struct OrbisKernelEvent {
    uint64_t ident;
    int16_t filter;
    uint16_t flags;
    uint32_t fflags;
    int64_t data;
    void *udata;
} OrbisKernelEvent;

typedef struct SceKernelTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
} SceKernelTimeval;

typedef int32_t OrbisKernelEqueue;

int sceKernelCreateEqueue(OrbisKernelEqueue *eqOut, const char *name);
int sceKernelDeleteEqueue(OrbisKernelEqueue eq);
int sceKernelWaitEqueue(OrbisKernelEqueue eq, OrbisKernelEvent *eventsOut, int numEvents, int *outCount, const uint32_t *timeout);
int ps4_equeue_post_event(OrbisKernelEqueue eq, uint64_t ident, int16_t filter, int64_t data, void *udata);
int sceKernelAddUserEvent(OrbisKernelEqueue eq, int id);
int sceKernelAddUserEventEdge(OrbisKernelEqueue eq, int id);
int sceKernelTriggerUserEvent(OrbisKernelEqueue eq, int id, void *udata);
int sceKernelDeleteUserEvent(OrbisKernelEqueue eq, int id);
uint64_t sceKernelGetEventId(const OrbisKernelEvent *ev);
int sceKernelGetEventFilter(const OrbisKernelEvent *ev);
void *sceKernelGetEventUserData(const OrbisKernelEvent *ev);
int64_t sceKernelGetEventData(const OrbisKernelEvent *ev);

void shim_sceKernelCreateEqueue(GuestContext *ctx);
void shim_sceKernelDeleteEqueue(GuestContext *ctx);
void shim_sceKernelWaitEqueue(GuestContext *ctx);
void shim_sceKernelAddUserEvent(GuestContext *ctx);
void shim_sceKernelAddUserEventEdge(GuestContext *ctx);
void shim_sceKernelTriggerUserEvent(GuestContext *ctx);
void shim_sceKernelDeleteUserEvent(GuestContext *ctx);
void shim_sceKernelGetEventId(GuestContext *ctx);
void shim_sceKernelGetEventFilter(GuestContext *ctx);
void shim_sceKernelGetEventUserData(GuestContext *ctx);
void shim_sceKernelGetEventData(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_EQUEUE_H
