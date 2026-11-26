// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpCommon, following shadPS4 src/core/libraries/np/np_common.cpp. The mutex, condition
// variable and thread wrappers work on the guest's ScePthread* variables through the libkernel
// implementation (ps4_sync.c, ps4_threading.c), as shadPS4's call its posix_pthread_*
// functions. A callout context owns a guest-visible thread that runs on its own guest context
// and calls each entry's guest handler once the entry's deadline has passed.

#include "ps4_np_common.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ps4_kernel_sys.h"
#include "ps4_sync.h"
#include "ps4_threading.h"

#define NPC_GPTR(ctx, a) ((a) ? (void *)((ctx)->mem_base + (a)) : NULL)

// Orbis mutex types (libkernel PthreadMutexType).
#define NPC_MUTEX_TYPE_ERRORCHECK 1
#define NPC_MUTEX_TYPE_RECURSIVE 2
// Orbis scheduling policies and the default priority of posix_pthread_attr_init.
#define NPC_SCHED_FIFO 1
#define NPC_SCHED_OTHER 2
#define NPC_DEFAULT_PRIO 700
// libkernel's smallest thread stack (posix_pthread_attr_setstacksize).
#define NPC_STACK_MIN (16 * 1024)

// shadPS4 NormalizeNpCommonResult: a positive POSIX errno becomes an SCE kernel error.
static int32_t npc_result(int rc) {
    return rc <= 0 ? rc : (int32_t)(0x80020000u + (uint32_t)rc);
}

//=============================================================================
// NpId / online id helpers
//=============================================================================

int32_t sceNpCmpOnlineId(const OrbisNpOnlineId *id1, const OrbisNpOnlineId *id2) {
    if (!id1 || !id2) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (strncmp(id1->data, id2->data, ORBIS_NP_ONLINEID_MAX_LENGTH) != 0) {
        return ORBIS_NP_UTIL_ERROR_NOT_MATCH;
    }
    return 0;
}

int32_t sceNpCmpNpId(const OrbisNpId *id1, const OrbisNpId *id2) {
    if (!id1 || !id2) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (strncmp(id1->handle.data, id2->handle.data, ORBIS_NP_ONLINEID_MAX_LENGTH) != 0) {
        return ORBIS_NP_UTIL_ERROR_NOT_MATCH;
    }
    for (int i = 0; i < 8; i++) {
        if (id1->opt[i] != id2->opt[i]) return ORBIS_NP_UTIL_ERROR_NOT_MATCH;
    }
    for (int i = 0; i < 8; i++) {
        if (id1->reserved[i] != id2->reserved[i]) return ORBIS_NP_UTIL_ERROR_NOT_MATCH;
    }
    return 0;
}

