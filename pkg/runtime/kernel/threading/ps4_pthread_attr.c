// SPDX-License-Identifier: GPL-2.0-or-later
//
// Thread attributes, after shadPS4 (src/core/libraries/kernel/threads/pthread_attr.cpp). The
// guest's ScePthreadAttr variable holds a host pointer to a PthreadAttr. The pthread_attr_*
// functions return a POSIX errno, the scePthreadAttr* ones 0x80020000 + errno (shadPS4 ORBIS()).
//
// Deliberate difference from shadPS4: pthread_attr_setscope accepts PTHREAD_SCOPE_PROCESS and
// PTHREAD_SCOPE_SYSTEM as FreeBSD libthr does; shadPS4's check joins the two comparisons with
// || and so rejects every scope.

#include "ps4_threading.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static inline uint64_t orbis_err(int e) { return e ? (uint64_t)(0x80020000u + (uint32_t)e) : 0; }

// A shim pair: pthread_attr_<name> returns impl's errno, scePthreadAttr<Sce> the Orbis error.
#define ATTR_SHIMS(posix, sce, impl)                                                               \
  void shim_pthread_attr_##posix(GuestContext *ctx) {                                              \
    ctx->rax = (uint64_t)(uint32_t)impl(ctx);                                                      \
    SHIM_RETURN();                                                                                 \
  }                                                                                                \
  void shim_scePthreadAttr##sce(GuestContext *ctx) {                                               \
    ctx->rax = orbis_err(impl(ctx));                                                               \
    SHIM_RETURN();                                                                                 \
  }

// Priority range and default per scheduling policy (shadPS4 ThrPriorities: FIFO, OTHER, RR).
static const struct {
  int min, max, def;
} g_prio[3] = {{0x100, 0x2FF, 0x2BC}, {0x300, 0x3BF, 0x384}, {0x100, 0x2FF, 0x2BC}};

static inline void *gptr(GuestContext *ctx, uint64_t addr) { return addr ? ctx->mem_base + addr : NULL; }

// The attribute in the guest variable at rdi; NULL for a NULL variable or one holding NULL.
static PthreadAttr *attr_of(GuestContext *ctx) {
  const uint64_t *var = (const uint64_t *)gptr(ctx, ctx->rdi);
  return var ? (PthreadAttr *)(uintptr_t)*var : NULL;
}

int ps4_pthread_attr_getaffinity(const PthreadAttr *attr, size_t cpusetsize, void *cpusetp) {
  if (!attr || !cpusetp) return EINVAL;
  if (attr->cpuset) {
    memcpy(cpusetp, attr->cpuset, cpusetsize < attr->cpusetsize ? cpusetsize : attr->cpusetsize);
  } else {
    memset(cpusetp, 0xff, cpusetsize);
  }
  return 0;
}

int ps4_pthread_attr_setaffinity(PthreadAttr *attr, size_t cpusetsize, const void *cpusetp) {
  if (!attr) return EINVAL;
  if (cpusetsize == 0 || !cpusetp) {
    free(attr->cpuset);
    attr->cpuset = NULL;
    attr->cpusetsize = 0;
    return 0;
  }
  if (!attr->cpuset) {
    attr->cpuset = calloc(1, sizeof(Ps4Cpuset));
    if (!attr->cpuset) return ENOMEM;
    attr->cpusetsize = sizeof(Ps4Cpuset);
  }
  memcpy(attr->cpuset, cpusetp, cpusetsize < sizeof(Ps4Cpuset) ? cpusetsize : sizeof(Ps4Cpuset));
  return 0;
}

void ps4_pthread_attr_copy(PthreadAttr *dst, const PthreadAttr *src) {
  void *set = dst->cpuset;
  *dst = *src;
  dst->cpuset = NULL;
  dst->cpusetsize = 0;
  if (!src->cpuset || !src->cpusetsize) {
    free(set);
    return;
  }
  if (!set) set = calloc(1, sizeof(Ps4Cpuset));
  if (!set) return;
  memset(set, 0, sizeof(Ps4Cpuset));
  memcpy(set, src->cpuset, src->cpusetsize < sizeof(Ps4Cpuset) ? src->cpusetsize : sizeof(Ps4Cpuset));
  dst->cpuset = set;
  dst->cpusetsize = sizeof(Ps4Cpuset);
}

void ps4_pthread_attr_release(PthreadAttr *attr) {
  free(attr->cpuset);
  attr->cpuset = NULL;
  attr->cpusetsize = 0;
}

static int attr_init(GuestContext *ctx) {
  if (!ctx->rdi) return EINVAL;
  PthreadAttr *a = (PthreadAttr *)malloc(sizeof(PthreadAttr));
  if (!a) return ENOMEM;
  *a = ps4_pthread_attr_default;
  *(uint64_t *)(ctx->mem_base + ctx->rdi) = (uint64_t)(uintptr_t)a;
  return 0;
}
ATTR_SHIMS(init, Init, attr_init)

