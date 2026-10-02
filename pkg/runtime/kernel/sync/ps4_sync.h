// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_SYNC_H
#define PS4_SYNC_H

#include "recomp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

// POSIX Mutex, Cond, Rwlock shims
void shim_pthread_mutex_init(GuestContext *ctx);
void shim_pthread_mutex_lock(GuestContext *ctx);
void shim_pthread_mutex_trylock(GuestContext *ctx);
void shim_pthread_mutex_unlock(GuestContext *ctx);
void shim_pthread_mutex_destroy(GuestContext *ctx);
void shim_pthread_mutexattr_init(GuestContext *ctx);
void shim_pthread_mutexattr_settype(GuestContext *ctx);
void shim_pthread_mutexattr_destroy(GuestContext *ctx);
void shim_pthread_cond_init(GuestContext *ctx);
void shim_pthread_cond_wait(GuestContext *ctx);
void shim_pthread_cond_timedwait(GuestContext *ctx);
void shim_pthread_cond_signal(GuestContext *ctx);
void shim_pthread_cond_broadcast(GuestContext *ctx);
void shim_pthread_cond_destroy(GuestContext *ctx);
void shim_pthread_rwlock_rdlock(GuestContext *ctx);
void shim_pthread_rwlock_wrlock(GuestContext *ctx);
void shim_pthread_rwlock_unlock(GuestContext *ctx);
void shim_pthread_rwlock_init(GuestContext *ctx);
void shim_pthread_rwlock_destroy(GuestContext *ctx);
void shim_pthread_mutex_timedlock(GuestContext *ctx);
void shim_pthread_mutexattr_setprotocol(GuestContext *ctx);
void shim_pthread_once(GuestContext *ctx);

// Orbis Mutex & Cond shims
void shim_scePthreadMutexInit(GuestContext *ctx);
void shim_scePthreadMutexLock(GuestContext *ctx);
void shim_scePthreadMutexTrylock(GuestContext *ctx);
void shim_scePthreadMutexTimedlock(GuestContext *ctx);
void shim_scePthreadMutexUnlock(GuestContext *ctx);
void shim_scePthreadMutexDestroy(GuestContext *ctx);
void shim_scePthreadMutexattrInit(GuestContext *ctx);
void shim_scePthreadMutexattrDestroy(GuestContext *ctx);
void shim_scePthreadMutexattrSettype(GuestContext *ctx);
void shim_scePthreadMutexattrSetprotocol(GuestContext *ctx);
void shim_scePthreadOnce(GuestContext *ctx);
void shim_scePthreadCondInit(GuestContext *ctx);
void shim_scePthreadCondDestroy(GuestContext *ctx);
void shim_scePthreadCondSignal(GuestContext *ctx);
void shim_scePthreadCondBroadcast(GuestContext *ctx);
void shim_scePthreadCondWait(GuestContext *ctx);
void shim_scePthreadCondTimedwait(GuestContext *ctx);
void shim_scePthreadCondattrInit(GuestContext *ctx);
void shim_scePthreadCondattrDestroy(GuestContext *ctx);

// C++ ABI guard shims
void shim_cxa_guard_acquire(GuestContext *ctx);
void shim_cxa_guard_release(GuestContext *ctx);
void shim_cxa_guard_abort(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SYNC_H
