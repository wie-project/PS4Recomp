// SPDX-License-Identifier: GPL-2.0-or-later
//
// Guest pthreads, after shadPS4 (src/core/libraries/kernel/threads/pthread.cpp, pthread_spec.cpp,
// pthread_clean.cpp, thread_state.cpp and stack.cpp). Every guest thread is a host thread that
// runs recompiled code on its own GuestContext; its RecompThread is the ScePthread handle the
// guest sees. Thread attributes are in ps4_pthread_attr.c.
//
// The pthread_* functions return a POSIX errno and the scePthread* ones the Orbis kernel error
// (0x80020000 + errno), as shadPS4 registers them (plain or through its ORBIS() wrapper).
//
// Deliberate differences from shadPS4:
// - pthread_get_user_context_np reads another thread's guest registers from the host registers
//   recompiled code keeps them in when that thread is suspended in guest code, otherwise from
//   its context; shadPS4 on macOS reads the native x86 thread state.
// - Asynchronous cancellation of another thread takes effect at that thread's next cancellation
//   point: shadPS4 interrupts it with a host signal, and recompiled code cannot be unwound from
//   an arbitrary instruction.
// - A thread attribute asking for a suspended thread (pthread_attr_setcreatesuspend_np) makes
//   the create call fail with ENOTSUP, where shadPS4 asserts.
// - pthread_getname_np pads the name with NULs to the 32-byte buffer (shadPS4 copies only the
//   name's characters).
// - The main thread ends with a plain host pthread_exit when it calls pthread_exit or is
//   cancelled; it has no joiner to wake.

#include "recomp_runtime.h"
#include "ps4_threading.h"
#include <inttypes.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <pthread/qos.h>
#endif

#define ORBIS_KERNEL_ERROR_ESRCH 0x80020003u
#define PTHREAD_DESTRUCTOR_ITERATIONS 4 // shadPS4 PthreadDestructorIterations

// shadPS4 ORBIS(): a nonzero POSIX result becomes 0x80020000 + errno.
static inline uint64_t orbis_err(int e) { return e ? (uint64_t)(0x80020000u + (uint32_t)e) : 0; }

#define RET_POSIX(e)                                                                               \
  do {                                                                                             \
    ctx->rax = (uint64_t)(uint32_t)(e);                                                            \
    SHIM_RETURN();                                                                                 \
  } while (0)
#define RET_ORBIS(e)                                                                               \
  do {                                                                                             \
    ctx->rax = orbis_err(e);                                                                       \
    SHIM_RETURN();                                                                                 \
  } while (0)

static inline void *gptr(GuestContext *ctx, uint64_t addr) { return addr ? ctx->mem_base + addr : NULL; }

const PthreadAttr ps4_pthread_attr_default = {
    .sched_policy = PS4_SCHED_FIFO,
    .sched_inherit = PS4_PTHREAD_INHERIT_SCHED,
    .prio = PS4_PRIO_FIFO_DEFAULT,
    .suspend = 0,
    .flags = PTHREAD_ATTR_SCOPE_SYSTEM,
    .stackaddr_attr = NULL,
    .stacksize_attr = PS4_THR_STACK_DEFAULT,
    .guardsize_attr = 0,
    .cpusetsize = 0,
    .cpuset = NULL,
};

static RecompThread *g_threads = NULL;
static RecompThread *g_gc_list = NULL;
static pthread_mutex_t g_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_next_tid = 1001;
static _Thread_local RecompThread *g_current_thread = NULL;
static _Atomic uint64_t g_thread_dtors; // _sceKernelSetThreadDtors

static void register_thread(RecompThread *t) {
  pthread_mutex_lock(&g_threads_mutex);
  t->next = g_threads;
  g_threads = t;
  pthread_mutex_unlock(&g_threads_mutex);
}

static void unregister_thread_locked(RecompThread *t) {
  RecompThread **curr = &g_threads;
  while (*curr) {
    if (*curr == t) {
      *curr = t->next;
      break;
    }
    curr = &(*curr)->next;
  }
}

// Garbage collection logic matching shadps4 ThreadState::ShouldCollect / TryCollect
static inline bool should_collect_locked(RecompThread *t) {
  return atomic_load_explicit(&t->refcount, memory_order_acquire) == 0 &&
         atomic_load_explicit(&t->state, memory_order_acquire) == PTHREAD_STATE_DEAD &&
         (atomic_load_explicit(&t->flags, memory_order_acquire) & THREAD_FLAG_DETACHED);
}

// Releases t->lock before returning
static void try_collect_locked(RecompThread *t) {
  if (!should_collect_locked(t)) {
    pthread_mutex_unlock(&t->lock);
    return;
  }

  pthread_mutex_lock(&g_threads_mutex);
  if (should_collect_locked(t)) {
    unregister_thread_locked(t);
    t->next = g_gc_list;
    g_gc_list = t;
  }
  pthread_mutex_unlock(&g_threads_mutex);

  pthread_mutex_unlock(&t->lock);
}

static void thread_init_locks(RecompThread *t) {
  pthread_mutex_init(&t->lock, NULL);
  pthread_mutex_init(&t->join_wait_mutex, NULL);
  pthread_cond_init(&t->join_wait_cv, NULL);
  pthread_mutex_init(&t->cancel_lk, NULL);
  atomic_store_explicit(&t->cancel_enable, true, memory_order_relaxed);
}

static void thread_free(RecompThread *t) {
  if (t->ctx) {
    recomp_free_thread_context(t->ctx);
    t->ctx = NULL;
  }
  ps4_pthread_attr_release(&t->attr);
  pthread_mutex_destroy(&t->lock);
  pthread_mutex_destroy(&t->join_wait_mutex);
  pthread_cond_destroy(&t->join_wait_cv);
  pthread_mutex_destroy(&t->cancel_lk);
  free(t);
}

// Free resources of terminated detached threads in g_gc_list
void recomp_thread_collect(void) {
  RecompThread *reclaim_list = NULL;

  pthread_mutex_lock(&g_threads_mutex);
  RecompThread **curr = &g_gc_list;
  while (*curr) {
    RecompThread *t = *curr;
    if (atomic_load_explicit(&t->tid, memory_order_acquire) == TID_TERMINATED) {
      *curr = t->next;
      t->next = reclaim_list;
      reclaim_list = t;
    } else {
      curr = &(*curr)->next;
    }
  }
  pthread_mutex_unlock(&g_threads_mutex);

  while (reclaim_list) {
    RecompThread *t = reclaim_list;
    reclaim_list = t->next;
    thread_free(t);
  }
}

// The thread with this handle in g_threads; g_threads_mutex held.
static RecompThread *find_thread_locked(uint64_t handle) {
  for (RecompThread *curr = g_threads; curr; curr = curr->next) {
    if ((uint64_t)curr == handle || curr->thread_id == handle) return curr;
    if (curr->ctx) {
      if (curr->ctx->fs_base == handle) return curr;
      if (curr->ctx->fs_base && curr->ctx->mem_base && curr->ctx->fs_base + 0x18ULL <= curr->ctx->mem_size) {
        uint64_t t = *(uint64_t *)(curr->ctx->mem_base + curr->ctx->fs_base + 0x10ULL);
        if (t != 0 && t == handle) return curr;
      }
    }
  }
  return NULL;
}

// Handle 0 finds no thread.
RecompThread *recomp_find_thread(uint64_t handle) {
  if (handle == 0) return NULL;
  pthread_mutex_lock(&g_threads_mutex);
  RecompThread *t = find_thread_locked(handle);
  pthread_mutex_unlock(&g_threads_mutex);
  return t;
}

// A reference to the thread with this handle that keeps it from being collected (shadPS4
// ThreadState::RefAdd); ESRCH when there is none, or when it is dead and include_dead is false.
static int thread_ref(uint64_t handle, bool include_dead, RecompThread **out) {
  if (handle == 0) return ESRCH;
  pthread_mutex_lock(&g_threads_mutex);
  RecompThread *t = find_thread_locked(handle);
  if (!t || (!include_dead && atomic_load_explicit(&t->state, memory_order_acquire) == PTHREAD_STATE_DEAD)) {
    pthread_mutex_unlock(&g_threads_mutex);
    return ESRCH;
  }
  atomic_fetch_add_explicit(&t->refcount, 1, memory_order_acq_rel);
  pthread_mutex_unlock(&g_threads_mutex);
  *out = t;
  return 0;
}

// Drops a thread_ref reference with t->lock held, releasing the lock (shadPS4 RefDelete).
static void thread_unref_locked(RecompThread *t) {
  atomic_fetch_sub_explicit(&t->refcount, 1, memory_order_acq_rel);
  try_collect_locked(t);
}

static void thread_unref(RecompThread *t) {
  pthread_mutex_lock(&t->lock);
  thread_unref_locked(t);
}

static void thread_unref_cb(void *t) { thread_unref((RecompThread *)t); }

// The live thread with this handle, referenced and locked (shadPS4 FindThread(.., false)).
static int thread_find_locked(uint64_t handle, RecompThread **out) {
  RecompThread *t;
  const int e = thread_ref(handle, false, &t);
  if (e) return e;
  pthread_mutex_lock(&t->lock);
  if (atomic_load_explicit(&t->state, memory_order_acquire) == PTHREAD_STATE_DEAD) {
    thread_unref_locked(t);
    return ESRCH;
  }
  *out = t;
  return 0;
}

