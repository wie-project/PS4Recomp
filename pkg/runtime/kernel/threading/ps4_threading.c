#include "recomp_runtime.h"
#include "ps4_threading.h"
#include <errno.h>
#include <sched.h>
#include <signal.h>

// scePthread* report failures as SCE kernel errors (0x80020000 | errno),
// while the POSIX shims they forward to return plain errno values.
#define SCE_PTHREAD_ERR(ctx)                                                   \
  do {                                                                         \
    uint32_t e_ = (uint32_t)(ctx)->rax;                                        \
    if (e_ != 0 && e_ < 0x100)                                                 \
      (ctx)->rax = 0x80020000ULL | e_;                                         \
  } while (0)

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

// Handle 0 finds no thread (join, detach, cancel and RaiseException report ESRCH or do
// nothing). AttrGet, Rename and Getname treat 0 as the calling thread themselves.
RecompThread *recomp_find_thread(uint64_t handle) {
  if (handle == 0) return NULL;
  pthread_mutex_lock(&g_threads_mutex);
  RecompThread *curr = g_threads;
  while (curr) {
    if ((uint64_t)curr == handle || curr->thread_id == handle) {
      pthread_mutex_unlock(&g_threads_mutex);
      return curr;
    }
    if (curr->ctx) {
      if (curr->ctx->fs_base == handle) {
        pthread_mutex_unlock(&g_threads_mutex);
        return curr;
      }
      if (curr->ctx->fs_base && curr->ctx->mem_base &&
          curr->ctx->fs_base + 0x18ULL <= curr->ctx->mem_size) {
        uint64_t t = *(uint64_t *)(curr->ctx->mem_base + curr->ctx->fs_base + 0x10ULL);
        if (t != 0 && t == handle) {
          pthread_mutex_unlock(&g_threads_mutex);
          return curr;
        }
      }
    }
    curr = curr->next;
  }
  pthread_mutex_unlock(&g_threads_mutex);
  return NULL;
}

RecompThread *recomp_get_current_thread(void) {
  return g_current_thread;
}

// Fiber host threads run on behalf of whichever guest thread resumed them.
void recomp_set_current_thread(RecompThread *t) {
  g_current_thread = t;
}

static inline RecompThread *find_thread(uint64_t handle) {
  return recomp_find_thread(handle);
}

const char *recomp_get_current_thread_name(void) {
  if (g_current_thread && g_current_thread->name[0]) {
    return g_current_thread->name;
  }
  return "main";
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
  g_main_thread_obj.stack_base = ctx->stack_base;
  g_main_thread_obj.stack_size = ctx->stack_alloc_size;
  strncpy(g_main_thread_obj.name, "main", sizeof(g_main_thread_obj.name) - 1);
  if (ctx->fs_base && ctx->mem_base) {
    *(uint64_t *)(ctx->mem_base + ctx->fs_base + 0x10ULL) = (uint64_t)&g_main_thread_obj;
  }
  g_current_thread = &g_main_thread_obj;
  register_thread(&g_main_thread_obj);
}

// Orders `finished` against `detached` so exactly one side releases a
// detached thread's resources.
static pthread_mutex_t g_detach_mutex = PTHREAD_MUTEX_INITIALIZER;

// Common end of a guest thread, whether its entry returned or it called
// scePthreadExit: TLS destructors, then release the guest stack and context
// of detached threads (joinable ones are released by join).
static void recomp_thread_epilogue(RecompThread *t, GuestContext *ctx) {
  fprintf(stderr, "[ps4-thread] Thread exited: id=%llu, name='%s', ret=0x%llx\n",
          (unsigned long long)ctx->thread_id, t->name[0] ? t->name : "unnamed", (unsigned long long)t->ret_val);

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

  pthread_mutex_lock(&g_detach_mutex);
  t->finished = true;
  bool release = t->detached;
  pthread_mutex_unlock(&g_detach_mutex);

  if (release) {
    unregister_thread(t);
    recomp_free_thread_context(ctx);
    free(t);
  }
}

