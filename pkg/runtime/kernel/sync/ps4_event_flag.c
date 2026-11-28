// SPDX-License-Identifier: GPL-2.0-or-later
//
// Orbis event flags, after shadPS4 (src/core/libraries/kernel/threads/event_flag.cpp):
//  * sceKernelCreateEventFlag: EINVAL for a NULL handle or name, any option parameter, or an
//    attribute above MULTI | TH_PRIO; ENAMETOOLONG for a name of 32 bytes or more. Without
//    attributes the flag is SINGLE (one waiter at a time; a second is EPERM) and FIFO.
//  * a wait whose condition holds returns the pattern and applies its clear mode at once;
//    otherwise the thread is queued (FIFO, or by thread priority with TH_PRIO) and
//    sceKernelSetEventFlag hands the new pattern to the queued waiters in queue order, applying
//    each one's clear mode before it looks at the next.
//  * a wait that times out returns the current pattern, ETIMEDOUT and sets *timeout to 0; a
//    successful wait leaves *timeout as it was. sceKernelPollEventFlag is a wait with no time,
//    EBUSY instead of ETIMEDOUT.
//  * sceKernelCancelEventFlag sets the pattern and wakes every waiter with ECANCELED and that
//    pattern; sceKernelDeleteEventFlag wakes them with EACCES. An unknown handle is ESRCH.
//  * sceKernelClearEventFlag ANDs the pattern with its argument.
//  * sceKernelOpenEventFlag / sceKernelCloseEventFlag (named flags shared between processes)
//    are stubs in shadPS4 as well.
//
// Deliberate differences: shadPS4 asserts on an attribute or wait mode it does not know
// (UNREACHABLE); here they fail with EINVAL. A waiter that is granted, canceled or deleted is
// taken off the queue at once instead of when it wakes, so a second Set before it runs cannot
// apply its clear mode twice. Flags live in a refcounted handle table (ps4_handle_table.h).
#include "ps4_event_flag.h"
#include "ps4_handle_table.h"
#include "ps4_threading.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENT_FLAGS 0xFFFFu
#define EVF_NAME_MAX 32

typedef enum { EVF_CLEAR_NONE, EVF_CLEAR_ALL, EVF_CLEAR_BITS } EvfClearMode;
typedef enum { EVF_WAITING, EVF_READY, EVF_CANCELED, EVF_DELETED } EvfWaitState;

typedef struct EvfWaiter {
    struct EvfWaiter *next;
    pthread_cond_t cond;
    uint64_t bits;
    bool wait_or;
    EvfClearMode clear_mode;
    uint64_t result;
    int32_t priority;
    EvfWaitState state;
} EvfWaiter;

typedef struct EventFlag {
    Ps4HandleObject h; // first: the handle table hands out &h
    pthread_mutex_t mutex;
    EvfWaiter *waiters; // queue order
    uint64_t bits;
    bool single_thread;
    bool by_priority;
    bool deleted;
    char name[EVF_NAME_MAX];
} EventFlag;

static Ps4HandleTable g_event_flags = PS4_HANDLE_TABLE_INIT(MAX_EVENT_FLAGS);

static void event_flag_free(Ps4HandleObject *obj) {
    EventFlag *f = (EventFlag *)obj;
    pthread_mutex_destroy(&f->mutex);
    free(f);
}

static EventFlag *event_flag_get(OrbisKernelEventFlag id) {
    return id <= UINT32_MAX ? (EventFlag *)ps4_handle_get(&g_event_flags, (uint32_t)id) : NULL;
}

static void event_flag_put(EventFlag *f) {
    ps4_handle_put(&g_event_flags, &f->h);
}

static bool condition_met(const EventFlag *f, const EvfWaiter *w) {
    return w->wait_or ? (f->bits & w->bits) != 0 : (f->bits & w->bits) == w->bits;
}

static void apply_clear(EventFlag *f, const EvfWaiter *w) {
    if (w->clear_mode == EVF_CLEAR_ALL) {
        f->bits = 0;
    } else if (w->clear_mode == EVF_CLEAR_BITS) {
        f->bits &= ~w->bits;
    }
}