int32_t sceNpCmpNpIdInOrder(const OrbisNpId *id1, const OrbisNpId *id2, uint32_t *out_result) {
    if (!id1 || !id2 || !out_result) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    const int compare = strncmp(id1->handle.data, id2->handle.data, ORBIS_NP_ONLINEID_MAX_LENGTH);
    if (compare != 0) {
        *out_result = compare < 0 ? (uint32_t)-1 : 1;
        return 0;
    }
    for (int i = 0; i < 8; i++) {
        if (id1->opt[i] != id2->opt[i]) {
            *out_result = id1->opt[i] < id2->opt[i] ? (uint32_t)-1 : 1;
            return 0;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (id1->reserved[i] != id2->reserved[i]) {
            *out_result = id1->reserved[i] < id2->reserved[i] ? (uint32_t)-1 : 1;
            return 0;
        }
    }
    *out_result = 0;
    return 0;
}

int32_t sceNpGetPlatformType(const OrbisNpId *np_id) {
    if (!np_id) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    // The platform tag is the little-endian string in opt[4..7].
    static const uint32_t kTagPs3 = 0x00337370;  // "ps3"
    static const uint32_t kTagVita = 0x32707370; // "psp2"
    static const uint32_t kTagPs4 = 0x00347370;  // "ps4"
    if (np_id->opt[4] == 0) {
        return ORBIS_NP_PLATFORM_TYPE_NONE;
    }
    uint32_t tag;
    memcpy(&tag, np_id->opt + 4, sizeof(tag));
    if (tag == kTagPs3) return ORBIS_NP_PLATFORM_TYPE_PS3;
    if (tag == kTagVita) return ORBIS_NP_PLATFORM_TYPE_VITA;
    if (tag == kTagPs4) return ORBIS_NP_PLATFORM_TYPE_PS4;
    return ORBIS_NP_ERROR_INVALID_PLATFORM_TYPE;
}

int32_t sceNpIntIsValidOnlineId(const OrbisNpOnlineId *id) {
    if (!id || id->term != 0) {
        return 0;
    }
    const size_t len = strnlen(id->data, ORBIS_NP_ONLINEID_MAX_LENGTH + 1);
    if (len < 3 || len > ORBIS_NP_ONLINEID_MAX_LENGTH) {
        return 0;
    }
    if (!isalnum((unsigned char)id->data[0])) {
        return 0;
    }
    for (size_t i = 1; i < len; i++) {
        const unsigned char c = (unsigned char)id->data[i];
        if (!isalnum(c) && c != '_' && c != '-') {
            return 0;
        }
    }
    return 1;
}

void sceNpGetSdkVersion(char *version_buf) {
    // As in the real library there is no argument check.
    OrbisKernelSwVersion sw = {.Size = sizeof(OrbisKernelSwVersion)};
    sceKernelGetSystemSwVersion(&sw);
    const uint32_t hex = sw.Version;
    snprintf(version_buf, 8, "%d.%02d", (int)(((hex >> 0x18) & 0xf) + (hex >> 0x1c) * 10),
             (int)(((hex >> 0x10) & 0xf) + ((hex >> 0x14) & 0xf) * 10));
}

int32_t sceNpGetSystemClockUsec(int64_t *usec) {
    // sceKernelClockGettime(ORBIS_CLOCK_MONOTONIC) on the host monotonic clock.
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return npc_result(errno);
    }
    *usec = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    return 0;
}

//=============================================================================
// Mutex, condition variable and thread wrappers
//=============================================================================

uint32_t sceNpMutexInit(GuestContext *ctx, uint64_t mutex, uint64_t flags) {
    // The name only labels the kernel object; the attributes are the default (error check)
    // made recursive by flag bit 0.
    const int type = (flags & 1) ? NPC_MUTEX_TYPE_RECURSIVE : NPC_MUTEX_TYPE_ERRORCHECK;
    return (uint32_t)npc_result(ps4_guest_mutex_init(ctx, mutex, type));
}

uint32_t sceNpMutexLock(GuestContext *ctx, uint64_t mutex) {
    return (uint32_t)npc_result(ps4_guest_mutex_lock(ctx, mutex));
}

uint32_t sceNpMutexUnlock(GuestContext *ctx, uint64_t mutex) {
    return (uint32_t)npc_result(ps4_guest_mutex_unlock(ctx, mutex));
}

uint32_t sceNpMutexTryLock(GuestContext *ctx, uint64_t mutex) {
    const int rc = ps4_guest_mutex_trylock(ctx, mutex);
    if (rc == EBUSY) {
        return (uint32_t)ORBIS_NP_LW_MUTEX_ERROR_BUSY;
    }
    return (uint32_t)npc_result(rc);
}

int32_t sceNpMutexDestroy(GuestContext *ctx, uint64_t mutex) {
    return npc_result(ps4_guest_mutex_destroy(ctx, mutex));
}

int32_t sceNpCondInit(GuestContext *ctx, uint64_t cond) {
    return npc_result(ps4_guest_cond_init(ctx, cond));
}

int32_t sceNpCondDestroy(GuestContext *ctx, uint64_t cond) {
    return npc_result(ps4_guest_cond_destroy(ctx, cond));
}

int32_t sceNpCondSignal(GuestContext *ctx, uint64_t cond) {
    return npc_result(ps4_guest_cond_signal(ctx, cond));
}

