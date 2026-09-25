#include "recomp_runtime.h"
#include <errno.h>

#define MUTEX_MAP_SIZE 1024

typedef struct MutexNode {
  uint64_t guest_addr;
  pthread_mutex_t host_mutex;
  int type;
  struct MutexNode *next;
} MutexNode;

static MutexNode *g_mutex_map[MUTEX_MAP_SIZE] = {0};
static pthread_mutex_t g_mutex_map_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t *get_host_mutex_with_type(uint64_t guest_addr, int guest_type) {
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
  node->type = guest_type;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  int host_type = PTHREAD_MUTEX_NORMAL;
  if (guest_type == 2) {
    host_type = PTHREAD_MUTEX_RECURSIVE;
  } else if (guest_type == 1) {
    host_type = PTHREAD_MUTEX_ERRORCHECK;
  }
  pthread_mutexattr_settype(&attr, host_type);
  pthread_mutex_init(&node->host_mutex, &attr);
  pthread_mutexattr_destroy(&attr);

  node->next = g_mutex_map[bucket];
  g_mutex_map[bucket] = node;
  pthread_mutex_unlock(&g_mutex_map_lock);
  return &node->host_mutex;
}

static pthread_mutex_t *get_host_mutex(uint64_t guest_addr) {
  return get_host_mutex_with_type(guest_addr, 0);
}