// FIFO: at the end. By priority, as shadPS4 orders them: before the first waiter with a lower
// priority value, after the ones with a higher or equal value.
static void add_waiter(EventFlag *f, EvfWaiter *w) {
    EvfWaiter **link = &f->waiters;
    while (*link && (!f->by_priority || (*link)->priority >= w->priority)) {
        link = &(*link)->next;
    }
    w->next = *link;
    *link = w;
}

static void remove_waiter(EventFlag *f, EvfWaiter *w) {
    for (EvfWaiter **link = &f->waiters; *link; link = &(*link)->next) {
        if (*link == w) {
            *link = w->next;
            return;
        }
    }
}

// Wakes and unlinks every waiter with state and result; returns how many there were.
static int32_t release_all(EventFlag *f, EvfWaitState state, uint64_t result) {
    int32_t n = 0;
    for (EvfWaiter *w = f->waiters; w; w = w->next, n++) {
        w->result = result;
        w->state = state;
        pthread_cond_signal(&w->cond);
    }
    f->waiters = NULL;
    return n;
}

int32_t sceKernelCreateEventFlag(OrbisKernelEventFlag *idOut, const char *name, uint32_t attr, uint64_t initPattern,
                                 const OrbisKernelEventFlagOptParam *opt) {
    if (!idOut || !name) {
        return (int32_t)SCE_KERNEL_ERROR_EINVAL;
    }
    if (opt || attr > (SCE_KERNEL_EVF_ATTR_MULTI | SCE_KERNEL_EVF_ATTR_TH_PRIO)) {
        return (int32_t)SCE_KERNEL_ERROR_EINVAL;
    }
    if (strlen(name) >= EVF_NAME_MAX) {
        return (int32_t)SCE_KERNEL_ERROR_ENAMETOOLONG;
    }
    const uint32_t queue = attr & 0xfu, threads = attr & 0xf0u;
    if (queue > SCE_KERNEL_EVF_ATTR_TH_PRIO || (threads != 0 && threads != SCE_KERNEL_EVF_ATTR_SINGLE &&
                                                threads != SCE_KERNEL_EVF_ATTR_MULTI)) {
        return (int32_t)SCE_KERNEL_ERROR_EINVAL;
    }
    EventFlag *f = calloc(1, sizeof(*f));
    if (!f) {
        return (int32_t)(0x80020000u + 12u); // ENOMEM
    }
    f->h.destroy = event_flag_free;
    pthread_mutex_init(&f->mutex, NULL);
    f->bits = initPattern;
    f->single_thread = threads != SCE_KERNEL_EVF_ATTR_MULTI;
    f->by_priority = queue == SCE_KERNEL_EVF_ATTR_TH_PRIO;
    strlcpy(f->name, name, sizeof(f->name));
    const uint32_t id = ps4_handle_insert(&g_event_flags, &f->h);
    if (id == 0) {
        event_flag_free(&f->h);
        return (int32_t)(0x80020000u + 23u); // ENFILE
    }
    *idOut = id;
    return 0;
}

int32_t sceKernelDeleteEventFlag(OrbisKernelEventFlag id) {
    EventFlag *f = id <= UINT32_MAX ? (EventFlag *)ps4_handle_remove(&g_event_flags, (uint32_t)id) : NULL;
    if (!f) {
        return (int32_t)SCE_KERNEL_ERROR_ESRCH;
    }
    pthread_mutex_lock(&f->mutex);
    f->deleted = true;
    release_all(f, EVF_DELETED, 0);
    pthread_mutex_unlock(&f->mutex);
    event_flag_put(f);
    return 0;
}

