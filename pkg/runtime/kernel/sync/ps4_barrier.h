// SPDX-License-Identifier: GPL-2.0-or-later
//
// Barriers (libkernel / libScePosix pthread_barrier*), after shadPS4 kernel/threads/barrier.cpp.
#ifndef PS4_BARRIER_H
#define PS4_BARRIER_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_pthread_barrier_init(GuestContext *ctx);
void shim_pthread_barrier_wait(GuestContext *ctx);
void shim_pthread_barrier_destroy(GuestContext *ctx);
void shim_pthread_barrierattr_init(GuestContext *ctx);
void shim_pthread_barrierattr_destroy(GuestContext *ctx);
void shim_pthread_barrierattr_getpshared(GuestContext *ctx);
void shim_pthread_barrierattr_setpshared(GuestContext *ctx);

void shim_scePthreadBarrierInit(GuestContext *ctx);
void shim_scePthreadBarrierWait(GuestContext *ctx);
void shim_scePthreadBarrierDestroy(GuestContext *ctx);
void shim_scePthreadBarrierattrInit(GuestContext *ctx);
void shim_scePthreadBarrierattrDestroy(GuestContext *ctx);
void shim_scePthreadBarrierattrGetpshared(GuestContext *ctx);
void shim_scePthreadBarrierattrSetpshared(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
