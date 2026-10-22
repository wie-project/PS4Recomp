#include "ps4_fiber.h"
#include "ps4_threading.h"
#include <errno.h>
#include <mach/mach_time.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define FIBER_MAGIC_START 0xDEF1649CU
#define FIBER_MAGIC_END 0xB37592A0U
#define FIBER_OPT_MAGIC 0xBB40E64DU
#define FIBER_HOST_STACK_SIZE (8u * 1024u * 1024u)

enum {
  FIBER_STATE_IDLE = 0,
  FIBER_STATE_RUNNING = 1,
  FIBER_STATE_FINALIZED = 2,
  // Coroutine mode: the fiber has called Switch/ReturnToThread but its host
  // thread is still on the fiber's host stack. Whatever runs next on that
  // thread marks it IDLE once the stack is free.
  FIBER_STATE_SUSPENDING = 3,
};

// Guest-visible fiber object (the game allocates 256 bytes, 8-byte aligned).
typedef struct OrbisFiberObject {
  uint32_t magic_start;
  _Atomic uint32_t state;
  uint64_t host; // struct FiberHost *
  uint64_t entry;
  uint64_t arg_on_initialize;
  uint64_t addr_context;
  uint64_t size_context;
  char name[ORBIS_FIBER_MAX_NAME_LENGTH + 1];
  uint32_t flags;
  uint8_t reserved[ORBIS_FIBER_OBJECT_SIZE - 88 - 4];
  uint32_t magic_end;
} OrbisFiberObject;
_Static_assert(sizeof(OrbisFiberObject) == ORBIS_FIBER_OBJECT_SIZE, "fiber object size");

typedef struct OrbisFiberInfo {
  uint64_t size;
  uint64_t entry;
  uint64_t arg_on_initialize;
  uint64_t addr_context;
  uint64_t size_context;
  char name[ORBIS_FIBER_MAX_NAME_LENGTH + 1];
  uint64_t size_context_margin;
  uint8_t pad[48];
} OrbisFiberInfo;
_Static_assert(sizeof(OrbisFiberInfo) == ORBIS_FIBER_INFO_SIZE, "fiber info size");

// PS4_FIBER_THREADS=1 runs every fiber on its own host thread (the original
// implementation, kept for comparison); by default fibers are coroutines.
static bool g_fiber_threads;

// Switch statistics, printed with the GNM perf line (ps4_fiber_print_stats).
// A transfer is one hand-over: thread -> fiber (run), fiber -> fiber (switch)
// or fiber -> thread (return). In thread mode its gap is the wall time from
// the baton post to the target thread running again; nothing runs on the
// guest thread meanwhile.
enum { FSTAT_RUN, FSTAT_SWITCH, FSTAT_RETURN, FSTAT_KINDS, FSTAT_NONE = FSTAT_KINDS };
#define FSTAT_BUCKETS 256 // 1 us buckets; the last one collects everything slower
static _Atomic uint64_t g_fstat_n[FSTAT_KINDS];
static _Atomic uint64_t g_fstat_ticks[FSTAT_KINDS];
static _Atomic uint64_t g_fstat_post_ticks;
static _Atomic uint64_t g_fstat_slept;
static _Atomic uint32_t g_fstat_hist[FSTAT_BUCKETS];
static _Atomic uint64_t g_fstat_creates;
static double g_ticks_per_us;
// PS4_FIBER_EXTRA_US (thread mode): busy-wait this long after every transfer.
static uint64_t g_fiber_extra_ticks;

typedef struct FiberBaton {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool ready;
  uint8_t kind;
  uint64_t post_t;
} FiberBaton;

struct FiberHost;

// Thread side of a guest thread that runs fibers.
typedef struct FiberWorker {
  GuestContext *ctx;
  RecompThread *thread;
  FiberBaton baton;
  // The fiber that runs on this guest thread, if any. A fiber that is IDLE (and so may be
  // finalized) is never any worker's current fiber (ps4_fiber_endpoint_host_thread).
  struct FiberHost *_Atomic current;
  uint64_t return_arg;
  // Coroutine mode: saved host stack pointer of the thread's own code while
  // a fiber runs, and the fiber to mark IDLE once we are back on it.
  void *sp;
  _Atomic uint32_t *release;
  pthread_t host; // the guest thread's host thread
  int slot;       // index in g_workers, or -1
} FiberWorker;

typedef struct FiberHost {
  uint64_t guest_addr;
  GuestContext *fctx;
  pthread_t host_thread;
  _Atomic bool host_started; // host_thread is valid
  bool kill;
  FiberBaton baton;
  FiberWorker *worker;
  uint64_t in_arg;
  // Coroutine mode: the fiber's host stack (mapping and saved stack pointer)
  // and the fiber to mark IDLE once this one is running.
  void *stack;
  void *sp;
  _Atomic uint32_t *release;
} FiberHost;

static _Thread_local FiberHost *tl_fiber_self = NULL;
static _Thread_local FiberWorker *tl_worker = NULL;
static pthread_key_t g_worker_key;
static pthread_once_t g_worker_key_once = PTHREAD_ONCE_INIT;

static void baton_init(FiberBaton *b) {
  pthread_mutex_init(&b->mutex, NULL);
  pthread_cond_init(&b->cond, NULL);
  b->ready = false;
  b->kind = FSTAT_NONE;
}

