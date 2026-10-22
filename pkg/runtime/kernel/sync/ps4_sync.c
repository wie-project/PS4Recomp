#include "recomp_runtime.h"
#include "ps4_sync.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>

// ============================================================================
// Guest Synchronization Memory Pool
// ============================================================================
// In FreeBSD/PS4, pthread_mutex_t, pthread_cond_t, and pthread_rwlock_t are
// pointers to structures (e.g. struct pthread_mutex *). Functions like
// scePthreadMutexInit receive a pointer to pointer (PthreadMutexT* mutex) and
// allocate memory for the structure in guest address space, writing the
// allocated pointer back to *mutex.
// For example, Unity accesses [mutex->m_flags] at offset +0x20 right after init.
// We provide a dedicated 4 MB pool of guest memory for these structures.

static uint64_t g_sync_pool_base = 0;
static uint64_t g_sync_pool_offset = 0;
static const uint64_t g_sync_pool_size = 4 * 1024 * 1024; // 4 MB
static pthread_mutex_t g_sync_pool_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct SyncFreeSlot {
  struct SyncFreeSlot *next;
} SyncFreeSlot;

static SyncFreeSlot *g_sync_free_slots = NULL;

static uint64_t alloc_guest_sync_mem(GuestContext *ctx, size_t size) {
  if (size < 128) {
    size = 128;
  }
  size = (size + 15) & ~15ULL;

  pthread_mutex_lock(&g_sync_pool_lock);
  if (!g_sync_pool_base) {
    g_sync_pool_base = recomp_vm_alloc_named(ctx, g_sync_pool_size, PROT_READ | PROT_WRITE, 0, "sync_pool");
    g_sync_pool_offset = 0;
  }

  if (size == 128 && g_sync_free_slots) {
    SyncFreeSlot *slot = g_sync_free_slots;
    g_sync_free_slots = slot->next;
    uint64_t guest_addr = (uint64_t)((uintptr_t)slot - (uintptr_t)ctx->mem_base);
    memset((void *)slot, 0, 128);
    pthread_mutex_unlock(&g_sync_pool_lock);
    return guest_addr;
  }

  if (g_sync_pool_offset + size <= g_sync_pool_size) {
    uint64_t guest_addr = g_sync_pool_base + g_sync_pool_offset;
    g_sync_pool_offset += size;
    memset((void *)(ctx->mem_base + guest_addr), 0, size);
    pthread_mutex_unlock(&g_sync_pool_lock);
    return guest_addr;
  }
  pthread_mutex_unlock(&g_sync_pool_lock);

  // Fallback: allocate new page extent
  uint64_t fallback_addr = recomp_vm_alloc_named(ctx, size < 4096 ? 4096 : size, PROT_READ | PROT_WRITE, 0, "sync_obj");
  if (fallback_addr) {
    memset((void *)(ctx->mem_base + fallback_addr), 0, size);
  }
  return fallback_addr;
}

static void free_guest_sync_mem(GuestContext *ctx, uint64_t guest_addr, size_t size) {
  if (!guest_addr) {
    return;
  }
  if (size < 128) {
    size = 128;
  }
  pthread_mutex_lock(&g_sync_pool_lock);
  if (size == 128 && guest_addr >= g_sync_pool_base && guest_addr < g_sync_pool_base + g_sync_pool_size) {
    SyncFreeSlot *slot = (SyncFreeSlot *)(ctx->mem_base + guest_addr);
    slot->next = g_sync_free_slots;
    g_sync_free_slots = slot;
  }
  pthread_mutex_unlock(&g_sync_pool_lock);
}

// ============================================================================
// Mutex Table & Resolution
// ============================================================================

#define MUTEX_MAP_SIZE 1024

typedef struct MutexNode {
  uint64_t guest_addr;
  pthread_mutex_t host_mutex;
  struct MutexNode *next;
} MutexNode;

static MutexNode *g_mutex_map[MUTEX_MAP_SIZE] = {0};
static pthread_mutex_t g_mutex_map_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_mutex_static_lock = PTHREAD_MUTEX_INITIALIZER;

static inline uint64_t get_current_thread_handle(GuestContext *ctx) {
  if (ctx && ctx->fs_base) {
    uint64_t t = MEM_U64(ctx->fs_base + 0x10);
    return (t != 0) ? t : ctx->fs_base;
  }
  if (ctx && ctx->thread_id) {
    return ctx->thread_id;
  }
  return 1000;
}

static pthread_mutex_t *get_host_mutex(uint64_t guest_addr) {
  uint64_t bucket = (guest_addr >> 3) % MUTEX_MAP_SIZE;
  pthread_mutex_lock(&g_mutex_map_lock);
  MutexNode *curr = g_mutex_map[bucket];
  while (curr) {
    if (curr->guest_addr == guest_addr) {
      pthread_mutex_unlock(&g_mutex_map_lock);
      return &curr->host_mutex;
    }
    curr = curr->next;
  }
  MutexNode *node = (MutexNode *)calloc(1, sizeof(MutexNode));
  node->guest_addr = guest_addr;
  pthread_mutex_init(&node->host_mutex, NULL);
  node->next = g_mutex_map[bucket];
  g_mutex_map[bucket] = node;
  pthread_mutex_unlock(&g_mutex_map_lock);
  return &node->host_mutex;
}

