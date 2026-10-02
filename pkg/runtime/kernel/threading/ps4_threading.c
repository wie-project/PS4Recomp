#include "recomp_runtime.h"
#include "ps4_threading.h"
#include <errno.h>
#include <sched.h>
#include <signal.h>

// Multi-threading runtime support
typedef struct RecompPthreadAttr {
  uint32_t sched_policy;
  int32_t sched_inherit;
  int32_t prio;
  int32_t suspend;
  uint32_t flags;
  uint64_t stackaddr_attr;
  uint64_t stacksize_attr;
  uint64_t guardsize_attr;
  uint64_t cpusetsize;
  uint64_t cpuset;
} RecompPthreadAttr;

static RecompPthreadAttr *get_pthread_attr(GuestContext *ctx, uint64_t attr_addr) {
  if (!attr_addr) return NULL;
  uint64_t ptr = *(uint64_t *)(ctx->mem_base + attr_addr);
  if (!ptr) return NULL;
  return (RecompPthreadAttr *)ptr;
}

typedef struct PthreadCleanupEntry {
  uint64_t routine;
  uint64_t arg;
  bool onheap;
  struct PthreadCleanupEntry *next;
} PthreadCleanupEntry;

typedef struct RecompThread {
  pthread_t host_thread;
  uint64_t thread_id;
  GuestContext *ctx;
  uint64_t start_routine;
  uint64_t arg;
  uint64_t ret_val;
  bool finished;
  bool joined;
  bool detached;
  int cancel_state;
  PthreadCleanupEntry *cleanup_stack;
  struct RecompThread *next;
} RecompThread;

static RecompThread *g_threads = NULL;
static pthread_mutex_t g_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_thread_counter = 1000;
static _Thread_local RecompThread *g_current_thread = NULL;

static void register_thread(RecompThread *t) {
  pthread_mutex_lock(&g_threads_mutex);
  t->next = g_threads;
  g_threads = t;
  pthread_mutex_unlock(&g_threads_mutex);
}

static RecompThread *find_thread(uint64_t handle) {
  pthread_mutex_lock(&g_threads_mutex);
  RecompThread *curr = g_threads;
  while (curr) {
    if ((uint64_t)curr == handle || curr->thread_id == handle) {
      pthread_mutex_unlock(&g_threads_mutex);
      return curr;
    }
    curr = curr->next;
  }
  pthread_mutex_unlock(&g_threads_mutex);
  return NULL;
}

static void unregister_thread(RecompThread *t) {
  pthread_mutex_lock(&g_threads_mutex);
  RecompThread **curr = &g_threads;
  while (*curr) {
    if (*curr == t) {
      *curr = t->next;
      break;
    }
    curr = &(*curr)->next;
  }
  pthread_mutex_unlock(&g_threads_mutex);
}

static RecompThread g_main_thread_obj = {0};

void recomp_init_main_thread(GuestContext *ctx) {
  g_main_thread_obj.host_thread = pthread_self();
  g_main_thread_obj.thread_id = ctx->thread_id ? ctx->thread_id : 1000;
  g_main_thread_obj.ctx = ctx;
  g_current_thread = &g_main_thread_obj;
  register_thread(&g_main_thread_obj);
}