int32_t sceNpCondTimedwait(GuestContext *ctx, uint64_t cond, uint64_t mutex, uint32_t usec) {
    // usec == 0 waits without a timeout (posix_pthread_cond_wait in shadPS4).
    const int rc = ps4_guest_cond_wait(ctx, cond, mutex, usec);
    if (usec != 0 && rc == ETIMEDOUT) {
        return ORBIS_NP_LW_COND_ERROR_TIMEDOUT;
    }
    return npc_result(rc);
}

// The attributes shadPS4's sceNpCreateThread builds: posix_pthread_attr_init's defaults with the
// stack size (EINVAL below the 16 KiB posix_pthread_attr_setstacksize accepts), and with a priority FIFO scheduling (SDK 2.50 and later) at that priority. The
// affinity mask is not stored: this runtime's thread affinity calls (scePthreadAttrSetaffinity,
// scePthreadSetaffinity) leave scheduling to the host, so a pinned NP thread is scheduled the
// same way as one created with scePthreadAttrSetaffinity.
static int npc_thread_attr(GuestContext *ctx, PthreadAttr *attr, int32_t priority, uint64_t stack_size) {
    if (stack_size < NPC_STACK_MIN) {
        return EINVAL;
    }
    memset(attr, 0, sizeof(*attr));
    attr->sched_policy = NPC_SCHED_OTHER;
    attr->prio = NPC_DEFAULT_PRIO;
    attr->stacksize_attr = (size_t)stack_size;
    if (priority != 0) {
        int32_t sdk_version = 0;
        if (sceKernelGetCompiledSdkVersion(ctx, &sdk_version) >= 0 && sdk_version >= 0x2500000) {
            attr->sched_policy = NPC_SCHED_FIFO;
        }
        attr->prio = priority;
    }
    return 0;
}

int32_t sceNpCreateThread(GuestContext *ctx, uint64_t thread, uint64_t start_routine, uint64_t arg,
                          int32_t priority, uint64_t stack_size, uint64_t affinity_mask,
                          const char *name) {
    (void)affinity_mask;
    PthreadAttr attr;
    int rc = npc_thread_attr(ctx, &attr, priority, stack_size);
    if (rc == 0) {
        rc = ps4_create_guest_thread(ctx, thread, &attr, start_routine, arg, name);
    }
    return npc_result(rc);
}

uint32_t sceNpJoinThread(uint64_t thread, uint64_t *ret) {
    return (uint32_t)npc_result(ps4_join_guest_thread(thread, ret));
}

//=============================================================================
// Callout context
//=============================================================================

#define NPC_CTX_MUTEX(c) ((c) + offsetof(OrbisNpCalloutContext, mutex))
#define NPC_CTX_COND(c) ((c) + offsetof(OrbisNpCalloutContext, cond))

// shadPS4 NpCalloutThreadMain: runs due entries in deadline order and sleeps until the next
// deadline (or a new entry / stop request signals the condition).
static void npc_callout_thread_main(GuestContext *ctx, void *arg) {
    const uint64_t cctx = (uint64_t)(uintptr_t)arg;
    OrbisNpCalloutContext *c = NPC_GPTR(ctx, cctx);
    sceNpMutexLock(ctx, NPC_CTX_MUTEX(cctx));
    for (;;) {
        int64_t now_usec = 0;
        sceNpGetSystemClockUsec(&now_usec);
        uint32_t wait_usec = 0;
        if (c->head == 0) {
            if (c->stop_requested > 0) break;
        } else {
            OrbisNpCalloutEntry *entry = NPC_GPTR(ctx, c->head);
            const uint64_t deadline = (uint64_t)entry->deadline_usec;
            const uint64_t now = (uint64_t)now_usec;
            const uint64_t remaining = deadline - now;
            if (deadline < now || remaining == 0) {
                c->head = entry->next;
                const uint64_t handler = entry->handler;
                const uint64_t handler_arg = entry->arg;
                sceNpMutexUnlock(ctx, NPC_CTX_MUTEX(cctx));
                if (handler) {
                    recomp_shim_call_guest(ctx, handler, 0, handler_arg, 0, 0, 0);
                }
                sceNpMutexLock(ctx, NPC_CTX_MUTEX(cctx));
                continue;
            }
            if (c->stop_requested > 0) break;
            wait_usec = remaining > 0xffffffffULL ? 0xffffffffU : (uint32_t)remaining;
        }
        sceNpCondTimedwait(ctx, NPC_CTX_COND(cctx), NPC_CTX_MUTEX(cctx), wait_usec);
        if (c->stop_requested > 0) break;
    }
    sceNpMutexUnlock(ctx, NPC_CTX_MUTEX(cctx));
}

