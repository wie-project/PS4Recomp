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
int sceKernelWaitEqueue(OrbisKernelEqueue eq, OrbisKernelEvent *eventsOut, int numEvents, int *outCount, const SceKernelTimeval *timeout);
int ps4_equeue_post_event(OrbisKernelEqueue eq, uint64_t ident, int16_t filter, int64_t data, void *udata);

void shim_sceKernelCreateEqueue(GuestContext *ctx);
void shim_sceKernelDeleteEqueue(GuestContext *ctx);
void shim_sceKernelWaitEqueue(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_EQUEUE_H