int32_t sceKernelSetEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern) {
    EventFlag *f = event_flag_get(id);
    if (!f) {
        return (int32_t)SCE_KERNEL_ERROR_ESRCH;
    }
    pthread_mutex_lock(&f->mutex);
    f->bits |= bitPattern;
    EvfWaiter **link = &f->waiters;
    while (*link) {
        EvfWaiter *w = *link;
        if (!condition_met(f, w)) {
            link = &w->next;
            continue;
        }
        w->result = f->bits;
        apply_clear(f, w);
        *link = w->next;
        w->state = EVF_READY;
        pthread_cond_signal(&w->cond);
    }
    pthread_mutex_unlock(&f->mutex);
    event_flag_put(f);
    return 0;
}

int32_t sceKernelClearEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern) {
    EventFlag *f = event_flag_get(id);
    if (!f) {
        return (int32_t)SCE_KERNEL_ERROR_ESRCH;
    }
    pthread_mutex_lock(&f->mutex);
    f->bits &= bitPattern;
    pthread_mutex_unlock(&f->mutex);
    event_flag_put(f);
    return 0;
}

int32_t sceKernelCancelEventFlag(OrbisKernelEventFlag id, uint64_t setPattern, int32_t *pNumWaitThreads) {
    EventFlag *f = event_flag_get(id);
    if (!f) {
        return (int32_t)SCE_KERNEL_ERROR_ESRCH;
    }
    pthread_mutex_lock(&f->mutex);
    f->bits = setPattern;
    const int32_t n = release_all(f, EVF_CANCELED, setPattern);
    if (pNumWaitThreads) {
        *pNumWaitThreads = n;
    }
    pthread_mutex_unlock(&f->mutex);
    event_flag_put(f);
    return 0;
}

// Splits a wait mode; false for one shadPS4 does not accept.
static bool parse_wait_mode(uint32_t waitMode, bool *wait_or, EvfClearMode *clear_mode) {
    switch (waitMode & 0xfu) {
    case SCE_KERNEL_EVF_WAITMODE_AND:
        *wait_or = false;
        break;
    case SCE_KERNEL_EVF_WAITMODE_OR:
        *wait_or = true;
        break;
    default:
        return false;
    }
    switch (waitMode & 0xf0u) {
    case 0:
        *clear_mode = EVF_CLEAR_NONE;
        return true;
    case SCE_KERNEL_EVF_WAITMODE_CLEAR_ALL:
        *clear_mode = EVF_CLEAR_ALL;
        return true;
    case SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT:
        *clear_mode = EVF_CLEAR_BITS;
        return true;
    default:
        return false;
    }
}

// EventFlagInternal::Wait (mutex held). timeout NULL waits without limit.
static int32_t evf_wait_locked(EventFlag *f, uint64_t bits, bool wait_or, EvfClearMode clear_mode, uint64_t *result,
                               OrbisKernelUseconds *timeout) {
    if (f->deleted) {
        return (int32_t)SCE_KERNEL_ERROR_EACCES;
    }
    if (f->single_thread && f->waiters) {
        return (int32_t)SCE_KERNEL_ERROR_EPERM;
    }
    EvfWaiter w = {.bits = bits, .wait_or = wait_or, .clear_mode = clear_mode, .state = EVF_WAITING};
    RecompThread *self = recomp_get_current_thread();
    w.priority = self ? self->attr.prio : 0;
    if (condition_met(f, &w)) {
        if (result) {
            *result = f->bits;
        }
        apply_clear(f, &w);
        return 0;
    }

    pthread_cond_init(&w.cond, NULL);
    add_waiter(f, &w);
    if (!timeout) {
        while (w.state == EVF_WAITING) {
            pthread_cond_wait(&w.cond, &f->mutex);
        }
    } else {
        const uint64_t deadline = ps4_mono_ns() + (uint64_t)*timeout * 1000u;
        while (w.state == EVF_WAITING && ps4_cond_wait_until(&w.cond, &f->mutex, deadline)) {
        }
    }
    pthread_cond_destroy(&w.cond);

    if (w.state == EVF_WAITING) { // timed out
        remove_waiter(f, &w);
        if (result) {
            *result = f->bits;
        }
        *timeout = 0;
        return (int32_t)SCE_KERNEL_ERROR_ETIMEDOUT;
    }
    if (result) {
        *result = w.result;
    }
    switch (w.state) {
    case EVF_CANCELED:
        return (int32_t)SCE_KERNEL_ERROR_ECANCELED;
    case EVF_DELETED:
        return (int32_t)SCE_KERNEL_ERROR_EACCES;
    default:
        return 0;
    }
}