RecompThread *recomp_get_current_thread(void) {
  return g_current_thread;
}

void recomp_set_current_thread(RecompThread *t) {
  g_current_thread = t;
}

const char *recomp_get_current_thread_name(void) {
  if (g_current_thread && g_current_thread->name[0]) {
    return g_current_thread->name;
  }
  return "main";
}

static RecompThread g_main_thread_obj = {0};

void recomp_init_main_thread(GuestContext *ctx) {
  if (ctx->thread_id == 0) ctx->thread_id = 1000;
  g_main_thread_obj.magic = THREAD_MAGIC;
  atomic_store_explicit(&g_main_thread_obj.tid, (int)ctx->thread_id, memory_order_relaxed);
  atomic_store_explicit(&g_main_thread_obj.refcount, 1, memory_order_relaxed);
  atomic_store_explicit(&g_main_thread_obj.state, PTHREAD_STATE_RUNNING, memory_order_relaxed);
  atomic_store_explicit(&g_main_thread_obj.flags, 0, memory_order_relaxed);
  thread_init_locks(&g_main_thread_obj);
  g_main_thread_obj.host_thread = pthread_self();
  g_main_thread_obj.thread_id = ctx->thread_id;
  g_main_thread_obj.ctx = ctx;
  g_main_thread_obj.stack_base = ctx->stack_base;
  g_main_thread_obj.stack_size = ctx->stack_alloc_size;
  g_main_thread_obj.attr = ps4_pthread_attr_default;
  g_main_thread_obj.attr.stackaddr_attr = (void *)(uintptr_t)ctx->stack_base;
  g_main_thread_obj.attr.stacksize_attr = ctx->stack_alloc_size;
  atomic_store_explicit(&g_main_thread_obj.start_ready, 1, memory_order_relaxed);
  strncpy(g_main_thread_obj.name, "main", sizeof(g_main_thread_obj.name) - 1);
  if (ctx->fs_base && ctx->mem_base) {
    *(uint64_t *)(ctx->mem_base + ctx->fs_base + 0x10ULL) = (uint64_t)&g_main_thread_obj;
  }
  g_current_thread = &g_main_thread_obj;
  register_thread(&g_main_thread_obj);
}

RecompThread *recomp_init_system_thread(GuestContext *ctx, const char *name) {
  if (!ctx) return NULL;
  RecompThread *t = (RecompThread *)calloc(1, sizeof(RecompThread));
  if (!t) return NULL;

  int tid = atomic_fetch_add(&g_next_tid, 1);
  ctx->thread_id = (uint64_t)tid;
  t->magic = THREAD_MAGIC;
  atomic_store_explicit(&t->tid, tid, memory_order_relaxed);
  atomic_store_explicit(&t->refcount, 1, memory_order_relaxed);
  atomic_store_explicit(&t->state, PTHREAD_STATE_RUNNING, memory_order_relaxed);
  atomic_store_explicit(&t->flags, THREAD_FLAG_DETACHED, memory_order_relaxed);
  t->detached = true;
  thread_init_locks(t);

  t->host_thread = pthread_self();
  t->thread_id = (uint64_t)tid;
  t->ctx = ctx;
  t->stack_base = ctx->stack_base;
  t->stack_size = ctx->stack_alloc_size;
  t->attr = ps4_pthread_attr_default;
  t->attr.stackaddr_attr = (void *)(uintptr_t)ctx->stack_base;
  t->attr.stacksize_attr = ctx->stack_alloc_size;
  atomic_store_explicit(&t->start_ready, 1, memory_order_relaxed);
  if (name && name[0]) {
    strncpy(t->name, name, sizeof(t->name) - 1);
  } else {
    strncpy(t->name, "system_worker", sizeof(t->name) - 1);
  }

  if (ctx->fs_base && ctx->mem_base) {
    *(uint64_t *)(ctx->mem_base + ctx->fs_base + 0x10ULL) = (uint64_t)t;
  }
  g_current_ctx = ctx;
  g_current_thread = t;
  register_thread(t);
  return t;
}

void recomp_cleanup_system_thread(RecompThread *t) {
  if (!t) return;
  pthread_mutex_lock(&t->lock);
  atomic_store_explicit(&t->state, PTHREAD_STATE_DEAD, memory_order_release);
  atomic_store_explicit(&t->refcount, 0, memory_order_relaxed);
  try_collect_locked(t);
  if (g_current_thread == t) {
    g_current_thread = NULL;
  }
}

// ---- Thread-specific data (shadPS4 pthread_spec.cpp). The key table is process-wide; each
// thread's values and the key generation they were set under are in its GuestContext.

typedef struct PthreadKey {
  bool allocated;
  uint32_t seqno;
  uint64_t destructor;
} PthreadKey;

static PthreadKey g_keys[RECOMP_PTHREAD_KEYS_MAX];
static pthread_mutex_t g_key_lock = PTHREAD_MUTEX_INITIALIZER;

uint64_t ps4_pthread_key_destructor(uint32_t key) {
  if (key >= RECOMP_PTHREAD_KEYS_MAX) return 0;
  pthread_mutex_lock(&g_key_lock);
  const uint64_t d = g_keys[key].allocated ? g_keys[key].destructor : 0;
  pthread_mutex_unlock(&g_key_lock);
  return d;
}

static int key_create(uint32_t *key, uint64_t destructor) {
  pthread_mutex_lock(&g_key_lock);
  for (uint32_t k = 0; k < RECOMP_PTHREAD_KEYS_MAX; k++) {
    if (!g_keys[k].allocated) {
      g_keys[k].allocated = true;
      g_keys[k].destructor = destructor;
      g_keys[k].seqno++;
      pthread_mutex_unlock(&g_key_lock);
      *key = k;
      return 0;
    }
  }
  pthread_mutex_unlock(&g_key_lock);
  return EAGAIN;
}

static int key_delete(uint32_t key) {
  if (key >= RECOMP_PTHREAD_KEYS_MAX) return EINVAL;
  pthread_mutex_lock(&g_key_lock);
  const int e = g_keys[key].allocated ? 0 : EINVAL;
  g_keys[key].allocated = false;
  pthread_mutex_unlock(&g_key_lock);
  return e;
}

static int key_set(GuestContext *ctx, uint32_t key, uint64_t value) {
  if (key >= RECOMP_PTHREAD_KEYS_MAX) return EINVAL;
  pthread_mutex_lock(&g_key_lock);
  const bool allocated = g_keys[key].allocated;
  const uint32_t seqno = g_keys[key].seqno;
  pthread_mutex_unlock(&g_key_lock);
  if (!allocated) return EINVAL;
  ctx->tls_keys[key] = value;
  ctx->tls_seqno[key] = seqno;
  return 0;
}

static uint64_t key_get(GuestContext *ctx, uint32_t key) {
  if (key >= RECOMP_PTHREAD_KEYS_MAX) return 0;
  pthread_mutex_lock(&g_key_lock);
  const bool live = g_keys[key].allocated && g_keys[key].seqno == ctx->tls_seqno[key];
  pthread_mutex_unlock(&g_key_lock);
  return live ? ctx->tls_keys[key] : 0;
}

// Calls the guest function fn(arg) on ctx.
static void guest_call(GuestContext *ctx, uint64_t fn, uint64_t arg) {
  ctx->rdi = arg;
  ctx->rsp -= 8;
  MEM_U64(ctx->rsp) = 0xdeadbeefULL;
  ctx->rip = fn;
  recomp_dispatch(ctx, fn);
}

// shadPS4 _thread_cleanupspecific: the destructors of the thread's non-NULL values, for up to
// PTHREAD_DESTRUCTOR_ITERATIONS rounds while destructors set new ones. A value of a deleted
// key (or one set under an older generation of it) is dropped without its destructor.
static void cleanup_specific(GuestContext *ctx) {
  for (int i = 0; i < PTHREAD_DESTRUCTOR_ITERATIONS; i++) {
    bool any = false;
    for (uint32_t key = 0; key < RECOMP_PTHREAD_KEYS_MAX; key++) {
      const uint64_t data = ctx->tls_keys[key];
      if (!data) continue;
      any = true;
      pthread_mutex_lock(&g_key_lock);
      const uint64_t dtor =
          g_keys[key].allocated && g_keys[key].seqno == ctx->tls_seqno[key] ? g_keys[key].destructor : 0;
      pthread_mutex_unlock(&g_key_lock);
      ctx->tls_keys[key] = 0;
      if (dtor) guest_call(ctx, dtor, data);
    }
    if (!any) break;
  }
}

// ---- Thread exit (shadPS4 posix_pthread_exit, ExitThread).

void ps4_thread_set_dtors(uint64_t dtor) { atomic_store_explicit(&g_thread_dtors, dtor, memory_order_release); }

static void run_cleanup(GuestContext *ctx, PthreadCleanupEntry *c) {
  if (c->host_fn) {
    c->host_fn(c->host_arg);
  } else if (c->routine) {
    guest_call(ctx, c->routine, c->arg);
  }
}

