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

// Scheduling policies (shadPS4 SchedPolicy) and their priority ranges (pthread_attr.cpp).
#define PS4_SCHED_FIFO 1
#define PS4_SCHED_OTHER 2
#define PS4_SCHED_RR 3
#define PS4_PTHREAD_INHERIT_SCHED 4
#define PS4_PRIO_FIFO_LOWEST 256
#define PS4_PRIO_FIFO_HIGHEST 767
#define PS4_PRIO_FIFO_DEFAULT 700

// Stack sizes (shadPS4 pthread.h, pthread_attr.cpp): the attribute default, the smallest size
// an attribute accepts, and what a thread on a runtime-allocated stack gets on top of it for
// HLE code (posix_pthread_create_name_np).
#define PS4_THR_STACK_DEFAULT (1024u * 1024u)
#define PS4_THR_STACK_MIN (16u * 1024u)
#define PS4_THR_STACK_HLE_EXTRA (128u * 1024u)

// The return value of a thread that was cancelled (FreeBSD PTHREAD_CANCELED).
#define PS4_PTHREAD_CANCELED 1ull

// The CPU set of the affinity calls (shadPS4 Cpuset).
typedef struct Ps4Cpuset {
  uint64_t bits;
  uint64_t reserved;
} Ps4Cpuset;

// A thread attribute (shadPS4 PthreadAttr). The guest's ScePthreadAttr variable holds a host
// pointer to one. stackaddr_attr is a guest address; cpuset is a host allocation owned by the
// attribute (or thread) holding it.
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

// The default attribute (shadPS4 PthreadAttrDefault).
extern const PthreadAttr ps4_pthread_attr_default;