#if defined(__APPLE__)
#include <pthread/qos.h>
// PS4_GUEST_WORKER_QOS=<default|utility|initiated|interactive>: QoS class of the game's job
// workers (guest threads named "NdJobWorkerThread*"), set when the thread starts. Unset means
// "initiated": above other apps' default-class work, which otherwise takes the cores from the
// workers while a frame waits for a job, and below the CP, encoder and audio threads
// (USER_INTERACTIVE). "default" leaves the class the thread got at creation (QOS_CLASS_DEFAULT).
// Idle workers spin instead of sleeping, so "interactive" puts six spinning threads next to the
// CP and encoder threads; it exists for experiments only.
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
  else {
    fprintf(stderr, "[ps4-thread] PS4_GUEST_WORKER_QOS='%s' unknown (default|utility|initiated|interactive); unchanged\n", e);
    return;
  }
  qos_class_t before = QOS_CLASS_UNSPECIFIED;
  int rel = 0;
  pthread_get_qos_class_np(pthread_self(), &before, &rel);
  int err = pthread_set_qos_class_self_np(cls, 0);
  if (err != 0) {
    fprintf(stderr, "[ps4-thread] '%s': setting QoS %s failed: error %d\n", name, e, err);
    return;
  }
  fprintf(stderr, "[ps4-thread] '%s': QoS 0x%x -> %s (0x%x)\n", name, (unsigned)before, e, (unsigned)cls);
}
#endif