// Ends the calling thread t with status: its cleanup handlers (last pushed first), the
// _sceKernelSetThreadDtors routine and its thread-specific data destructors run on ctx (the
// context the thread runs on: its own or a fiber's), then a joiner is woken.
__attribute__((noreturn)) static void exit_thread(GuestContext *ctx, RecompThread *t, uint64_t status) {
  if (!t || t == &g_main_thread_obj) pthread_exit(NULL);
  if (!ctx) ctx = t->ctx;
  t->exiting = true;
  atomic_store_explicit(&t->cancel_enable, false, memory_order_relaxed);
  atomic_store_explicit(&t->cancel_async, false, memory_order_relaxed);
  t->ret_val = status;
  while (t->cleanup_stack) {
    PthreadCleanupEntry *c = t->cleanup_stack;
    t->cleanup_stack = c->next;
    run_cleanup(ctx, c);
    if (c->onheap) free(c);
  }
  const uint64_t dtors = atomic_load_explicit(&g_thread_dtors, memory_order_acquire);
  if (dtors) guest_call(ctx, dtors, 0);
  cleanup_specific(ctx);

  pthread_mutex_lock(&t->lock);
  atomic_store_explicit(&t->state, PTHREAD_STATE_DEAD, memory_order_release);
  t->finished = true;
  atomic_fetch_sub_explicit(&t->refcount, 1, memory_order_acq_rel);
  try_collect_locked(t); // unlocks t->lock

  // Wake up any joiner waiting on join_wait_cv
  pthread_mutex_lock(&t->join_wait_mutex);
  atomic_store_explicit(&t->tid, TID_TERMINATED, memory_order_release);
  pthread_cond_broadcast(&t->join_wait_cv);
  pthread_mutex_unlock(&t->join_wait_mutex);

  pthread_exit(NULL);
}

// ---- Cancellation (shadPS4 posix_pthread_cancel, PthreadTestCancel).

static inline bool should_cancel(const RecompThread *t) {
  return t && !t->exiting && atomic_load_explicit(&t->cancel_pending, memory_order_acquire) &&
         atomic_load_explicit(&t->cancel_enable, memory_order_acquire);
}

void ps4_thread_testcancel(void) {
  RecompThread *t = g_current_thread;
  if (should_cancel(t)) exit_thread(g_current_ctx, t, PS4_PTHREAD_CANCELED);
}

bool ps4_thread_cancel_wait_begin(RecompThread *t, pthread_mutex_t *mx, pthread_cond_t *cv) {
  if (!t) return false;
  pthread_mutex_lock(&t->cancel_lk);
  const bool cancel = should_cancel(t);
  if (!cancel) {
    t->cancel_wait_mx = mx;
    t->cancel_wait_cv = cv;
  }
  pthread_mutex_unlock(&t->cancel_lk);
  return cancel;
}

int ps4_thread_waits_on(uint64_t handle, const pthread_cond_t *cv) {
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  pthread_mutex_lock(&t->cancel_lk);
  const bool waiting = t->cancel_wait_cv == cv;
  pthread_mutex_unlock(&t->cancel_lk);
  thread_unref_locked(t);
  return waiting ? 0 : 1;
}

void ps4_thread_cancel_wait_end(RecompThread *t) {
  if (!t) return;
  pthread_mutex_lock(&t->cancel_lk);
  t->cancel_wait_mx = NULL;
  t->cancel_wait_cv = NULL;
  pthread_mutex_unlock(&t->cancel_lk);
}

// Wakes t from the cancellation point it sleeps in, if any. The sleeper holds its mutex while it
// publishes it (under cancel_lk), so this only tries the mutex while holding cancel_lk and
// retries until the sleeper has released it by going to sleep or has withdrawn.
static void cancel_wake(RecompThread *t) {
  for (;;) {
    pthread_mutex_lock(&t->cancel_lk);
    pthread_mutex_t *mx = t->cancel_wait_mx;
    if (!mx) {
      pthread_mutex_unlock(&t->cancel_lk);
      return;
    }
    if (pthread_mutex_trylock(mx) == 0) {
      pthread_cond_broadcast(t->cancel_wait_cv);
      pthread_mutex_unlock(mx);
      pthread_mutex_unlock(&t->cancel_lk);
      return;
    }
    pthread_mutex_unlock(&t->cancel_lk);
    sched_yield();
  }
}

static int thread_cancel(uint64_t handle) {
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  const bool newly_pending = !atomic_exchange_explicit(&t->cancel_pending, true, memory_order_acq_rel);
  const bool self = t == g_current_thread;
  pthread_mutex_unlock(&t->lock);
  if (newly_pending && !self) cancel_wake(t);
  thread_unref(t);
  if (self && atomic_load_explicit(&t->cancel_async, memory_order_acquire)) ps4_thread_testcancel();
  return 0;
}

#define CANCEL_ENABLE 0
#define CANCEL_DISABLE 1
#define CANCEL_DEFERRED 0
#define CANCEL_ASYNCHRONOUS 2

static int set_cancel_state(GuestContext *ctx, int state, uint64_t oldstate_addr) {
  RecompThread *t = g_current_thread;
  const bool old = t ? atomic_load_explicit(&t->cancel_enable, memory_order_acquire) : true;
  switch (state) {
  case CANCEL_DISABLE:
    if (t) atomic_store_explicit(&t->cancel_enable, false, memory_order_release);
    break;
  case CANCEL_ENABLE:
    if (t) atomic_store_explicit(&t->cancel_enable, true, memory_order_release);
    ps4_thread_testcancel();
    break;
  default:
    return EINVAL;
  }
  if (oldstate_addr) *(int32_t *)(ctx->mem_base + oldstate_addr) = old ? CANCEL_ENABLE : CANCEL_DISABLE;
  return 0;
}

static int set_cancel_type(GuestContext *ctx, int type, uint64_t oldtype_addr) {
  RecompThread *t = g_current_thread;
  const bool old_async = t ? atomic_load_explicit(&t->cancel_async, memory_order_acquire) : false;
  switch (type) {
  case CANCEL_DEFERRED:
    if (t) atomic_store_explicit(&t->cancel_async, false, memory_order_release);
    break;
  case CANCEL_ASYNCHRONOUS:
    if (t) atomic_store_explicit(&t->cancel_async, true, memory_order_release);
    ps4_thread_testcancel();
    break;
  default:
    return EINVAL;
  }
  if (oldtype_addr) {
    *(int32_t *)(ctx->mem_base + oldtype_addr) = old_async ? CANCEL_ASYNCHRONOUS : CANCEL_DEFERRED;
  }
  return 0;
}

void ps4_thread_cleanup_push_host(PthreadCleanupEntry *e, void (*fn)(void *), void *arg) {
  RecompThread *t = g_current_thread;
  if (!t) return;
  memset(e, 0, sizeof(*e));
  e->host_fn = fn;
  e->host_arg = arg;
  e->next = t->cleanup_stack;
  t->cleanup_stack = e;
}

void ps4_thread_cleanup_pop_host(PthreadCleanupEntry *e) {
  RecompThread *t = g_current_thread;
  if (t && t->cleanup_stack == e) t->cleanup_stack = e->next;
}

#if defined(__APPLE__)
static void guest_worker_apply_qos(const char *name) {
  static const char kPrefix[] = "NdJobWorkerThread";
  if (strncmp(name, kPrefix, sizeof kPrefix - 1) != 0) return;
  const char *e = getenv("PS4_GUEST_WORKER_QOS");
  if (!e || !e[0]) e = "initiated";
  if (strcmp(e, "default") == 0) return;
  qos_class_t cls;
  if (strcmp(e, "utility") == 0) cls = QOS_CLASS_UTILITY;
  else if (strcmp(e, "initiated") == 0) cls = QOS_CLASS_USER_INITIATED;
  else if (strcmp(e, "interactive") == 0) cls = QOS_CLASS_USER_INTERACTIVE;
  else return;
  pthread_set_qos_class_self_np(cls, 0);
}
#endif

static void *recomp_host_thread_runner(void *arg) {
  RecompThread *t = (RecompThread *)arg;

  while (!atomic_load_explicit(&t->start_ready, memory_order_acquire)) {
    sched_yield();
  }
  atomic_thread_fence(memory_order_seq_cst);

  GuestContext *ctx = t->ctx;
  g_current_ctx = ctx;
  g_current_thread = t;

  if (t->name[0] != '\0') {
#if defined(__APPLE__)
    pthread_setname_np(t->name);
    guest_worker_apply_qos(t->name);
#elif defined(__linux__)
    pthread_setname_np(t->host_thread, t->name);
#endif
  }

  if (t->host_entry) {
    t->host_entry(ctx, t->host_arg);
    exit_thread(ctx, t, 0);
  }

  // Push dummy return address on guest stack
  ctx->rsp -= 8;
  MEM_U64(ctx->rsp) = 0xdeadbeefULL;

  // First parameter in AMD64 calling convention is RDI
  ctx->rdi = t->arg;
  ctx->rip = t->start_routine;

  recomp_dispatch(ctx, t->start_routine);

  exit_thread(g_current_ctx ? g_current_ctx : ctx, t, ctx->rax);
}