// A cleanup handler (shadPS4 PthreadCleanup): the guest routine(arg), or for a handler the
// runtime pushes itself (a join or pthread_once backing out on cancellation) host_fn(host_arg).
typedef struct PthreadCleanupEntry {
  uint64_t routine;
  uint64_t arg;
  void (*host_fn)(void *arg);
  void *host_arg;
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
  // A thread made by ps4_create_host_entry_thread runs host_entry(ctx, host_arg) instead of
  // the guest start_routine.
  void (*host_entry)(GuestContext *ctx, void *arg);
  void *host_arg;
  uint64_t ret_val;
  char name[64];

  bool finished;
  bool joined;
  bool detached;
  _Atomic int start_ready;
  PthreadCleanupEntry *cleanup_stack;

  // Deferred cancellation (shadPS4 Pthread cancel_*): a pending cancel acts at the thread's
  // next cancellation point while cancellation is enabled. A thread blocked in a cancellation
  // point publishes the host mutex and condition it sleeps on under cancel_lk, so that
  // pthread_cancel can wake it.
  pthread_mutex_t cancel_lk;
  _Atomic bool cancel_pending;
  _Atomic bool cancel_enable;
  _Atomic bool cancel_async;
  bool exiting;
  pthread_mutex_t *cancel_wait_mx;
  pthread_cond_t *cancel_wait_cv;

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

// Thread entry points for HLE libraries that create and join guest-visible threads themselves
// (libSceNpCommon's sceNpCreateThread and callout thread). They return 0 or a POSIX errno, as
// the pthread_* shims do. thread_out is the guest address of a ScePthread variable that
// receives the new thread's handle (0 for none); attr may be NULL for the defaults.
int ps4_create_guest_thread(GuestContext *ctx, uint64_t thread_out, const PthreadAttr *attr,
                            uint64_t start_routine, uint64_t arg, const char *name);
// Like ps4_create_guest_thread, but the thread runs the host function entry(child_ctx, arg) on
// its own guest context and stack: a library worker that shadPS4 starts with a host start
// routine on a guest pthread. The thread's return value is 0.
int ps4_create_host_entry_thread(GuestContext *ctx, uint64_t thread_out, const PthreadAttr *attr,
                                 void (*entry)(GuestContext *child_ctx, void *arg), void *arg,
                                 const char *name);
// Joins the thread with the given ScePthread handle; *ret (when not NULL) receives its return
// value.
int ps4_join_guest_thread(uint64_t handle, uint64_t *ret);

// Cancellation points of blocking calls outside this file (condition variables, semaphores).
// With mx locked, ps4_thread_cancel_wait_begin publishes the mutex and condition the calling
// thread is about to sleep on and returns true when a cancel is already pending (the caller
// must not sleep then); ps4_thread_cancel_wait_end withdraws them. A cancel broadcasts cv with
// mx locked, so it cannot be lost between the check and the sleep. Both accept t == NULL (a
// host thread that is not a guest pthread), which is never cancelled.
bool ps4_thread_cancel_wait_begin(RecompThread *t, pthread_mutex_t *mx, pthread_cond_t *cv);
void ps4_thread_cancel_wait_end(RecompThread *t);
// Whether the live thread with this handle sleeps on cv in a cancellation point: 0 when it
// does, 1 when it does not, ESRCH when there is no such thread (pthread_cond_signalto_np).
int ps4_thread_waits_on(uint64_t handle, const pthread_cond_t *cv);
// Acts on a pending cancel of the calling thread (shadPS4 PthreadTestCancel): runs its cleanup
// handlers and destructors and ends it with PS4_PTHREAD_CANCELED. Returns when there is none.
void ps4_thread_testcancel(void);
// Pushes and pops a host cleanup handler of the calling thread (e lives in the caller's frame;
// pop removes it without running it). Without a current guest thread they do nothing.
void ps4_thread_cleanup_push_host(PthreadCleanupEntry *e, void (*fn)(void *), void *arg);
void ps4_thread_cleanup_pop_host(PthreadCleanupEntry *e);

// Thread attribute helpers shared with the thread calls (ps4_pthread_attr.c). They take the
// attribute itself and return 0 or a POSIX errno.
int ps4_pthread_attr_getaffinity(const PthreadAttr *attr, size_t cpusetsize, void *cpusetp);
int ps4_pthread_attr_setaffinity(PthreadAttr *attr, size_t cpusetsize, const void *cpusetp);
// Copies src into dst; dst gets its own copy of the CPU set (any it had is reused or freed).
void ps4_pthread_attr_copy(PthreadAttr *dst, const PthreadAttr *src);
// Frees what an attribute owns (its CPU set).
void ps4_pthread_attr_release(PthreadAttr *attr);

// The routine _sceKernelSetThreadDtors registered, called by every exiting thread.
void ps4_thread_set_dtors(uint64_t dtor);
// The destructor registered for a pthread key (0 when the key is not allocated).
uint64_t ps4_pthread_key_destructor(uint32_t key);

// Thread shims (ps4_threading.c): pthread_* return a POSIX errno, scePthread* the Orbis error.
void shim_pthread_create(GuestContext *ctx);
void shim_pthread_create_name_np(GuestContext *ctx);
void shim_pthread_join(GuestContext *ctx);
void shim_pthread_timedjoin_np(GuestContext *ctx);
void shim_pthread_detach(GuestContext *ctx);
void shim_pthread_exit(GuestContext *ctx);
void shim_pthread_self(GuestContext *ctx);
void shim_pthread_equal(GuestContext *ctx);
void shim_pthread_yield(GuestContext *ctx);
void shim_pthread_getthreadid_np(GuestContext *ctx);
void shim_pthread_key_create(GuestContext *ctx);
void shim_pthread_key_delete(GuestContext *ctx);
void shim_pthread_setspecific(GuestContext *ctx);
void shim_pthread_getspecific(GuestContext *ctx);
void shim___pthread_cleanup_push_imp(GuestContext *ctx);
void shim___pthread_cleanup_pop_imp(GuestContext *ctx);
void shim_pthread_cleanup_push(GuestContext *ctx);
void shim_pthread_cleanup_pop(GuestContext *ctx);
void shim_pthread_getschedparam(GuestContext *ctx);
void shim_pthread_setschedparam(GuestContext *ctx);
void shim_pthread_setprio(GuestContext *ctx);
void shim_sched_get_priority_max(GuestContext *ctx);
void shim_sched_get_priority_min(GuestContext *ctx);
void shim_pthread_getaffinity_np(GuestContext *ctx);
void shim_pthread_setaffinity_np(GuestContext *ctx);
void shim_pthread_getname_np(GuestContext *ctx);
void shim_pthread_rename_np(GuestContext *ctx);
void shim_pthread_set_name_np(GuestContext *ctx);
void shim_pthread_cancel(GuestContext *ctx);
void shim_pthread_setcancelstate(GuestContext *ctx);
void shim_pthread_setcanceltype(GuestContext *ctx);
void shim_pthread_testcancel(GuestContext *ctx);
void shim_pthread_attr_get_np(GuestContext *ctx);
void shim_pthread_suspend_user_context_np(GuestContext *ctx);
void shim_pthread_resume_user_context_np(GuestContext *ctx);
void shim_pthread_suspend_np(GuestContext *ctx);
void shim_pthread_resume_np(GuestContext *ctx);
void shim_pthread_get_user_context_np(GuestContext *ctx);
void shim_pthread_set_user_context_np(GuestContext *ctx);
void shim_pthread_sigmask(GuestContext *ctx);
void shim___tls_get_addr(GuestContext *ctx);

void shim_scePthreadCreate(GuestContext *ctx);
void shim_scePthreadJoin(GuestContext *ctx);
void shim_scePthreadDetach(GuestContext *ctx);
void shim_scePthreadExit(GuestContext *ctx);
void shim_scePthreadSelf(GuestContext *ctx);
void shim_scePthreadEqual(GuestContext *ctx);
void shim_scePthreadYield(GuestContext *ctx);
void shim_scePthreadGetthreadid(GuestContext *ctx);
void shim_scePthreadKeyCreate(GuestContext *ctx);
void shim_scePthreadKeyDelete(GuestContext *ctx);
void shim_scePthreadSetspecific(GuestContext *ctx);
void shim_scePthreadGetspecific(GuestContext *ctx);
void shim_scePthreadGetschedparam(GuestContext *ctx);
void shim_scePthreadSetschedparam(GuestContext *ctx);
void shim_scePthreadGetprio(GuestContext *ctx);
void shim_scePthreadSetprio(GuestContext *ctx);
void shim_scePthreadGetaffinity(GuestContext *ctx);
void shim_scePthreadSetaffinity(GuestContext *ctx);
void shim_scePthreadGetname(GuestContext *ctx);
void shim_scePthreadRename(GuestContext *ctx);
void shim_scePthreadCancel(GuestContext *ctx);
void shim_scePthreadSetcancelstate(GuestContext *ctx);
void shim_scePthreadSetcanceltype(GuestContext *ctx);
void shim_scePthreadTestcancel(GuestContext *ctx);
void shim_scePthreadAttrGet(GuestContext *ctx);

// Thread attribute shims (ps4_pthread_attr.c).
#define PS4_ATTR_SHIM_DECL(posix, sce)                                                             \
  void shim_pthread_attr_##posix(GuestContext *ctx);                                               \
  void shim_scePthreadAttr##sce(GuestContext *ctx);
PS4_ATTR_SHIM_DECL(init, Init)
PS4_ATTR_SHIM_DECL(destroy, Destroy)
PS4_ATTR_SHIM_DECL(getdetachstate, Getdetachstate)
PS4_ATTR_SHIM_DECL(getguardsize, Getguardsize)
PS4_ATTR_SHIM_DECL(getinheritsched, Getinheritsched)
PS4_ATTR_SHIM_DECL(getschedparam, Getschedparam)
PS4_ATTR_SHIM_DECL(getschedpolicy, Getschedpolicy)
PS4_ATTR_SHIM_DECL(getscope, Getscope)
PS4_ATTR_SHIM_DECL(getstack, Getstack)
PS4_ATTR_SHIM_DECL(getstackaddr, Getstackaddr)
PS4_ATTR_SHIM_DECL(getstacksize, Getstacksize)
PS4_ATTR_SHIM_DECL(setschedpolicy, Setschedpolicy)
PS4_ATTR_SHIM_DECL(setscope, Setscope)
PS4_ATTR_SHIM_DECL(setstack, Setstack)
PS4_ATTR_SHIM_DECL(setstackaddr, Setstackaddr)
PS4_ATTR_SHIM_DECL(setstacksize, Setstacksize)
PS4_ATTR_SHIM_DECL(setdetachstate, Setdetachstate)
PS4_ATTR_SHIM_DECL(setschedparam, Setschedparam)
PS4_ATTR_SHIM_DECL(setinheritsched, Setinheritsched)
PS4_ATTR_SHIM_DECL(setguardsize, Setguardsize)
#undef PS4_ATTR_SHIM_DECL
void shim_pthread_attr_setcreatesuspend_np(GuestContext *ctx);
void shim_scePthreadAttrSetcreatesuspend(GuestContext *ctx);
void shim_pthread_attr_getaffinity_np(GuestContext *ctx);
void shim_pthread_attr_setaffinity_np(GuestContext *ctx);
void shim_scePthreadAttrGetaffinity(GuestContext *ctx);
void shim_scePthreadAttrSetaffinity(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_THREADING_H