static void *recomp_host_thread_runner(void *arg) {
  RecompThread *t = (RecompThread *)arg;
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

  fprintf(stderr, "[ps4-thread] Thread started: id=%llu, name='%s', entry=0x%llx, arg=0x%llx\n",
          (unsigned long long)ctx->thread_id, t->name[0] ? t->name : "unnamed", (unsigned long long)t->start_routine, (unsigned long long)t->arg);

  // Push dummy return address on guest stack
  ctx->rsp -= 8;
  MEM_U64(ctx->rsp) = 0xdeadbeefULL;

  // First parameter in AMD64 calling convention is RDI
  ctx->rdi = t->arg;
  ctx->rip = t->start_routine;

  recomp_dispatch(ctx, t->start_routine);

  t->ret_val = ctx->rax;
  recomp_thread_epilogue(t, ctx);
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
  t->stack_base = child_ctx->stack_base;
  t->stack_size = stack_size;
  t->start_routine = start_routine;
  t->arg = arg;
  t->finished = false;
  t->joined = false;
  t->detached = false;

  if (child_ctx->fs_base && ctx->mem_base) {
    *(uint64_t *)(ctx->mem_base + child_ctx->fs_base + 0x10ULL) = (uint64_t)t;
  }

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
    pthread_detach(t->host_thread);
    pthread_mutex_lock(&g_detach_mutex);
    bool release = t->finished && !t->detached && t->ctx && t != &g_main_thread_obj;
    t->detached = true;
    pthread_mutex_unlock(&g_detach_mutex);
    if (release) {
      // Already exited: nobody else will free it.
      unregister_thread(t);
      recomp_free_thread_context(t->ctx);
      free(t);
    }
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
  t->stack_base = child_ctx->stack_base;
  t->stack_size = stack_size;
  t->start_routine = start_routine;
  t->arg = arg;
  t->finished = false;
  t->joined = false;
  t->detached = false;
  if (name_addr != 0) {
    const char *name = (const char *)(ctx->mem_base + name_addr);
    if (name) {
      strncpy(t->name, name, sizeof(t->name) - 1);
      t->name[sizeof(t->name) - 1] = '\0';
    }
  }

  if (child_ctx->fs_base && ctx->mem_base) {
    *(uint64_t *)(ctx->mem_base + child_ctx->fs_base + 0x10ULL) = (uint64_t)t;
  }

  register_thread(t);

  fprintf(stderr, "[ps4-thread] Thread created: id=%d, name='%s', entry=0x%llx, arg=0x%llx\n",
          (int)t->thread_id, t->name[0] ? t->name : "unnamed", (unsigned long long)start_routine, (unsigned long long)arg);

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

void shim_scePthreadJoin(GuestContext *ctx) {
  shim_pthread_join(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadDetach(GuestContext *ctx) {
  shim_pthread_detach(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadExit(GuestContext *ctx) {
  RecompThread *t = g_current_thread;
  if (t && t->ctx == ctx && t != &g_main_thread_obj) {
    t->ret_val = ctx->rdi;
    recomp_thread_epilogue(t, ctx);
  } else if (t) {
    t->finished = true;
    t->ret_val = ctx->rdi;
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
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadAttrDestroy(GuestContext *ctx) {
  shim_pthread_attr_destroy(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadAttrSetstacksize(GuestContext *ctx) {
  shim_pthread_attr_setstacksize(ctx);
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadAttrSetdetachstate(GuestContext *ctx) {
  shim_pthread_attr_setdetachstate(ctx);
  SCE_PTHREAD_ERR(ctx);
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
  uint64_t thread_handle = ctx->rdi;
  uint64_t attr_addr = ctx->rsi;
  RecompPthreadAttr *pattr = get_pthread_attr(ctx, attr_addr);
  if (!pattr) {
    if (attr_addr && ctx->mem_base) {
      pattr = (RecompPthreadAttr *)calloc(1, sizeof(RecompPthreadAttr));
      *(uint64_t *)(ctx->mem_base + attr_addr) = (uint64_t)pattr;
    }
  }
  if (!pattr) {
    ctx->rax = 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
    SHIM_RETURN();
  }

  RecompThread *t = thread_handle ? find_thread(thread_handle) : g_current_thread;
  if (!t) {
    t = g_current_thread;
  }

  pattr->sched_policy = 1;
  pattr->sched_inherit = 0;
  pattr->prio = 700;
  pattr->cpuset = 0x7f;

  if (t) {
    pattr->stackaddr_attr = t->stack_base ? t->stack_base : (t->ctx ? t->ctx->stack_base : 0);
    pattr->stacksize_attr = t->stack_size ? t->stack_size : (t->ctx ? t->ctx->stack_alloc_size : 2 * 1024 * 1024);
    if (t->detached) {
      pattr->flags |= 1;
    }
  } else {
    pattr->stackaddr_attr = 0;
    pattr->stacksize_attr = 2 * 1024 * 1024;
  }

  // Logged on the first call only.
  static int s_logged;
  if (!__atomic_exchange_n(&s_logged, 1, __ATOMIC_RELAXED))
    fprintf(stderr, "[ps4-thread] scePthreadAttrGet: thread=0x%llx -> stackaddr=0x%llx, stacksize=%llu (first call)\n",
            (unsigned long long)thread_handle, (unsigned long long)pattr->stackaddr_attr,
            (unsigned long long)pattr->stacksize_attr);

  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceKernelIsStack(GuestContext *ctx) {
  uint64_t addr = ctx->rdi;
  uint64_t start_ptr = ctx->rsi;
  uint64_t end_ptr = ctx->rdx;

  uint64_t stack_start = 0;
  uint64_t stack_end = 0;

  pthread_mutex_lock(&g_threads_mutex);
  RecompThread *curr = g_threads;
  while (curr) {
    uint64_t sbase = curr->stack_base;
    uint64_t ssize = curr->stack_size;
    if (sbase && ssize && addr >= sbase && addr < (sbase + ssize)) {
      stack_start = sbase;
      stack_end = sbase + ssize;
      break;
    }
    curr = curr->next;
  }
  pthread_mutex_unlock(&g_threads_mutex);

  if (start_ptr && ctx->mem_base) {
    *(uint64_t *)(ctx->mem_base + start_ptr) = stack_start;
  }
  if (end_ptr && ctx->mem_base) {
    *(uint64_t *)(ctx->mem_base + end_ptr) = stack_end;
  }

  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadGetname(GuestContext *ctx) {
  uint64_t thread_handle = ctx->rdi;
  uint64_t buf_ptr = ctx->rsi;
  if (buf_ptr) {
    RecompThread *t = thread_handle ? find_thread(thread_handle) : g_current_thread;
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
    RecompThread *t = handle ? find_thread(handle) : g_current_thread;
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
  SCE_PTHREAD_ERR(ctx);
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
  SCE_PTHREAD_ERR(ctx);
}

void shim_scePthreadGetspecific(GuestContext *ctx) {
  shim_pthread_getspecific(ctx);
}

void shim___tls_get_addr(GuestContext *ctx) {
  uint64_t ti_addr = ctx->rdi;
  if (ti_addr) {
    // tls_index {module, offset}. Only the eboot's static TLS block exists; it ends at fs base
    // (variant II, recomp_tls_block_offset). Without a PT_TLS segment the offset stays fs-relative.
    uint64_t offset = *(uint64_t *)(ctx->mem_base + ti_addr + 8);
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    ctx->rax = ctx->fs_base - recomp_tls_block_offset(proc) + offset;
  } else {
    ctx->rax = ctx->fs_base;
  }
  SHIM_RETURN();
}

// ---- Frame stall watchdog (diagnostics) -----------------------------------------------
// PS4_STALL_WATCHDOG=<ms>: when no frame has been presented for <ms> milliseconds, print what
// every guest thread is executing (guest block address and host function), so long frames can
// be attributed to a thread and a guest function.
// PS4_STALL_PROFILE=<ms>: from <ms> into a stall until the next frame, sample every guest
// thread every millisecond and print the most frequent functions per thread at its end.
#include <dlfcn.h>
#include <mach/mach.h>
#include <time.h>
#include <mach/mach_vm.h>
#include <pthread/qos.h>
#include "ps4_stall_profile.h"

static StallProfile *g_stall_prof;
static uint64_t g_prof_for; // present time of the stall being profiled (0: none)
// Profile slot of every guest thread sampled in the current stall, by thread id, so threads
// created or exiting during the stall do not shift the slots of the others. Watchdog only.
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

// dladdr scans the ~64k local symbols of the executable linearly (~60 us per call); a report
// symbolizes up to 8 pcs of every stored sample, so results are cached per pc in a fixed table.
// Only the watchdog thread uses it.
#define STALL_SYM_CACHE 16384
static struct {
  uint64_t pc, start;
  const char *name; // NULL: dladdr found no symbol
} g_stall_sym_cache[STALL_SYM_CACHE];

static int stall_symbolize(uint64_t pc, uint64_t *start, const char **name) {
  const uint64_t h = (pc * 0x9E3779B97F4A7C15ull) >> 50; // 14 bits
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

// One sample of every guest thread: PC, LR and the saved return addresses along the
// frame-pointer chain. Threads are suspended only while their registers and stack are read;
// nothing that can take a lock (malloc, dladdr) runs while one is suspended.
// g_threads_mutex is held for the whole walk, so no entry is unregistered and freed meanwhile,
// and a detached thread that is exiting waits in unregister_thread before pthread_exit, which
// keeps its pthread_t valid. A suspended thread cannot hold the mutex (the watchdog does). Locks
// taken under it: libpthread's thread-list lock inside pthread_mach_thread_np (only while no
// thread is suspended; libpthread never waits for other locks while holding it). A joinable
// thread can still be joined (its pthread_t freed) during the walk; pthread_mach_thread_np looks
// the pthread_t up in libpthread's thread list and returns MACH_PORT_NULL for one that is gone.
// Recompiled guest code builds no frame records (see the contract at recomp_enter_guest):
// there x29 still names the frame of recomp_enter_guest and LR the return address of the
// function's own last call, so a sample whose PC is in guest code keeps only the PC.
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
        if (frame[0] <= fp) break; // the chain grows toward the stack base
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
  if (in_guest_code && !g_prof_no_chain_noted) { // no thread is suspended here
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
    // The GuestContext of what the thread runs (a fiber's while it runs one): its rip is the
    // guest address of the last shim it called (guest code itself never writes rip).
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
  // Sampling must keep its 1 ms period while the game keeps every core busy.
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  const uint64_t dump_ns = (uint64_t)g_stall_dump_ms * 1000000ull;
  const uint64_t prof_ns = (uint64_t)g_stall_prof_ms * 1000000ull;
  uint64_t reported_for = 0;
  uint64_t prev_wake = 0, max_gap = 0; // max_gap: longest time between wake-ups of this stall
  // Wake-ups follow fixed deadlines, so the time a sampling round takes does not stretch the period.
  const uint64_t period_ns = prof_ns ? 1000000ull : 5000000ull;
  uint64_t deadline = stall_now_ns();
  for (;;) {
    deadline += period_ns;
    const uint64_t before = stall_now_ns();
    if (deadline > before)
      usleep((useconds_t)((deadline - before) / 1000));
    else
      deadline = before; // a round overran the period: do not try to catch up
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
    if (g_prof_for && g_prof_for != last) { // a frame ended the stall being profiled
      stall_profile_cpu_end(g_stall_prof);
      stall_profile_report_gap(g_stall_prof, stderr, (double)(last - g_prof_for) / 1e6, 6, (double)max_gap / 1e6);
      g_prof_for = 0;
      prev_wake = deadline = stall_now_ns(); // the report itself is not a gap of the next stall
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

void ps4_stall_watchdog_note_present(void) {
  static int started = 0;
  g_last_present_ns = stall_now_ns();
  if (!started) {
    started = 1;
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
}