// ---- Join and detach (shadPS4 JoinThread, posix_pthread_detach).

// The joiner recorded for a caller that is not a guest pthread (an HLE host thread).
static RecompThread g_host_joiner;

static void join_backout(void *arg) {
  RecompThread *t = (RecompThread *)arg;
  RecompThread *cur = g_current_thread;
  if (cur) {
    pthread_mutex_lock(&cur->lock);
    cur->join_target = NULL;
    pthread_mutex_unlock(&cur->lock);
  }
  pthread_mutex_lock(&t->lock);
  t->joiner = NULL;
  pthread_mutex_unlock(&t->lock);
}

// Waits for the thread to end (until abs, CLOCK_REALTIME, when not NULL). A cancellation point.
static int join_thread(uint64_t handle, uint64_t *thread_return, const struct timespec *abs) {
  RecompThread *cur = g_current_thread;
  if (handle == 0) return EINVAL;
  RecompThread *t;
  if (thread_ref(handle, true, &t) != 0) return ESRCH;
  if (t == cur) {
    thread_unref(t);
    return EDEADLK;
  }

  pthread_mutex_lock(&t->lock);
  int ret = 0;
  if (atomic_load_explicit(&t->flags, memory_order_acquire) & THREAD_FLAG_DETACHED) {
    ret = EINVAL;
  } else if (t->joiner != NULL) {
    ret = ENOTSUP; // Multiple joiners are not supported.
  }
  if (ret) {
    thread_unref_locked(t);
    return ret;
  }
  t->joiner = cur ? cur : &g_host_joiner;
  pthread_mutex_unlock(&t->lock);
  if (cur) {
    pthread_mutex_lock(&cur->lock);
    cur->join_target = t;
    pthread_mutex_unlock(&cur->lock);
  }

  // A cancel while waiting backs the join out, then drops the reference.
  PthreadCleanupEntry unref, backout;
  ps4_thread_cleanup_push_host(&unref, thread_unref_cb, t);
  ps4_thread_cleanup_push_host(&backout, join_backout, t);
  ps4_thread_testcancel();

  pthread_mutex_lock(&t->join_wait_mutex);
  while (atomic_load_explicit(&t->tid, memory_order_acquire) != TID_TERMINATED) {
    if (ps4_thread_cancel_wait_begin(cur, &t->join_wait_mutex, &t->join_wait_cv)) {
      pthread_mutex_unlock(&t->join_wait_mutex);
      ps4_thread_testcancel();
      pthread_mutex_lock(&t->join_wait_mutex);
      continue;
    }
    const int r = abs ? pthread_cond_timedwait(&t->join_wait_cv, &t->join_wait_mutex, abs)
                      : pthread_cond_wait(&t->join_wait_cv, &t->join_wait_mutex);
    ps4_thread_cancel_wait_end(cur);
    if (r == ETIMEDOUT && atomic_load_explicit(&t->tid, memory_order_acquire) != TID_TERMINATED) {
      ret = ETIMEDOUT;
      break;
    }
  }
  pthread_mutex_unlock(&t->join_wait_mutex);
  ps4_thread_cleanup_pop_host(&backout);
  ps4_thread_cleanup_pop_host(&unref);
  if (cur) {
    pthread_mutex_lock(&cur->lock);
    cur->join_target = NULL;
    pthread_mutex_unlock(&cur->lock);
  }

  if (ret == ETIMEDOUT) {
    join_backout(t);
    thread_unref(t);
    return ret;
  }

  const uint64_t ret_val = t->ret_val;
  pthread_join(t->host_thread, NULL);

  pthread_mutex_lock(&t->lock);
  atomic_fetch_or_explicit(&t->flags, THREAD_FLAG_DETACHED, memory_order_release);
  t->joined = true;
  t->detached = true;
  t->joiner = NULL;
  thread_unref_locked(t); // unlocks t->lock

  if (thread_return != NULL) {
    *thread_return = ret_val;
  }
  recomp_thread_collect();
  return 0;
}

static int detach_thread(uint64_t handle) {
  if (handle == 0) return EINVAL;
  RecompThread *t;
  const int e = thread_ref(handle, true, &t);
  if (e) return e;

  pthread_mutex_lock(&t->lock);
  if ((atomic_load_explicit(&t->flags, memory_order_acquire) & THREAD_FLAG_DETACHED) || t->joiner != NULL) {
    thread_unref_locked(t);
    return EINVAL;
  }
  pthread_detach(t->host_thread);
  atomic_fetch_or_explicit(&t->flags, THREAD_FLAG_DETACHED, memory_order_release);
  t->detached = true;
  thread_unref_locked(t); // unlocks t->lock

  recomp_thread_collect();
  return 0;
}

// ---- Thread creation (shadPS4 posix_pthread_create_name_np, ThreadState::CreateStack).

static int do_create_thread(GuestContext *ctx, uint64_t thread_ptr_addr, const PthreadAttr *attr,
                            uint64_t start_routine, uint64_t arg, const char *name,
                            void (*host_entry)(GuestContext *, void *), void *host_arg) {
  recomp_thread_collect();

  PthreadAttr a = attr ? *attr : ps4_pthread_attr_default;
  a.cpuset = NULL;
  a.cpusetsize = 0;
  if (a.suspend) return ENOTSUP;
  RecompThread *cur = g_current_thread;
  if (cur && a.sched_inherit == PS4_PTHREAD_INHERIT_SCHED) {
    pthread_mutex_lock(&cur->lock);
    if (cur->attr.flags & PTHREAD_ATTR_SCOPE_SYSTEM) {
      a.flags |= PTHREAD_ATTR_SCOPE_SYSTEM;
    } else {
      a.flags &= ~(uint32_t)PTHREAD_ATTR_SCOPE_SYSTEM;
    }
    a.prio = cur->attr.prio;
    a.sched_policy = cur->attr.sched_policy;
    pthread_mutex_unlock(&cur->lock);
  }

  RecompThread *t = (RecompThread *)calloc(1, sizeof(RecompThread));
  if (!t) {
    return EAGAIN;
  }

  t->magic = THREAD_MAGIC;
  int tid = atomic_fetch_add(&g_next_tid, 1);
  atomic_store_explicit(&t->tid, tid, memory_order_relaxed);
  atomic_store_explicit(&t->refcount, 1, memory_order_relaxed);
  atomic_store_explicit(&t->state, PTHREAD_STATE_RUNNING, memory_order_relaxed);
  thread_init_locks(t);

  t->thread_id = (uint64_t)tid;
  t->start_routine = start_routine;
  t->arg = arg;
  t->host_entry = host_entry;
  t->host_arg = host_arg;

  // A thread on a runtime-allocated stack gets room for HLE code on top of the attribute's size.
  const uint64_t user_stack = (uint64_t)(uintptr_t)a.stackaddr_attr;
  if (!user_stack) a.stacksize_attr += PS4_THR_STACK_HLE_EXTRA;
  GuestContext *child_ctx = user_stack ? recomp_create_thread_context_on_stack(ctx, user_stack, a.stacksize_attr)
                                       : recomp_create_thread_context(ctx, a.stacksize_attr);
  if (!child_ctx) {
    thread_free(t);
    return EAGAIN;
  }
  if (user_stack) {
    a.guardsize_attr = 0;
    a.flags |= PTHREAD_ATTR_STACK_USER;
    t->stack_base = user_stack;
    t->stack_size = a.stacksize_attr;
  } else {
    a.stackaddr_attr = (void *)(uintptr_t)child_ctx->stack_base;
    a.flags &= ~(uint32_t)PTHREAD_ATTR_STACK_USER;
    t->stack_base = child_ctx->stack_base;
    t->stack_size = child_ctx->stack_alloc_size;
  }
  t->attr = a;
  if (attr && attr->cpuset) ps4_pthread_attr_setaffinity(&t->attr, attr->cpusetsize, attr->cpuset);
  if (a.flags & PTHREAD_ATTR_DETACHED) {
    atomic_fetch_or_explicit(&t->flags, THREAD_FLAG_DETACHED, memory_order_relaxed);
    t->detached = true;
  }

  if (name && name[0]) {
    strncpy(t->name, name, sizeof(t->name) - 1);
  } else {
    snprintf(t->name, sizeof(t->name), "Thread%d", tid);
  }

  child_ctx->thread_id = t->thread_id;
  t->ctx = child_ctx;

  if (child_ctx->fs_base && child_ctx->mem_base) {
    *(uint64_t *)(child_ctx->mem_base + child_ctx->fs_base + 0x10ULL) = (uint64_t)t;
  }

  register_thread(t);

  if (thread_ptr_addr) {
    *(uint64_t *)(ctx->mem_base + thread_ptr_addr) = (uint64_t)t;
  }

  int ret = pthread_create(&t->host_thread, NULL, recomp_host_thread_runner, t);
  if (ret != 0) {
    if (thread_ptr_addr) *(uint64_t *)(ctx->mem_base + thread_ptr_addr) = 0;
    pthread_mutex_lock(&t->lock);
    atomic_fetch_or_explicit(&t->flags, THREAD_FLAG_DETACHED, memory_order_relaxed);
    atomic_store_explicit(&t->refcount, 0, memory_order_relaxed);
    atomic_store_explicit(&t->state, PTHREAD_STATE_DEAD, memory_order_relaxed);
    atomic_store_explicit(&t->tid, TID_TERMINATED, memory_order_relaxed);
    try_collect_locked(t);
    recomp_thread_collect();
    return ret;
  }

  if (t->detached) {
    pthread_detach(t->host_thread);
  }

  atomic_thread_fence(memory_order_seq_cst);
  atomic_store_explicit(&t->start_ready, 1, memory_order_release);
  return 0;
}