static void *recomp_host_thread_runner(void *arg) {
  RecompThread *t = (RecompThread *)arg;
  GuestContext *ctx = t->ctx;
  g_current_ctx = ctx;
  g_current_thread = t;

  // Push dummy return address on guest stack
  ctx->rsp -= 8;
  MEM_U64(ctx->rsp) = 0xdeadbeefULL;

  // First parameter in AMD64 calling convention is RDI
  ctx->rdi = t->arg;
  ctx->rip = t->start_routine;

  recomp_dispatch(ctx, t->start_routine);

  t->ret_val = ctx->rax;

  // Run registered pthread TLS key destructors
  GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
  for (int iter = 0; iter < 4; iter++) {
    bool has_active = false;
    for (int k = 0; k < 128; k++) {
      uint64_t val = ctx->tls_keys[k];
      uint64_t dtor = proc->tls_destructors[k];
      if (val != 0 && dtor != 0) {
        ctx->tls_keys[k] = 0;
        has_active = true;
        ctx->rdi = val;
        ctx->rsp -= 8;
        MEM_U64(ctx->rsp) = 0xdeadbeefULL;
        ctx->rip = dtor;
        recomp_dispatch(ctx, dtor);
      }
    }
    if (!has_active) break;
  }

  t->finished = true;

  if (t->detached) {
    unregister_thread(t);
    recomp_free_thread_context(ctx);
    free(t);
  }
  return NULL;
}

