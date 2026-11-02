// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_THREADING_H
#define PS4_THREADING_H

#include "recomp_context.h"
#ifndef __cplusplus
#include <stdatomic.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THREAD_MAGIC 0xd09ba115U
#define TID_TERMINATED 1

typedef enum PthreadState {
  PTHREAD_STATE_RUNNING = 0,
  PTHREAD_STATE_DEAD = 1,
} PthreadState;

typedef enum ThreadFlags {
  THREAD_FLAG_PRIVATE = 1,
  THREAD_FLAG_NEED_SUSPEND = 2,
  THREAD_FLAG_SUSPENDED = 4,
  THREAD_FLAG_DETACHED = 8,
} ThreadFlags;

typedef enum PthreadAttrFlags {
  PTHREAD_ATTR_SCOPE_PROCESS = 0,
  PTHREAD_ATTR_DETACHED = 1,
  PTHREAD_ATTR_SCOPE_SYSTEM = 2,
  PTHREAD_ATTR_INHERIT_SCHED = 4,
  PTHREAD_ATTR_NO_FLOAT = 8,
  PTHREAD_ATTR_STACK_USER = 0x100,
} PthreadAttrFlags;

typedef struct PthreadAttr {
  int sched_policy;
  int sched_inherit;
  int prio;
  int suspend;
  uint32_t flags;
  void *stackaddr_attr;
  size_t stacksize_attr;
  size_t guardsize_attr;
  size_t cpusetsize;
  void *cpuset;
} PthreadAttr;

typedef struct PthreadCleanupEntry {
  uint64_t routine;
  uint64_t arg;
  bool onheap;
  struct PthreadCleanupEntry *next;
} PthreadCleanupEntry;

typedef struct RecompThread {
  uint32_t magic;
  _Atomic int tid;
  _Atomic int refcount;
  _Atomic int state;
  _Atomic uint32_t flags;

  pthread_mutex_t lock;
  pthread_mutex_t join_wait_mutex;
  pthread_cond_t join_wait_cv;

  struct RecompThread *joiner;
  struct RecompThread *join_target;

  pthread_t host_thread;
  uint64_t thread_id;
  GuestContext *ctx;
  uint64_t start_routine;
  uint64_t arg;
  uint64_t ret_val;
  char name[64];

  bool finished;
  bool joined;
  bool detached;
  _Atomic int start_ready;
  int cancel_state;
  PthreadCleanupEntry *cleanup_stack;

  uint64_t stack_base;
  uint64_t stack_size;

  PthreadAttr attr;

  struct RecompThread *next;
} RecompThread;

void recomp_init_main_thread(GuestContext *ctx);
RecompThread *recomp_init_system_thread(GuestContext *ctx, const char *name);
void recomp_cleanup_system_thread(RecompThread *t);
RecompThread *recomp_find_thread(uint64_t handle);
RecompThread *recomp_get_current_thread(void);
void recomp_set_current_thread(RecompThread *t);
const char *recomp_get_current_thread_name(void);
void shim_sceKernelIsStack(GuestContext *ctx);
void recomp_thread_collect(void);

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
void shim_pthread_once(GuestContext *ctx);
void shim_pthread_cancel(GuestContext *ctx);

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
void shim_scePthreadOnce(GuestContext *ctx);
void shim_scePthreadAttrGetaffinity(GuestContext *ctx);
void shim_scePthreadAttrGetdetachstate(GuestContext *ctx);
void shim_scePthreadAttrGet(GuestContext *ctx);
void shim_scePthreadGetname(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_THREADING_H