static void baton_destroy(FiberBaton *b) {
  pthread_cond_destroy(&b->cond);
  pthread_mutex_destroy(&b->mutex);
}

static void baton_post(FiberBaton *b, int kind) {
  const uint64_t t0 = mach_absolute_time();
  pthread_mutex_lock(&b->mutex);
  b->ready = true;
  b->kind = (uint8_t)kind;
  b->post_t = t0;
  pthread_cond_signal(&b->cond);
  pthread_mutex_unlock(&b->mutex);
  atomic_fetch_add_explicit(&g_fstat_post_ticks, mach_absolute_time() - t0, memory_order_relaxed);
}

static void baton_wait(FiberBaton *b) {
  bool slept = false;
  pthread_mutex_lock(&b->mutex);
  while (!b->ready) {
    slept = true;
    pthread_cond_wait(&b->cond, &b->mutex);
  }
  b->ready = false;
  const int kind = b->kind;
  const uint64_t post_t = b->post_t;
  pthread_mutex_unlock(&b->mutex);
  if (kind == FSTAT_NONE) return;
  uint64_t now = mach_absolute_time();
  if (g_fiber_extra_ticks) {
    const uint64_t until = now + g_fiber_extra_ticks;
    while ((now = mach_absolute_time()) < until) {
    }
  }
  const uint64_t dt = now - post_t;
  atomic_fetch_add_explicit(&g_fstat_n[kind], 1, memory_order_relaxed);
  atomic_fetch_add_explicit(&g_fstat_ticks[kind], dt, memory_order_relaxed);
  if (slept) atomic_fetch_add_explicit(&g_fstat_slept, 1, memory_order_relaxed);
  uint64_t us = (uint64_t)((double)dt / g_ticks_per_us);
  atomic_fetch_add_explicit(&g_fstat_hist[us < FSTAT_BUCKETS ? us : FSTAT_BUCKETS - 1], 1, memory_order_relaxed);
}

static void fiber_global_init(void) {
  mach_timebase_info_data_t tb;
  mach_timebase_info(&tb);
  g_ticks_per_us = 1000.0 * tb.denom / tb.numer;
  const char *e = getenv("PS4_FIBER_THREADS");
  g_fiber_threads = e && atoi(e) > 0;
  fprintf(stderr, "[ps4-fiber] fibers run %s\n",
          g_fiber_threads ? "on their own host threads (PS4_FIBER_THREADS)" : "as coroutines on the worker threads");
  e = getenv("PS4_FIBER_EXTRA_US");
  if (g_fiber_threads && e && atoi(e) > 0) {
    g_fiber_extra_ticks = (uint64_t)(atoi(e) * g_ticks_per_us);
    fprintf(stderr, "[ps4-fiber] adding %d us to every fiber transfer\n", atoi(e));
  }
}

static inline void fstat_count(int kind) {
  atomic_fetch_add_explicit(&g_fstat_n[kind], 1, memory_order_relaxed);
}

void ps4_fiber_print_stats(int frames) {
  uint64_t n[FSTAT_KINDS], ticks = 0, total = 0;
  for (int k = 0; k < FSTAT_KINDS; k++) {
    n[k] = atomic_exchange(&g_fstat_n[k], 0);
    ticks += atomic_exchange(&g_fstat_ticks[k], 0);
    total += n[k];
  }
  uint32_t hist[FSTAT_BUCKETS];
  for (int i = 0; i < FSTAT_BUCKETS; i++) hist[i] = atomic_exchange(&g_fstat_hist[i], 0);
  const uint64_t post_ticks = atomic_exchange(&g_fstat_post_ticks, 0);
  const uint64_t slept = atomic_exchange(&g_fstat_slept, 0);
  if (!total || !g_ticks_per_us) return;
  if (!g_fiber_threads) {
    fprintf(stderr, "[ps4-fiber] per frame: %.0f transfers (%.0f run, %.0f switch, %.0f return), coroutines\n",
            (double)total / frames, (double)n[FSTAT_RUN] / frames, (double)n[FSTAT_SWITCH] / frames,
            (double)n[FSTAT_RETURN] / frames);
    return;
  }
  uint64_t acc = 0;
  int p50 = -1, p95 = -1, p99 = -1;
  for (int i = 0; i < FSTAT_BUCKETS; i++) {
    acc += hist[i];
    if (p50 < 0 && acc * 100 >= total * 50) p50 = i;
    if (p95 < 0 && acc * 100 >= total * 95) p95 = i;
    if (p99 < 0 && acc * 100 >= total * 99) p99 = i;
  }
  fprintf(stderr, "[ps4-fiber] per frame: %.0f transfers (%.0f run, %.0f switch, %.0f return; %.0f%% slept), "
                  "gap avg %.1f us p50 %d p95 %d p99 %d%s us, gap total %.2f ms, signal %.2f ms; %llu fibers\n",
          (double)total / frames, (double)n[FSTAT_RUN] / frames, (double)n[FSTAT_SWITCH] / frames,
          (double)n[FSTAT_RETURN] / frames, 100.0 * slept / total, (double)ticks / g_ticks_per_us / total, p50, p95,
          p99, p99 >= FSTAT_BUCKETS - 1 ? "+" : "", (double)ticks / g_ticks_per_us / 1000.0 / frames,
          (double)post_ticks / g_ticks_per_us / 1000.0 / frames,
          (unsigned long long)atomic_load(&g_fstat_creates));
}