int ps4_create_guest_thread(GuestContext *ctx, uint64_t thread_out, const PthreadAttr *attr,
                            uint64_t start_routine, uint64_t arg, const char *name) {
  return do_create_thread(ctx, thread_out, attr, start_routine, arg, name, NULL, NULL);
}

int ps4_create_host_entry_thread(GuestContext *ctx, uint64_t thread_out, const PthreadAttr *attr,
                                 void (*entry)(GuestContext *child_ctx, void *arg), void *arg,
                                 const char *name) {
  if (!entry) return EINVAL;
  return do_create_thread(ctx, thread_out, attr, 0, 0, name, entry, arg);
}

int ps4_join_guest_thread(uint64_t handle, uint64_t *ret) {
  return join_thread(handle, ret, NULL);
}

// posix_pthread_create_name_np(thread, attr, start_routine, arg, name); a NULL or unmapped
// name gives "Thread<tid>".
static int create_from_guest(GuestContext *ctx, uint64_t name_addr) {
  const char *name = NULL;
  if (name_addr && name_addr < ctx->mem_size) name = (const char *)(ctx->mem_base + name_addr);
  const PthreadAttr *attr = NULL;
  if (ctx->rsi) attr = (const PthreadAttr *)(uintptr_t)*(uint64_t *)(ctx->mem_base + ctx->rsi);
  return do_create_thread(ctx, ctx->rdi, attr, ctx->rdx, ctx->rcx, name, NULL, NULL);
}

void shim_pthread_create(GuestContext *ctx) { RET_POSIX(create_from_guest(ctx, 0)); }

void shim_pthread_create_name_np(GuestContext *ctx) { RET_POSIX(create_from_guest(ctx, ctx->r8)); }

void shim_scePthreadCreate(GuestContext *ctx) { RET_ORBIS(create_from_guest(ctx, ctx->r8)); }

static int join_from_guest(GuestContext *ctx, uint64_t handle, uint64_t retval_addr, const struct timespec *abs) {
  uint64_t retval = 0;
  const int err = join_thread(handle, &retval, abs);
  if (err == 0 && retval_addr) *(uint64_t *)(ctx->mem_base + retval_addr) = retval;
  return err;
}

void shim_pthread_join(GuestContext *ctx) { RET_POSIX(join_from_guest(ctx, ctx->rdi, ctx->rsi, NULL)); }

void shim_scePthreadJoin(GuestContext *ctx) { RET_ORBIS(join_from_guest(ctx, ctx->rdi, ctx->rsi, NULL)); }

// pthread_timedjoin_np(thread, value_ptr, abstime): abstime on CLOCK_REALTIME.
void shim_pthread_timedjoin_np(GuestContext *ctx) {
  const int64_t *ts = (const int64_t *)gptr(ctx, ctx->rdx);
  if (!ts || ts[0] < 0 || ts[1] < 0 || ts[1] >= 1000000000) RET_POSIX(EINVAL);
  const struct timespec abs = {(time_t)ts[0], (long)ts[1]};
  RET_POSIX(join_from_guest(ctx, ctx->rdi, ctx->rsi, &abs));
}

void shim_pthread_detach(GuestContext *ctx) { RET_POSIX(detach_thread(ctx->rdi)); }

void shim_scePthreadDetach(GuestContext *ctx) { RET_ORBIS(detach_thread(ctx->rdi)); }

// pthread_exit and scePthreadExit (both shadPS4 posix_pthread_exit).
void shim_pthread_exit(GuestContext *ctx) { exit_thread(ctx, g_current_thread, ctx->rdi); }

void shim_scePthreadExit(GuestContext *ctx) { exit_thread(ctx, g_current_thread, ctx->rdi); }

void shim_pthread_self(GuestContext *ctx) {
  if (ctx->fs_base) {
    uint64_t t = MEM_U64(ctx->fs_base + 0x10);
    ctx->rax = (t != 0) ? t : ctx->fs_base;
  } else if (g_current_thread) {
    ctx->rax = (uint64_t)g_current_thread;
  } else {
    ctx->rax = 0x1000ULL;
  }
  SHIM_RETURN();
}

void shim_scePthreadSelf(GuestContext *ctx) { shim_pthread_self(ctx); }

void shim_pthread_equal(GuestContext *ctx) {
  ctx->rax = ctx->rdi == ctx->rsi ? 1 : 0;
  SHIM_RETURN();
}

void shim_scePthreadEqual(GuestContext *ctx) { shim_pthread_equal(ctx); }