void shim_pthread_create(GuestContext *ctx) {
  uint64_t thread_ptr_addr = ctx->rdi;
  uint64_t attr_addr = ctx->rsi;
  uint64_t start_routine = ctx->rdx;
  uint64_t arg = ctx->rcx;

  (void)attr_addr;

  RecompThread *t = (RecompThread *)calloc(1, sizeof(RecompThread));
  if (!t) {
    set_guest_errno(ctx, ENOMEM);
    ctx->rax = (uint64_t)ENOMEM;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&g_threads_mutex);
  t->thread_id = ++g_thread_counter;
  pthread_mutex_unlock(&g_threads_mutex);

  uint64_t stack_size = 4 * 1024 * 1024;
  RecompPthreadAttr *attr = get_pthread_attr(ctx, attr_addr);
  if (attr && attr->stacksize_attr >= 16 * 1024) {
    stack_size = attr->stacksize_attr;
  }

  GuestContext *child_ctx = recomp_create_thread_context(ctx, stack_size);
  if (!child_ctx) {
    free(t);
    set_guest_errno(ctx, ENOMEM);
    ctx->rax = (uint64_t)ENOMEM;
    SHIM_RETURN();
  }
  child_ctx->thread_id = t->thread_id;

  t->ctx = child_ctx;
  t->start_routine = start_routine;
  t->arg = arg;
  t->finished = false;
  t->joined = false;
  t->detached = false;

  register_thread(t);

  // Store thread handle in guest pointer
  if (thread_ptr_addr != 0) {
    *(uint64_t *)(ctx->mem_base + thread_ptr_addr) = (uint64_t)t;
  }

  int ret = pthread_create(&t->host_thread, NULL, recomp_host_thread_runner, t);
  if (ret != 0) {
    unregister_thread(t);
    recomp_free_thread_context(child_ctx);
    free(t);
    set_guest_errno(ctx, ret);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
  }

  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_join(GuestContext *ctx) {
  uint64_t handle = ctx->rdi;
  uint64_t retval_ptr_addr = ctx->rsi;

  RecompThread *t = find_thread(handle);
  if (!t) {
    ctx->rax = 0;
    SHIM_RETURN();
  }

  pthread_join(t->host_thread, NULL);
  t->joined = true;

  if (retval_ptr_addr) {
    *(uint64_t *)(ctx->mem_base + retval_ptr_addr) = t->ret_val;
  }

  unregister_thread(t);
  if (t->ctx) {
    recomp_free_thread_context(t->ctx);
  }
  free(t);

  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_detach(GuestContext *ctx) {
  uint64_t handle = ctx->rdi;
  RecompThread *t = find_thread(handle);
  if (t) {
    t->detached = true;
    pthread_detach(t->host_thread);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_self(GuestContext *ctx) {
  if (ctx && ctx->fs_base) {
    uint64_t t = MEM_U64(ctx->fs_base + 0x10);
    ctx->rax = (t != 0) ? t : ctx->fs_base;
  } else if (g_current_thread) {
    ctx->rax = (uint64_t)g_current_thread;
  } else {
    ctx->rax = 0x1000ULL;
  }
  SHIM_RETURN();
}

void shim_pthread_equal(GuestContext *ctx) {
  uint64_t t1 = ctx->rdi;
  uint64_t t2 = ctx->rsi;
  ctx->rax = (t1 == t2) ? 1 : 0;
  SHIM_RETURN();
}

static uint32_t g_next_key = 1;
static pthread_mutex_t g_key_lock = PTHREAD_MUTEX_INITIALIZER;

void shim_pthread_key_create(GuestContext *ctx) {
  uint64_t key_ptr_addr = ctx->rdi;
  uint64_t destructor_addr = ctx->rsi;
  pthread_mutex_lock(&g_key_lock);
  uint32_t key = g_next_key++;
  pthread_mutex_unlock(&g_key_lock);
  if (key_ptr_addr) {
    *(uint32_t *)(ctx->mem_base + key_ptr_addr) = key;
  }
  if (key < 128) {
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    proc->tls_destructors[key] = destructor_addr;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_key_delete(GuestContext *ctx) {
  uint32_t key = (uint32_t)ctx->rdi;
  if (key < 128) {
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    proc->tls_destructors[key] = 0;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_setspecific(GuestContext *ctx) {
  uint32_t key = (uint32_t)ctx->rdi;
  uint64_t val = ctx->rsi;
  if (key < 128) {
    ctx->tls_keys[key] = val;
    ctx->rax = 0;
  } else {
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)EINVAL;
  }
  SHIM_RETURN();
}

void shim_pthread_getspecific(GuestContext *ctx) {
  uint32_t key = (uint32_t)ctx->rdi;
  if (key < 128) {
    ctx->rax = ctx->tls_keys[key];
  } else {
    ctx->rax = 0;
  }
  SHIM_RETURN();
}

void shim_pthread_attr_init(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  if (!attr_addr) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  RecompPthreadAttr *pattr = (RecompPthreadAttr *)calloc(1, sizeof(RecompPthreadAttr));
  if (!pattr) {
    ctx->rax = (uint64_t)ENOMEM;
    SHIM_RETURN();
  }
  pattr->sched_policy = 1; // Fifo
  pattr->sched_inherit = 4; // InheritSched
  pattr->prio = 700; // ORBIS_KERNEL_PRIO_FIFO_DEFAULT
  pattr->suspend = 0;
  pattr->flags = 2; // ScopeSystem
  pattr->stackaddr_attr = 0;
  pattr->stacksize_attr = 1024 * 1024; // 1MB default
  pattr->guardsize_attr = 0;
  pattr->cpusetsize = 0;
  pattr->cpuset = 0;

  *(uint64_t *)(ctx->mem_base + attr_addr) = (uint64_t)pattr;
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_attr_destroy(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  if (attr_addr) {
    RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
    if (pattr) {
      free(pattr);
      *(uint64_t *)(ctx->mem_base + attr_addr) = 0;
    }
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_attr_setdetachstate(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  int detachstate = (int)ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (pattr) {
    if (detachstate) {
      pattr->flags |= 1; // Detached
    } else {
      pattr->flags &= ~1;
    }
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_attr_setstacksize(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  size_t stacksize = (size_t)ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (pattr) {
    pattr->stacksize_attr = stacksize;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_getschedparam(GuestContext *ctx) {
  uint64_t pol_addr = ctx->rsi;
  uint64_t param_addr = ctx->rdx;
  if (pol_addr) {
    *(int *)(ctx->mem_base + pol_addr) = SCHED_OTHER;
  }
  if (param_addr) {
    struct sched_param *sp = (struct sched_param *)(ctx->mem_base + param_addr);
    sp->sched_priority = 0;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_setschedparam(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_setcanceltype(GuestContext *ctx) {
  uint64_t old_addr = ctx->rsi;
  if (old_addr) {
    *(int *)(ctx->mem_base + old_addr) = 0;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_setcancelstate(GuestContext *ctx) {
  int state = (int)ctx->rdi;
  uint64_t old_addr = ctx->rsi;
  if (state != 0 && state != 1) { // 0: PTHREAD_CANCEL_ENABLE, 1: PTHREAD_CANCEL_DISABLE
    ctx->rax = EINVAL;
    SHIM_RETURN();
  }
  RecompThread *t = g_current_thread;
  int old = t ? t->cancel_state : 0;
  if (old_addr && ctx->mem_base) {
    *(int *)(ctx->mem_base + old_addr) = old;
  }
  if (t) {
    t->cancel_state = state;
  }
  pthread_setcancelstate(state == 0 ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE, NULL);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim___pthread_cleanup_push_imp(GuestContext *ctx) {
  uint64_t routine = ctx->rdi;
  uint64_t arg = ctx->rsi;
  uint64_t newbuf = ctx->rdx;
  if (newbuf && ctx->mem_base) {
    *(uint64_t *)(ctx->mem_base + newbuf) = routine;
    *(uint64_t *)(ctx->mem_base + newbuf + 8) = arg;
    *(int32_t *)(ctx->mem_base + newbuf + 16) = 0;
  }
  RecompThread *t = g_current_thread;
  if (t) {
    PthreadCleanupEntry *e = (PthreadCleanupEntry *)calloc(1, sizeof(PthreadCleanupEntry));
    if (e) {
      e->routine = routine;
      e->arg = arg;
      e->onheap = false;
      e->next = t->cleanup_stack;
      t->cleanup_stack = e;
    }
  }
  SHIM_RETURN();
}

void shim_pthread_cleanup_push(GuestContext *ctx) {
  uint64_t routine = ctx->rdi;
  uint64_t arg = ctx->rsi;
  RecompThread *t = g_current_thread;
  if (t) {
    PthreadCleanupEntry *e = (PthreadCleanupEntry *)calloc(1, sizeof(PthreadCleanupEntry));
    if (e) {
      e->routine = routine;
      e->arg = arg;
      e->onheap = true;
      e->next = t->cleanup_stack;
      t->cleanup_stack = e;
    }
  }
  SHIM_RETURN();
}

void shim___pthread_cleanup_pop_imp(GuestContext *ctx) {
  int execute = (int)ctx->rdi;
  RecompThread *t = g_current_thread;
  if (t && t->cleanup_stack) {
    PthreadCleanupEntry *top = t->cleanup_stack;
    t->cleanup_stack = top->next;
    uint64_t routine = top->routine;
    uint64_t arg = top->arg;
    free(top);
    if (execute && routine) {
      ctx->rdi = arg;
      ctx->rsp -= 8;
      MEM_U64(ctx->rsp) = 0xdeadbeefULL;
      ctx->rip = routine;
      recomp_dispatch(ctx, routine);
    }
  }
  SHIM_RETURN();
}

void shim_pthread_cleanup_pop(GuestContext *ctx) {
  shim___pthread_cleanup_pop_imp(ctx);
}

void shim_sched_get_priority_max(GuestContext *ctx) {
  int policy = (int)ctx->rdi;
  int ret = sched_get_priority_max(policy);
  if (ret < 0) {
    ret = 0;
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

void shim_sched_get_priority_min(GuestContext *ctx) {
  int policy = (int)ctx->rdi;
  int ret = sched_get_priority_min(policy);
  if (ret < 0) {
    ret = 0;
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

void shim_pthread_sigmask(GuestContext *ctx) {
  int how = (int)ctx->rdi;
  uint64_t set_addr = ctx->rsi;
  uint64_t oldset_addr = ctx->rdx;

  sigset_t host_set, host_oldset;
  sigset_t *p_set = NULL;
  sigset_t *p_old = oldset_addr ? &host_oldset : NULL;

  if (set_addr) {
    uint32_t *guest_bits = (uint32_t *)(ctx->mem_base + set_addr);
    host_set = (sigset_t)guest_bits[0];
    p_set = &host_set;
  }

  int ret = pthread_sigmask(how, p_set, p_old);
  if (ret != 0) {
    set_guest_errno(ctx, ret);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
  }
  if (oldset_addr) {
    uint32_t *guest_old = (uint32_t *)(ctx->mem_base + oldset_addr);
    guest_old[0] = (uint32_t)host_oldset;
    guest_old[1] = 0;
    guest_old[2] = 0;
    guest_old[3] = 0;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

// scePthread management shims
void shim_scePthreadCreate(GuestContext *ctx) {
  uint64_t thread_ptr_addr = ctx->rdi;
  uint64_t attr_addr = ctx->rsi;
  uint64_t start_routine = ctx->rdx;
  uint64_t arg = ctx->rcx;
  uint64_t name_addr = ctx->r8;

  RecompThread *t = (RecompThread *)calloc(1, sizeof(RecompThread));
  if (!t) {
    set_guest_errno(ctx, ENOMEM);
    ctx->rax = (uint64_t)ENOMEM;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&g_threads_mutex);
  t->thread_id = ++g_thread_counter;
  pthread_mutex_unlock(&g_threads_mutex);

  uint64_t stack_size = 4 * 1024 * 1024;
  RecompPthreadAttr *attr = get_pthread_attr(ctx, attr_addr);
  if (attr && attr->stacksize_attr >= 16 * 1024) {
    stack_size = attr->stacksize_attr;
  }

  GuestContext *child_ctx = recomp_create_thread_context(ctx, stack_size);
  if (!child_ctx) {
    free(t);
    set_guest_errno(ctx, ENOMEM);
    ctx->rax = (uint64_t)ENOMEM;
    SHIM_RETURN();
  }
  child_ctx->thread_id = t->thread_id;

  t->ctx = child_ctx;
  t->start_routine = start_routine;
  t->arg = arg;
  t->finished = false;
  t->joined = false;
  t->detached = false;

  register_thread(t);

  if (thread_ptr_addr != 0) {
    *(uint64_t *)(ctx->mem_base + thread_ptr_addr) = (uint64_t)t;
  }

  int ret = pthread_create(&t->host_thread, NULL, recomp_host_thread_runner, t);
  if (ret != 0) {
    unregister_thread(t);
    recomp_free_thread_context(child_ctx);
    free(t);
    set_guest_errno(ctx, ret);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
  }

  if (name_addr != 0) {
    const char *name = (const char *)(ctx->mem_base + name_addr);
#if defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__linux__)
    pthread_setname_np(t->host_thread, name);
#endif
  }

  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadJoin(GuestContext *ctx) {
  shim_pthread_join(ctx);
}

void shim_scePthreadDetach(GuestContext *ctx) {
  shim_pthread_detach(ctx);
}

void shim_scePthreadExit(GuestContext *ctx) {
  if (g_current_thread) {
    g_current_thread->finished = true;
    g_current_thread->ret_val = ctx->rdi;
  }
  pthread_exit(NULL);
}

void shim_scePthreadSelf(GuestContext *ctx) {
  shim_pthread_self(ctx);
}

void shim_scePthreadEqual(GuestContext *ctx) {
  shim_pthread_equal(ctx);
}

void shim_scePthreadYield(GuestContext *ctx) {
  sched_yield();
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadGetthreadid(GuestContext *ctx) {
  ctx->rax = g_current_thread ? g_current_thread->thread_id : (ctx->thread_id ? ctx->thread_id : 1000);
  SHIM_RETURN();
}

void shim_scePthreadSetprio(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadGetprio(GuestContext *ctx) {
  uint64_t prio_ptr = ctx->rsi;
  if (prio_ptr) {
    *(int32_t *)(ctx->mem_base + prio_ptr) = 0;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadSetaffinity(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadGetaffinity(GuestContext *ctx) {
  uint64_t mask_ptr = ctx->rsi;
  if (mask_ptr) {
    *(uint64_t *)(ctx->mem_base + mask_ptr) = 0xFFULL;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

// scePthread attribute shims
void shim_scePthreadAttrInit(GuestContext *ctx) {
  shim_pthread_attr_init(ctx);
}

void shim_scePthreadAttrDestroy(GuestContext *ctx) {
  shim_pthread_attr_destroy(ctx);
}

void shim_scePthreadAttrSetstacksize(GuestContext *ctx) {
  shim_pthread_attr_setstacksize(ctx);
}

void shim_scePthreadAttrSetdetachstate(GuestContext *ctx) {
  shim_pthread_attr_setdetachstate(ctx);
}

void shim_scePthreadAttrSetschedpolicy(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint32_t policy = (uint32_t)ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (pattr) {
    pattr->sched_policy = policy;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrSetschedparam(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint64_t param_addr = ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (pattr && param_addr) {
    pattr->prio = *(int32_t *)(ctx->mem_base + param_addr);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrGetschedparam(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint64_t param_addr = ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (param_addr) {
    *(int32_t *)(ctx->mem_base + param_addr) = pattr ? pattr->prio : 700;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrSetinheritsched(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  int32_t inherit = (int32_t)ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (pattr) {
    pattr->sched_inherit = inherit;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrSetaffinity(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint64_t mask = ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (pattr) {
    pattr->cpuset = mask;
    pattr->cpusetsize = sizeof(uint64_t);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrGetaffinity(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint64_t mask_ptr = ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (mask_ptr) {
    uint64_t mask = (pattr && pattr->cpuset) ? pattr->cpuset : 0x7fULL;
    *(uint64_t *)(ctx->mem_base + mask_ptr) = mask;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrGetdetachstate(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint64_t state_ptr = ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (state_ptr) {
    int32_t state = (pattr && (pattr->flags & 1)) ? 1 : 0;
    *(int32_t *)(ctx->mem_base + state_ptr) = state;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrGet(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (pattr) {
    pattr->sched_policy = 1;
    pattr->sched_inherit = 0;
    pattr->prio = 700;
    pattr->stacksize_attr = 2 * 1024 * 1024;
    pattr->cpuset = 0x7f;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadGetname(GuestContext *ctx) {
  uint64_t thread_handle = ctx->rdi;
  uint64_t buf_ptr = ctx->rsi;
  if (buf_ptr) {
    RecompThread *t = find_thread(thread_handle);
    if (t && t->host_thread) {
      char name[64] = {0};
      pthread_getname_np(t->host_thread, name, sizeof(name));
      if (name[0] != '\0') {
        strncpy((char *)(ctx->mem_base + buf_ptr), name, 32);
      } else {
        snprintf((char *)(ctx->mem_base + buf_ptr), 32, "thread_%llu", (unsigned long long)t->thread_id);
      }
    } else {
      snprintf((char *)(ctx->mem_base + buf_ptr), 32, "ps4_thread");
    }
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_cancel(GuestContext *ctx) {
  uint64_t thread_handle = ctx->rdi;
  RecompThread *t = find_thread(thread_handle);
  if (t && t->host_thread) {
    pthread_cancel(t->host_thread);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadSetcancelstate(GuestContext *ctx) {
  int state = (int)ctx->rdi;
  uint64_t old_addr = ctx->rsi;
  if (state != 0 && state != 1) { // 0: PTHREAD_CANCEL_ENABLE, 1: PTHREAD_CANCEL_DISABLE
    ctx->rax = 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
    SHIM_RETURN();
  }
  RecompThread *t = g_current_thread;
  int old = t ? t->cancel_state : 0;
  if (old_addr && ctx->mem_base) {
    *(int *)(ctx->mem_base + old_addr) = old;
  }
  if (t) {
    t->cancel_state = state;
  }
  pthread_setcancelstate(state == 0 ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE, NULL);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrGetstacksize(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint64_t stacksize_ptr = ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (!pattr || !stacksize_ptr || !ctx->mem_base) {
    ctx->rax = 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
    SHIM_RETURN();
  }
  *(uint64_t *)(ctx->mem_base + stacksize_ptr) = pattr->stacksize_attr ? pattr->stacksize_attr : 2097152ULL;
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrGetstack(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint64_t stackaddr_ptr = ctx->rsi;
  uint64_t stacksize_ptr = ctx->rdx;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (!pattr || !stackaddr_ptr || !stacksize_ptr || !ctx->mem_base) {
    ctx->rax = 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
    SHIM_RETURN();
  }
  *(uint64_t *)(ctx->mem_base + stackaddr_ptr) = pattr->stackaddr_attr;
  *(uint64_t *)(ctx->mem_base + stacksize_ptr) = pattr->stacksize_attr ? pattr->stacksize_attr : 2097152ULL;
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrGetstackaddr(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint64_t stackaddr_ptr = ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (!pattr || !stackaddr_ptr || !ctx->mem_base) {
    ctx->rax = 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
    SHIM_RETURN();
  }
  *(uint64_t *)(ctx->mem_base + stackaddr_ptr) = pattr->stackaddr_attr;
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadAttrSetstack(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  uint64_t stackaddr = ctx->rsi;
  uint64_t stacksize = ctx->rdx;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (pattr) {
    pattr->stackaddr_attr = stackaddr;
    pattr->stacksize_attr = stacksize;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadRename(GuestContext *ctx) {
  uint64_t handle = ctx->rdi;
  uint64_t name_addr = ctx->rsi;
  const char *name = (name_addr && ctx->mem_base) ? (const char *)(ctx->mem_base + name_addr) : NULL;
  if (name) {
    RecompThread *t = find_thread(handle);
    if (t) {
#if defined(__APPLE__)
      if (t == g_current_thread || (t->ctx && t->ctx == g_current_ctx)) {
        pthread_setname_np(name);
      }
#elif defined(__linux__)
      pthread_setname_np(t->host_thread, name);
#endif
    } else if (handle == 0 || (ctx && handle == ctx->thread_id)) {
#if defined(__APPLE__)
      pthread_setname_np(name);
#endif
    }
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadGetschedparam(GuestContext *ctx) {
  uint64_t pol_addr = ctx->rsi;
  uint64_t param_addr = ctx->rdx;
  if (pol_addr && ctx->mem_base) {
    *(int *)(ctx->mem_base + pol_addr) = 1; // SCHED_FIFO
  }
  if (param_addr && ctx->mem_base) {
    *(int32_t *)(ctx->mem_base + param_addr) = 700; // default prio
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadSetschedparam(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

// scePthread TLS shims
void shim_scePthreadKeyCreate(GuestContext *ctx) {
  shim_pthread_key_create(ctx);
}

void shim_scePthreadKeyDelete(GuestContext *ctx) {
  uint32_t key = (uint32_t)ctx->rdi;
  if (key < 128) {
    ctx->tls_keys[key] = 0;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    proc->tls_destructors[key] = 0;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadSetspecific(GuestContext *ctx) {
  shim_pthread_setspecific(ctx);
}

void shim_scePthreadGetspecific(GuestContext *ctx) {
  shim_pthread_getspecific(ctx);
}

void shim___tls_get_addr(GuestContext *ctx) {
  uint64_t ti_addr = ctx->rdi;
  if (ti_addr) {
    uint64_t offset = *(uint64_t *)(ctx->mem_base + ti_addr + 8);
    ctx->rax = ctx->fs_base + offset;
  } else {
    ctx->rax = ctx->fs_base;
  }
  SHIM_RETURN();
}
