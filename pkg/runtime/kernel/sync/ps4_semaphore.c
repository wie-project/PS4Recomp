#include "ps4_semaphore.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEMS 1024
#define SEM_MAGIC 0x53454d31 // 'SEM1'

typedef struct {
  uint32_t magic;
  int in_use;
  int value;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} RecompInternalSem;

static RecompInternalSem g_sem_table[MAX_SEMS];
static pthread_mutex_t g_sem_table_lock = PTHREAD_MUTEX_INITIALIZER;

static int alloc_sem_slot(void) {
  pthread_mutex_lock(&g_sem_table_lock);
  for (int i = 1; i < MAX_SEMS; i++) {
    if (!g_sem_table[i].in_use) {
      g_sem_table[i].in_use = 1;
      pthread_mutex_unlock(&g_sem_table_lock);
      return i;
    }
  }
  pthread_mutex_unlock(&g_sem_table_lock);
  return -1;
}

static RecompInternalSem *get_sem(uint32_t id) {
  if (id == 0 || id >= MAX_SEMS) {
    return NULL;
  }
  RecompInternalSem *s = &g_sem_table[id];
  if (!s->in_use || s->magic != SEM_MAGIC) {
    return NULL;
  }
  return s;
}

// sem_init(sem_t *sem, int pshared, unsigned int value)
void shim_sem_init(GuestContext *ctx) {
  uint64_t sem_addr = ctx->rdi;
  int pshared = (int)ctx->rsi;
  unsigned int value = (unsigned int)ctx->rdx;
  (void)pshared;

  if (!sem_addr) {
    set_guest_errno(ctx, EFAULT);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  int slot = alloc_sem_slot();
  if (slot < 0) {
    set_guest_errno(ctx, ENOSPC);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  RecompInternalSem *s = &g_sem_table[slot];
  s->magic = SEM_MAGIC;
  s->value = (int)value;
  pthread_mutex_init(&s->mutex, NULL);
  pthread_cond_init(&s->cond, NULL);

  // Store slot id in guest sem_t (both 32-bit and 64-bit safe)
  *(uint64_t *)(ctx->mem_base + sem_addr) = (uint64_t)slot;

  ctx->rax = 0;
  SHIM_RETURN();
}

// sem_destroy(sem_t *sem)
void shim_sem_destroy(GuestContext *ctx) {
  uint64_t sem_addr = ctx->rdi;
  if (!sem_addr) {
    set_guest_errno(ctx, EFAULT);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  uint32_t id = *(uint32_t *)(ctx->mem_base + sem_addr);
  RecompInternalSem *s = get_sem(id);
  if (!s) {
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&s->mutex);
  s->magic = 0;
  pthread_cond_broadcast(&s->cond);
  pthread_mutex_unlock(&s->mutex);

  pthread_mutex_destroy(&s->mutex);
  pthread_cond_destroy(&s->cond);

  pthread_mutex_lock(&g_sem_table_lock);
  s->in_use = 0;
  pthread_mutex_unlock(&g_sem_table_lock);

  ctx->rax = 0;
  SHIM_RETURN();
}

// sem_wait(sem_t *sem)
void shim_sem_wait(GuestContext *ctx) {
  uint64_t sem_addr = ctx->rdi;
  if (!sem_addr) {
    set_guest_errno(ctx, EFAULT);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  uint32_t id = *(uint32_t *)(ctx->mem_base + sem_addr);
  RecompInternalSem *s = get_sem(id);
  if (!s) {
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&s->mutex);
  while (s->value <= 0) {
    pthread_cond_wait(&s->cond, &s->mutex);
  }
  s->value--;
  pthread_mutex_unlock(&s->mutex);

  ctx->rax = 0;
  SHIM_RETURN();
}

// sem_trywait(sem_t *sem)
void shim_sem_trywait(GuestContext *ctx) {
  uint64_t sem_addr = ctx->rdi;
  if (!sem_addr) {
    set_guest_errno(ctx, EFAULT);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  uint32_t id = *(uint32_t *)(ctx->mem_base + sem_addr);
  RecompInternalSem *s = get_sem(id);
  if (!s) {
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&s->mutex);
  if (s->value > 0) {
    s->value--;
    pthread_mutex_unlock(&s->mutex);
    ctx->rax = 0;
  } else {
    pthread_mutex_unlock(&s->mutex);
    set_guest_errno(ctx, EAGAIN);
    ctx->rax = (uint64_t)-1;
  }
  SHIM_RETURN();
}

// sem_post(sem_t *sem)
void shim_sem_post(GuestContext *ctx) {
  uint64_t sem_addr = ctx->rdi;
  if (!sem_addr) {
    set_guest_errno(ctx, EFAULT);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  uint32_t id = *(uint32_t *)(ctx->mem_base + sem_addr);
  RecompInternalSem *s = get_sem(id);
  if (!s) {
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&s->mutex);
  s->value++;
  pthread_cond_signal(&s->cond);
  pthread_mutex_unlock(&s->mutex);

  ctx->rax = 0;
  SHIM_RETURN();
}

// sem_getvalue(sem_t *sem, int *sval)
void shim_sem_getvalue(GuestContext *ctx) {
  uint64_t sem_addr = ctx->rdi;
  uint64_t sval_addr = ctx->rsi;
  if (!sem_addr || !sval_addr) {
    set_guest_errno(ctx, EFAULT);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  uint32_t id = *(uint32_t *)(ctx->mem_base + sem_addr);
  RecompInternalSem *s = get_sem(id);
  if (!s) {
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&s->mutex);
  int cur_val = s->value;
  pthread_mutex_unlock(&s->mutex);

  *(int *)(ctx->mem_base + sval_addr) = cur_val;
  ctx->rax = 0;
  SHIM_RETURN();
}