void shim_pthread_mutex_init(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t attr_addr = ctx->rsi;
  int guest_type = 0;
  if (attr_addr) {
    guest_type = *(int *)(ctx->mem_base + attr_addr);
  }
  (void)get_host_mutex_with_type(mtx_addr, guest_type);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutex_lock(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  pthread_mutex_t *m = get_host_mutex(mtx_addr);
  ctx->rax = (uint64_t)pthread_mutex_lock(m);
  SHIM_RETURN();
}

void shim_pthread_mutex_trylock(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  pthread_mutex_t *m = get_host_mutex(mtx_addr);
  ctx->rax = (uint64_t)pthread_mutex_trylock(m);
  SHIM_RETURN();
}

void shim_pthread_mutex_unlock(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  pthread_mutex_t *m = get_host_mutex(mtx_addr);
  ctx->rax = (uint64_t)pthread_mutex_unlock(m);
  SHIM_RETURN();
}

void shim_pthread_mutex_destroy(GuestContext *ctx) {
  uint64_t mtx_addr = ctx->rdi;
  uint64_t bucket = (mtx_addr >> 3) % MUTEX_MAP_SIZE;
  pthread_mutex_lock(&g_mutex_map_lock);
  MutexNode **curr = &g_mutex_map[bucket];
  while (*curr) {
    if ((*curr)->guest_addr == mtx_addr) {
      MutexNode *to_free = *curr;
      *curr = (*curr)->next;
      pthread_mutex_destroy(&to_free->host_mutex);
      free(to_free);
      break;
    }
    curr = &(*curr)->next;
  }
  pthread_mutex_unlock(&g_mutex_map_lock);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutexattr_init(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  if (attr_addr) {
    *(uint32_t *)(ctx->mem_base + attr_addr) = 0;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutexattr_settype(GuestContext *ctx) {
  uint64_t attr_addr = ctx->rdi;
  int type = (int)ctx->rsi;
  if (attr_addr) {
    *(uint32_t *)(ctx->mem_base + attr_addr) = (uint32_t)type;
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_mutexattr_destroy(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

// Condition variables
#define COND_MAP_SIZE 1024
typedef struct CondNode {
  uint64_t guest_addr;
  pthread_cond_t host_cond;
  struct CondNode *next;
} CondNode;

static CondNode *g_cond_map[COND_MAP_SIZE] = {0};
static pthread_mutex_t g_cond_map_lock = PTHREAD_MUTEX_INITIALIZER;

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

void shim_pthread_cond_init(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  (void)get_host_cond(cond_addr);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_cond_wait(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t mtx_addr = ctx->rsi;
  pthread_cond_t *c = get_host_cond(cond_addr);
  pthread_mutex_t *m = get_host_mutex(mtx_addr);
  ctx->rax = (uint64_t)pthread_cond_wait(c, m);
  SHIM_RETURN();
}

void shim_pthread_cond_timedwait(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t mtx_addr = ctx->rsi;
  uint64_t abstime_addr = ctx->rdx;
  pthread_cond_t *c = get_host_cond(cond_addr);
  pthread_mutex_t *m = get_host_mutex(mtx_addr);

  struct timespec ts;
  if (abstime_addr) {
    uint64_t *guest_ts = (uint64_t *)(ctx->mem_base + abstime_addr);
    ts.tv_sec = (time_t)guest_ts[0];
    ts.tv_nsec = (long)guest_ts[1];
  }
  ctx->rax = (uint64_t)pthread_cond_timedwait(c, m, abstime_addr ? &ts : NULL);
  SHIM_RETURN();
}

void shim_pthread_cond_signal(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  pthread_cond_t *c = get_host_cond(cond_addr);
  ctx->rax = (uint64_t)pthread_cond_signal(c);
  SHIM_RETURN();
}

void shim_pthread_cond_broadcast(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  pthread_cond_t *c = get_host_cond(cond_addr);
  ctx->rax = (uint64_t)pthread_cond_broadcast(c);
  SHIM_RETURN();
}

void shim_pthread_cond_destroy(GuestContext *ctx) {
  uint64_t cond_addr = ctx->rdi;
  uint64_t bucket = (cond_addr >> 3) % COND_MAP_SIZE;
  pthread_mutex_lock(&g_cond_map_lock);
  CondNode **curr = &g_cond_map[bucket];
  while (*curr) {
    if ((*curr)->guest_addr == cond_addr) {
      CondNode *to_free = *curr;
      *curr = (*curr)->next;
      pthread_cond_destroy(&to_free->host_cond);
      free(to_free);
      break;
    }
    curr = &(*curr)->next;
  }
  pthread_mutex_unlock(&g_cond_map_lock);
  ctx->rax = 0;
  SHIM_RETURN();
}

// RWLock
#define RWLOCK_MAP_SIZE 1024
typedef struct RwlockNode {
  uint64_t guest_addr;
  pthread_rwlock_t host_rwlock;
  struct RwlockNode *next;
} RwlockNode;

static RwlockNode *g_rwlock_map[RWLOCK_MAP_SIZE] = {0};
static pthread_mutex_t g_rwlock_map_lock = PTHREAD_MUTEX_INITIALIZER;

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

void shim_pthread_rwlock_rdlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(rw_addr);
  ctx->rax = (uint64_t)pthread_rwlock_rdlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_wrlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(rw_addr);
  ctx->rax = (uint64_t)pthread_rwlock_wrlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_unlock(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  pthread_rwlock_t *rw = get_host_rwlock(rw_addr);
  ctx->rax = (uint64_t)pthread_rwlock_unlock(rw);
  SHIM_RETURN();
}

void shim_pthread_rwlock_init(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  (void)get_host_rwlock(rw_addr);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_pthread_rwlock_destroy(GuestContext *ctx) {
  uint64_t rw_addr = ctx->rdi;
  uint64_t bucket = (rw_addr >> 3) % RWLOCK_MAP_SIZE;
  pthread_mutex_lock(&g_rwlock_map_lock);
  RwlockNode **curr = &g_rwlock_map[bucket];
  while (*curr) {
    if ((*curr)->guest_addr == rw_addr) {
      RwlockNode *to_free = *curr;
      *curr = (*curr)->next;
      pthread_rwlock_destroy(&to_free->host_rwlock);
      free(to_free);
      break;
    }
    curr = &(*curr)->next;
  }
  pthread_mutex_unlock(&g_rwlock_map_lock);
  ctx->rax = 0;
  SHIM_RETURN();
}

static pthread_mutex_t g_once_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_once_cond = PTHREAD_COND_INITIALIZER;

void shim_pthread_once(GuestContext *ctx) {
  uint64_t once_control_addr = ctx->rdi;
  uint64_t init_routine_addr = ctx->rsi;
  int *ctrl = (int *)(ctx->mem_base + once_control_addr);

  pthread_mutex_lock(&g_once_mutex);
  while (*ctrl == 1) {
    pthread_cond_wait(&g_once_cond, &g_once_mutex);
  }
  if (*ctrl == 2) {
    pthread_mutex_unlock(&g_once_mutex);
    ctx->rax = 0;
    SHIM_RETURN();
  }
  *ctrl = 1;
  pthread_mutex_unlock(&g_once_mutex);

  ctx->rsp -= 8;
  MEM_U64(ctx->rsp) = 0xdeadbeefULL;
  recomp_dispatch(ctx, init_routine_addr);

  pthread_mutex_lock(&g_once_mutex);
  *ctrl = 2;
  pthread_cond_broadcast(&g_once_cond);
  pthread_mutex_unlock(&g_once_mutex);

  ctx->rax = 0;
  SHIM_RETURN();
}

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
}

// scePthreadMutex shims
void shim_scePthreadMutexInit(GuestContext *ctx) {
  shim_pthread_mutex_init(ctx);
}

void shim_scePthreadMutexLock(GuestContext *ctx) {
  shim_pthread_mutex_lock(ctx);
}

void shim_scePthreadMutexTrylock(GuestContext *ctx) {
  shim_pthread_mutex_trylock(ctx);
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

// scePthreadCond shims
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
  pthread_cond_t *c = get_host_cond(cond_addr);
  pthread_mutex_t *m = get_host_mutex(mtx_addr);

  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  struct timespec ts;
  ts.tv_sec = now.tv_sec + (time_t)(usec / 1000000ULL);
  ts.tv_nsec = now.tv_nsec + (long)((usec % 1000000ULL) * 1000ULL);
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1000000000L;
  }
  int ret = pthread_cond_timedwait(c, m, &ts);
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

void shim_scePthreadCondattrInit(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_scePthreadCondattrDestroy(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}
