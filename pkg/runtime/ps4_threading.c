#include "recomp_runtime.h"
#include <errno.h>
#include <sched.h>

// Multi-threading runtime support
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
    free(ctx);
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

  // 4MB guest stack
  GuestContext *child_ctx = recomp_create_thread_context(ctx, 4 * 1024 * 1024);
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
    free(child_ctx);
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
    free(t->ctx);
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
  if (g_current_thread) {
    ctx->rax = (uint64_t)g_current_thread;
  } else {
    ctx->rax = (uint64_t)&g_main_thread_obj;
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