static int32_t evf_wait(OrbisKernelEventFlag id, uint64_t bitPattern, uint32_t waitMode, uint64_t *resultPattern,
                        OrbisKernelUseconds *timeout) {
    EventFlag *f = event_flag_get(id);
    if (!f) {
        return (int32_t)SCE_KERNEL_ERROR_ESRCH;
    }
    bool wait_or = false;
    EvfClearMode clear_mode = EVF_CLEAR_NONE;
    int32_t rc;
    if (bitPattern == 0 || !parse_wait_mode(waitMode, &wait_or, &clear_mode)) {
        rc = (int32_t)SCE_KERNEL_ERROR_EINVAL;
    } else {
        pthread_mutex_lock(&f->mutex);
        rc = evf_wait_locked(f, bitPattern, wait_or, clear_mode, resultPattern, timeout);
        pthread_mutex_unlock(&f->mutex);
    }
    event_flag_put(f);
    return rc;
}

int32_t sceKernelWaitEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern, uint32_t waitMode, uint64_t *resultPattern,
                               OrbisKernelUseconds *timeout) {
    return evf_wait(id, bitPattern, waitMode, resultPattern, timeout);
}

int32_t sceKernelPollEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern, uint32_t waitMode, uint64_t *resultPattern) {
    OrbisKernelUseconds micros = 0;
    const int32_t rc = evf_wait(id, bitPattern, waitMode, resultPattern, &micros);
    return rc == (int32_t)SCE_KERNEL_ERROR_ETIMEDOUT ? (int32_t)SCE_KERNEL_ERROR_EBUSY : rc;
}

static inline void *guest_ptr(GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

// Guest shims
void shim_sceKernelCreateEventFlag(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelCreateEventFlag(guest_ptr(ctx, ctx->rdi), guest_ptr(ctx, ctx->rsi),
                                                           (uint32_t)ctx->rdx, ctx->rcx, guest_ptr(ctx, ctx->r8));
    SHIM_RETURN();
}

void shim_sceKernelDeleteEventFlag(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelDeleteEventFlag(ctx->rdi);
    SHIM_RETURN();
}

// shadPS4 stub (named event flags shared between processes).
void shim_sceKernelOpenEventFlag(GuestContext *ctx) {
    fprintf(stderr, "[libkernel] sceKernelOpenEventFlag (STUBBED)\n");
    ctx->rax = 0;
    SHIM_RETURN();
}

// shadPS4 stub (named event flags shared between processes).
void shim_sceKernelCloseEventFlag(GuestContext *ctx) {
    fprintf(stderr, "[libkernel] sceKernelCloseEventFlag (STUBBED)\n");
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelSetEventFlag(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelSetEventFlag(ctx->rdi, ctx->rsi);
    SHIM_RETURN();
}

void shim_sceKernelClearEventFlag(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelClearEventFlag(ctx->rdi, ctx->rsi);
    SHIM_RETURN();
}

void shim_sceKernelPollEventFlag(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelPollEventFlag(ctx->rdi, ctx->rsi, (uint32_t)ctx->rdx,
                                                         guest_ptr(ctx, ctx->rcx));
    SHIM_RETURN();
}

void shim_sceKernelWaitEventFlag(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelWaitEventFlag(ctx->rdi, ctx->rsi, (uint32_t)ctx->rdx,
                                                         guest_ptr(ctx, ctx->rcx), guest_ptr(ctx, ctx->r8));
    SHIM_RETURN();
}

void shim_sceKernelCancelEventFlag(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelCancelEventFlag(ctx->rdi, ctx->rsi, guest_ptr(ctx, ctx->rdx));
    SHIM_RETURN();
}