// Every worker, for ps4_fiber_endpoint_host_thread, which runs in signal handlers and so can
// neither lock nor wait. A lookup counts itself in g_lookups while it uses workers and fibers it
// found through the table. Whoever frees a worker or a fiber first makes it unreachable from the
// table (clears the worker's slot; a fiber is unreachable once it is IDLE, see
// FiberWorker.current) and then waits in lookup_quiesce until no lookup is in progress, so a
// lookup never reads freed memory. Every access is sequentially consistent: a lookup either
// counted itself before the freer looked at g_lookups (the freer waits for it) or reads the
// table after the object became unreachable. A lookup blocks signals while it runs, so no signal
// handler on its thread (a guest exception handler may finalize a fiber) waits for it.
// Freed slots are reused.
#define MAX_WORKERS 1024
static FiberWorker *_Atomic g_workers[MAX_WORKERS];
static _Atomic int g_nworkers; // slots in use or used before (at most MAX_WORKERS)
static _Atomic unsigned g_lookups;

static void lookup_quiesce(void) {
  for (unsigned spins = 0; atomic_load(&g_lookups) != 0; spins++)
    if (spins >= 64) sched_yield();
}

static void worker_destroy(void *p) {
  FiberWorker *w = (FiberWorker *)p;
  if (!w) return;
  if (w->slot >= 0) {
    atomic_store(&g_workers[w->slot], NULL);
    lookup_quiesce();
  }
  baton_destroy(&w->baton);
  free(w);
}

bool ps4_fiber_threads_mode(void) { return g_fiber_threads; }

bool ps4_fiber_endpoint_host_thread(const GuestContext *ctx, pthread_t *out) {
  // Asynchronous signals stay pending until the lookup is done; faults are not blocked.
  sigset_t all, old;
  sigfillset(&all);
  sigdelset(&all, SIGSEGV);
  sigdelset(&all, SIGBUS);
  sigdelset(&all, SIGILL);
  sigdelset(&all, SIGFPE);
  sigdelset(&all, SIGTRAP);
  pthread_sigmask(SIG_BLOCK, &all, &old);
  atomic_fetch_add(&g_lookups, 1);
  bool found = false;
  const int n = atomic_load(&g_nworkers);
  for (int i = 0; i < n && !found; i++) {
    FiberWorker *w = atomic_load(&g_workers[i]);
    if (!w) continue;
    FiberHost *h = atomic_load(&w->current);
    if (w->ctx == ctx) {
      *out = g_fiber_threads && h && atomic_load(&h->host_started) ? h->host_thread : w->host;
      found = true;
    } else if (h && h->fctx == ctx) {
      *out = g_fiber_threads && atomic_load(&h->host_started) ? h->host_thread : w->host;
      found = true;
    }
  }
  atomic_fetch_sub(&g_lookups, 1);
  pthread_sigmask(SIG_SETMASK, &old, NULL);
  return found;
}

// Puts w into a free slot of g_workers (or a new one); -1 when the table is full.
static int worker_publish(FiberWorker *w) {
  for (;;) {
    int n = atomic_load(&g_nworkers);
    for (int i = 0; i < n; i++) {
      FiberWorker *expected = NULL;
      if (atomic_compare_exchange_strong(&g_workers[i], &expected, w)) return i;
    }
    if (n >= MAX_WORKERS) return -1;
    // Grow by one slot; whoever gets to the new slot first takes it, so scan again.
    atomic_compare_exchange_strong(&g_nworkers, &n, n + 1);
  }
}

static void worker_key_init(void) {
  pthread_key_create(&g_worker_key, worker_destroy);
}

static FiberWorker *get_worker(GuestContext *ctx) {
  if (tl_worker) return tl_worker;
  pthread_once(&g_worker_key_once, worker_key_init);
  FiberWorker *w = (FiberWorker *)calloc(1, sizeof(FiberWorker));
  if (!w) return NULL;
  w->ctx = ctx;
  w->thread = recomp_get_current_thread();
  w->host = pthread_self();
  baton_init(&w->baton);
  pthread_setspecific(g_worker_key, w);
  tl_worker = w;
  w->slot = worker_publish(w);
  static _Atomic bool warned;
  if (w->slot < 0 && !atomic_exchange(&warned, true))
    fprintf(stderr, "[ps4-fiber] more than %d live guest threads run fibers: guest exceptions for the "
                    "later ones go to their own host thread, not to the fiber they run\n", MAX_WORKERS);
  return w;
}

static OrbisFiberObject *fiber_obj(GuestContext *ctx, uint64_t addr) {
  return (OrbisFiberObject *)(ctx->mem_base + addr);
}

static bool fiber_valid(const OrbisFiberObject *f) {
  return f->magic_start == FIBER_MAGIC_START && f->magic_end == FIBER_MAGIC_END && f->host != 0;
}

// Moves f from IDLE to `to`. On Orbis a fiber can be run or switched to as
// soon as it has called sceFiberSwitch; in coroutine mode its host thread
// may still be leaving the fiber's stack for a few instructions (SUSPENDING),
// so wait for that instead of failing.
static bool fiber_take_idle(OrbisFiberObject *f, uint32_t to) {
  for (unsigned spins = 0;; spins++) {
    uint32_t expected = FIBER_STATE_IDLE;
    if (atomic_compare_exchange_weak(&f->state, &expected, to)) return true;
    if (expected != FIBER_STATE_IDLE && expected != FIBER_STATE_SUSPENDING) return false;
    if (spins >= 64) sched_yield();
  }
}