static uint64_t resolve_mutex_obj(GuestContext *ctx, uint64_t mtx_addr, int guest_type, int auto_create) {
  if (!mtx_addr) {
    return 0;
  }
  // If mtx_addr is already inside our sync pool, caller passed struct directly
  if (mtx_addr >= g_sync_pool_base && mtx_addr < g_sync_pool_base + g_sync_pool_size) {
    return mtx_addr;
  }
  // Otherwise dereference pointer
  uint64_t *p_mtx = (uint64_t *)(ctx->mem_base + mtx_addr);
  uint64_t obj = __atomic_load_n(p_mtx, __ATOMIC_ACQUIRE);
  if (obj <= 2) {
    if (obj == 2) { // THR_MUTEX_DESTROYED
      return 0;
    }
    if (!auto_create) {
      return 0;
    }
    pthread_mutex_lock(&g_mutex_static_lock);
    obj = *p_mtx;
    if (obj <= 1) { // 0 = THR_MUTEX_INITIALIZER, 1 = THR_ADAPTIVE_MUTEX_INITIALIZER
      uint64_t new_obj = alloc_guest_sync_mem(ctx, 128);
      uint32_t *m_flags = (uint32_t *)(ctx->mem_base + new_obj + 0x20);
      uint32_t f_type = (obj == 1) ? 4 : (guest_type ? (uint32_t)guest_type : 1);
      *m_flags = f_type;
      (void)get_host_mutex(new_obj);
      __atomic_store_n(p_mtx, new_obj, __ATOMIC_RELEASE);
      obj = new_obj;
    }
    pthread_mutex_unlock(&g_mutex_static_lock);
    return (obj == 2) ? 0 : obj;
  }
  return obj;
}

