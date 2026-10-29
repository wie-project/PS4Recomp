#include "ps4_sync_objects.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint64_t g_base; // guest address of the arena; 0 until the first object
static uint8_t *g_host;         // the arena in host memory
static uint64_t g_next;         // slots from here on were never used
static uint32_t *g_free;        // freed slots, a stack
static uint64_t g_nfree;
static _Atomic uint8_t *g_live; // 1 per slot holding a live object

uint64_t sync_obj_alloc(GuestContext *ctx) {
  pthread_mutex_lock(&g_lock);
  uint64_t base = atomic_load_explicit(&g_base, memory_order_relaxed);
  if (!base) {
    const uint64_t a = recomp_vm_alloc_named(ctx, SYNC_OBJ_STRIDE * SYNC_OBJ_SLOTS, PROT_READ | PROT_WRITE, 0,
                                             "sync_objects");
    g_free = (uint32_t *)malloc(sizeof(uint32_t) * SYNC_OBJ_SLOTS);
    g_live = (_Atomic uint8_t *)calloc(SYNC_OBJ_SLOTS, 1);
    if (a == (uint64_t)-1 || !g_free || !g_live) {
      pthread_mutex_unlock(&g_lock);
      return 0;
    }
    g_host = ctx->mem_base + a;
    base = a;
    atomic_store_explicit(&g_base, base, memory_order_release);
  }
  uint64_t slot;
  if (g_nfree) {
    slot = g_free[--g_nfree];
  } else if (g_next < SYNC_OBJ_SLOTS) {
    slot = g_next++;
  } else {
    pthread_mutex_unlock(&g_lock);
    return 0;
  }
  memset(g_host + slot * SYNC_OBJ_STRIDE, 0, SYNC_OBJ_STRIDE);
  atomic_store_explicit(&g_live[slot], 1, memory_order_release);
  pthread_mutex_unlock(&g_lock);
  return base + slot * SYNC_OBJ_STRIDE;
}

int64_t sync_obj_index(uint64_t v) {
  const uint64_t base = atomic_load_explicit(&g_base, memory_order_acquire);
  if (!base || v < base) return -1;
  const uint64_t off = v - base;
  if (off >= SYNC_OBJ_STRIDE * SYNC_OBJ_SLOTS || off % SYNC_OBJ_STRIDE) return -1;
  return (int64_t)(off / SYNC_OBJ_STRIDE);
}

int64_t sync_obj_slot(uint64_t v) {
  const int64_t slot = sync_obj_index(v);
  return slot >= 0 && atomic_load_explicit(&g_live[slot], memory_order_acquire) ? slot : -1;
}

void sync_obj_free(uint64_t v) {
  pthread_mutex_lock(&g_lock);
  const int64_t slot = sync_obj_slot(v);
  if (slot >= 0) {
    atomic_store_explicit(&g_live[slot], 0, memory_order_release);
    g_free[g_nfree++] = (uint32_t)slot;
  }
  pthread_mutex_unlock(&g_lock);
}
