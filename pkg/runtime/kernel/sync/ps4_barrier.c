// SPDX-License-Identifier: GPL-2.0-or-later
//
// Barriers, after shadPS4 (src/core/libraries/kernel/threads/barrier.cpp). The guest's
// ScePthreadBarrier variable holds the id of a refcounted host object (ps4_handle_table.h): a
// thread waiting on a barrier that is destroyed meanwhile keeps it alive until it leaves, as
// shadPS4's shared state does. The pthread_barrier* functions return a POSIX errno, the
// scePthreadBarrier* ones 0x80020000 + errno.

#include "ps4_barrier.h"
#include "ps4_handle_table.h"
#include <errno.h>
#include <stdlib.h>

// The waiter that completes a barrier gets PTHREAD_BARRIER_SERIAL_THREAD (shadPS4
// PthreadBarrierSerialThread).
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)
#define BARRIER_IDS_MAX 0xFFFFu

typedef struct Barrier {
  Ps4HandleObject h;
  pthread_mutex_t mutex;
  pthread_cond_t cv;
  uint32_t count;
  uint32_t arrived;
  uint64_t generation;
} Barrier;

static Ps4HandleTable g_barriers = PS4_HANDLE_TABLE_INIT(BARRIER_IDS_MAX);

static inline uint64_t orbis_err(int e) { return e ? (uint64_t)(0x80020000u + (uint32_t)e) : 0; }

static inline uint64_t *guest_var(GuestContext *ctx, uint64_t addr) {
  return addr ? (uint64_t *)(ctx->mem_base + addr) : NULL;
}

static void barrier_destroy_obj(Ps4HandleObject *o) {
  Barrier *b = (Barrier *)o;
  pthread_cond_destroy(&b->cv);
  pthread_mutex_destroy(&b->mutex);
  free(b);
}

// The barrier the variable names, with a reference; NULL for a NULL or empty variable.
static Barrier *barrier_get(GuestContext *ctx, uint64_t addr) {
  const uint64_t *var = guest_var(ctx, addr);
  if (!var || *var == 0 || *var > BARRIER_IDS_MAX) return NULL;
  return (Barrier *)ps4_handle_get(&g_barriers, (uint32_t)*var);
}

// shadPS4 BarrierInit: a NULL barrier or a count of 0 is EINVAL; the attribute is not used.
static int barrier_init(GuestContext *ctx, uint64_t addr, uint32_t count) {
  uint64_t *var = guest_var(ctx, addr);
  if (!var || count == 0) return EINVAL;
  Barrier *b = (Barrier *)calloc(1, sizeof(Barrier));
  if (!b) return ENOMEM;
  pthread_mutex_init(&b->mutex, NULL);
  pthread_cond_init(&b->cv, NULL);
  b->count = count;
  b->h.destroy = barrier_destroy_obj;
  const uint32_t id = ps4_handle_insert(&g_barriers, &b->h);
  if (!id) {
    barrier_destroy_obj(&b->h);
    return ENOMEM;
  }
  *var = id;
  return 0;
}

// The thread that completes the barrier returns PTHREAD_BARRIER_SERIAL_THREAD, the others 0.
static int barrier_wait(GuestContext *ctx, uint64_t addr) {
  Barrier *b = barrier_get(ctx, addr);
  if (!b) return EINVAL;
  int ret = 0;
  pthread_mutex_lock(&b->mutex);
  if (++b->arrived == b->count) {
    b->arrived = 0;
    ++b->generation;
    pthread_cond_broadcast(&b->cv);
    ret = PTHREAD_BARRIER_SERIAL_THREAD;
  } else {
    const uint64_t generation = b->generation;
    while (b->generation == generation) pthread_cond_wait(&b->cv, &b->mutex);
  }
  pthread_mutex_unlock(&b->mutex);
  ps4_handle_put(&g_barriers, &b->h);
  return ret;
}