// Give the fiber the identity of the guest thread it is about to run on.
static void fiber_adopt_thread(GuestContext *f, const GuestContext *t) {
  f->mem_base = t->mem_base;
  f->mem_size = t->mem_size;
  f->heap_ptr = t->heap_ptr;
  f->fs_base = t->fs_base;
  f->gs_base = t->gs_base;
  f->thread_id = t->thread_id;
  f->stack_base = t->stack_base;
  f->stack_alloc_size = t->stack_alloc_size;
  memcpy(f->tls_keys, t->tls_keys, sizeof(f->tls_keys));
  f->process_ctx = t->process_ctx ? t->process_ctx : (GuestContext *)t;
  f->proc_param_addr = t->proc_param_addr;
  f->args_addr = t->args_addr;
}

// TLS values written while the fiber ran belong to the guest thread.
static void fiber_release_thread(const GuestContext *f, GuestContext *t) {
  memcpy(t->tls_keys, f->tls_keys, sizeof(t->tls_keys));
}

// Guest code and the shim bridge find the GuestContext of what runs on a guest thread
// through the TCB slot [fs_base + 0x70] (see the contract at recomp_enter_guest): the
// thread's own context, or the fiber's while a fiber runs. Every hand-over points it at the
// endpoint that runs next on thread t, before that endpoint runs.
static void tcb_set_running(const GuestContext *t, GuestContext *running) {
  if (t->mem_base && t->fs_base)
    *(uint64_t *)(t->mem_base + t->fs_base + RECOMP_TCB_CTX_SLOT) = (uint64_t)(uintptr_t)running;
}

// h is finalized (it was IDLE, so no worker's current fiber): wait for lookups that may
// still use it, then free it.
static void fiber_free_host(FiberHost *h) {
  lookup_quiesce();
  baton_destroy(&h->baton);
  if (h->stack) munmap(h->stack, FIBER_HOST_STACK_SIZE);
  free(h->fctx);
  free(h);
}

static void write_u64(GuestContext *ctx, uint64_t addr, uint64_t val) {
  if (addr) *(uint64_t *)(ctx->mem_base + addr) = val;
}

static void fiber_entry_returned(FiberHost *h) {
  OrbisFiberObject *f = fiber_obj(h->fctx, h->guest_addr);
  fprintf(stderr, "[ps4-fiber] FATAL: fiber '%s' (0x%llx) returned from its entry function\n",
          f->name, (unsigned long long)h->guest_addr);
  fflush(stderr);
  abort();
}

// Start fiber h's guest entry function on the current host stack.
static void fiber_call_entry(FiberHost *h, uint64_t arg_on_run) {
  GuestContext *ctx = h->fctx;
  OrbisFiberObject *f = fiber_obj(ctx, h->guest_addr);
  ctx->rsp = (f->addr_context + f->size_context) & ~0xFULL;
  ctx->rbp = 0;
  ctx->rdi = f->arg_on_initialize;
  ctx->rsi = arg_on_run;
  recomp_call_guest(ctx, f->entry);
  fiber_entry_returned(h);
}

// ---- Thread mode: one host thread per fiber, handed over with batons ----

static void *fiber_host_main(void *arg);

// Hand the worker to fiber h. The caller has already moved h to RUNNING.
static int fiber_resume(FiberHost *h, FiberWorker *w, uint64_t arg, int kind) {
  fiber_adopt_thread(h->fctx, w->ctx);
  h->worker = w;
  h->in_arg = arg;
  w->current = h;
  if (!h->host_started) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, FIBER_HOST_STACK_SIZE);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int ret = pthread_create(&h->host_thread, &attr, fiber_host_main, h);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
      w->current = NULL;
      return ret;
    }
    h->host_started = true;
    atomic_fetch_add_explicit(&g_fstat_creates, 1, memory_order_relaxed);
  }
  tcb_set_running(w->ctx, h->fctx);
  baton_post(&h->baton, kind);
  return 0;
}

// Block the current fiber until someone runs or switches to it again.
static uint64_t fiber_suspend(FiberHost *self) {
  baton_wait(&self->baton);
  if (self->kill) {
    fiber_free_host(self);
    pthread_exit(NULL);
  }
  recomp_set_current_thread(self->worker->thread);
  return self->in_arg;
}

static void *fiber_host_main(void *arg) {
  FiberHost *h = (FiberHost *)arg;
  tl_fiber_self = h;
  g_current_ctx = h->fctx;
  uint64_t arg_on_run = fiber_suspend(h);

  OrbisFiberObject *f = fiber_obj(h->fctx, h->guest_addr);
  if (f->name[0] != '\0') {
#if defined(__APPLE__)
    pthread_setname_np(f->name);
#endif
  }
  fiber_call_entry(h, arg_on_run);
  return NULL;
}

