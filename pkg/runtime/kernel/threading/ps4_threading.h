// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_THREADING_H
#define PS4_THREADING_H

#include "recomp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void recomp_init_main_thread(GuestContext *ctx);

// POSIX Pthread shims
void shim_pthread_create(GuestContext *ctx);
void shim_pthread_join(GuestContext *ctx);
void shim_pthread_detach(GuestContext *ctx);
void shim_pthread_self(GuestContext *ctx);
void shim_pthread_equal(GuestContext *ctx);
void shim_pthread_key_create(GuestContext *ctx);
void shim_pthread_key_delete(GuestContext *ctx);
void shim_pthread_setspecific(GuestContext *ctx);
void shim_pthread_getspecific(GuestContext *ctx);
void shim_pthread_attr_init(GuestContext *ctx);
void shim_pthread_attr_destroy(GuestContext *ctx);
void shim_pthread_attr_setdetachstate(GuestContext *ctx);
void shim_pthread_attr_setstacksize(GuestContext *ctx);
void shim_pthread_getschedparam(GuestContext *ctx);
void shim_pthread_setschedparam(GuestContext *ctx);
void shim_pthread_setcanceltype(GuestContext *ctx);
void shim_pthread_setcancelstate(GuestContext *ctx);
void shim___pthread_cleanup_push_imp(GuestContext *ctx);
void shim___pthread_cleanup_pop_imp(GuestContext *ctx);
void shim_pthread_cleanup_push(GuestContext *ctx);
void shim_pthread_cleanup_pop(GuestContext *ctx);
void shim_sched_get_priority_max(GuestContext *ctx);
void shim_sched_get_priority_min(GuestContext *ctx);
void shim_pthread_sigmask(GuestContext *ctx);

// Orbis Pthread shims
void shim_scePthreadCreate(GuestContext *ctx);
void shim_scePthreadJoin(GuestContext *ctx);
void shim_scePthreadDetach(GuestContext *ctx);
void shim_scePthreadExit(GuestContext *ctx);
void shim_scePthreadSelf(GuestContext *ctx);
void shim_scePthreadEqual(GuestContext *ctx);
void shim_scePthreadYield(GuestContext *ctx);
void shim_scePthreadGetthreadid(GuestContext *ctx);
void shim_scePthreadSetprio(GuestContext *ctx);
void shim_scePthreadGetprio(GuestContext *ctx);
void shim_scePthreadSetaffinity(GuestContext *ctx);
void shim_scePthreadGetaffinity(GuestContext *ctx);
void shim_scePthreadAttrInit(GuestContext *ctx);
void shim_scePthreadAttrDestroy(GuestContext *ctx);
void shim_scePthreadAttrSetstacksize(GuestContext *ctx);
void shim_scePthreadAttrSetdetachstate(GuestContext *ctx);
void shim_scePthreadAttrSetschedpolicy(GuestContext *ctx);
void shim_scePthreadAttrSetschedparam(GuestContext *ctx);
void shim_scePthreadAttrGetschedparam(GuestContext *ctx);
void shim_scePthreadAttrSetinheritsched(GuestContext *ctx);
void shim_scePthreadAttrSetaffinity(GuestContext *ctx);
void shim_scePthreadSetcancelstate(GuestContext *ctx);
void shim_scePthreadAttrGetstacksize(GuestContext *ctx);
void shim_scePthreadAttrGetstack(GuestContext *ctx);
void shim_scePthreadAttrGetstackaddr(GuestContext *ctx);
void shim_scePthreadAttrSetstack(GuestContext *ctx);
void shim_scePthreadRename(GuestContext *ctx);
void shim_scePthreadGetschedparam(GuestContext *ctx);
void shim_scePthreadSetschedparam(GuestContext *ctx);
void shim_scePthreadKeyCreate(GuestContext *ctx);
void shim_scePthreadKeyDelete(GuestContext *ctx);
void shim_scePthreadSetspecific(GuestContext *ctx);
void shim_scePthreadGetspecific(GuestContext *ctx);
void shim___tls_get_addr(GuestContext *ctx);
void shim_pthread_once(GuestContext *ctx);
void shim_pthread_cancel(GuestContext *ctx);
void shim_scePthreadOnce(GuestContext *ctx);
void shim_scePthreadAttrGetaffinity(GuestContext *ctx);
void shim_scePthreadAttrGetdetachstate(GuestContext *ctx);
void shim_scePthreadAttrGet(GuestContext *ctx);
void shim_scePthreadGetname(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_THREADING_H