// A barrier some thread waits on is EBUSY.
static int barrier_destroy(GuestContext *ctx, uint64_t addr) {
  Barrier *b = barrier_get(ctx, addr);
  if (!b) return EINVAL;
  uint64_t *var = guest_var(ctx, addr);
  pthread_mutex_lock(&b->mutex);
  if (b->arrived != 0) {
    pthread_mutex_unlock(&b->mutex);
    ps4_handle_put(&g_barriers, &b->h);
    return EBUSY;
  }
  Ps4HandleObject *table_ref = ps4_handle_remove(&g_barriers, (uint32_t)*var);
  *var = 0;
  pthread_mutex_unlock(&b->mutex);
  if (table_ref) ps4_handle_put(&g_barriers, table_ref);
  ps4_handle_put(&g_barriers, &b->h);
  return 0;
}

void shim_pthread_barrier_init(GuestContext *ctx) {
  ctx->rax = (uint64_t)(uint32_t)barrier_init(ctx, ctx->rdi, (uint32_t)ctx->rdx);
  SHIM_RETURN();
}

// scePthreadBarrierInit(barrier, attr, count, name)
void shim_scePthreadBarrierInit(GuestContext *ctx) {
  ctx->rax = orbis_err(barrier_init(ctx, ctx->rdi, (uint32_t)ctx->rdx));
  SHIM_RETURN();
}

void shim_pthread_barrier_wait(GuestContext *ctx) {
  ctx->rax = (uint64_t)(uint32_t)barrier_wait(ctx, ctx->rdi);
  SHIM_RETURN();
}

// scePthreadBarrierWait converts only a failure; PTHREAD_BARRIER_SERIAL_THREAD stays -1.
void shim_scePthreadBarrierWait(GuestContext *ctx) {
  const int r = barrier_wait(ctx, ctx->rdi);
  ctx->rax = r > 0 ? orbis_err(r) : (uint64_t)(uint32_t)r;
  SHIM_RETURN();
}

void shim_pthread_barrier_destroy(GuestContext *ctx) {
  ctx->rax = (uint64_t)(uint32_t)barrier_destroy(ctx, ctx->rdi);
  SHIM_RETURN();
}

void shim_scePthreadBarrierDestroy(GuestContext *ctx) {
  ctx->rax = orbis_err(barrier_destroy(ctx, ctx->rdi));
  SHIM_RETURN();
}

// ---- Barrier attributes: the ScePthreadBarrierattr variable holds the attribute itself (its
// process-shared setting, always 0) with BATTR_VALID set by init and cleared by destroy.
#define BATTR_VALID (1ull << 63)

static uint64_t *battr_var(GuestContext *ctx, uint64_t addr) {
  uint64_t *var = guest_var(ctx, addr);
  return var && (*var & BATTR_VALID) ? var : NULL;
}

static int battr_init(GuestContext *ctx) {
  uint64_t *var = guest_var(ctx, ctx->rdi);
  if (!var) return EINVAL;
  *var = BATTR_VALID;
  return 0;
}

static int battr_destroy(GuestContext *ctx) {
  uint64_t *var = battr_var(ctx, ctx->rdi);
  if (!var) return EINVAL;
  *var = 0;
  return 0;
}

static int battr_getpshared(GuestContext *ctx) {
  int32_t *out = (int32_t *)guest_var(ctx, ctx->rsi);
  if (!battr_var(ctx, ctx->rdi) || !out) return EINVAL;
  *out = 0;
  return 0;
}

// Only PTHREAD_PROCESS_PRIVATE (0) is supported.
static int battr_setpshared(GuestContext *ctx) {
  return battr_var(ctx, ctx->rdi) && ctx->rsi == 0 ? 0 : EINVAL;
}

#define BATTR_SHIMS(posix, sce, impl)                                                              \
  void shim_pthread_barrierattr_##posix(GuestContext *ctx) {                                       \
    ctx->rax = (uint64_t)(uint32_t)impl(ctx);                                                      \
    SHIM_RETURN();                                                                                 \
  }                                                                                                \
  void shim_scePthreadBarrierattr##sce(GuestContext *ctx) {                                        \
    ctx->rax = orbis_err(impl(ctx));                                                               \
    SHIM_RETURN();                                                                                 \
  }

BATTR_SHIMS(init, Init, battr_init)
BATTR_SHIMS(destroy, Destroy, battr_destroy)
BATTR_SHIMS(getpshared, Getpshared, battr_getpshared)
BATTR_SHIMS(setpshared, Setpshared, battr_setpshared)
