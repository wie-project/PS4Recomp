// SPDX-License-Identifier: GPL-2.0-or-later
//
// POSIX semaphores (sem_*, scePthreadSem*) and Orbis kernel semaphores (sceKernel*Sema), after
// shadPS4 (src/core/libraries/kernel/threads/semaphore.cpp):
//
// POSIX semaphores
//  * a NULL or unknown semaphore fails with EINVAL; sem_init with a NULL sem_t succeeds without
//    creating one, and a value above SEM_VALUE_MAX (0x7FFFFFFF) is EINVAL.
//  * a wait takes a token that is available before it looks at the timeout, so a bad timespec
//    does not fail a wait that would succeed; then tv_nsec outside [0, 1e9) is EINVAL.
//    sem_timedwait sleeps until the absolute CLOCK_REALTIME deadline, sem_reltimedwait_np (and
//    scePthreadSemTimedwait) for a relative number of microseconds; a miss is ETIMEDOUT, and a
//    post that raced the timeout still wins.
//  * sem_post at SEM_VALUE_MAX is EOVERFLOW; a post wakes every waiter (libkernel does) and the
//    ones that lose the race wait again.
//  * scePthreadSem* return ORBIS_KERNEL_ERROR_UNKNOWN + errno; scePthreadSemInit takes no flags.
//
// Kernel semaphores
//  * attr 1 queues waiters in FIFO order, 0 and 2 by thread priority (a lower value first, FIFO
//    among equals).
//  * sceKernelSignalSema adds its count (EINVAL past the maximum) and then hands the tokens to
//    the queued waiters in queue order: each waiter whose count is covered is woken with its
//    tokens taken, the others keep waiting.
//  * sceKernelWaitSema with a timeout of 0 is ETIMEDOUT at once. A timed wait that succeeds
//    lowers *timeout by the time it waited; one that fails sets it to 0.
//  * sceKernelCancelSema wakes every waiter with ECANCELED, reports how many there were and
//    resets the count (setCount, or the initial count when it is negative).
//  * sceKernelDeleteSema wakes every waiter with EACCES. An unknown handle is ESRCH.
//
// Deliberate differences: objects live in refcounted handle tables (ps4_handle_table.h), so a
// semaphore deleted while threads wait on it is freed after they leave, and a call on a deleted
// semaphore fails (EINVAL / ESRCH) instead of touching freed memory; the guest sem_t holds a
// table id, not a host pointer.
#include "ps4_semaphore.h"
#include "ps4_handle_table.h"
#include "ps4_threading.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ORBIS_KERNEL_ERROR_UNKNOWN 0x80020000u
#define ORBIS_KERNEL_ERROR_ESRCH ((int32_t)0x80020003)
#define ORBIS_KERNEL_ERROR_EACCES ((int32_t)0x8002000D)
#define ORBIS_KERNEL_ERROR_EBUSY ((int32_t)0x80020010)
#define ORBIS_KERNEL_ERROR_EINVAL ((int32_t)0x80020016)
#define ORBIS_KERNEL_ERROR_ETIMEDOUT ((int32_t)0x8002003C)
#define ORBIS_KERNEL_ERROR_ECANCELED ((int32_t)0x80020055)

#define ORBIS_KERNEL_SEM_VALUE_MAX 0x7FFFFFFF
#define MAX_SEMAPHORES 0xFFFFu // shadPS4 MAX_ORBIS_SEMAPHORES

static inline int32_t kernel_error(int err) {
    return (int32_t)(ORBIS_KERNEL_ERROR_UNKNOWN + (uint32_t)err);
}

// =============================================================================================
// POSIX semaphores

typedef struct PosixSem {
    Ps4HandleObject h; // first: the handle table hands out &h
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int32_t value;
    bool destroyed;
} PosixSem;

static Ps4HandleTable g_posix_sems = PS4_HANDLE_TABLE_INIT(MAX_SEMAPHORES);

static void posix_sem_free(Ps4HandleObject *obj) {
    PosixSem *s = (PosixSem *)obj;
    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    free(s);
}

// The semaphore a guest sem_t holds (with a reference), or NULL for a NULL sem_t or unknown id.
static PosixSem *posix_sem_get(GuestContext *ctx, uint64_t sem_addr) {
    if (!sem_addr) {
        return NULL;
    }
    const uint64_t id = *(const uint64_t *)(ctx->mem_base + sem_addr);
    return id <= UINT32_MAX ? (PosixSem *)ps4_handle_get(&g_posix_sems, (uint32_t)id) : NULL;
}