static int attr_destroy(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  if (!a) return EINVAL;
  ps4_pthread_attr_release(a);
  free(a);
  *(uint64_t *)(ctx->mem_base + ctx->rdi) = 0;
  return 0;
}
ATTR_SHIMS(destroy, Destroy, attr_destroy)

// ---- Getters: (attr, out).

static int attr_getdetachstate(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  int32_t *out = (int32_t *)gptr(ctx, ctx->rsi);
  if (!a || !out) return EINVAL;
  *out = (a->flags & PTHREAD_ATTR_DETACHED) ? 1 : 0;
  return 0;
}
ATTR_SHIMS(getdetachstate, Getdetachstate, attr_getdetachstate)

static int attr_getguardsize(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  uint64_t *out = (uint64_t *)gptr(ctx, ctx->rsi);
  if (!a || !out) return EINVAL;
  *out = a->guardsize_attr;
  return 0;
}
ATTR_SHIMS(getguardsize, Getguardsize, attr_getguardsize)

// shadPS4 does not check the output pointer here; a NULL one is EINVAL rather than a crash.
static int attr_getinheritsched(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  int32_t *out = (int32_t *)gptr(ctx, ctx->rsi);
  if (!a || !out) return EINVAL;
  *out = a->sched_inherit;
  return 0;
}
ATTR_SHIMS(getinheritsched, Getinheritsched, attr_getinheritsched)

static int attr_getschedparam(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  int32_t *out = (int32_t *)gptr(ctx, ctx->rsi);
  if (!a || !out) return EINVAL;
  *out = a->prio;
  return 0;
}
ATTR_SHIMS(getschedparam, Getschedparam, attr_getschedparam)

static int attr_getschedpolicy(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  int32_t *out = (int32_t *)gptr(ctx, ctx->rsi);
  if (!a || !out) return EINVAL;
  *out = a->sched_policy;
  return 0;
}
ATTR_SHIMS(getschedpolicy, Getschedpolicy, attr_getschedpolicy)

static int attr_getscope(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  int32_t *out = (int32_t *)gptr(ctx, ctx->rsi);
  if (!a || !out) return EINVAL;
  *out = (a->flags & PTHREAD_ATTR_SCOPE_SYSTEM) ? PTHREAD_ATTR_SCOPE_SYSTEM : PTHREAD_ATTR_SCOPE_PROCESS;
  return 0;
}
ATTR_SHIMS(getscope, Getscope, attr_getscope)

static int attr_getstack(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  uint64_t *addr = (uint64_t *)gptr(ctx, ctx->rsi);
  uint64_t *size = (uint64_t *)gptr(ctx, ctx->rdx);
  if (!a || !addr || !size) return EINVAL;
  *addr = (uint64_t)(uintptr_t)a->stackaddr_attr;
  *size = a->stacksize_attr;
  return 0;
}
ATTR_SHIMS(getstack, Getstack, attr_getstack)

static int attr_getstackaddr(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  uint64_t *out = (uint64_t *)gptr(ctx, ctx->rsi);
  if (!a || !out) return EINVAL;
  *out = (uint64_t)(uintptr_t)a->stackaddr_attr;
  return 0;
}
ATTR_SHIMS(getstackaddr, Getstackaddr, attr_getstackaddr)

static int attr_getstacksize(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  uint64_t *out = (uint64_t *)gptr(ctx, ctx->rsi);
  if (!a || !out) return EINVAL;
  *out = a->stacksize_attr;
  return 0;
}
ATTR_SHIMS(getstacksize, Getstacksize, attr_getstacksize)

// ---- Setters: (attr, value).

static int attr_setcreatesuspend(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  if (!a) return EINVAL;
  a->suspend = 1;
  return 0;
}

void shim_pthread_attr_setcreatesuspend_np(GuestContext *ctx) {
  ctx->rax = (uint64_t)(uint32_t)attr_setcreatesuspend(ctx);
  SHIM_RETURN();
}

void shim_scePthreadAttrSetcreatesuspend(GuestContext *ctx) {
  ctx->rax = orbis_err(attr_setcreatesuspend(ctx));
  SHIM_RETURN();
}

// A new policy also takes that policy's default priority.
static int attr_setschedpolicy(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  const int policy = (int)ctx->rsi;
  if (!a) return EINVAL;
  if (policy < PS4_SCHED_FIFO || policy > PS4_SCHED_RR) return ENOTSUP;
  a->sched_policy = policy;
  a->prio = g_prio[policy - 1].def;
  return 0;
}
ATTR_SHIMS(setschedpolicy, Setschedpolicy, attr_setschedpolicy)