// pthread_yield and scePthreadYield (shadPS4 posix_pthread_yield, a void function).
void shim_pthread_yield(GuestContext *ctx) {
  sched_yield();
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadYield(GuestContext *ctx) { shim_pthread_yield(ctx); }

// pthread_getthreadid_np and scePthreadGetthreadid: the calling thread's id.
void shim_pthread_getthreadid_np(GuestContext *ctx) {
  ctx->rax = g_current_thread ? g_current_thread->thread_id : (ctx->thread_id ? ctx->thread_id : 1000);
  SHIM_RETURN();
}

void shim_scePthreadGetthreadid(GuestContext *ctx) { shim_pthread_getthreadid_np(ctx); }

// ---- Thread-specific data shims.

static int key_create_shim(GuestContext *ctx) {
  uint32_t key = 0;
  const int e = key_create(&key, ctx->rsi);
  if (e == 0 && ctx->rdi) *(uint32_t *)(ctx->mem_base + ctx->rdi) = key;
  return e;
}

void shim_pthread_key_create(GuestContext *ctx) { RET_POSIX(key_create_shim(ctx)); }

void shim_scePthreadKeyCreate(GuestContext *ctx) { RET_ORBIS(key_create_shim(ctx)); }

void shim_pthread_key_delete(GuestContext *ctx) { RET_POSIX(key_delete((uint32_t)ctx->rdi)); }

void shim_scePthreadKeyDelete(GuestContext *ctx) { RET_ORBIS(key_delete((uint32_t)ctx->rdi)); }

void shim_pthread_setspecific(GuestContext *ctx) { RET_POSIX(key_set(ctx, (uint32_t)ctx->rdi, ctx->rsi)); }

void shim_scePthreadSetspecific(GuestContext *ctx) { RET_ORBIS(key_set(ctx, (uint32_t)ctx->rdi, ctx->rsi)); }

void shim_pthread_getspecific(GuestContext *ctx) {
  ctx->rax = key_get(ctx, (uint32_t)ctx->rdi);
  SHIM_RETURN();
}

void shim_scePthreadGetspecific(GuestContext *ctx) { shim_pthread_getspecific(ctx); }

// ---- Cleanup handlers (shadPS4 pthread_clean.cpp).

static void cleanup_push(uint64_t routine, uint64_t arg) {
  RecompThread *t = g_current_thread;
  if (!t) return;
  PthreadCleanupEntry *c = (PthreadCleanupEntry *)calloc(1, sizeof(PthreadCleanupEntry));
  if (!c) return;
  c->routine = routine;
  c->arg = arg;
  c->onheap = true;
  c->next = t->cleanup_stack;
  t->cleanup_stack = c;
}

// __pthread_cleanup_push_imp(routine, arg, buffer): the guest's buffer is not needed, the entry
// is kept on the host.
void shim___pthread_cleanup_push_imp(GuestContext *ctx) {
  cleanup_push(ctx->rdi, ctx->rsi);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_cleanup_push(GuestContext *ctx) { shim___pthread_cleanup_push_imp(ctx); }

void shim___pthread_cleanup_pop_imp(GuestContext *ctx) {
  const int execute = (int)ctx->rdi;
  RecompThread *t = g_current_thread;
  if (t && t->cleanup_stack) {
    PthreadCleanupEntry *c = t->cleanup_stack;
    t->cleanup_stack = c->next;
    if (execute) run_cleanup(ctx, c);
    if (c->onheap) free(c);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_cleanup_pop(GuestContext *ctx) { shim___pthread_cleanup_pop_imp(ctx); }

// ---- Scheduling (shadPS4 posix_pthread_getschedparam / setschedparam / setprio,
// scePthreadGetprio, posix_sched_get_priority_*). Priorities are kept, not applied to the host
// threads (shadPS4's _thr_setscheduler is a TODO as well).

static int get_sched(uint64_t handle, int *policy, int *prio) {
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  *policy = t->attr.sched_policy;
  *prio = t->attr.prio;
  thread_unref_locked(t);
  return 0;
}

static int getschedparam_shim(GuestContext *ctx) {
  int32_t *policy = (int32_t *)gptr(ctx, ctx->rsi);
  int32_t *param = (int32_t *)gptr(ctx, ctx->rdx);
  if (!policy || !param) return EINVAL;
  int pol, prio;
  const int e = get_sched(ctx->rdi, &pol, &prio);
  if (e) return e;
  *policy = pol;
  *param = prio;
  return 0;
}

void shim_pthread_getschedparam(GuestContext *ctx) { RET_POSIX(getschedparam_shim(ctx)); }

void shim_scePthreadGetschedparam(GuestContext *ctx) { RET_ORBIS(getschedparam_shim(ctx)); }

static int setschedparam_shim(GuestContext *ctx) {
  const int32_t *param = (const int32_t *)gptr(ctx, ctx->rdx);
  if (ctx->rdi == 0 || !param) return EINVAL;
  RecompThread *t;
  const int e = thread_find_locked(ctx->rdi, &t);
  if (e) return e;
  t->attr.sched_policy = (int)ctx->rsi;
  t->attr.prio = *param;
  thread_unref_locked(t);
  return 0;
}

void shim_pthread_setschedparam(GuestContext *ctx) { RET_POSIX(setschedparam_shim(ctx)); }

void shim_scePthreadSetschedparam(GuestContext *ctx) { RET_ORBIS(setschedparam_shim(ctx)); }

// scePthreadGetprio(thread, *priority): any failure is ESRCH.
void shim_scePthreadGetprio(GuestContext *ctx) {
  int32_t *out = (int32_t *)gptr(ctx, ctx->rsi);
  int pol, prio;
  if (get_sched(ctx->rdi, &pol, &prio) != 0) {
    ctx->rax = ORBIS_KERNEL_ERROR_ESRCH;
    SHIM_RETURN();
  }
  if (out) *out = prio;
  ctx->rax = 0;
  SHIM_RETURN();
}

static int setprio(uint64_t handle, int prio) {
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  t->attr.prio = prio;
  thread_unref_locked(t);
  return 0;
}

void shim_pthread_setprio(GuestContext *ctx) { RET_POSIX(setprio(ctx->rdi, (int)ctx->rsi)); }

void shim_scePthreadSetprio(GuestContext *ctx) { RET_ORBIS(setprio(ctx->rdi, (int)ctx->rsi)); }

static int priority_limit(int policy, int limit) {
  return policy == PS4_SCHED_FIFO || policy == PS4_SCHED_RR ? limit : EINVAL;
}

void shim_sched_get_priority_max(GuestContext *ctx) {
  RET_POSIX(priority_limit((int)ctx->rdi, PS4_PRIO_FIFO_HIGHEST));
}

void shim_sched_get_priority_min(GuestContext *ctx) {
  RET_POSIX(priority_limit((int)ctx->rdi, PS4_PRIO_FIFO_LOWEST));
}

// ---- Affinity (shadPS4 posix_pthread_getaffinity_np / setaffinity_np, Pthread::SetAffinity).
// The mask is kept in the thread's attribute; shadPS4 does not apply it to the host thread
// either, and macOS has no CPU affinity call.

static int get_affinity(uint64_t handle, size_t cpusetsize, void *cpusetp) {
  if (handle == 0 || !cpusetp) return EINVAL;
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  const int r = ps4_pthread_attr_getaffinity(&t->attr, cpusetsize, cpusetp);
  thread_unref_locked(t);
  return r;
}

static int set_affinity(uint64_t handle, size_t cpusetsize, const void *cpusetp) {
  if (handle == 0 || !cpusetp) return EINVAL;
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  int r = ps4_pthread_attr_setaffinity(&t->attr, cpusetsize, cpusetp);
  // Pthread::SetAffinity: hosts with fewer than 8 CPUs ignore the call; otherwise a thread
  // whose set was just cleared is EINVAL.
  if (r == 0 && sysconf(_SC_NPROCESSORS_ONLN) >= 8 && t->attr.cpuset == NULL) r = EINVAL;
  thread_unref_locked(t);
  return r;
}

void shim_pthread_getaffinity_np(GuestContext *ctx) {
  RET_POSIX(get_affinity(ctx->rdi, (size_t)ctx->rsi, gptr(ctx, ctx->rdx)));
}

void shim_pthread_setaffinity_np(GuestContext *ctx) {
  RET_POSIX(set_affinity(ctx->rdi, (size_t)ctx->rsi, gptr(ctx, ctx->rdx)));
}

// scePthreadGetaffinity(thread, *mask) and scePthreadSetaffinity(thread, mask): the first word
// of the CPU set.
void shim_scePthreadGetaffinity(GuestContext *ctx) {
  Ps4Cpuset set;
  const int e = get_affinity(ctx->rdi, sizeof(set), &set);
  if (e == 0 && ctx->rsi) *(uint64_t *)(ctx->mem_base + ctx->rsi) = set.bits;
  RET_ORBIS(e);
}

void shim_scePthreadSetaffinity(GuestContext *ctx) {
  const Ps4Cpuset set = {.bits = ctx->rsi, .reserved = 0};
  RET_ORBIS(set_affinity(ctx->rdi, sizeof(set), &set));
}

// ---- Names (shadPS4 posix_pthread_getname_np, posix_pthread_rename_np).

static int get_name(GuestContext *ctx, uint64_t handle, uint64_t buf_addr) {
  if (!buf_addr || buf_addr >= ctx->mem_size) return EINVAL;
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  strncpy((char *)(ctx->mem_base + buf_addr), t->name, 32);
  thread_unref_locked(t);
  return 0;
}

void shim_pthread_getname_np(GuestContext *ctx) { RET_POSIX(get_name(ctx, ctx->rdi, ctx->rsi)); }

void shim_scePthreadGetname(GuestContext *ctx) { RET_ORBIS(get_name(ctx, ctx->rdi, ctx->rsi)); }

// A NULL name is the empty name. Only the calling thread's host name can be set on macOS.
static int rename_thread(GuestContext *ctx, uint64_t handle, uint64_t name_addr) {
  const char *name = name_addr && name_addr < ctx->mem_size ? (const char *)(ctx->mem_base + name_addr) : "";
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  memset(t->name, 0, sizeof(t->name));
  strncpy(t->name, name, sizeof(t->name) - 1);
  if (t == g_current_thread) pthread_setname_np(t->name);
  thread_unref_locked(t);
  return 0;
}

void shim_pthread_rename_np(GuestContext *ctx) { RET_POSIX(rename_thread(ctx, ctx->rdi, ctx->rsi)); }

void shim_scePthreadRename(GuestContext *ctx) { RET_ORBIS(rename_thread(ctx, ctx->rdi, ctx->rsi)); }

// pthread_set_name_np is a void function.
void shim_pthread_set_name_np(GuestContext *ctx) {
  (void)rename_thread(ctx, ctx->rdi, ctx->rsi);
  ctx->rax = 0;
  SHIM_RETURN();
}

// ---- Cancellation shims. scePthreadCancel / Setcancelstate / Setcanceltype convert a failure
// themselves (ErrnoToSceKernelError), the same as ORBIS().

void shim_pthread_cancel(GuestContext *ctx) { RET_POSIX(thread_cancel(ctx->rdi)); }

void shim_scePthreadCancel(GuestContext *ctx) { RET_ORBIS(thread_cancel(ctx->rdi)); }

void shim_pthread_setcancelstate(GuestContext *ctx) { RET_POSIX(set_cancel_state(ctx, (int)ctx->rdi, ctx->rsi)); }

void shim_scePthreadSetcancelstate(GuestContext *ctx) { RET_ORBIS(set_cancel_state(ctx, (int)ctx->rdi, ctx->rsi)); }

void shim_pthread_setcanceltype(GuestContext *ctx) { RET_POSIX(set_cancel_type(ctx, (int)ctx->rdi, ctx->rsi)); }

void shim_scePthreadSetcanceltype(GuestContext *ctx) { RET_ORBIS(set_cancel_type(ctx, (int)ctx->rdi, ctx->rsi)); }

void shim_pthread_testcancel(GuestContext *ctx) {
  ps4_thread_testcancel();
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadTestcancel(GuestContext *ctx) { shim_pthread_testcancel(ctx); }

// ---- Attribute of a running thread (shadPS4 posix_pthread_attr_get_np).

static int attr_get_np(GuestContext *ctx, uint64_t handle, uint64_t dst_addr) {
  PthreadAttr *dst = dst_addr ? (PthreadAttr *)(uintptr_t)*(uint64_t *)(ctx->mem_base + dst_addr) : NULL;
  if (handle == 0 || !dst) return EINVAL;
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  PthreadAttr a = t->attr;
  if (atomic_load_explicit(&t->flags, memory_order_acquire) & THREAD_FLAG_DETACHED) a.flags |= PTHREAD_ATTR_DETACHED;
  ps4_pthread_attr_copy(dst, &a);
  thread_unref_locked(t);
  return 0;
}

void shim_pthread_attr_get_np(GuestContext *ctx) { RET_POSIX(attr_get_np(ctx, ctx->rdi, ctx->rsi)); }

void shim_scePthreadAttrGet(GuestContext *ctx) { RET_ORBIS(attr_get_np(ctx, ctx->rdi, ctx->rsi)); }

// ---- Suspension and user contexts (shadPS4 posix_pthread_{suspend,resume}_user_context_np,
// posix_pthread_{get,set}_user_context_np, posix_pthread_{suspend,resume}_np). Another thread
// is stopped with the Mach thread_suspend, as shadPS4 does on macOS; the calling thread is
// only flagged.

static int suspend_thread(uint64_t handle, bool suspend) {
  if (handle == 0) return EINVAL;
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  if (suspend) {
    atomic_fetch_or_explicit(&t->flags, THREAD_FLAG_SUSPENDED, memory_order_acq_rel);
  } else {
    atomic_fetch_and_explicit(&t->flags, ~(uint32_t)THREAD_FLAG_SUSPENDED, memory_order_acq_rel);
  }
  const bool other = t != g_current_thread;
  const pthread_t host = t->host_thread;
  pthread_mutex_unlock(&t->lock);
#if defined(__APPLE__)
  if (other) {
    const mach_port_t port = pthread_mach_thread_np(host);
    if (suspend) {
      thread_suspend(port);
    } else {
      thread_resume(port);
    }
  }
#endif
  thread_unref(t);
  return 0;
}

void shim_pthread_suspend_user_context_np(GuestContext *ctx) { RET_POSIX(suspend_thread(ctx->rdi, true)); }

void shim_pthread_resume_user_context_np(GuestContext *ctx) { RET_POSIX(suspend_thread(ctx->rdi, false)); }

void shim_pthread_suspend_np(GuestContext *ctx) { RET_POSIX(suspend_thread(ctx->rdi, true)); }

void shim_pthread_resume_np(GuestContext *ctx) { RET_POSIX(suspend_thread(ctx->rdi, false)); }

// Orbis ucontext_t (0x500 bytes): uc_mcontext at 0x40 (FreeBSD amd64 mcontext_t).
#define UCONTEXT_SIZE 0x500
#define UC_MC(off) (0x40 + (off))
enum {
  MC_RDI = UC_MC(0x08), MC_RSI = UC_MC(0x10), MC_RDX = UC_MC(0x18), MC_RCX = UC_MC(0x20),
  MC_R8 = UC_MC(0x28), MC_R9 = UC_MC(0x30), MC_RAX = UC_MC(0x38), MC_RBX = UC_MC(0x40),
  MC_RBP = UC_MC(0x48), MC_R10 = UC_MC(0x50), MC_R11 = UC_MC(0x58), MC_R12 = UC_MC(0x60),
  MC_R13 = UC_MC(0x68), MC_R14 = UC_MC(0x70), MC_R15 = UC_MC(0x78), MC_RIP = UC_MC(0xa0),
  MC_RSP = UC_MC(0xb8),
};

typedef struct GuestRegs {
  uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8, r9, r10, r11, r12, r13, r14, r15, rip;
} GuestRegs;

static void regs_from_ctx(GuestRegs *r, const GuestContext *c) {
  *r = (GuestRegs){c->rax, c->rbx, c->rcx, c->rdx, c->rsi, c->rdi, c->rbp, c->rsp, c->r8,
                   c->r9,  c->r10, c->r11, c->r12, c->r13, c->r14, c->r15, c->rip};
}

// The guest registers of another thread: when it is stopped in recompiled code they are the
// host registers the lifter pins them to (as the exception handler bridge reads them);
// anywhere else (a shim, the runtime) the ones the bridge stored in its context.
static bool regs_of_thread(RecompThread *t, GuestRegs *r) {
  GuestContext *c = t->ctx;
  if (!c) return false;
  regs_from_ctx(r, c);
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
  if (atomic_load_explicit(&t->flags, memory_order_acquire) & THREAD_FLAG_SUSPENDED) {
    arm_thread_state64_t st;
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    if (thread_get_state(pthread_mach_thread_np(t->host_thread), ARM_THREAD_STATE64, (thread_state_t)&st, &count) ==
            KERN_SUCCESS &&
        recomp_pc_in_guest_code((uint64_t)arm_thread_state64_get_pc(st)) && st.__x[28] == (uint64_t)c->mem_base &&
        st.__x[26] >= 0x10000ULL && st.__x[26] < c->mem_size) {
      r->rax = st.__x[19], r->rbx = st.__x[20], r->rcx = st.__x[21], r->rdx = st.__x[22];
      r->rsi = st.__x[23], r->rdi = st.__x[24], r->rbp = st.__x[25], r->rsp = st.__x[26];
      r->r8 = st.__x[8], r->r9 = st.__x[9], r->r10 = st.__x[10], r->r11 = st.__x[11];
      r->r12 = st.__x[12], r->r13 = st.__x[13], r->r14 = st.__x[14], r->r15 = st.__x[15];
    }
  }
#endif
  return true;
}

// pthread_get_user_context_np(thread, ucontext): the thread's guest registers; for the calling
// thread the state at its call. A stack pointer outside the thread's stack (or no state) gives
// the top of its stack, and no instruction pointer its start routine.
static int get_user_context(GuestContext *ctx, uint64_t handle, uint64_t uc_addr) {
  if (handle == 0 || !uc_addr || uc_addr + UCONTEXT_SIZE > ctx->mem_size) return EINVAL;
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  const uint64_t stack_base = (uint64_t)(uintptr_t)t->attr.stackaddr_attr;
  const uint64_t stack_top = stack_base + t->attr.stacksize_attr;
  const uint64_t start_pc = t->start_routine;
  const bool self = t == g_current_thread;
  pthread_mutex_unlock(&t->lock);

  GuestRegs r = {0};
  bool got_state;
  if (self) {
    // At the call: rsp past the return address, rip the return address.
    regs_from_ctx(&r, ctx);
    r.rip = MEM_U64(ctx->rsp);
    r.rsp = ctx->rsp + 8;
    got_state = true;
  } else {
    got_state = regs_of_thread(t, &r);
  }
  thread_unref(t);

  if (stack_base && stack_top > stack_base && (!got_state || r.rsp < stack_base || r.rsp >= stack_top)) {
    r.rsp = (stack_top - 0x100) & ~0xFULL;
    r.rbp = r.rsp;
  }
  if (r.rip == 0) r.rip = start_pc;

  uint8_t *uc = ctx->mem_base + uc_addr;
  memset(uc, 0, UCONTEXT_SIZE);
  const struct {
    int off;
    uint64_t v;
  } fields[] = {{MC_RDI, r.rdi}, {MC_RSI, r.rsi}, {MC_RDX, r.rdx}, {MC_RCX, r.rcx}, {MC_R8, r.r8},
                {MC_R9, r.r9},   {MC_RAX, r.rax}, {MC_RBX, r.rbx}, {MC_RBP, r.rbp}, {MC_R10, r.r10},
                {MC_R11, r.r11}, {MC_R12, r.r12}, {MC_R13, r.r13}, {MC_R14, r.r14}, {MC_R15, r.r15},
                {MC_RIP, r.rip}, {MC_RSP, r.rsp}};
  for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) memcpy(uc + fields[i].off, &fields[i].v, 8);
  return 0;
}

void shim_pthread_get_user_context_np(GuestContext *ctx) { RET_POSIX(get_user_context(ctx, ctx->rdi, ctx->rsi)); }

// pthread_set_user_context_np(thread, ucontext) checks its arguments and the thread only, as in
// shadPS4.
static int set_user_context(uint64_t handle, uint64_t uc_addr) {
  if (handle == 0 || !uc_addr) return EINVAL;
  RecompThread *t;
  const int e = thread_find_locked(handle, &t);
  if (e) return e;
  thread_unref_locked(t);
  return 0;
}

void shim_pthread_set_user_context_np(GuestContext *ctx) { RET_POSIX(set_user_context(ctx->rdi, ctx->rsi)); }

void shim_pthread_sigmask(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim___tls_get_addr(GuestContext *ctx) {
  uint64_t ti_addr = ctx->rdi;
  if (!ti_addr) {
    ctx->rax = 0;
    SHIM_RETURN();
  }
  uint64_t module_id = *(uint64_t *)(ctx->mem_base + ti_addr);
  uint64_t offset = *(uint64_t *)(ctx->mem_base + ti_addr + 8);
  uint64_t mod_off = recomp_tls_module_offset(module_id);
  if (mod_off) {
    ctx->rax = ctx->fs_base - mod_off + offset;
  } else {
    ctx->rax = ctx->fs_base + offset;
  }
  SHIM_RETURN();
}

void shim_sceKernelIsStack(GuestContext *ctx) {
  uint64_t addr = ctx->rdi;
  pthread_mutex_lock(&g_threads_mutex);
  for (RecompThread *curr = g_threads; curr; curr = curr->next) {
    if (curr->stack_base && addr >= curr->stack_base && addr < curr->stack_base + curr->stack_size) {
      pthread_mutex_unlock(&g_threads_mutex);
      ctx->rax = 1;
      SHIM_RETURN();
    }
  }
  pthread_mutex_unlock(&g_threads_mutex);
  ctx->rax = 0;
  SHIM_RETURN();
}

#include "ps4_stall_profile.h"
#if defined(__APPLE__)
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/mach_vm.h>

static StallProfile *g_stall_prof;
static uint64_t g_prof_for;
static uint64_t g_prof_slot_ids[STALL_PROFILE_MAX_THREADS];
static int g_prof_nslots;

static void stall_profile_begin(void) {
  stall_profile_reset(g_stall_prof);
  g_prof_nslots = 0;
  stall_profile_cpu_begin(g_stall_prof);
}

static int stall_profile_slot(uint64_t thread_id) {
  for (int i = 0; i < g_prof_nslots; i++)
    if (g_prof_slot_ids[i] == thread_id) return i;
  if (g_prof_nslots == STALL_PROFILE_MAX_THREADS) return -1;
  g_prof_slot_ids[g_prof_nslots] = thread_id;
  return g_prof_nslots++;
}

#define STALL_SYM_CACHE 16384
static struct {
  uint64_t pc, start;
  const char *name;
} g_stall_sym_cache[STALL_SYM_CACHE];

static int stall_symbolize(uint64_t pc, uint64_t *start, const char **name) {
  const uint64_t h = (pc * 0x9E3779B97F4A7C15ull) >> 50;
  if (g_stall_sym_cache[h].pc != pc) {
    Dl_info di;
    const int ok = dladdr((void *)pc, &di) && di.dli_sname;
    g_stall_sym_cache[h].pc = pc;
    g_stall_sym_cache[h].start = ok ? (uint64_t)(uintptr_t)di.dli_saddr : 0;
    g_stall_sym_cache[h].name = ok ? di.dli_sname : NULL;
  }
  if (!g_stall_sym_cache[h].name) return 0;
  *start = g_stall_sym_cache[h].start;
  *name = g_stall_sym_cache[h].name;
  return 1;
}

static int g_prof_no_chain_noted;

static void stall_profile_sample_all(void) {
  int n = 0, in_guest_code = 0;
  pthread_mutex_lock(&g_threads_mutex);
  for (RecompThread *t = g_threads; t && n < STALL_PROFILE_MAX_THREADS; t = t->next) {
    if (t->finished || !t->ctx) continue;
    n++;
    const mach_port_t port = pthread_mach_thread_np(t->host_thread);
    if (port == MACH_PORT_NULL || thread_suspend(port) != KERN_SUCCESS) continue;
    uint64_t pcs[STALL_PROFILE_DEPTH];
    int np = 0;
    arm_thread_state64_t st;
    mach_msg_type_number_t cnt = ARM_THREAD_STATE64_COUNT;
    if (thread_get_state(port, ARM_THREAD_STATE64, (thread_state_t)&st, &cnt) == KERN_SUCCESS) {
      pcs[np++] = (uint64_t)arm_thread_state64_get_pc(st);
      const int guest = recomp_pc_in_guest_code(pcs[0]);
      in_guest_code |= guest;
      if (!guest) pcs[np++] = (uint64_t)arm_thread_state64_get_lr(st);
      uint64_t fp = guest ? 0 : (uint64_t)arm_thread_state64_get_fp(st);
      while (np < STALL_PROFILE_DEPTH && fp && !(fp & 15)) {
        uint64_t frame[2];
        mach_vm_size_t got = 0;
        if (mach_vm_read_overwrite(mach_task_self(), fp, sizeof(frame), (mach_vm_address_t)(uintptr_t)frame,
                                   &got) != KERN_SUCCESS || got != sizeof(frame) || !frame[1])
          break;
        pcs[np++] = frame[1];
        if (frame[0] <= fp) break;
        fp = frame[0];
      }
    }
    thread_resume(port);
    if (np) {
      const int slot = stall_profile_slot(t->thread_id);
      stall_profile_add(g_stall_prof, slot, t->name[0] ? t->name : "unnamed", pcs, np);
      stall_profile_thread_cpu(g_stall_prof, slot, stall_profile_thread_cpu_us(t->host_thread));
    }
  }
  pthread_mutex_unlock(&g_threads_mutex);
  if (in_guest_code && !g_prof_no_chain_noted) {
    g_prof_no_chain_noted = 1;
    fprintf(stderr, "[stall-profile] samples in recompiled guest code: PC only (no frame chain)\n");
  }
}

static _Atomic uint64_t g_last_present_ns;

static uint64_t stall_now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static void stall_dump_threads(double stalled_ms) {
  fprintf(stderr, "[stall] no frame for %.0f ms; guest threads:\n", stalled_ms);
  pthread_mutex_lock(&g_threads_mutex);
  for (RecompThread *t = g_threads; t; t = t->next) {
    if (t->finished || !t->ctx) continue;
    const char *sym = "?";
    uint64_t pc = 0;
    mach_port_t port = pthread_mach_thread_np(t->host_thread);
    arm_thread_state64_t st;
    mach_msg_type_number_t cnt = ARM_THREAD_STATE64_COUNT;
    if (port != MACH_PORT_NULL &&
        thread_get_state(port, ARM_THREAD_STATE64, (thread_state_t)&st, &cnt) == KERN_SUCCESS) {
      pc = (uint64_t)arm_thread_state64_get_pc(st);
      Dl_info di;
      if (dladdr((void *)pc, &di) && di.dli_sname) sym = di.dli_sname;
    }
    const GuestContext *run = t->ctx;
    if (t->ctx->mem_base && t->ctx->fs_base) {
      const uint64_t slot = *(const uint64_t *)(t->ctx->mem_base + t->ctx->fs_base + RECOMP_TCB_CTX_SLOT);
      if (slot) run = (const GuestContext *)(uintptr_t)slot;
    }
    fprintf(stderr, "[stall]   %-28s last shim 0x%llx  mxcsr 0x%x%s  host %s\n", t->name[0] ? t->name : "unnamed",
            (unsigned long long)run->rip, run->mxcsr, run != t->ctx ? " (fiber)" : "", sym);
  }
  pthread_mutex_unlock(&g_threads_mutex);
}

static long g_stall_dump_ms, g_stall_prof_ms;

static void *stall_watchdog_main(void *arg) {
  (void)arg;
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  const uint64_t dump_ns = (uint64_t)g_stall_dump_ms * 1000000ull;
  const uint64_t prof_ns = (uint64_t)g_stall_prof_ms * 1000000ull;
  uint64_t reported_for = 0;
  uint64_t prev_wake = 0, max_gap = 0;
  const uint64_t period_ns = prof_ns ? 1000000ull : 5000000ull;
  uint64_t deadline = stall_now_ns();
  for (;;) {
    deadline += period_ns;
    const uint64_t before = stall_now_ns();
    if (deadline > before)
      usleep((useconds_t)((deadline - before) / 1000));
    else
      deadline = before;
    const uint64_t last = g_last_present_ns;
    if (!last) continue;
    const uint64_t now = stall_now_ns();
    if (g_prof_for && prev_wake && now - prev_wake > max_gap) max_gap = now - prev_wake;
    prev_wake = now;
    if (dump_ns && now - last > dump_ns && reported_for != last) {
      reported_for = last;
      stall_dump_threads((double)(now - last) / 1e6);
    }
    if (!prof_ns) continue;
    if (g_prof_for && g_prof_for != last) {
      stall_profile_cpu_end(g_stall_prof);
      stall_profile_report_gap(g_stall_prof, stderr, (double)(last - g_prof_for) / 1e6, 6, (double)max_gap / 1e6);
      g_prof_for = 0;
      prev_wake = deadline = stall_now_ns();
    }
    if (now - last > prof_ns) {
      if (g_prof_for != last) {
        stall_profile_begin();
        g_prof_for = last;
        max_gap = 0;
      }
      stall_profile_sample_all();
    }
  }
  return NULL;
}

static pthread_once_t g_stall_once = PTHREAD_ONCE_INIT;

static void stall_watchdog_start_once(void) {
  const char *e = getenv("PS4_STALL_WATCHDOG");
  const char *p = getenv("PS4_STALL_PROFILE");
  g_stall_dump_ms = e ? atol(e) : 0;
  g_stall_prof_ms = p ? atol(p) : 0;
  if (g_stall_dump_ms < 0) g_stall_dump_ms = 0;
  if (g_stall_prof_ms < 0) g_stall_prof_ms = 0;
  if (g_stall_prof_ms > 0) g_stall_prof = stall_profile_create(stall_symbolize);
  if (g_stall_prof_ms > 0 && !g_stall_prof) g_stall_prof_ms = 0;
  if (g_stall_dump_ms > 0 || g_stall_prof_ms > 0) {
    pthread_t th;
    pthread_create(&th, NULL, stall_watchdog_main, NULL);
    pthread_detach(th);
  }
}

void ps4_stall_watchdog_arm(void) {
  pthread_once(&g_stall_once, stall_watchdog_start_once);
  uint64_t zero = 0;
  atomic_compare_exchange_strong(&g_last_present_ns, &zero, stall_now_ns());
}

void ps4_stall_watchdog_note_present(void) {
  pthread_once(&g_stall_once, stall_watchdog_start_once);
  g_last_present_ns = stall_now_ns();
}
#else
void ps4_stall_watchdog_arm(void) {}
void ps4_stall_watchdog_note_present(void) {}
#endif
