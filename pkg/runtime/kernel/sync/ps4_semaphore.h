#ifndef PS4_SEMAPHORE_H
#define PS4_SEMAPHORE_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// POSIX semaphore shims
void shim_sem_init(GuestContext *ctx);
void shim_sem_destroy(GuestContext *ctx);
void shim_sem_wait(GuestContext *ctx);
void shim_sem_trywait(GuestContext *ctx);
void shim_sem_post(GuestContext *ctx);
void shim_sem_getvalue(GuestContext *ctx);

// scePthread semaphore shims
void shim_scePthreadSemInit(GuestContext *ctx);
void shim_scePthreadSemDestroy(GuestContext *ctx);
void shim_scePthreadSemWait(GuestContext *ctx);
void shim_scePthreadSemTrywait(GuestContext *ctx);
void shim_scePthreadSemTimedwait(GuestContext *ctx);
void shim_scePthreadSemPost(GuestContext *ctx);
void shim_scePthreadSemGetvalue(GuestContext *ctx);

// Orbis Kernel Semaphores
typedef int32_t OrbisKernelSema;

int sceKernelCreateSema(OrbisKernelSema *sem, const char *pName, uint32_t attr, int32_t initCount, int32_t maxCount, const void *pOptParam);
int sceKernelDeleteSema(OrbisKernelSema sem);
int sceKernelWaitSema(OrbisKernelSema sem, int32_t needCount, uint32_t *pTimeout);
int sceKernelSignalSema(OrbisKernelSema sem, int32_t signalCount);
int sceKernelPollSema(OrbisKernelSema sem, int32_t needCount);
int sceKernelCancelSema(OrbisKernelSema sem, int32_t setCount, int32_t *pNumWaitThreads);

void shim_sceKernelCreateSema(GuestContext *ctx);
void shim_sceKernelDeleteSema(GuestContext *ctx);
void shim_sceKernelWaitSema(GuestContext *ctx);
void shim_sceKernelSignalSema(GuestContext *ctx);
void shim_sceKernelPollSema(GuestContext *ctx);
void shim_sceKernelCancelSema(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SEMAPHORE_H