// ---- Coroutine mode: fibers run on the host thread of the guest thread ----
// that runs them, each on its own host stack.
//
// A fiber that suspended on one worker can be resumed on another, so after a
// context switch returns, code must not use thread-locals it read before the
// switch (the compiler may reuse the old thread's TLS address). Everything a
// host thread needs for the next endpoint (tl_fiber_self, g_current_ctx) is
// set by the switching thread before it switches; after the switch only
// locals and FiberHost/FiberWorker fields are used.
//
// ps4_fiber_ctx_switch(save_sp, load_sp) saves the callee-saved registers
// (x19-x30, d8-d15) on the current stack, stores sp to *save_sp, and
// restores the same frame from load_sp. A new fiber's stack starts with such
// a frame that "returns" into ps4_fiber_co_trampoline with x19 = FiberHost
// and x20 = fiber_co_main.
// FPCR and MXCSR need no slot in the frame: a switch happens inside a shim, and
// the shim bridge keeps the switching endpoint's guest FPCR in its frame on that
// endpoint's host stack and writes it back when the endpoint resumes, whatever
// FPCR (round to nearest, but the FZ of the last endpoint) the host thread has
// then. MXCSR is in each endpoint's own GuestContext, the one the TCB slot names
// while it runs.
__attribute__((visibility("hidden"))) void ps4_fiber_ctx_switch(void **save_sp, void *load_sp);
__attribute__((visibility("hidden"))) extern const char ps4_fiber_co_trampoline[];

__asm__(".text\n"
        ".p2align 4\n"
        ".private_extern _ps4_fiber_ctx_switch\n"
        "_ps4_fiber_ctx_switch:\n"
        "  sub sp, sp, #160\n"
        "  stp x19, x20, [sp, #0]\n"
        "  stp x21, x22, [sp, #16]\n"
        "  stp x23, x24, [sp, #32]\n"
        "  stp x25, x26, [sp, #48]\n"
        "  stp x27, x28, [sp, #64]\n"
        "  stp x29, x30, [sp, #80]\n"
        "  stp d8, d9, [sp, #96]\n"
        "  stp d10, d11, [sp, #112]\n"
        "  stp d12, d13, [sp, #128]\n"
        "  stp d14, d15, [sp, #144]\n"
        "  mov x9, sp\n"
        "  str x9, [x0]\n"
        "  mov sp, x1\n"
        "  ldp x19, x20, [sp, #0]\n"
        "  ldp x21, x22, [sp, #16]\n"
        "  ldp x23, x24, [sp, #32]\n"
        "  ldp x25, x26, [sp, #48]\n"
        "  ldp x27, x28, [sp, #64]\n"
        "  ldp x29, x30, [sp, #80]\n"
        "  ldp d8, d9, [sp, #96]\n"
        "  ldp d10, d11, [sp, #112]\n"
        "  ldp d12, d13, [sp, #128]\n"
        "  ldp d14, d15, [sp, #144]\n"
        "  add sp, sp, #160\n"
        "  ret\n"
        ".p2align 4\n"
        ".private_extern _ps4_fiber_co_trampoline\n"
        "_ps4_fiber_co_trampoline:\n"
        "  mov x0, x19\n"
        "  blr x20\n"
        "  brk #0xf1\n");

// Runs right after a switch, on the endpoint that was switched to: the
// endpoint we came from is off its host stack now and may be claimed.
static inline void co_finish_switch(_Atomic uint32_t **release) {
  _Atomic uint32_t *st = *release;
  if (st) {
    *release = NULL;
    atomic_store(st, FIBER_STATE_IDLE);
  }
}

static void fiber_co_main(FiberHost *h) {
  co_finish_switch(&h->release);
  fiber_call_entry(h, h->in_arg);
}

static bool co_alloc_stack(FiberHost *h) {
  if (h->stack) return true;
  void *p = mmap(NULL, FIBER_HOST_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (p == MAP_FAILED) return false;
  mprotect(p, (size_t)getpagesize(), PROT_NONE); // guard page
  h->stack = p;
  uint64_t *frame = (uint64_t *)((uint8_t *)p + FIBER_HOST_STACK_SIZE) - 20; // 160-byte switch frame
  memset(frame, 0, 160);
  frame[0] = (uint64_t)(uintptr_t)h;            // x19
  frame[1] = (uint64_t)(uintptr_t)fiber_co_main; // x20
  frame[11] = (uint64_t)(uintptr_t)ps4_fiber_co_trampoline; // x30; x29 = 0 ends the frame chain
  h->sp = frame;
  atomic_fetch_add_explicit(&g_fstat_creates, 1, memory_order_relaxed);
  return true;
}

// Prepare fiber h to run on worker w's host thread and make it the thread's
// current endpoint. The caller then switches to h->sp.
static void co_enter(FiberHost *h, FiberWorker *w, uint64_t arg, _Atomic uint32_t *release) {
  fiber_adopt_thread(h->fctx, w->ctx);
  h->worker = w;
  h->in_arg = arg;
  h->release = release;
  w->current = h;
  tl_fiber_self = h;
  g_current_ctx = h->fctx;
  tcb_set_running(w->ctx, h->fctx);
}

// ---- Guest API ----

static int32_t fiber_initialize(GuestContext *ctx, uint64_t fiber_addr, uint64_t name_addr,
                                uint64_t entry, uint64_t arg_on_init, uint64_t addr_context,
                                uint64_t size_context, uint64_t opt_addr) {
  if (!fiber_addr || !name_addr || !entry) return (int32_t)ORBIS_FIBER_ERROR_NULL;
  if (fiber_addr & 7) return (int32_t)ORBIS_FIBER_ERROR_ALIGNMENT;
  if (opt_addr & 7) return (int32_t)ORBIS_FIBER_ERROR_ALIGNMENT;
  if (addr_context) {
    if ((addr_context & 0xF) || (size_context & 0xF)) return (int32_t)ORBIS_FIBER_ERROR_ALIGNMENT;
    if (size_context < ORBIS_FIBER_CONTEXT_MINIMUM_SIZE) return (int32_t)ORBIS_FIBER_ERROR_RANGE;
  } else if (size_context) {
    return (int32_t)ORBIS_FIBER_ERROR_INVALID;
  }

  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, fiber_global_init);
  FiberHost *h = (FiberHost *)calloc(1, sizeof(FiberHost));
  GuestContext *fctx = (GuestContext *)calloc(1, sizeof(GuestContext));
  if (!h || !fctx) {
    free(h);
    free(fctx);
    return (int32_t)ORBIS_FIBER_ERROR_INVALID;
  }
  fiber_adopt_thread(fctx, ctx);
  fctx->fpu_cw = ctx->fpu_cw ? ctx->fpu_cw : 0x037F;
  fctx->mxcsr = ctx->mxcsr ? ctx->mxcsr : 0x1F80;
  h->guest_addr = fiber_addr;
  h->fctx = fctx;
  baton_init(&h->baton);

  OrbisFiberObject *f = fiber_obj(ctx, fiber_addr);
  memset(f, 0, sizeof(*f));
  f->magic_start = FIBER_MAGIC_START;
  f->magic_end = FIBER_MAGIC_END;
  f->host = (uint64_t)(uintptr_t)h;
  f->entry = entry;
  f->arg_on_initialize = arg_on_init;
  f->addr_context = addr_context;
  f->size_context = size_context;
  strncpy(f->name, (const char *)(ctx->mem_base + name_addr), ORBIS_FIBER_MAX_NAME_LENGTH);
  atomic_store(&f->state, FIBER_STATE_IDLE);
  return 0;
}

