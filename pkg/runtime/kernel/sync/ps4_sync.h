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
void shim_pthread_mutex_isowned_np(GuestContext *ctx);
void shim_pthread_mutexattr_init(GuestContext *ctx);
void shim_pthread_condattr_init(GuestContext *ctx);
void shim_pthread_condattr_destroy(GuestContext *ctx);
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
void shim_pthread_rwlock_tryrdlock(GuestContext *ctx);
void shim_pthread_rwlock_trywrlock(GuestContext *ctx);
void shim_pthread_rwlock_unlock(GuestContext *ctx);
void shim_pthread_rwlock_init(GuestContext *ctx);
void shim_pthread_rwlock_destroy(GuestContext *ctx);
void shim_pthread_rwlockattr_init(GuestContext *ctx);
void shim_pthread_rwlockattr_destroy(GuestContext *ctx);
void shim_pthread_rwlockattr_settype_np(GuestContext *ctx);
void shim_pthread_rwlockattr_gettype_np(GuestContext *ctx);
void shim_pthread_rwlockattr_setpshared(GuestContext *ctx);
void shim_pthread_rwlockattr_getpshared(GuestContext *ctx);
void shim_scePthreadRwlockTryrdlock(GuestContext *ctx);
void shim_scePthreadRwlockTrywrlock(GuestContext *ctx);
void shim_scePthreadRwlockInit(GuestContext *ctx);
void shim_scePthreadRwlockDestroy(GuestContext *ctx);
void shim_scePthreadRwlockRdlock(GuestContext *ctx);
void shim_scePthreadRwlockWrlock(GuestContext *ctx);
void shim_scePthreadRwlockUnlock(GuestContext *ctx);
void shim_scePthreadRwlockattrInit(GuestContext *ctx);
void shim_scePthreadRwlockattrDestroy(GuestContext *ctx);
void shim_scePthreadRwlockattrSettype(GuestContext *ctx);
void shim_scePthreadRwlockattrGettype(GuestContext *ctx);
void shim_scePthreadRwlockattrSetpshared(GuestContext *ctx);
void shim_scePthreadRwlockattrGetpshared(GuestContext *ctx);
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
void shim_scePthreadMutexIsownedNp(GuestContext *ctx);
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

// Host entry points for HLE libraries that use guest ScePthreadMutex / ScePthreadCond variables
// themselves (libSceNpCommon). addr is the guest address of the variable and ctx the calling
// thread's context; they return 0 or a POSIX errno, as the pthread_* shims do, and EINVAL for
// a NULL address.
// type is the guest mutex type (1 error check, 2 recursive, 3 normal, 4 adaptive).
int ps4_guest_mutex_init(GuestContext *ctx, uint64_t addr, int type);
int ps4_guest_mutex_lock(GuestContext *ctx, uint64_t addr);
int ps4_guest_mutex_trylock(GuestContext *ctx, uint64_t addr);
int ps4_guest_mutex_unlock(GuestContext *ctx, uint64_t addr);
int ps4_guest_mutex_destroy(GuestContext *ctx, uint64_t addr);
int ps4_guest_cond_init(GuestContext *ctx, uint64_t addr);
int ps4_guest_cond_destroy(GuestContext *ctx, uint64_t addr);
int ps4_guest_cond_signal(GuestContext *ctx, uint64_t addr);
// Waits on the condition with the mutex held by the caller; usec == 0 waits without a timeout,
// otherwise ETIMEDOUT is returned once usec microseconds have passed.
int ps4_guest_cond_wait(GuestContext *ctx, uint64_t cond_addr, uint64_t mutex_addr, uint64_t usec);

// Prints guest mutex statistics gathered over the last `frames` frames.
void ps4_sync_print_stats(int frames);
// Relocks of a mutex by its owner since start, by mutex type: out[0..4] for types 0..4,
// out[5] for any other value.
void ps4_sync_relocks_by_type(uint64_t out[6]);

// C++ ABI guard shims
void shim_cxa_guard_acquire(GuestContext *ctx);
void shim_cxa_guard_release(GuestContext *ctx);
void shim_cxa_guard_abort(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SYNC_H