static int attr_setscope(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  const uint32_t scope = (uint32_t)ctx->rsi;
  if (!a) return EINVAL;
  if (scope != PTHREAD_ATTR_SCOPE_PROCESS && scope != PTHREAD_ATTR_SCOPE_SYSTEM) return EINVAL;
  if (scope == PTHREAD_ATTR_SCOPE_SYSTEM) {
    a->flags |= PTHREAD_ATTR_SCOPE_SYSTEM;
  } else {
    a->flags &= ~(uint32_t)PTHREAD_ATTR_SCOPE_SYSTEM;
  }
  return 0;
}
ATTR_SHIMS(setscope, Setscope, attr_setscope)

static int attr_setstack(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  if (!a || !ctx->rsi || ctx->rdx < PS4_THR_STACK_MIN) return EINVAL;
  a->stackaddr_attr = (void *)(uintptr_t)ctx->rsi;
  a->stacksize_attr = (size_t)ctx->rdx;
  return 0;
}
ATTR_SHIMS(setstack, Setstack, attr_setstack)

static int attr_setstackaddr(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  if (!a || !ctx->rsi) return EINVAL;
  a->stackaddr_attr = (void *)(uintptr_t)ctx->rsi;
  return 0;
}
ATTR_SHIMS(setstackaddr, Setstackaddr, attr_setstackaddr)

static int attr_setstacksize(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  if (!a || ctx->rsi < PS4_THR_STACK_MIN) return EINVAL;
  a->stacksize_attr = (size_t)ctx->rsi;
  return 0;
}
ATTR_SHIMS(setstacksize, Setstacksize, attr_setstacksize)

static int attr_setdetachstate(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  const int state = (int)ctx->rsi;
  if (!a || (state != 0 && state != 1)) return EINVAL;
  if (state) {
    a->flags |= PTHREAD_ATTR_DETACHED;
  } else {
    a->flags &= ~(uint32_t)PTHREAD_ATTR_DETACHED;
  }
  return 0;
}
ATTR_SHIMS(setdetachstate, Setdetachstate, attr_setdetachstate)

// A FIFO or round-robin priority must be in that policy's range; a NULL parameter is ENOTSUP.
static int attr_setschedparam(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  const int32_t *param = (const int32_t *)gptr(ctx, ctx->rsi);
  if (!a) return EINVAL;
  if (!param) return ENOTSUP;
  const int policy = a->sched_policy;
  if (policy == PS4_SCHED_FIFO || policy == PS4_SCHED_RR) {
    if (*param < g_prio[policy - 1].min || *param > g_prio[policy - 1].max) return ENOTSUP;
  }
  a->prio = *param;
  return 0;
}
ATTR_SHIMS(setschedparam, Setschedparam, attr_setschedparam)

static int attr_setinheritsched(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  const int inherit = (int)ctx->rsi;
  if (!a) return EINVAL;
  if (inherit != PS4_PTHREAD_INHERIT_SCHED && inherit != 0) return ENOTSUP;
  a->sched_inherit = inherit;
  return 0;
}
ATTR_SHIMS(setinheritsched, Setinheritsched, attr_setinheritsched)

static int attr_setguardsize(GuestContext *ctx) {
  PthreadAttr *a = attr_of(ctx);
  if (!a) return EINVAL;
  a->guardsize_attr = (size_t)ctx->rsi;
  return 0;
}
ATTR_SHIMS(setguardsize, Setguardsize, attr_setguardsize)

// ---- Affinity: pthread_attr_getaffinity_np(attr, cpusetsize, cpuset) and the scePthreadAttr
// calls on the first word of the set (shadPS4 scePthreadAttrGetaffinity / Setaffinity).

void shim_pthread_attr_getaffinity_np(GuestContext *ctx) {
  ctx->rax = (uint64_t)(uint32_t)ps4_pthread_attr_getaffinity(attr_of(ctx), (size_t)ctx->rsi, gptr(ctx, ctx->rdx));
  SHIM_RETURN();
}

void shim_pthread_attr_setaffinity_np(GuestContext *ctx) {
  ctx->rax = (uint64_t)(uint32_t)ps4_pthread_attr_setaffinity(attr_of(ctx), (size_t)ctx->rsi, gptr(ctx, ctx->rdx));
  SHIM_RETURN();
}

void shim_scePthreadAttrGetaffinity(GuestContext *ctx) {
  Ps4Cpuset set;
  uint64_t *mask = (uint64_t *)gptr(ctx, ctx->rsi);
  int e = ps4_pthread_attr_getaffinity(attr_of(ctx), sizeof(set), &set);
  if (e == 0 && !mask) e = EINVAL;
  if (e == 0) *mask = set.bits;
  ctx->rax = orbis_err(e);
  SHIM_RETURN();
}

void shim_scePthreadAttrSetaffinity(GuestContext *ctx) {
  const Ps4Cpuset set = {.bits = ctx->rsi, .reserved = 0};
  ctx->rax = orbis_err(ps4_pthread_attr_setaffinity(attr_of(ctx), sizeof(set), &set));
  SHIM_RETURN();
}