int32_t sceNpCalloutInitCtx(GuestContext *ctx, uint64_t callout_ctx, const char *name,
                            uint64_t stack_size, int32_t priority, uint64_t affinity_mask) {
    OrbisNpCalloutContext *c = NPC_GPTR(ctx, callout_ctx);
    if (c->active >= 1) {
        return ORBIS_NP_CALLOUT_ERROR_ALREADY_INITIALIZED;
    }
    c->stop_requested = 0;
    c->head = 0;
    int32_t rc = (int32_t)sceNpMutexInit(ctx, NPC_CTX_MUTEX(callout_ctx), 1);
    if (rc < 0) return rc;
    rc = sceNpCondInit(ctx, NPC_CTX_COND(callout_ctx));
    if (rc >= 0) {
        (void)affinity_mask;
        PthreadAttr attr;
        int err = npc_thread_attr(ctx, &attr, priority, stack_size);
        if (err == 0) {
            err = ps4_create_host_entry_thread(ctx, callout_ctx + offsetof(OrbisNpCalloutContext, thread),
                                               &attr, npc_callout_thread_main,
                                               (void *)(uintptr_t)callout_ctx, name);
        }
        rc = npc_result(err);
        if (rc >= 0) {
            c->active = 1;
            return 0;
        }
        sceNpCondDestroy(ctx, NPC_CTX_COND(callout_ctx));
    }
    sceNpMutexDestroy(ctx, NPC_CTX_MUTEX(callout_ctx));
    return rc;
}

// sceNpCalloutStartOnCtx and sceNpCalloutStartOnCtx64 differ only in how the delay is widened.
static int32_t npc_callout_start(GuestContext *ctx, uint64_t callout_ctx, uint64_t entry_addr,
                                 int64_t delay_usec, uint64_t handler, uint64_t arg) {
    OrbisNpCalloutContext *c = NPC_GPTR(ctx, callout_ctx);
    if (c->active == 0) {
        return ORBIS_NP_CALLOUT_ERROR_NOT_INITIALIZED;
    }
    OrbisNpCalloutEntry *entry = NPC_GPTR(ctx, entry_addr);
    entry->next = 0;
    entry->handler = handler;
    entry->arg = arg;
    int64_t now = 0;
    sceNpGetSystemClockUsec(&now);
    entry->deadline_usec = now + delay_usec;

    sceNpMutexLock(ctx, NPC_CTX_MUTEX(callout_ctx));
    for (uint64_t cur = c->head; cur; cur = ((OrbisNpCalloutEntry *)NPC_GPTR(ctx, cur))->next) {
        if (cur == entry_addr) {
            sceNpMutexUnlock(ctx, NPC_CTX_MUTEX(callout_ctx));
            return ORBIS_NP_CALLOUT_ERROR_DUPLICATE_ENTRY;
        }
    }
    // Insert before the first entry that is due no earlier.
    uint64_t *prev_next = &c->head;
    for (uint64_t cur = c->head; cur;) {
        OrbisNpCalloutEntry *e = NPC_GPTR(ctx, cur);
        if (e->deadline_usec >= entry->deadline_usec) break;
        prev_next = &e->next;
        cur = e->next;
    }
    entry->next = *prev_next;
    *prev_next = entry_addr;
    sceNpCondSignal(ctx, NPC_CTX_COND(callout_ctx));
    sceNpMutexUnlock(ctx, NPC_CTX_MUTEX(callout_ctx));
    return 0;
}

int32_t sceNpCalloutStartOnCtx64(GuestContext *ctx, uint64_t callout_ctx, uint64_t entry,
                                 int64_t delay_usec, uint64_t handler, uint64_t arg) {
    return npc_callout_start(ctx, callout_ctx, entry, delay_usec, handler, arg);
}