void shim_pthread_mutex_init(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t attr_addr = ctx->rsi;
  if (!mtx_addr) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  int guest_type = 1; // Default is ErrorCheck
  if (attr_addr) {
    uint64_t p_attr = *(uint64_t *)(ctx->mem_base + attr_addr);
    if (p_attr >= g_sync_pool_base && p_attr < g_sync_pool_base + g_sync_pool_size) {
      guest_type = *(int *)(ctx->mem_base + p_attr);
    } else {
      guest_type = *(int *)(ctx->mem_base + attr_addr);
    }
    if (guest_type < 1 || guest_type > 4) {
      guest_type = 1;
    }
  }

  uint64_t obj = alloc_guest_sync_mem(ctx, 128);
  *(uint64_t *)(ctx->mem_base + mtx_addr) = obj;

  uint32_t *m_flags = (uint32_t *)(ctx->mem_base + obj + 0x20);
  *m_flags = (uint32_t)guest_type;

  (void)get_host_mutex(obj);

  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutex_lock(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t obj = resolve_mutex_obj(ctx, mtx_addr, 0, 1);
  if (!obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint32_t flags = *(uint32_t *)(ctx->mem_base + obj + 0x20);
  int type = (int)(flags & 0xff);
  uint64_t curthread = get_current_thread_handle(ctx);
  uint64_t *p_owner = (uint64_t *)(ctx->mem_base + obj);
  int32_t *p_count = (int32_t *)(ctx->mem_base + obj + 0x8);

  if (__atomic_load_n(p_owner, __ATOMIC_ACQUIRE) == curthread) {
    if (type == 2) { // Recursive
      (*p_count)++;
      ctx->rax = 0;
      SHIM_RETURN();
    }
    if (type == 1 || type == 4) { // ErrorCheck or AdaptiveNp
      ctx->rax = (uint64_t)EDEADLK;
      SHIM_RETURN();
    }
    // Normal (3): POSIX specifies deadlock on self-lock. Fall through to host lock.
  }

  pthread_mutex_t *m = get_host_mutex(obj);
  int ret = pthread_mutex_lock(m);
  if (ret == 0) {
    __atomic_store_n(p_owner, curthread, __ATOMIC_RELEASE);
    *p_count = 0;
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

void shim_pthread_mutex_trylock(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t obj = resolve_mutex_obj(ctx, mtx_addr, 0, 1);
  if (!obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint32_t flags = *(uint32_t *)(ctx->mem_base + obj + 0x20);
  int type = (int)(flags & 0xff);
  uint64_t curthread = get_current_thread_handle(ctx);
  uint64_t *p_owner = (uint64_t *)(ctx->mem_base + obj);
  int32_t *p_count = (int32_t *)(ctx->mem_base + obj + 0x8);

  if (__atomic_load_n(p_owner, __ATOMIC_ACQUIRE) == curthread) {
    if (type == 2) { // Recursive
      (*p_count)++;
      ctx->rax = 0;
      SHIM_RETURN();
    }
    ctx->rax = (uint64_t)EBUSY;
    SHIM_RETURN();
  }

  pthread_mutex_t *m = get_host_mutex(obj);
  int ret = pthread_mutex_trylock(m);
  if (ret == 0) {
    __atomic_store_n(p_owner, curthread, __ATOMIC_RELEASE);
    *p_count = 0;
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

static int host_mutex_timedlock(pthread_mutex_t *m, uint64_t usec) {
  if (pthread_mutex_trylock(m) == 0) {
    return 0;
  }
  if (usec == 0) {
    return EBUSY;
  }
  struct timespec start, now;
  clock_gettime(CLOCK_MONOTONIC, &start);
  while (1) {
    sched_yield();
    if (pthread_mutex_trylock(m) == 0) {
      return 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t elapsed = (uint64_t)(now.tv_sec - start.tv_sec) * 1000000ULL +
                       (uint64_t)(now.tv_nsec - start.tv_nsec) / 1000ULL;
    if (elapsed >= usec) {
      return ETIMEDOUT;
    }
    if (usec - elapsed > 1000) {
      usleep(50);
    }
  }
}

void shim_pthread_mutex_timedlock(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t abstime_addr = ctx->rsi;
  uint64_t obj = resolve_mutex_obj(ctx, mtx_addr, 0, 1);
  if (!obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint32_t flags = *(uint32_t *)(ctx->mem_base + obj + 0x20);
  int type = (int)(flags & 0xff);
  uint64_t curthread = get_current_thread_handle(ctx);
  uint64_t *p_owner = (uint64_t *)(ctx->mem_base + obj);
  int32_t *p_count = (int32_t *)(ctx->mem_base + obj + 0x8);

  if (__atomic_load_n(p_owner, __ATOMIC_ACQUIRE) == curthread) {
    if (type == 2) { // Recursive
      (*p_count)++;
      ctx->rax = 0;
      SHIM_RETURN();
    }
    if (abstime_addr) {
      struct timespec now;
      clock_gettime(CLOCK_REALTIME, &now);
      struct timespec *ts = (struct timespec *)(ctx->mem_base + abstime_addr);
      if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
        ctx->rax = (uint64_t)EINVAL;
        SHIM_RETURN();
      }
      int64_t diff_sec = (int64_t)ts->tv_sec - (int64_t)now.tv_sec;
      int64_t diff_nsec = (int64_t)ts->tv_nsec - (int64_t)now.tv_nsec;
      int64_t total_usec = diff_sec * 1000000LL + diff_nsec / 1000LL;
      if (total_usec > 0) {
        usleep((useconds_t)total_usec);
      }
      ctx->rax = (uint64_t)ETIMEDOUT;
      SHIM_RETURN();
    }
    ctx->rax = (uint64_t)EDEADLK;
    SHIM_RETURN();
  }

  pthread_mutex_t *m = get_host_mutex(obj);
  if (!abstime_addr) {
    int ret = pthread_mutex_lock(m);
    if (ret == 0) {
      __atomic_store_n(p_owner, curthread, __ATOMIC_RELEASE);
      *p_count = 0;
    }
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
  }
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  struct timespec *ts = (struct timespec *)(ctx->mem_base + abstime_addr);
  if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  int64_t diff_sec = (int64_t)ts->tv_sec - (int64_t)now.tv_sec;
  int64_t diff_nsec = (int64_t)ts->tv_nsec - (int64_t)now.tv_nsec;
  int64_t total_usec = diff_sec * 1000000LL + diff_nsec / 1000LL;
  if (total_usec <= 0) {
    if (pthread_mutex_trylock(m) == 0) {
      __atomic_store_n(p_owner, curthread, __ATOMIC_RELEASE);
      *p_count = 0;
      ctx->rax = 0;
    } else {
      ctx->rax = (uint64_t)ETIMEDOUT;
    }
    SHIM_RETURN();
  }
  int ret = host_mutex_timedlock(m, (uint64_t)total_usec);
  if (ret == 0) {
    __atomic_store_n(p_owner, curthread, __ATOMIC_RELEASE);
    *p_count = 0;
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

void shim_pthread_mutex_unlock(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t obj = resolve_mutex_obj(ctx, mtx_addr, 0, 0);
  if (!obj) {
    ctx->rax = (uint64_t)EPERM;
    SHIM_RETURN();
  }
  uint32_t flags = *(uint32_t *)(ctx->mem_base + obj + 0x20);
  int type = (int)(flags & 0xff);
  uint64_t curthread = get_current_thread_handle(ctx);
  uint64_t *p_owner = (uint64_t *)(ctx->mem_base + obj);
  int32_t *p_count = (int32_t *)(ctx->mem_base + obj + 0x8);

  if (__atomic_load_n(p_owner, __ATOMIC_ACQUIRE) != curthread) {
    ctx->rax = (uint64_t)EPERM;
    SHIM_RETURN();
  }
  if (type == 2 && *p_count > 0) {
    (*p_count)--;
    ctx->rax = 0;
    SHIM_RETURN();
  }
  __atomic_store_n(p_owner, 0, __ATOMIC_RELEASE);
  *p_count = 0;
  pthread_mutex_t *m = get_host_mutex(obj);
  ctx->rax = (uint64_t)pthread_mutex_unlock(m);
  SHIM_RETURN();
}

void shim_pthread_mutex_destroy(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t obj = resolve_mutex_obj(ctx, mtx_addr, 0, 0);
  if (!obj) {
    ctx->rax = 0;
    SHIM_RETURN();
  }
  uint64_t *p_owner = (uint64_t *)(ctx->mem_base + obj);
  if (__atomic_load_n(p_owner, __ATOMIC_ACQUIRE) != 0) {
    ctx->rax = (uint64_t)EBUSY;
    SHIM_RETURN();
  }
  uint64_t bucket = (obj >> 3) % MUTEX_MAP_SIZE;
  pthread_mutex_lock(&g_mutex_map_lock);
  MutexNode **curr = &g_mutex_map[bucket];
  while (*curr) {
    if ((*curr)->guest_addr == obj) {
      MutexNode *to_free = *curr;
      *curr = (*curr)->next;
      pthread_mutex_destroy(&to_free->host_mutex);
      free(to_free);
      break;
    }
    curr = &(*curr)->next;
  }
  pthread_mutex_unlock(&g_mutex_map_lock);

  if (mtx_addr != obj) {
    *(uint64_t *)(ctx->mem_base + mtx_addr) = 2; // THR_MUTEX_DESTROYED
  }
  free_guest_sync_mem(ctx, obj, 128);

  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutex_isowned_np(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t obj = resolve_mutex_obj(ctx, mtx_addr, 0, 0);
  if (!obj) {
    ctx->rax = 0;
    SHIM_RETURN();
  }
  uint64_t curthread = get_current_thread_handle(ctx);
  uint64_t owner = __atomic_load_n((uint64_t *)(ctx->mem_base + obj), __ATOMIC_ACQUIRE);
  ctx->rax = (owner == curthread) ? 1 : 0;
  SHIM_RETURN();
}

void shim_scePthreadMutexIsownedNp(GuestContext *ctx) {
  shim_pthread_mutex_isowned_np(ctx);
}

void shim_pthread_mutexattr_init(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  if (!attr_addr) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint64_t attr_obj = alloc_guest_sync_mem(ctx, 128);
  *(uint64_t *)(ctx->mem_base + attr_addr) = attr_obj;
  *(uint32_t *)(ctx->mem_base + attr_obj) = 1; // PthreadMutexType::ErrorCheck = 1
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutexattr_settype(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  int type = (int)ctx->rsi;
  if (attr_addr) {
    if (attr_addr >= g_sync_pool_base && attr_addr < g_sync_pool_base + g_sync_pool_size) {
      *(uint32_t *)(ctx->mem_base + attr_addr) = (uint32_t)type;
    } else {
      uint64_t attr_obj = *(uint64_t *)(ctx->mem_base + attr_addr);
      if (attr_obj >= g_sync_pool_base && attr_obj < g_sync_pool_base + g_sync_pool_size) {
        *(uint32_t *)(ctx->mem_base + attr_obj) = (uint32_t)type;
      }
      *(uint32_t *)(ctx->mem_base + attr_addr) = (uint32_t)type;
    }
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutexattr_destroy(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  if (attr_addr) {
    if (attr_addr >= g_sync_pool_base && attr_addr < g_sync_pool_base + g_sync_pool_size) {
      free_guest_sync_mem(ctx, attr_addr, 128);
    } else {
      uint64_t attr_obj = *(uint64_t *)(ctx->mem_base + attr_addr);
      if (attr_obj >= g_sync_pool_base && attr_obj < g_sync_pool_base + g_sync_pool_size) {
        free_guest_sync_mem(ctx, attr_obj, 128);
        *(uint64_t *)(ctx->mem_base + attr_addr) = 0;
      }
    }
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

// ============================================================================
// Condition Variables Table & Resolution
// ============================================================================

#define COND_MAP_SIZE 1024

typedef struct CondNode {
  uint64_t guest_addr;
  pthread_cond_t host_cond;
  struct CondNode *next;
} CondNode;

static CondNode *g_cond_map[COND_MAP_SIZE] = {0};
static pthread_mutex_t g_cond_map_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cond_static_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t *get_host_cond(uint64_t guest_addr) {
  uint64_t bucket = (guest_addr >> 3) % COND_MAP_SIZE;
  pthread_mutex_lock(&g_cond_map_lock);
  CondNode *curr = g_cond_map[bucket];
  while (curr) {
    if (curr->guest_addr == guest_addr) {
      pthread_mutex_unlock(&g_cond_map_lock);
      return &curr->host_cond;
    }
    curr = curr->next;
  }
  CondNode *node = (CondNode *)calloc(1, sizeof(CondNode));
  node->guest_addr = guest_addr;
  pthread_cond_init(&node->host_cond, NULL);
  node->next = g_cond_map[bucket];
  g_cond_map[bucket] = node;
  pthread_mutex_unlock(&g_cond_map_lock);
  return &node->host_cond;
}

static uint64_t resolve_cond_obj(GuestContext *ctx, uint64_t cond_addr, int auto_create) {
  if (!cond_addr) {
    return 0;
  }
  if (cond_addr >= g_sync_pool_base && cond_addr < g_sync_pool_base + g_sync_pool_size) {
    return cond_addr;
  }
  uint64_t *p_cond = (uint64_t *)(ctx->mem_base + cond_addr);
  uint64_t obj = __atomic_load_n(p_cond, __ATOMIC_ACQUIRE);
  if (obj <= 1) {
    if (obj == 1) { // THR_COND_DESTROYED
      return 0;
    }
    if (!auto_create) {
      return 0;
    }
    pthread_mutex_lock(&g_cond_static_lock);
    obj = *p_cond;
    if (obj == 0) { // THR_COND_INITIALIZER
      uint64_t new_obj = alloc_guest_sync_mem(ctx, 128);
      (void)get_host_cond(new_obj);
      __atomic_store_n(p_cond, new_obj, __ATOMIC_RELEASE);
      obj = new_obj;
    }
    pthread_mutex_unlock(&g_cond_static_lock);
    return (obj == 1) ? 0 : obj;
  }
  return obj;
}

void shim_pthread_cond_init(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  if (!cond_addr) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint64_t obj = alloc_guest_sync_mem(ctx, 128);
  *(uint64_t *)(ctx->mem_base + cond_addr) = obj;
  (void)get_host_cond(obj);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_cond_wait(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t mtx_addr = ctx->rsi;
  uint64_t cond_obj = resolve_cond_obj(ctx, cond_addr, 1);
  uint64_t mtx_obj = resolve_mutex_obj(ctx, mtx_addr, 0, 1);
  if (!cond_obj || !mtx_obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint64_t curthread = get_current_thread_handle(ctx);
  uint64_t *p_owner = (uint64_t *)(ctx->mem_base + mtx_obj);
  int32_t *p_count = (int32_t *)(ctx->mem_base + mtx_obj + 0x8);

  if (__atomic_load_n(p_owner, __ATOMIC_ACQUIRE) != curthread) {
    ctx->rax = (uint64_t)EPERM;
    SHIM_RETURN();
  }

  int32_t old_recurse = *p_count;
  *p_count = 0;
  __atomic_store_n(p_owner, 0, __ATOMIC_RELEASE);

  pthread_cond_t *c = get_host_cond(cond_obj);
  pthread_mutex_t *m = get_host_mutex(mtx_obj);

  int ret = pthread_cond_wait(c, m);

  __atomic_store_n(p_owner, curthread, __ATOMIC_RELEASE);
  *p_count = old_recurse;

  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

void shim_pthread_cond_timedwait(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t mtx_addr = ctx->rsi;
  uint64_t abstime_addr = ctx->rdx;
  uint64_t cond_obj = resolve_cond_obj(ctx, cond_addr, 1);
  uint64_t mtx_obj = resolve_mutex_obj(ctx, mtx_addr, 0, 1);
  if (!cond_obj || !mtx_obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint64_t curthread = get_current_thread_handle(ctx);
  uint64_t *p_owner = (uint64_t *)(ctx->mem_base + mtx_obj);
  int32_t *p_count = (int32_t *)(ctx->mem_base + mtx_obj + 0x8);

  if (__atomic_load_n(p_owner, __ATOMIC_ACQUIRE) != curthread) {
    ctx->rax = (uint64_t)EPERM;
    SHIM_RETURN();
  }

  struct timespec ts;
  if (abstime_addr) {
    uint64_t *guest_ts = (uint64_t *)(ctx->mem_base + abstime_addr);
    ts.tv_sec = (time_t)guest_ts[0];
    ts.tv_nsec = (long)guest_ts[1];
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L) {
      ctx->rax = (uint64_t)EINVAL;
      SHIM_RETURN();
    }
  }

  int32_t old_recurse = *p_count;
  *p_count = 0;
  __atomic_store_n(p_owner, 0, __ATOMIC_RELEASE);

  pthread_cond_t *c = get_host_cond(cond_obj);
  pthread_mutex_t *m = get_host_mutex(mtx_obj);

  int ret = pthread_cond_timedwait(c, m, abstime_addr ? &ts : NULL);

  __atomic_store_n(p_owner, curthread, __ATOMIC_RELEASE);
  *p_count = old_recurse;

  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

void shim_pthread_cond_signal(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t cond_obj = resolve_cond_obj(ctx, cond_addr, 1);
  if (!cond_obj) {
    ctx->rax = 0;
    SHIM_RETURN();
  }
  pthread_cond_t *c = get_host_cond(cond_obj);
  ctx->rax = (uint64_t)pthread_cond_signal(c);
  SHIM_RETURN();
}

void shim_pthread_cond_broadcast(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t cond_obj = resolve_cond_obj(ctx, cond_addr, 1);
  if (!cond_obj) {
    ctx->rax = 0;
    SHIM_RETURN();
  }
  pthread_cond_t *c = get_host_cond(cond_obj);
  ctx->rax = (uint64_t)pthread_cond_broadcast(c);
  SHIM_RETURN();
}

void shim_pthread_cond_destroy(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t cond_obj = resolve_cond_obj(ctx, cond_addr, 0);
  if (!cond_obj) {
    ctx->rax = 0;
    SHIM_RETURN();
  }
  uint64_t bucket = (cond_obj >> 3) % COND_MAP_SIZE;
  pthread_mutex_lock(&g_cond_map_lock);
  CondNode **curr = &g_cond_map[bucket];
  while (*curr) {
    if ((*curr)->guest_addr == cond_obj) {
      CondNode *to_free = *curr;
      *curr = (*curr)->next;
      pthread_cond_destroy(&to_free->host_cond);
      free(to_free);
      break;
    }
    curr = &(*curr)->next;
  }
  pthread_mutex_unlock(&g_cond_map_lock);

  if (cond_addr != cond_obj) {
    *(uint64_t *)(ctx->mem_base + cond_addr) = 1; // THR_COND_DESTROYED
  }
  free_guest_sync_mem(ctx, cond_obj, 128);

  ctx->rax = 0;
  SHIM_RETURN();
}

// ============================================================================
// RWLock Table & Resolution
// ============================================================================

#define RWLOCK_MAP_SIZE 1024

typedef struct RwlockNode {
  uint64_t guest_addr;
  pthread_rwlock_t host_rwlock;
  struct RwlockNode *next;
} RwlockNode;

static RwlockNode *g_rwlock_map[RWLOCK_MAP_SIZE] = {0};
static pthread_mutex_t g_rwlock_map_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_rwlock_static_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_rwlock_t *get_host_rwlock(uint64_t guest_addr) {
  uint64_t bucket = (guest_addr >> 3) % RWLOCK_MAP_SIZE;
  pthread_mutex_lock(&g_rwlock_map_lock);
  RwlockNode *curr = g_rwlock_map[bucket];
  while (curr) {
    if (curr->guest_addr == guest_addr) {
      pthread_mutex_unlock(&g_rwlock_map_lock);
      return &curr->host_rwlock;
    }
    curr = curr->next;
  }
  RwlockNode *node = (RwlockNode *)calloc(1, sizeof(RwlockNode));
  node->guest_addr = guest_addr;
  pthread_rwlock_init(&node->host_rwlock, NULL);
  node->next = g_rwlock_map[bucket];
  g_rwlock_map[bucket] = node;
  pthread_mutex_unlock(&g_rwlock_map_lock);
  return &node->host_rwlock;
}

static uint64_t resolve_rwlock_obj(GuestContext *ctx, uint64_t rw_addr, int auto_create) {
  if (!rw_addr) {
    return 0;
  }
  if (rw_addr >= g_sync_pool_base && rw_addr < g_sync_pool_base + g_sync_pool_size) {
    return rw_addr;
  }
  uint64_t *p_rw = (uint64_t *)(ctx->mem_base + rw_addr);
  uint64_t obj = __atomic_load_n(p_rw, __ATOMIC_ACQUIRE);
  if (obj <= 1) {
    if (obj == 1) { // THR_RWLOCK_DESTROYED
      return 0;
    }
    if (!auto_create) {
      return 0;
    }
    pthread_mutex_lock(&g_rwlock_static_lock);
    obj = *p_rw;
    if (obj == 0) {
      uint64_t new_obj = alloc_guest_sync_mem(ctx, 128);
      (void)get_host_rwlock(new_obj);
      __atomic_store_n(p_rw, new_obj, __ATOMIC_RELEASE);
      obj = new_obj;
    }
    pthread_mutex_unlock(&g_rwlock_static_lock);
    return (obj == 1) ? 0 : obj;
  }
  return obj;
}

void shim_pthread_rwlock_init(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  if (!rw_addr) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint64_t obj = alloc_guest_sync_mem(ctx, 128);
  *(uint64_t *)(ctx->mem_base + rw_addr) = obj;
  (void)get_host_rwlock(obj);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_rwlock_rdlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  uint64_t obj = resolve_rwlock_obj(ctx, rw_addr, 1);
  if (!obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  pthread_rwlock_t *rw = get_host_rwlock(obj);
  ctx->rax = (uint64_t)pthread_rwlock_rdlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_wrlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  uint64_t obj = resolve_rwlock_obj(ctx, rw_addr, 1);
  if (!obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  pthread_rwlock_t *rw = get_host_rwlock(obj);
  ctx->rax = (uint64_t)pthread_rwlock_wrlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_tryrdlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  uint64_t obj = resolve_rwlock_obj(ctx, rw_addr, 1);
  if (!obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  pthread_rwlock_t *rw = get_host_rwlock(obj);
  ctx->rax = (uint64_t)pthread_rwlock_tryrdlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_trywrlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  uint64_t obj = resolve_rwlock_obj(ctx, rw_addr, 1);
  if (!obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  pthread_rwlock_t *rw = get_host_rwlock(obj);
  ctx->rax = (uint64_t)pthread_rwlock_trywrlock(rw);
  SHIM_RETURN();
}

void shim_scePthreadRwlockTryrdlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  uint64_t obj = resolve_rwlock_obj(ctx, rw_addr, 1);
  if (!obj) {
    ctx->rax = 0x80020016; // EINVAL
    SHIM_RETURN();
  }
  pthread_rwlock_t *rw = get_host_rwlock(obj);
  int ret = pthread_rwlock_tryrdlock(rw);
  if (ret != 0) {
    ctx->rax = (ret == EBUSY) ? 0x80020010 : (0x80020000 | (uint32_t)ret);
  } else {
    ctx->rax = 0;
  }
  SHIM_RETURN();
}

void shim_scePthreadRwlockTrywrlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  uint64_t obj = resolve_rwlock_obj(ctx, rw_addr, 1);
  if (!obj) {
    ctx->rax = 0x80020016; // EINVAL
    SHIM_RETURN();
  }
  pthread_rwlock_t *rw = get_host_rwlock(obj);
  int ret = pthread_rwlock_trywrlock(rw);
  if (ret != 0) {
    ctx->rax = (ret == EBUSY) ? 0x80020010 : (0x80020000 | (uint32_t)ret);
  } else {
    ctx->rax = 0;
  }
  SHIM_RETURN();
}

void shim_pthread_rwlock_unlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  uint64_t obj = resolve_rwlock_obj(ctx, rw_addr, 0);
  if (!obj) {
    ctx->rax = (uint64_t)EPERM;
    SHIM_RETURN();
  }
  pthread_rwlock_t *rw = get_host_rwlock(obj);
  ctx->rax = (uint64_t)pthread_rwlock_unlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_destroy(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  uint64_t obj = resolve_rwlock_obj(ctx, rw_addr, 0);
  if (!obj) {
    ctx->rax = 0;
    SHIM_RETURN();
  }
  uint64_t bucket = (obj >> 3) % RWLOCK_MAP_SIZE;
  pthread_mutex_lock(&g_rwlock_map_lock);
  RwlockNode **curr = &g_rwlock_map[bucket];
  while (*curr) {
    if ((*curr)->guest_addr == obj) {
      RwlockNode *to_free = *curr;
      *curr = (*curr)->next;
      pthread_rwlock_destroy(&to_free->host_rwlock);
      free(to_free);
      break;
    }
    curr = &(*curr)->next;
  }
  pthread_mutex_unlock(&g_rwlock_map_lock);

  if (rw_addr != obj) {
    *(uint64_t *)(ctx->mem_base + rw_addr) = 1; // THR_RWLOCK_DESTROYED
  }
  free_guest_sync_mem(ctx, obj, 128);

  ctx->rax = 0;
  SHIM_RETURN();
}

// ============================================================================
// Pthread Once
// ============================================================================

static pthread_mutex_t g_once_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_once_cond = PTHREAD_COND_INITIALIZER;

void shim_pthread_once(GuestContext *ctx) {
  uint64_t once_control_addr = ctx->rdi;
  uint64_t init_routine_addr = ctx->rsi;
  if (!once_control_addr) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  int *ctrl = (int *)(ctx->mem_base + once_control_addr);

  pthread_mutex_lock(&g_once_mutex);
  while (*ctrl == 2) { // 2 = InProgress
    pthread_cond_wait(&g_once_cond, &g_once_mutex);
  }
  if (*ctrl == 1) { // 1 = Done
    pthread_mutex_unlock(&g_once_mutex);
    ctx->rax = 0;
    SHIM_RETURN();
  }
  *ctrl = 2; // InProgress
  pthread_mutex_unlock(&g_once_mutex);

  ctx->rsp -= 8;
  MEM_U64(ctx->rsp) = 0xdeadbeefULL;
  recomp_dispatch(ctx, init_routine_addr);

  pthread_mutex_lock(&g_once_mutex);
  *ctrl = 1; // Done
  pthread_cond_broadcast(&g_once_cond);
  pthread_mutex_unlock(&g_once_mutex);

  ctx->rax = 0;
  SHIM_RETURN();
}

// ============================================================================
// Orbis / scePthread wrappers
// ============================================================================

void shim_scePthreadMutexInit(GuestContext *ctx) {
  shim_pthread_mutex_init(ctx);
}

void shim_scePthreadMutexLock(GuestContext *ctx) {
  shim_pthread_mutex_lock(ctx);
}

void shim_scePthreadMutexTrylock(GuestContext *ctx) {
  shim_pthread_mutex_trylock(ctx);
}

void shim_scePthreadMutexTimedlock(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t usec = ctx->rsi;
  uint64_t obj = resolve_mutex_obj(ctx, mtx_addr, 0, 1);
  if (!obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint32_t flags = *(uint32_t *)(ctx->mem_base + obj + 0x20);
  int type = (int)(flags & 0xff);
  uint64_t curthread = get_current_thread_handle(ctx);
  uint64_t *p_owner = (uint64_t *)(ctx->mem_base + obj);
  int32_t *p_count = (int32_t *)(ctx->mem_base + obj + 0x8);

  if (__atomic_load_n(p_owner, __ATOMIC_ACQUIRE) == curthread) {
    if (type == 2) { // Recursive
      (*p_count)++;
      ctx->rax = 0;
      SHIM_RETURN();
    }
    if (usec > 0) {
      usleep((useconds_t)usec);
    }
    ctx->rax = (uint64_t)ETIMEDOUT;
    SHIM_RETURN();
  }

  pthread_mutex_t *m = get_host_mutex(obj);
  int ret = host_mutex_timedlock(m, usec);
  if (ret == 0) {
    __atomic_store_n(p_owner, curthread, __ATOMIC_RELEASE);
    *p_count = 0;
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

void shim_scePthreadMutexUnlock(GuestContext *ctx) {
  shim_pthread_mutex_unlock(ctx);
}

void shim_scePthreadMutexDestroy(GuestContext *ctx) {
  shim_pthread_mutex_destroy(ctx);
}

void shim_scePthreadMutexattrInit(GuestContext *ctx) {
  shim_pthread_mutexattr_init(ctx);
}

void shim_scePthreadMutexattrDestroy(GuestContext *ctx) {
  shim_pthread_mutexattr_destroy(ctx);
}

void shim_scePthreadMutexattrSettype(GuestContext *ctx) {
  shim_pthread_mutexattr_settype(ctx);
}

void shim_scePthreadMutexattrSetprotocol(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutexattr_setprotocol(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadOnce(GuestContext *ctx) {
  shim_pthread_once(ctx);
}

void shim_scePthreadCondInit(GuestContext *ctx) {
  shim_pthread_cond_init(ctx);
}

void shim_scePthreadCondDestroy(GuestContext *ctx) {
  shim_pthread_cond_destroy(ctx);
}

void shim_scePthreadCondSignal(GuestContext *ctx) {
  shim_pthread_cond_signal(ctx);
}

void shim_scePthreadCondBroadcast(GuestContext *ctx) {
  shim_pthread_cond_broadcast(ctx);
}

void shim_scePthreadCondWait(GuestContext *ctx) {
  shim_pthread_cond_wait(ctx);
}

void shim_scePthreadCondTimedwait(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t mtx_addr = ctx->rsi;
  uint64_t usec = ctx->rdx;
  uint64_t cond_obj = resolve_cond_obj(ctx, cond_addr, 1);
  uint64_t mtx_obj = resolve_mutex_obj(ctx, mtx_addr, 0, 1);
  if (!cond_obj || !mtx_obj) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint64_t curthread = get_current_thread_handle(ctx);
  uint64_t *p_owner = (uint64_t *)(ctx->mem_base + mtx_obj);
  int32_t *p_count = (int32_t *)(ctx->mem_base + mtx_obj + 0x8);

  if (__atomic_load_n(p_owner, __ATOMIC_ACQUIRE) != curthread) {
    ctx->rax = (uint64_t)EPERM;
    SHIM_RETURN();
  }

  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  struct timespec ts;
  ts.tv_sec = now.tv_sec + (time_t)(usec / 1000000ULL);
  ts.tv_nsec = now.tv_nsec + (long)((usec % 1000000ULL) * 1000ULL);
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1000000000L;
  }

  int32_t old_recurse = *p_count;
  *p_count = 0;
  __atomic_store_n(p_owner, 0, __ATOMIC_RELEASE);

  pthread_cond_t *c = get_host_cond(cond_obj);
  pthread_mutex_t *m = get_host_mutex(mtx_obj);

  int ret = pthread_cond_timedwait(c, m, &ts);

  __atomic_store_n(p_owner, curthread, __ATOMIC_RELEASE);
  *p_count = old_recurse;

  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

void shim_scePthreadCondattrInit(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  if (!attr_addr) {
    ctx->rax = (uint64_t)EINVAL;
    SHIM_RETURN();
  }
  uint64_t attr_obj = alloc_guest_sync_mem(ctx, 128);
  *(uint64_t *)(ctx->mem_base + attr_addr) = attr_obj;
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadCondattrDestroy(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  if (attr_addr) {
    if (attr_addr >= g_sync_pool_base && attr_addr < g_sync_pool_base + g_sync_pool_size) {
      free_guest_sync_mem(ctx, attr_addr, 128);
    } else {
      uint64_t attr_obj = *(uint64_t *)(ctx->mem_base + attr_addr);
      if (attr_obj >= g_sync_pool_base && attr_obj < g_sync_pool_base + g_sync_pool_size) {
        free_guest_sync_mem(ctx, attr_obj, 128);
        *(uint64_t *)(ctx->mem_base + attr_addr) = 0;
      }
    }
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

// ============================================================================
// C++ ABI static guard shims (matching Itanium C++ ABI with recursion prevention)
// ============================================================================

static pthread_mutex_t g_cxa_guard_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cxa_guard_cond = PTHREAD_COND_INITIALIZER;

void shim_cxa_guard_acquire(GuestContext *ctx) {
  uint64_t guard_addr = ctx->rdi;
  if (!guard_addr) {
    ctx->rax = 0;
    SHIM_RETURN();
  }
  uint8_t *guard = (uint8_t *)(ctx->mem_base + guard_addr);
  // Fast path: if byte 0 is set, initialization is already complete.
  if (*guard & 1) {
    ctx->rax = 0;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&g_cxa_guard_mutex);
  while (1) {
    if (*guard & 1) {
      pthread_mutex_unlock(&g_cxa_guard_mutex);
      ctx->rax = 0;
      SHIM_RETURN();
    }
    // If byte 1 is set, initialization is in progress
    if (guard[1] != 0) {
      // Check if held by current thread (recursive static initialization).
      // We store thread_id in bytes 4..7
      uint32_t holder = *(uint32_t *)(guard + 4);
      uint32_t my_tid = (uint32_t)(ctx->thread_id ? ctx->thread_id : 1);
      if (holder == my_tid) {
        // Recursive static initialization detected on the same thread!
        // Returning 0 avoids recursive deadlock and allows caller to proceed.
        pthread_mutex_unlock(&g_cxa_guard_mutex);
        ctx->rax = 0;
        SHIM_RETURN();
      }
      // Held by another thread: wait for release/abort
      pthread_cond_wait(&g_cxa_guard_cond, &g_cxa_guard_mutex);
      continue;
    }

    // Mark as in-progress
    guard[1] = 1;
    uint32_t my_tid = (uint32_t)(ctx->thread_id ? ctx->thread_id : 1);
    *(uint32_t *)(guard + 4) = my_tid;
    pthread_mutex_unlock(&g_cxa_guard_mutex);
    ctx->rax = 1; // Proceed with initialization
    SHIM_RETURN();
  }
}

void shim_cxa_guard_release(GuestContext *ctx) {
  uint64_t guard_addr = ctx->rdi;
  if (guard_addr) {
    uint8_t *guard = (uint8_t *)(ctx->mem_base + guard_addr);
    pthread_mutex_lock(&g_cxa_guard_mutex);
    *guard = 1; // Mark completed
    guard[1] = 0; // Clear in-progress
    *(uint32_t *)(guard + 4) = 0;
    pthread_cond_broadcast(&g_cxa_guard_cond);
    pthread_mutex_unlock(&g_cxa_guard_mutex);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_cxa_guard_abort(GuestContext *ctx) {
  uint64_t guard_addr = ctx->rdi;
  if (guard_addr) {
    uint8_t *guard = (uint8_t *)(ctx->mem_base + guard_addr);
    pthread_mutex_lock(&g_cxa_guard_mutex);
    guard[1] = 0; // Clear in-progress
    *(uint32_t *)(guard + 4) = 0;
    pthread_cond_broadcast(&g_cxa_guard_cond);
    pthread_mutex_unlock(&g_cxa_guard_mutex);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

// ============================================================================
// Module Teardown
// ============================================================================

void ps4_sync_destroy(void) {
  pthread_mutex_lock(&g_mutex_map_lock);
  for (int i = 0; i < MUTEX_MAP_SIZE; i++) {
    MutexNode *curr = g_mutex_map[i];
    while (curr) {
      MutexNode *next = curr->next;
      pthread_mutex_destroy(&curr->host_mutex);
      free(curr);
      curr = next;
    }
    g_mutex_map[i] = NULL;
  }
  pthread_mutex_unlock(&g_mutex_map_lock);

  pthread_mutex_lock(&g_cond_map_lock);
  for (int i = 0; i < COND_MAP_SIZE; i++) {
    CondNode *curr = g_cond_map[i];
    while (curr) {
      CondNode *next = curr->next;
      pthread_cond_destroy(&curr->host_cond);
      free(curr);
      curr = next;
    }
    g_cond_map[i] = NULL;
  }
  pthread_mutex_unlock(&g_cond_map_lock);

  pthread_mutex_lock(&g_rwlock_map_lock);
  for (int i = 0; i < RWLOCK_MAP_SIZE; i++) {
    RwlockNode *curr = g_rwlock_map[i];
    while (curr) {
      RwlockNode *next = curr->next;
      pthread_rwlock_destroy(&curr->host_rwlock);
      free(curr);
      curr = next;
    }
    g_rwlock_map[i] = NULL;
  }
  pthread_mutex_unlock(&g_rwlock_map_lock);

  pthread_mutex_lock(&g_sync_pool_lock);
  g_sync_free_slots = NULL;
  g_sync_pool_base = 0;
  g_sync_pool_offset = 0;
  pthread_mutex_unlock(&g_sync_pool_lock);
}
