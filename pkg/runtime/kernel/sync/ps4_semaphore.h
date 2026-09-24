#ifndef PS4_SEMAPHORE_H
#define PS4_SEMAPHORE_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_sem_init(GuestContext *ctx);
void shim_sem_destroy(GuestContext *ctx);
void shim_sem_wait(GuestContext *ctx);
void shim_sem_trywait(GuestContext *ctx);
void shim_sem_post(GuestContext *ctx);
void shim_sem_getvalue(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SEMAPHORE_H