int32_t sceNpCalloutStopOnCtx(GuestContext *ctx, uint64_t callout_ctx, uint64_t entry,
                              uint32_t *removed) {
    OrbisNpCalloutContext *c = NPC_GPTR(ctx, callout_ctx);
    if (c->active == 0) {
        return ORBIS_NP_CALLOUT_ERROR_NOT_INITIALIZED;
    }
    sceNpMutexLock(ctx, NPC_CTX_MUTEX(callout_ctx));
    uint32_t found = 0;
    uint64_t *prev_next = &c->head;
    for (uint64_t cur = c->head; cur;) {
        OrbisNpCalloutEntry *e = NPC_GPTR(ctx, cur);
        if (cur == entry) {
            *prev_next = e->next;
            found = 1;
            break;
        }
        prev_next = &e->next;
        cur = e->next;
    }
    if (removed) {
        *removed = found;
    }
    sceNpMutexUnlock(ctx, NPC_CTX_MUTEX(callout_ctx));
    return 0;
}

void sceNpCalloutTermCtx(GuestContext *ctx, uint64_t callout_ctx) {
    OrbisNpCalloutContext *c = NPC_GPTR(ctx, callout_ctx);
    if (c->active == 0) {
        return;
    }
    sceNpMutexLock(ctx, NPC_CTX_MUTEX(callout_ctx));
    c->stop_requested = 1;
    sceNpCondSignal(ctx, NPC_CTX_COND(callout_ctx));
    sceNpMutexUnlock(ctx, NPC_CTX_MUTEX(callout_ctx));
    sceNpJoinThread(c->thread, NULL);
    sceNpCondDestroy(ctx, NPC_CTX_COND(callout_ctx));
    sceNpMutexDestroy(ctx, NPC_CTX_MUTEX(callout_ctx));
    c->active = 0;
}

//=============================================================================
// Shims
//=============================================================================

#define NPC_RET(expr)                                                          \
    do {                                                                       \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(expr);                         \
        SHIM_RETURN();                                                         \
    } while (0)

void shim_sceNpCmpOnlineId(GuestContext *ctx) {
    NPC_RET(sceNpCmpOnlineId(NPC_GPTR(ctx, ctx->rdi), NPC_GPTR(ctx, ctx->rsi)));
}

void shim_sceNpCmpNpId(GuestContext *ctx) {
    NPC_RET(sceNpCmpNpId(NPC_GPTR(ctx, ctx->rdi), NPC_GPTR(ctx, ctx->rsi)));
}

void shim_sceNpCmpNpIdInOrder(GuestContext *ctx) {
    NPC_RET(sceNpCmpNpIdInOrder(NPC_GPTR(ctx, ctx->rdi), NPC_GPTR(ctx, ctx->rsi), NPC_GPTR(ctx, ctx->rdx)));
}

// sceNpMutexInit(mutex, name, flags) and sceNpLwMutexInit, which forwards to it.
void shim_sceNpMutexInit(GuestContext *ctx) {
    NPC_RET(sceNpMutexInit(ctx, ctx->rdi, ctx->rdx));
}

void shim_sceNpMutexLock(GuestContext *ctx) {
    NPC_RET(sceNpMutexLock(ctx, ctx->rdi));
}

void shim_sceNpMutexUnlock(GuestContext *ctx) {
    NPC_RET(sceNpMutexUnlock(ctx, ctx->rdi));
}

void shim_sceNpMutexTryLock(GuestContext *ctx) {
    NPC_RET(sceNpMutexTryLock(ctx, ctx->rdi));
}

void shim_sceNpMutexDestroy(GuestContext *ctx) {
    NPC_RET(sceNpMutexDestroy(ctx, ctx->rdi));
}

void shim_sceNpLwMutexInit(GuestContext *ctx) {
    shim_sceNpMutexInit(ctx);
}

void shim_sceNpLwMutexLock(GuestContext *ctx) {
    shim_sceNpMutexLock(ctx);
}