static void posix_sem_put(PosixSem *s) {
    ps4_handle_put(&g_posix_sems, &s->h);
}

// posix_sem_init: 0 or the errno.
static int posix_sem_init_impl(GuestContext *ctx, uint64_t sem_addr, uint32_t value) {
    if (value > ORBIS_KERNEL_SEM_VALUE_MAX) {
        return EINVAL;
    }
    if (!sem_addr) {
        return 0;
    }
    PosixSem *s = calloc(1, sizeof(*s));
    if (!s) {
        return ENOMEM;
    }
    s->h.destroy = posix_sem_free;
    s->value = (int32_t)value;
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
    const uint32_t id = ps4_handle_insert(&g_posix_sems, &s->h);
    if (id == 0) {
        posix_sem_free(&s->h);
        return ENOSPC;
    }
    *(uint64_t *)(ctx->mem_base + sem_addr) = id;
    return 0;
}

static int posix_sem_destroy_impl(GuestContext *ctx, uint64_t sem_addr) {
    if (!sem_addr) {
        return EINVAL;
    }
    const uint64_t id = *(const uint64_t *)(ctx->mem_base + sem_addr);
    PosixSem *s = id <= UINT32_MAX ? (PosixSem *)ps4_handle_remove(&g_posix_sems, (uint32_t)id) : NULL;
    if (!s) {
        return EINVAL;
    }
    pthread_mutex_lock(&s->mutex);
    s->destroyed = true;
    pthread_cond_broadcast(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    posix_sem_put(s);
    *(uint64_t *)(ctx->mem_base + sem_addr) = 0;
    return 0;
}

// Takes a token if there is one (mutex held).
static bool posix_sem_try_acquire(PosixSem *s) {
    if (s->value > 0) {
        s->value--;
        return true;
    }
    return false;
}

// Deadlines more than this far away wait without limit: they would overflow the host wait.
#define FAR_FUTURE_SEC (30LL * 365 * 86400)

typedef enum { WAIT_FOREVER, WAIT_ABSOLUTE, WAIT_RELATIVE } PosixWaitKind;

static void posix_sem_put_cb(void *s) { posix_sem_put((PosixSem *)s); }

// PthreadSemWait: 0 or the errno. A cancellation point: a pending cancel ends the thread before
// it sleeps or when it wakes without a token (dropping the caller's reference to s).
static int posix_sem_wait_impl(PosixSem *s, PosixWaitKind kind, const int64_t *abstime, uint32_t usec) {
    RecompThread *self = recomp_get_current_thread();
    PthreadCleanupEntry put;
    ps4_thread_cleanup_push_host(&put, posix_sem_put_cb, s);
    pthread_mutex_lock(&s->mutex);
    uint64_t reltime_deadline = 0;
    bool first_sleep = true;
    int err = 0;
    for (;;) {
        if (s->destroyed) {
            err = EINVAL;
            break;
        }
        // libkernel checks and decrements the value before validating a timeout.
        if (posix_sem_try_acquire(s)) {
            break;
        }
        if (kind == WAIT_ABSOLUTE && (abstime[1] < 0 || abstime[1] >= 1000000000LL)) {
            err = EINVAL;
            break;
        }
        if (ps4_thread_cancel_wait_begin(self, &s->mutex, &s->cond)) {
            pthread_mutex_unlock(&s->mutex);
            ps4_thread_testcancel();
            pthread_mutex_lock(&s->mutex);
            continue;
        }
        bool woke = true;
        if (kind == WAIT_RELATIVE) {
            if (first_sleep) {
                reltime_deadline = ps4_mono_ns() + (uint64_t)usec * 1000u;
                first_sleep = false;
            }
            woke = ps4_cond_wait_until(&s->cond, &s->mutex, reltime_deadline);
        } else if (kind == WAIT_ABSOLUTE) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (abstime[0] < (int64_t)now.tv_sec + FAR_FUTURE_SEC) {
                const struct timespec deadline = {.tv_sec = (time_t)abstime[0], .tv_nsec = (long)abstime[1]};
                woke = pthread_cond_timedwait(&s->cond, &s->mutex, &deadline) != ETIMEDOUT;
            } else {
                pthread_cond_wait(&s->cond, &s->mutex);
            }
        } else {
            pthread_cond_wait(&s->cond, &s->mutex);
        }
        ps4_thread_cancel_wait_end(self);
        // A post racing the timeout (or a cancel) wins if its token is still available.
        if (!s->destroyed && posix_sem_try_acquire(s)) {
            break;
        }
        pthread_mutex_unlock(&s->mutex);
        ps4_thread_testcancel();
        pthread_mutex_lock(&s->mutex);
        if (!woke) {
            if (!s->destroyed && posix_sem_try_acquire(s)) {
                break;
            }
            err = s->destroyed ? EINVAL : ETIMEDOUT;
            break;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    ps4_thread_cleanup_pop_host(&put);
    return err;
}

static int posix_sem_post_impl(PosixSem *s) {
    pthread_mutex_lock(&s->mutex);
    int err = 0;
    if (s->value == ORBIS_KERNEL_SEM_VALUE_MAX) {
        err = EOVERFLOW;
    } else {
        s->value++;
        // libkernel wakes every waiter; the ones that do not get the token wait again.
        pthread_cond_broadcast(&s->cond);
    }
    pthread_mutex_unlock(&s->mutex);
    return err;
}

// A POSIX operation on the guest sem_t at sem_addr: EINVAL for a NULL or unknown semaphore.
typedef int (*PosixSemOp)(GuestContext *ctx, PosixSem *s);

static int with_posix_sem(GuestContext *ctx, uint64_t sem_addr, PosixSemOp op) {
    PosixSem *s = posix_sem_get(ctx, sem_addr);
    if (!s) {
        return EINVAL;
    }
    const int err = op(ctx, s);
    posix_sem_put(s);
    return err;
}

static int op_wait(GuestContext *ctx, PosixSem *s) {
    (void)ctx;
    return posix_sem_wait_impl(s, WAIT_FOREVER, NULL, 0);
}

static int op_timedwait(GuestContext *ctx, PosixSem *s) {
    const int64_t *abstime = ctx->rsi ? (const int64_t *)(ctx->mem_base + ctx->rsi) : NULL;
    return posix_sem_wait_impl(s, abstime ? WAIT_ABSOLUTE : WAIT_FOREVER, abstime, 0);
}

static int op_reltimedwait(GuestContext *ctx, PosixSem *s) {
    return posix_sem_wait_impl(s, WAIT_RELATIVE, NULL, (uint32_t)ctx->rsi);
}

static int op_trywait(GuestContext *ctx, PosixSem *s) {
    (void)ctx;
    pthread_mutex_lock(&s->mutex);
    const bool ok = !s->destroyed && posix_sem_try_acquire(s);
    pthread_mutex_unlock(&s->mutex);
    return ok ? 0 : EAGAIN;
}

static int op_post(GuestContext *ctx, PosixSem *s) {
    (void)ctx;
    return posix_sem_post_impl(s);
}

static int op_getvalue(GuestContext *ctx, PosixSem *s) {
    pthread_mutex_lock(&s->mutex);
    const int32_t value = s->value;
    pthread_mutex_unlock(&s->mutex);
    if (ctx->rsi) {
        *(int32_t *)(ctx->mem_base + ctx->rsi) = value;
    }
    return 0;
}

// Posix result: 0, or -1 with the guest errno.
static void posix_return(GuestContext *ctx, int err) {
    if (err != 0) {
        set_guest_errno(ctx, err);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
}

// scePthreadSem result: ORBIS_OK, or the kernel error of the errno (which stays set).
static void kernel_return(GuestContext *ctx, int err) {
    if (err != 0) {
        set_guest_errno(ctx, err);
        ctx->rax = (uint64_t)(int64_t)kernel_error(err);
    } else {
        ctx->rax = 0;
    }
}

// int sem_init(sem_t *sem, int pshared, unsigned int value)
void shim_sem_init(GuestContext *ctx) {
    posix_return(ctx, posix_sem_init_impl(ctx, ctx->rdi, (uint32_t)ctx->rdx));
    SHIM_RETURN();
}

// int sem_destroy(sem_t *sem)
void shim_sem_destroy(GuestContext *ctx) {
    posix_return(ctx, posix_sem_destroy_impl(ctx, ctx->rdi));
    SHIM_RETURN();
}

// int sem_wait(sem_t *sem)
void shim_sem_wait(GuestContext *ctx) {
    posix_return(ctx, with_posix_sem(ctx, ctx->rdi, op_wait));
    SHIM_RETURN();
}

// int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout): a NULL abs_timeout waits
// without limit.
void shim_sem_timedwait(GuestContext *ctx) {
    posix_return(ctx, with_posix_sem(ctx, ctx->rdi, op_timedwait));
    SHIM_RETURN();
}

// int sem_reltimedwait_np(sem_t *sem, useconds_t usec)
void shim_sem_reltimedwait_np(GuestContext *ctx) {
    posix_return(ctx, with_posix_sem(ctx, ctx->rdi, op_reltimedwait));
    SHIM_RETURN();
}

// int sem_trywait(sem_t *sem)
void shim_sem_trywait(GuestContext *ctx) {
    posix_return(ctx, with_posix_sem(ctx, ctx->rdi, op_trywait));
    SHIM_RETURN();
}

// int sem_post(sem_t *sem)
void shim_sem_post(GuestContext *ctx) {
    posix_return(ctx, with_posix_sem(ctx, ctx->rdi, op_post));
    SHIM_RETURN();
}

// int sem_getvalue(sem_t *sem, int *sval): sval is optional.
void shim_sem_getvalue(GuestContext *ctx) {
    posix_return(ctx, with_posix_sem(ctx, ctx->rdi, op_getvalue));
    SHIM_RETURN();
}

// int sem_setname(sem_t *sem, const char *name): names are not kept; only a NULL name fails.
void shim_sem_setname(GuestContext *ctx) {
    posix_return(ctx, ctx->rsi ? 0 : EINVAL);
    SHIM_RETURN();
}

// int scePthreadSemInit(ScePthreadSem *sem, int flag, unsigned int value, const char *name)
void shim_scePthreadSemInit(GuestContext *ctx) {
    if ((int32_t)ctx->rsi != 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_KERNEL_ERROR_EINVAL;
        SHIM_RETURN();
    }
    kernel_return(ctx, posix_sem_init_impl(ctx, ctx->rdi, (uint32_t)ctx->rdx));
    SHIM_RETURN();
}

void shim_scePthreadSemDestroy(GuestContext *ctx) {
    kernel_return(ctx, posix_sem_destroy_impl(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_scePthreadSemWait(GuestContext *ctx) {
    kernel_return(ctx, with_posix_sem(ctx, ctx->rdi, op_wait));
    SHIM_RETURN();
}

void shim_scePthreadSemTrywait(GuestContext *ctx) {
    kernel_return(ctx, with_posix_sem(ctx, ctx->rdi, op_trywait));
    SHIM_RETURN();
}

// int scePthreadSemTimedwait(ScePthreadSem *sem, SceKernelUseconds usec): sem_reltimedwait_np.
void shim_scePthreadSemTimedwait(GuestContext *ctx) {
    kernel_return(ctx, with_posix_sem(ctx, ctx->rdi, op_reltimedwait));
    SHIM_RETURN();
}

void shim_scePthreadSemPost(GuestContext *ctx) {
    kernel_return(ctx, with_posix_sem(ctx, ctx->rdi, op_post));
    SHIM_RETURN();
}

void shim_scePthreadSemGetvalue(GuestContext *ctx) {
    kernel_return(ctx, with_posix_sem(ctx, ctx->rdi, op_getvalue));
    SHIM_RETURN();
}

// =============================================================================================
// Orbis kernel semaphores

typedef enum { SEMA_WAITING, SEMA_SIGNALED, SEMA_DELETED, SEMA_CANCELED } SemaWaitState;

typedef struct SemaWaiter {
    struct SemaWaiter *next;
    pthread_cond_t cond;
    int32_t need_count;
    int32_t priority;
    SemaWaitState state;
} SemaWaiter;

typedef struct KernelSema {
    Ps4HandleObject h; // first: the handle table hands out &h
    pthread_mutex_t mutex;
    SemaWaiter *waiters; // queue order
    int32_t token_count;
    int32_t max_count;
    int32_t init_count;
    bool is_fifo;
    bool deleted;
    char name[32];
} KernelSema;

static Ps4HandleTable g_kernel_semas = PS4_HANDLE_TABLE_INIT(MAX_SEMAPHORES);

static void kernel_sema_free(Ps4HandleObject *obj) {
    KernelSema *s = (KernelSema *)obj;
    pthread_mutex_destroy(&s->mutex);
    free(s);
}

static KernelSema *kernel_sema_get(OrbisKernelSema sem) {
    return sem > 0 ? (KernelSema *)ps4_handle_get(&g_kernel_semas, (uint32_t)sem) : NULL;
}

static void kernel_sema_put(KernelSema *s) {
    ps4_handle_put(&g_kernel_semas, &s->h);
}

// The calling guest thread's priority (0 for a host thread).
static int32_t current_priority(void) {
    RecompThread *t = recomp_get_current_thread();
    return t ? t->attr.prio : 0;
}

// FIFO: at the end. By priority: before the first waiter with a higher value (FIFO among equals).
static void sema_add_waiter(KernelSema *s, SemaWaiter *w) {
    SemaWaiter **link = &s->waiters;
    while (*link && (s->is_fifo || (*link)->priority <= w->priority)) {
        link = &(*link)->next;
    }
    w->next = *link;
    *link = w;
}

static void sema_remove_waiter(KernelSema *s, SemaWaiter *w) {
    for (SemaWaiter **link = &s->waiters; *link; link = &(*link)->next) {
        if (*link == w) {
            *link = w->next;
            return;
        }
    }
}

// Wakes and unlinks every waiter with state.
static int32_t sema_release_all(KernelSema *s, SemaWaitState state) {
    int32_t n = 0;
    for (SemaWaiter *w = s->waiters; w; w = w->next, n++) {
        w->state = state;
        pthread_cond_signal(&w->cond);
    }
    s->waiters = NULL;
    return n;
}

int sceKernelCreateSema(OrbisKernelSema *sem, const char *pName, uint32_t attr, int32_t initCount, int32_t maxCount,
                        const void *pOptParam) {
    (void)pOptParam;
    if (!sem || !pName || attr > 2 || initCount < 0 || maxCount <= 0 || initCount > maxCount) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    KernelSema *s = calloc(1, sizeof(*s));
    if (!s) {
        return kernel_error(ENOMEM);
    }
    s->h.destroy = kernel_sema_free;
    pthread_mutex_init(&s->mutex, NULL);
    s->token_count = initCount;
    s->max_count = maxCount;
    s->init_count = initCount;
    s->is_fifo = attr == 1;
    strlcpy(s->name, pName, sizeof(s->name));
    const uint32_t id = ps4_handle_insert(&g_kernel_semas, &s->h);
    if (id == 0) {
        kernel_sema_free(&s->h);
        return kernel_error(ENFILE);
    }
    *sem = (OrbisKernelSema)id;
    return 0;
}

int sceKernelDeleteSema(OrbisKernelSema sem) {
    KernelSema *s = sem > 0 ? (KernelSema *)ps4_handle_remove(&g_kernel_semas, (uint32_t)sem) : NULL;
    if (!s) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    pthread_mutex_lock(&s->mutex);
    s->deleted = true;
    sema_release_all(s, SEMA_DELETED);
    pthread_mutex_unlock(&s->mutex);
    kernel_sema_put(s);
    return 0;
}

// OrbisSem::Wait (mutex held).
static int sema_wait_locked(KernelSema *s, bool can_block, int32_t need_count, uint32_t *timeout) {
    if (s->deleted) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    if (s->token_count >= need_count) {
        s->token_count -= need_count;
        return 0;
    }
    if (!can_block) {
        return ORBIS_KERNEL_ERROR_EBUSY;
    }
    if (timeout && *timeout == 0) {
        return ORBIS_KERNEL_ERROR_ETIMEDOUT;
    }

    SemaWaiter w = {
        .need_count = need_count,
        .priority = s->is_fifo ? 0 : current_priority(),
        .state = SEMA_WAITING,
    };
    pthread_cond_init(&w.cond, NULL);
    sema_add_waiter(s, &w);
    if (!timeout) {
        while (w.state == SEMA_WAITING) {
            pthread_cond_wait(&w.cond, &s->mutex);
        }
    } else {
        const uint64_t start = ps4_mono_ns();
        const uint64_t deadline = start + (uint64_t)*timeout * 1000u;
        while (w.state == SEMA_WAITING && ps4_cond_wait_until(&w.cond, &s->mutex, deadline)) {
        }
        if (w.state == SEMA_SIGNALED) {
            const uint64_t waited = (ps4_mono_ns() - start) / 1000u;
            *timeout = waited >= *timeout ? 0 : *timeout - (uint32_t)waited;
        } else {
            *timeout = 0;
        }
    }
    if (w.state == SEMA_WAITING) { // timed out
        sema_remove_waiter(s, &w);
    }
    pthread_cond_destroy(&w.cond);
    switch (w.state) {
    case SEMA_SIGNALED:
        return 0;
    case SEMA_DELETED:
        return ORBIS_KERNEL_ERROR_EACCES;
    case SEMA_CANCELED:
        return ORBIS_KERNEL_ERROR_ECANCELED;
    default:
        return ORBIS_KERNEL_ERROR_ETIMEDOUT;
    }
}

static int sema_wait(OrbisKernelSema sem, bool can_block, int32_t needCount, uint32_t *pTimeout) {
    KernelSema *s = kernel_sema_get(sem);
    if (!s) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    pthread_mutex_lock(&s->mutex);
    const int rc = sema_wait_locked(s, can_block, needCount, pTimeout);
    pthread_mutex_unlock(&s->mutex);
    kernel_sema_put(s);
    return rc;
}

int sceKernelWaitSema(OrbisKernelSema sem, int32_t needCount, uint32_t *pTimeout) {
    return sema_wait(sem, true, needCount, pTimeout);
}

int sceKernelPollSema(OrbisKernelSema sem, int32_t needCount) {
    return sema_wait(sem, false, needCount, NULL);
}

int sceKernelSignalSema(OrbisKernelSema sem, int32_t signalCount) {
    KernelSema *s = kernel_sema_get(sem);
    if (!s) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    int rc = 0;
    pthread_mutex_lock(&s->mutex);
    if (s->deleted) {
        rc = ORBIS_KERNEL_ERROR_ESRCH;
    } else if ((int64_t)s->token_count + signalCount > s->max_count) {
        rc = ORBIS_KERNEL_ERROR_EINVAL;
    } else {
        s->token_count += signalCount;
        // Hand the tokens to the waiters in queue order.
        SemaWaiter **link = &s->waiters;
        while (*link) {
            SemaWaiter *w = *link;
            if (w->need_count > s->token_count) {
                link = &w->next;
                continue;
            }
            *link = w->next;
            s->token_count -= w->need_count;
            w->state = SEMA_SIGNALED;
            pthread_cond_signal(&w->cond);
        }
    }
    pthread_mutex_unlock(&s->mutex);
    kernel_sema_put(s);
    return rc;
}

int sceKernelCancelSema(OrbisKernelSema sem, int32_t setCount, int32_t *pNumWaitThreads) {
    KernelSema *s = kernel_sema_get(sem);
    if (!s) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    int rc = 0;
    pthread_mutex_lock(&s->mutex);
    if (s->deleted) {
        rc = ORBIS_KERNEL_ERROR_ESRCH;
    } else {
        const int32_t n = sema_release_all(s, SEMA_CANCELED);
        if (pNumWaitThreads) {
            *pNumWaitThreads = n;
        }
        s->token_count = setCount < 0 ? s->init_count : setCount;
    }
    pthread_mutex_unlock(&s->mutex);
    kernel_sema_put(s);
    return rc;
}

// Guest shims for Kernel Semaphores
void shim_sceKernelCreateSema(GuestContext *ctx) {
    uint64_t semGuest = ctx->rdi;
    uint64_t pNameGuest = ctx->rsi;
    uint32_t attr = (uint32_t)ctx->rdx;
    int32_t initCount = (int32_t)ctx->rcx;
    int32_t maxCount = (int32_t)ctx->r8;
    uint64_t optGuest = ctx->r9;

    const char *pName = pNameGuest ? (const char *)(ctx->mem_base + pNameGuest) : NULL;
    const void *opt = optGuest ? (const void *)(ctx->mem_base + optGuest) : NULL;

    OrbisKernelSema sem = 0;
    int rc = sceKernelCreateSema(semGuest ? &sem : NULL, pName, attr, initCount, maxCount, opt);
    if (rc == 0) {
        *(int32_t *)(ctx->mem_base + semGuest) = sem;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelDeleteSema(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelDeleteSema((OrbisKernelSema)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceKernelWaitSema(GuestContext *ctx) {
    uint32_t *pTimeout = ctx->rdx ? (uint32_t *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceKernelWaitSema((OrbisKernelSema)ctx->rdi, (int32_t)ctx->rsi, pTimeout);
    SHIM_RETURN();
}

void shim_sceKernelSignalSema(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelSignalSema((OrbisKernelSema)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceKernelPollSema(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelPollSema((OrbisKernelSema)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceKernelCancelSema(GuestContext *ctx) {
    int32_t *pNumWait = ctx->rdx ? (int32_t *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceKernelCancelSema((OrbisKernelSema)ctx->rdi, (int32_t)ctx->rsi, pNumWait);
    SHIM_RETURN();
}