void shim_sceFiberOptParamInitialize(GuestContext *ctx) {
  uint64_t opt = ctx->rdi;
  if (!opt) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_FIBER_ERROR_NULL;
  } else if (opt & 7) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_FIBER_ERROR_ALIGNMENT;
  } else {
    memset(ctx->mem_base + opt, 0, ORBIS_FIBER_OPT_PARAM_SIZE);
    *(uint32_t *)(ctx->mem_base + opt) = FIBER_OPT_MAGIC;
    ctx->rax = 0;
  }
  SHIM_RETURN();
}

// _sceFiberInitializeImpl(fiber, name, entry, argOnInitialize, addrContext,
//                         sizeContext, optParam, buildVersion)
void shim_sceFiberInitializeImpl(GuestContext *ctx) {
  uint64_t opt_addr = *(uint64_t *)(ctx->mem_base + ctx->rsp + 8);
  int32_t ret = fiber_initialize(ctx, ctx->rdi, ctx->rsi, ctx->rdx, ctx->rcx, ctx->r8, ctx->r9, opt_addr);
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

void shim_sceFiberInitializeWithInternalOptionImpl(GuestContext *ctx) {
  shim_sceFiberInitializeImpl(ctx);
}

void shim_sceFiberFinalize(GuestContext *ctx) {
  uint64_t addr = ctx->rdi;
  int32_t ret = 0;
  if (!addr) {
    ret = (int32_t)ORBIS_FIBER_ERROR_NULL;
  } else if (addr & 7) {
    ret = (int32_t)ORBIS_FIBER_ERROR_ALIGNMENT;
  } else {
    OrbisFiberObject *f = fiber_obj(ctx, addr);
    if (!fiber_valid(f)) {
      ret = (int32_t)ORBIS_FIBER_ERROR_INVALID;
    } else if (!fiber_take_idle(f, FIBER_STATE_FINALIZED)) {
      ret = (int32_t)ORBIS_FIBER_ERROR_STATE;
    } else {
      FiberHost *h = (FiberHost *)(uintptr_t)f->host;
      if (h->host_started) {
        // The host thread is parked in fiber_suspend and frees itself.
        h->kill = true;
        baton_post(&h->baton, FSTAT_NONE);
      } else {
        // Coroutine mode: a suspended fiber is just its stack.
        fiber_free_host(h);
      }
      f->host = 0;
      f->magic_start = 0;
      f->magic_end = 0;
    }
  }
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

static int32_t fiber_attach_context(OrbisFiberObject *f, uint64_t addr_context, uint64_t size_context) {
  if (f->addr_context) return (int32_t)ORBIS_FIBER_ERROR_STATE;
  if (!addr_context) return (int32_t)ORBIS_FIBER_ERROR_NULL;
  if ((addr_context & 0xF) || (size_context & 0xF)) return (int32_t)ORBIS_FIBER_ERROR_ALIGNMENT;
  if (size_context < ORBIS_FIBER_CONTEXT_MINIMUM_SIZE) return (int32_t)ORBIS_FIBER_ERROR_RANGE;
  f->addr_context = addr_context;
  f->size_context = size_context;
  return 0;
}

// Claim fiber `addr` for running: validates it and moves it IDLE -> RUNNING.
static int32_t fiber_claim(GuestContext *ctx, uint64_t addr, OrbisFiberObject **out) {
  if (!addr) return (int32_t)ORBIS_FIBER_ERROR_NULL;
  if (addr & 7) return (int32_t)ORBIS_FIBER_ERROR_ALIGNMENT;
  OrbisFiberObject *f = fiber_obj(ctx, addr);
  if (!fiber_valid(f)) return (int32_t)ORBIS_FIBER_ERROR_INVALID;
  if (!fiber_take_idle(f, FIBER_STATE_RUNNING)) return (int32_t)ORBIS_FIBER_ERROR_STATE;
  *out = f;
  return 0;
}

static int32_t fiber_run(GuestContext *ctx, uint64_t addr, uint64_t addr_context, uint64_t size_context,
                         bool attach, uint64_t arg_on_run_to, uint64_t arg_on_return_addr) {
  if (tl_fiber_self) return (int32_t)ORBIS_FIBER_ERROR_PERMISSION;
  OrbisFiberObject *f = NULL;
  int32_t ret = fiber_claim(ctx, addr, &f);
  if (ret != 0) return ret;
  if (attach) {
    ret = fiber_attach_context(f, addr_context, size_context);
  } else if (!f->addr_context) {
    ret = (int32_t)ORBIS_FIBER_ERROR_INVALID;
  }
  FiberWorker *w = ret == 0 ? get_worker(ctx) : NULL;
  if (ret == 0 && !w) ret = (int32_t)ORBIS_FIBER_ERROR_INVALID;
  FiberHost *h = (FiberHost *)(uintptr_t)f->host;
  if (ret == 0 && !g_fiber_threads && !co_alloc_stack(h)) ret = (int32_t)ORBIS_FIBER_ERROR_INVALID;
  if (ret != 0) {
    atomic_store(&f->state, FIBER_STATE_IDLE);
    return ret;
  }

  w->return_arg = 0;
  if (g_fiber_threads) {
    int err = fiber_resume(h, w, arg_on_run_to, FSTAT_RUN);
    if (err != 0) {
      atomic_store(&f->state, FIBER_STATE_IDLE);
      return (int32_t)ORBIS_FIBER_ERROR_INVALID;
    }
    baton_wait(&w->baton);
  } else {
    fstat_count(FSTAT_RUN);
    co_enter(h, w, arg_on_run_to, NULL);
    ps4_fiber_ctx_switch(&w->sp, h->sp);
    // Back from sceFiberReturnToThread; always on this host thread, since
    // only this thread's fibers return to its stack.
    co_finish_switch(&w->release);
  }
  write_u64(ctx, arg_on_return_addr, w->return_arg);
  return 0;
}

static int32_t fiber_switch(GuestContext *ctx, uint64_t addr, uint64_t addr_context, uint64_t size_context,
                            bool attach, uint64_t arg_on_run_to, uint64_t arg_on_run_addr) {
  FiberHost *self = tl_fiber_self;
  if (!self) return (int32_t)ORBIS_FIBER_ERROR_PERMISSION;
  OrbisFiberObject *f = NULL;
  int32_t ret = fiber_claim(ctx, addr, &f);
  if (ret != 0) return ret;
  if (attach) {
    ret = fiber_attach_context(f, addr_context, size_context);
  } else if (!f->addr_context) {
    ret = (int32_t)ORBIS_FIBER_ERROR_INVALID;
  }
  FiberHost *h = (FiberHost *)(uintptr_t)f->host;
  if (ret == 0 && !g_fiber_threads && !co_alloc_stack(h)) ret = (int32_t)ORBIS_FIBER_ERROR_INVALID;
  if (ret != 0) {
    atomic_store(&f->state, FIBER_STATE_IDLE);
    return ret;
  }

  FiberWorker *w = self->worker;
  OrbisFiberObject *self_obj = fiber_obj(ctx, self->guest_addr);
  fiber_release_thread(self->fctx, w->ctx);
  if (!g_fiber_threads) {
    fstat_count(FSTAT_SWITCH);
    atomic_store(&self_obj->state, FIBER_STATE_SUSPENDING);
    co_enter(h, w, arg_on_run_to, &self_obj->state);
    ps4_fiber_ctx_switch(&self->sp, h->sp);
    // Resumed, possibly on another worker's host thread: locals only.
    co_finish_switch(&self->release);
    write_u64(ctx, arg_on_run_addr, self->in_arg);
    return 0;
  }
  // On Orbis the switch is atomic: once sceFiberSwitch is called the current
  // fiber can immediately be run/switched to from another thread. Mark it
  // idle before handing the worker to the target; a resume that races with
  // us just posts our baton, which fiber_suspend consumes right away. The
  // target is the worker's current fiber first: an IDLE fiber may be finalized.
  w->current = h;
  atomic_store(&self_obj->state, FIBER_STATE_IDLE);
  int err = fiber_resume(h, w, arg_on_run_to, FSTAT_SWITCH);
  if (err != 0) {
    uint32_t idle = FIBER_STATE_IDLE;
    // Unless another thread has claimed us meanwhile (then we may be finalized), we run on.
    w->current = atomic_compare_exchange_strong(&self_obj->state, &idle, FIBER_STATE_RUNNING) ? self : NULL;
    atomic_store(&f->state, FIBER_STATE_IDLE);
    return (int32_t)ORBIS_FIBER_ERROR_INVALID;
  }
  // From here another thread may claim this fiber; only touch locals.
  uint64_t arg = fiber_suspend(self);
  write_u64(ctx, arg_on_run_addr, arg);
  return 0;
}

// sceFiberRun(fiber, argOnRunTo, argOnReturn*)
void shim_sceFiberRun(GuestContext *ctx) {
  int32_t ret = fiber_run(ctx, ctx->rdi, 0, 0, false, ctx->rsi, ctx->rdx);
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

// sceFiberSwitch(fiber, argOnRunTo, argOnRun*)
void shim_sceFiberSwitch(GuestContext *ctx) {
  int32_t ret = fiber_switch(ctx, ctx->rdi, 0, 0, false, ctx->rsi, ctx->rdx);
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

// _sceFiberAttachContextAndRun(fiber, addrContext, sizeContext, argOnRunTo, argOnReturn*)
void shim_sceFiberAttachContextAndRun(GuestContext *ctx) {
  int32_t ret = fiber_run(ctx, ctx->rdi, ctx->rsi, ctx->rdx, true, ctx->rcx, ctx->r8);
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

// _sceFiberAttachContextAndSwitch(fiber, addrContext, sizeContext, argOnRunTo, argOnRun*)
void shim_sceFiberAttachContextAndSwitch(GuestContext *ctx) {
  int32_t ret = fiber_switch(ctx, ctx->rdi, ctx->rsi, ctx->rdx, true, ctx->rcx, ctx->r8);
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

// sceFiberReturnToThread(argOnReturn, argOnRun*)
void shim_sceFiberReturnToThread(GuestContext *ctx) {
  FiberHost *self = tl_fiber_self;
  if (!self) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_FIBER_ERROR_PERMISSION;
    SHIM_RETURN();
  }
  uint64_t arg_on_run_addr = ctx->rsi;
  FiberWorker *w = self->worker;
  OrbisFiberObject *self_obj = fiber_obj(ctx, self->guest_addr);
  fiber_release_thread(self->fctx, w->ctx);
  w->return_arg = ctx->rdi;
  w->current = NULL;
  tcb_set_running(w->ctx, w->ctx);
  uint64_t arg;
  if (!g_fiber_threads) {
    fstat_count(FSTAT_RETURN);
    atomic_store(&self_obj->state, FIBER_STATE_SUSPENDING);
    w->release = &self_obj->state;
    tl_fiber_self = NULL;
    g_current_ctx = w->ctx;
    ps4_fiber_ctx_switch(&self->sp, w->sp);
    // Resumed, possibly on another worker's host thread: locals only.
    co_finish_switch(&self->release);
    arg = self->in_arg;
  } else {
    atomic_store(&self_obj->state, FIBER_STATE_IDLE);
    baton_post(&w->baton, FSTAT_RETURN);
    arg = fiber_suspend(self);
  }
  write_u64(ctx, arg_on_run_addr, arg);
  ctx->rax = 0;
  SHIM_RETURN();
}

// sceFiberGetSelf(fiber**)
void shim_sceFiberGetSelf(GuestContext *ctx) {
  uint64_t out = ctx->rdi;
  if (!out) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_FIBER_ERROR_NULL;
  } else if (!tl_fiber_self) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_FIBER_ERROR_PERMISSION;
  } else {
    write_u64(ctx, out, tl_fiber_self->guest_addr);
    ctx->rax = 0;
  }
  SHIM_RETURN();
}

// sceFiberGetInfo(fiber, info*)
void shim_sceFiberGetInfo(GuestContext *ctx) {
  uint64_t addr = ctx->rdi;
  uint64_t info_addr = ctx->rsi;
  int32_t ret = 0;
  if (!addr || !info_addr) {
    ret = (int32_t)ORBIS_FIBER_ERROR_NULL;
  } else if ((addr & 7) || (info_addr & 7)) {
    ret = (int32_t)ORBIS_FIBER_ERROR_ALIGNMENT;
  } else {
    OrbisFiberObject *f = fiber_obj(ctx, addr);
    OrbisFiberInfo *info = (OrbisFiberInfo *)(ctx->mem_base + info_addr);
    if (!fiber_valid(f) || info->size != sizeof(OrbisFiberInfo)) {
      ret = (int32_t)ORBIS_FIBER_ERROR_INVALID;
    } else {
      info->entry = f->entry;
      info->arg_on_initialize = f->arg_on_initialize;
      info->addr_context = f->addr_context;
      info->size_context = f->size_context;
      memcpy(info->name, f->name, sizeof(info->name));
      // Context usage tracking is not enabled, so no margin is measured.
      info->size_context_margin = (uint64_t)-1;
    }
  }
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

// sceFiberRename(fiber, name)
void shim_sceFiberRename(GuestContext *ctx) {
  uint64_t addr = ctx->rdi;
  uint64_t name_addr = ctx->rsi;
  int32_t ret = 0;
  if (!addr || !name_addr) {
    ret = (int32_t)ORBIS_FIBER_ERROR_NULL;
  } else if (addr & 7) {
    ret = (int32_t)ORBIS_FIBER_ERROR_ALIGNMENT;
  } else {
    OrbisFiberObject *f = fiber_obj(ctx, addr);
    if (!fiber_valid(f)) {
      ret = (int32_t)ORBIS_FIBER_ERROR_INVALID;
    } else {
      memset(f->name, 0, sizeof(f->name));
      strncpy(f->name, (const char *)(ctx->mem_base + name_addr), ORBIS_FIBER_MAX_NAME_LENGTH);
    }
  }
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

// Context size checking fills fiber stacks with a pattern to measure peak
// usage. It only feeds sceFiberGetInfo's margin, which reports "unknown".
void shim_sceFiberStartContextSizeCheck(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceFiberStopContextSizeCheck(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}