void shim_sceNpLwMutexUnlock(GuestContext *ctx) {
    shim_sceNpMutexUnlock(ctx);
}

void shim_sceNpLwMutexTryLock(GuestContext *ctx) {
    shim_sceNpMutexTryLock(ctx);
}

void shim_sceNpLwMutexDestroy(GuestContext *ctx) {
    shim_sceNpMutexDestroy(ctx);
}

// sceNpCondInit(cond, name, flags): name and flags do not change the condition variable.
void shim_sceNpCondInit(GuestContext *ctx) {
    NPC_RET(sceNpCondInit(ctx, ctx->rdi));
}

void shim_sceNpCondDestroy(GuestContext *ctx) {
    NPC_RET(sceNpCondDestroy(ctx, ctx->rdi));
}

void shim_sceNpCondSignal(GuestContext *ctx) {
    NPC_RET(sceNpCondSignal(ctx, ctx->rdi));
}

void shim_sceNpCondTimedwait(GuestContext *ctx) {
    NPC_RET(sceNpCondTimedwait(ctx, ctx->rdi, ctx->rsi, (uint32_t)ctx->rdx));
}

// sceNpCreateThread(thread, start, arg, priority, stack_size, affinity_mask, name): the seventh
// argument is on the stack above the return address.
void shim_sceNpCreateThread(GuestContext *ctx) {
    const uint64_t name_addr = *(const uint64_t *)(ctx->mem_base + ctx->rsp + 8);
    NPC_RET(sceNpCreateThread(ctx, ctx->rdi, ctx->rsi, ctx->rdx, (int32_t)ctx->rcx, ctx->r8, ctx->r9,
                              NPC_GPTR(ctx, name_addr)));
}

void shim_sceNpJoinThread(GuestContext *ctx) {
    uint64_t ret = 0;
    const uint32_t rc = sceNpJoinThread(ctx->rdi, &ret);
    if (rc == 0 && ctx->rsi) {
        *(uint64_t *)(ctx->mem_base + ctx->rsi) = ret;
    }
    NPC_RET(rc);
}

void shim_sceNpGetSystemClockUsec(GuestContext *ctx) {
    NPC_RET(sceNpGetSystemClockUsec(NPC_GPTR(ctx, ctx->rdi)));
}

void shim_sceNpGetPlatformType(GuestContext *ctx) {
    NPC_RET(sceNpGetPlatformType(NPC_GPTR(ctx, ctx->rdi)));
}

void shim_sceNpIntIsValidOnlineId(GuestContext *ctx) {
    NPC_RET(sceNpIntIsValidOnlineId(NPC_GPTR(ctx, ctx->rdi)));
}

void shim_sceNpGetSdkVersion(GuestContext *ctx) {
    sceNpGetSdkVersion(NPC_GPTR(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceNpCalloutInitCtx(GuestContext *ctx) {
    NPC_RET(sceNpCalloutInitCtx(ctx, ctx->rdi, NPC_GPTR(ctx, ctx->rsi), ctx->rdx, (int32_t)ctx->rcx, ctx->r8));
}

void shim_sceNpCalloutStartOnCtx(GuestContext *ctx) {
    // A 32-bit delay, as shadPS4 truncates it before adding it to the clock.
    NPC_RET(npc_callout_start(ctx, ctx->rdi, ctx->rsi, (int64_t)(uint32_t)ctx->rdx, ctx->rcx, ctx->r8));
}

void shim_sceNpCalloutStartOnCtx64(GuestContext *ctx) {
    NPC_RET(sceNpCalloutStartOnCtx64(ctx, ctx->rdi, ctx->rsi, (int64_t)ctx->rdx, ctx->rcx, ctx->r8));
}

void shim_sceNpCalloutStopOnCtx(GuestContext *ctx) {
    NPC_RET(sceNpCalloutStopOnCtx(ctx, ctx->rdi, ctx->rsi, NPC_GPTR(ctx, ctx->rdx)));
}

void shim_sceNpCalloutTermCtx(GuestContext *ctx) {
    sceNpCalloutTermCtx(ctx, ctx->rdi);
    SHIM_RETURN();
}
