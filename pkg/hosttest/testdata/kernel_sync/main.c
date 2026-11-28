// SPDX-License-Identifier: GPL-2.0-or-later

// POSIX semaphores, Orbis kernel semaphores and event flags against shadPS4's
// kernel/threads/semaphore.cpp and event_flag.cpp: argument checks and error codes, timeouts,
// token hand-off to queued waiters in FIFO / priority order, cancel and delete wake-ups, and the
// event flag clear modes applied waiter by waiter. Waiting threads are real host threads, each
// with its own guest context and TCB in one test memory block.
#include "ps4_event_flag.h"
#include "ps4_semaphore.h"
#include "ps4_threading.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

// Each thread's guest priority (what ps4_threading's RecompThread.attr.prio holds).
static _Thread_local RecompThread g_self;
RecompThread *recomp_get_current_thread(void) { return &g_self; }

// The cancellation points of ps4_semaphore.c, for threads that are never cancelled
// (kernel_threads tests cancellation with ps4_threading.c itself).
bool ps4_thread_cancel_wait_begin(RecompThread *t, pthread_mutex_t *mx, pthread_cond_t *cv) {
    (void)t, (void)mx, (void)cv;
    return false;
}
void ps4_thread_cancel_wait_end(RecompThread *t) { (void)t; }
void ps4_thread_testcancel(void) {}
void ps4_thread_cleanup_push_host(PthreadCleanupEntry *e, void (*fn)(void *), void *arg) { (void)e, (void)fn, (void)arg; }
void ps4_thread_cleanup_pop_host(PthreadCleanupEntry *e) { (void)e; }

#define MEM_SIZE 0x400000ull
#define SLOT_SIZE 0x10000ull // per thread: stack, TCB and data
#define NAME 0x3F0000ull

#define E_EPERM ((int32_t)0x80020001)
#define E_ESRCH ((int32_t)0x80020003)
#define E_EACCES ((int32_t)0x8002000D)
#define E_EBUSY ((int32_t)0x80020010)
#define E_EINVAL ((int32_t)0x80020016)
#define E_EAGAIN ((int32_t)0x80020023)
#define E_ETIMEDOUT ((int32_t)0x8002003C)
#define E_ENAMETOOLONG ((int32_t)0x8002003F)
#define E_ECANCELED ((int32_t)0x80020055)

static uint8_t *mem;

// A guest thread: slot n of the memory block holds its stack (top at +0x8000), its TCB
// (+0x8000) and its data (+0xC000).
typedef struct {
    GuestContext ctx;
    uint64_t base;
} Guest;

static void guest_init(Guest *g, int slot) {
    memset(g, 0, sizeof(*g));
    g->base = (uint64_t)(slot + 1) * SLOT_SIZE;
    g->ctx.mem_base = mem;
    g->ctx.mem_size = MEM_SIZE;
    g->ctx.fs_base = g->base + 0x8000;
}

static uint64_t data(Guest *g, uint64_t off) { return g->base + 0xC000 + off; }
static int guest_errno(Guest *g) { return *(int *)(mem + g->ctx.fs_base + RECOMP_TCB_ERRNO_SLOT); }

static int64_t call(Guest *g, void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2) {
    *(int *)(mem + g->ctx.fs_base + RECOMP_TCB_ERRNO_SLOT) = 0;
    g->ctx.rsp = g->base + 0x8000 - 8;
    g->ctx.rdi = a0, g->ctx.rsi = a1, g->ctx.rdx = a2;
    shim(&g->ctx);
    CHECK(g->ctx.rsp == g->base + 0x8000);
    return (int64_t)g->ctx.rax;
}

static int32_t kcall(Guest *g, void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2) {
    return (int32_t)call(g, shim, a0, a1, a2);
}

static double mono_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void setup(void) {
    mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(mem != MAP_FAILED);
}

// ---------------------------------------------------------------------------------------------
// POSIX semaphores

static void test_posix_sem(void) {
    Guest g;
    guest_init(&g, 0);
    const uint64_t sem = data(&g, 0), sval = data(&g, 8), ts = data(&g, 16);

    // sem_init: a NULL sem_t succeeds without a semaphore; values above SEM_VALUE_MAX fail.
    CHECK(call(&g, shim_sem_init, 0, 0, 1) == 0);
    CHECK(call(&g, shim_sem_init, sem, 0, 0x80000000u) == -1 && guest_errno(&g) == EINVAL);
    CHECK(call(&g, shim_sem_init, sem, 0, 1) == 0);
    CHECK(call(&g, shim_sem_getvalue, sem, sval, 0) == 0 && *(int32_t *)(mem + sval) == 1);
    CHECK(call(&g, shim_sem_getvalue, sem, 0, 0) == 0); // sval is optional

    CHECK(call(&g, shim_sem_trywait, sem, 0, 0) == 0);
    CHECK(call(&g, shim_sem_trywait, sem, 0, 0) == -1 && guest_errno(&g) == EAGAIN);
    CHECK(call(&g, shim_sem_post, sem, 0, 0) == 0);

    // A token is taken before the timespec is looked at.
    int64_t *t = (int64_t *)(mem + ts);
    t[0] = 0, t[1] = -1;
    CHECK(call(&g, shim_sem_timedwait, sem, ts, 0) == 0);
    CHECK(call(&g, shim_sem_timedwait, sem, ts, 0) == -1 && guest_errno(&g) == EINVAL);
    // A deadline in the past times out; so does a relative wait, after its time.
    t[0] = 1, t[1] = 0;
    CHECK(call(&g, shim_sem_timedwait, sem, ts, 0) == -1 && guest_errno(&g) == ETIMEDOUT);
    double t0 = mono_s();
    CHECK(call(&g, shim_sem_reltimedwait_np, sem, 20000, 0) == -1 && guest_errno(&g) == ETIMEDOUT);
    CHECK(mono_s() - t0 >= 0.02);

    // sem_post at SEM_VALUE_MAX overflows.
    const uint64_t big = data(&g, 32);
    CHECK(call(&g, shim_sem_init, big, 0, 0x7FFFFFFF) == 0);
    CHECK(call(&g, shim_sem_post, big, 0, 0) == -1 && guest_errno(&g) == EOVERFLOW);
    CHECK(call(&g, shim_sem_destroy, big, 0, 0) == 0);

    // sem_setname only checks the name.
    CHECK(call(&g, shim_sem_setname, sem, 0, 0) == -1 && guest_errno(&g) == EINVAL);
    CHECK(call(&g, shim_sem_setname, sem, NAME, 0) == 0);

    // A destroyed semaphore is gone: its sem_t is cleared and every call is EINVAL.
    CHECK(call(&g, shim_sem_destroy, sem, 0, 0) == 0);
    CHECK(*(uint64_t *)(mem + sem) == 0);
    CHECK(call(&g, shim_sem_destroy, sem, 0, 0) == -1 && guest_errno(&g) == EINVAL);
    CHECK(call(&g, shim_sem_post, sem, 0, 0) == -1 && guest_errno(&g) == EINVAL);
    CHECK(call(&g, shim_sem_wait, 0, 0, 0) == -1 && guest_errno(&g) == EINVAL);

    // scePthreadSem: kernel errors from the same rules.
    CHECK(kcall(&g, shim_scePthreadSemInit, sem, 1, 0) == E_EINVAL);
    CHECK(kcall(&g, shim_scePthreadSemInit, sem, 0, 0) == 0);
    CHECK(kcall(&g, shim_scePthreadSemTrywait, sem, 0, 0) == E_EAGAIN);
    CHECK(kcall(&g, shim_scePthreadSemTimedwait, sem, 5000, 0) == E_ETIMEDOUT);
    CHECK(kcall(&g, shim_scePthreadSemPost, sem, 0, 0) == 0);
    CHECK(kcall(&g, shim_scePthreadSemGetvalue, sem, sval, 0) == 0 && *(int32_t *)(mem + sval) == 1);
    CHECK(kcall(&g, shim_scePthreadSemWait, sem, 0, 0) == 0);
    CHECK(kcall(&g, shim_scePthreadSemDestroy, sem, 0, 0) == 0);
    CHECK(kcall(&g, shim_scePthreadSemWait, sem, 0, 0) == E_EINVAL);
    printf("PASS posix-sem\n");
}

typedef struct {
    Guest g;
    uint64_t sem;
    void (*shim)(GuestContext *);
    uint64_t arg;
    atomic_int started;
    int64_t result;
    int err;
} PosixWaiter;

static void *posix_waiter(void *p) {
    PosixWaiter *w = p;
    atomic_store(&w->started, 1);
    w->result = call(&w->g, w->shim, w->sem, w->arg, 0);
    w->err = guest_errno(&w->g);
    return NULL;
}

static void test_posix_sem_threads(void) {
    Guest g;
    guest_init(&g, 0);
    const uint64_t sem = data(&g, 0);
    CHECK(call(&g, shim_sem_init, sem, 0, 0) == 0);

    // Two waiters, one post: one of them returns, the other waits for the second post.
    PosixWaiter w[2];
    pthread_t th[2];
    for (int i = 0; i < 2; i++) {
        memset(&w[i], 0, sizeof(w[i]));
        guest_init(&w[i].g, 1 + i);
        w[i].sem = sem;
        w[i].shim = shim_sem_wait;
        CHECK(pthread_create(&th[i], NULL, posix_waiter, &w[i]) == 0);
    }
    sleep_ms(30);
    CHECK(call(&g, shim_sem_post, sem, 0, 0) == 0);
    sleep_ms(30);
    CHECK(call(&g, shim_sem_post, sem, 0, 0) == 0);
    for (int i = 0; i < 2; i++) {
        pthread_join(th[i], NULL);
        CHECK(w[i].result == 0);
    }

    // A relative wait that a post ends early.
    PosixWaiter rw;
    memset(&rw, 0, sizeof(rw));
    guest_init(&rw.g, 1);
    rw.sem = sem;
    rw.shim = shim_sem_reltimedwait_np;
    rw.arg = 5000000;
    const double t0 = mono_s();
    CHECK(pthread_create(&th[0], NULL, posix_waiter, &rw) == 0);
    sleep_ms(30);
    CHECK(call(&g, shim_sem_post, sem, 0, 0) == 0);
    pthread_join(th[0], NULL);
    CHECK(rw.result == 0 && mono_s() - t0 < 1.0);

    // Destroying a semaphore wakes its waiter with EINVAL.
    PosixWaiter dw;
    memset(&dw, 0, sizeof(dw));
    guest_init(&dw.g, 1);
    dw.sem = sem;
    dw.shim = shim_sem_wait;
    CHECK(pthread_create(&th[0], NULL, posix_waiter, &dw) == 0);
    sleep_ms(30);
    CHECK(call(&g, shim_sem_destroy, sem, 0, 0) == 0);
    pthread_join(th[0], NULL);
    CHECK(dw.result == -1 && dw.err == EINVAL);
    printf("PASS posix-sem-threads\n");
}

// ---------------------------------------------------------------------------------------------
// Kernel semaphores

typedef struct {
    OrbisKernelSema sem;
    int32_t need;
    int32_t prio;
    uint32_t timeout; // 0xFFFFFFFF: no timeout
    int32_t result;
    uint32_t timeout_left;
    double woke_at;
    atomic_int started;
} SemaWaiter;

static void *sema_waiter(void *p) {
    SemaWaiter *w = p;
    g_self.attr.prio = w->prio;
    atomic_store(&w->started, 1);
    uint32_t timeout = w->timeout;
    w->result = sceKernelWaitSema(w->sem, w->need, w->timeout == 0xFFFFFFFFu ? NULL : &timeout);
    w->timeout_left = timeout;
    w->woke_at = mono_s();
    return NULL;
}

static void start_sema_waiter(pthread_t *th, SemaWaiter *w, OrbisKernelSema sem, int32_t need, int32_t prio,
                              uint32_t timeout) {
    memset(w, 0, sizeof(*w));
    w->sem = sem, w->need = need, w->prio = prio, w->timeout = timeout;
    CHECK(pthread_create(th, NULL, sema_waiter, w) == 0);
    while (!atomic_load(&w->started)) {
        sleep_ms(1);
    }
    sleep_ms(20); // let it queue
}

static void test_kernel_sema(void) {
    OrbisKernelSema s = 0;
    CHECK(sceKernelCreateSema(&s, NULL, 1, 0, 1, NULL) == E_EINVAL);
    CHECK(sceKernelCreateSema(&s, "s", 3, 0, 1, NULL) == E_EINVAL);
    CHECK(sceKernelCreateSema(&s, "s", 1, -1, 1, NULL) == E_EINVAL);
    CHECK(sceKernelCreateSema(&s, "s", 1, 0, 0, NULL) == E_EINVAL);
    CHECK(sceKernelCreateSema(&s, "s", 1, 2, 1, NULL) == E_EINVAL);
    CHECK(sceKernelCreateSema(&s, "s", 1, 1, 3, NULL) == 0 && s > 0);

    CHECK(sceKernelPollSema(s, 1) == 0);
    CHECK(sceKernelPollSema(s, 1) == E_EBUSY);
    uint32_t zero = 0;
    CHECK(sceKernelWaitSema(s, 1, &zero) == E_ETIMEDOUT);
    CHECK(sceKernelSignalSema(s, 4) == E_EINVAL); // past the maximum
    CHECK(sceKernelSignalSema(s, 3) == 0);
    CHECK(sceKernelPollSema(s, 3) == 0);

    // A timed wait that fails sets *timeout to 0.
    uint32_t timeout = 20000;
    const double t0 = mono_s();
    CHECK(sceKernelWaitSema(s, 1, &timeout) == E_ETIMEDOUT);
    CHECK(timeout == 0 && mono_s() - t0 >= 0.02);

    // FIFO hand-off: A needs 2, B needs 1. Signal 1 skips A and wakes B; signal 2 wakes A.
    pthread_t ta, tb;
    SemaWaiter a, b;
    start_sema_waiter(&ta, &a, s, 2, 0, 5000000);
    start_sema_waiter(&tb, &b, s, 1, 0, 0xFFFFFFFFu);
    CHECK(sceKernelSignalSema(s, 1) == 0);
    pthread_join(tb, NULL);
    CHECK(b.result == 0);
    CHECK(sceKernelPollSema(s, 1) == E_EBUSY); // B took the token
    CHECK(sceKernelSignalSema(s, 2) == 0);
    pthread_join(ta, NULL);
    CHECK(a.result == 0);
    CHECK(a.timeout_left > 0 && a.timeout_left < 5000000); // lowered by the time waited
    CHECK(sceKernelPollSema(s, 1) == E_EBUSY);

    // Cancel: every waiter gets ECANCELED, the count is reset (negative: the initial count).
    SemaWaiter c1, c2;
    pthread_t t1, t2;
    start_sema_waiter(&t1, &c1, s, 1, 0, 0xFFFFFFFFu);
    start_sema_waiter(&t2, &c2, s, 1, 0, 5000000);
    int32_t waiting = -1;
    CHECK(sceKernelCancelSema(s, -1, &waiting) == 0 && waiting == 2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    CHECK(c1.result == E_ECANCELED && c2.result == E_ECANCELED && c2.timeout_left == 0);
    CHECK(sceKernelPollSema(s, 1) == 0 && sceKernelPollSema(s, 1) == E_EBUSY); // initial count 1
    CHECK(sceKernelCancelSema(s, 3, NULL) == 0 && sceKernelPollSema(s, 3) == 0);

    // Delete: the waiter gets EACCES, the handle is gone.
    SemaWaiter d;
    pthread_t td;
    start_sema_waiter(&td, &d, s, 1, 0, 0xFFFFFFFFu);
    CHECK(sceKernelDeleteSema(s) == 0);
    pthread_join(td, NULL);
    CHECK(d.result == E_EACCES);
    CHECK(sceKernelDeleteSema(s) == E_ESRCH);
    CHECK(sceKernelSignalSema(s, 1) == E_ESRCH);
    CHECK(sceKernelWaitSema(s, 1, NULL) == E_ESRCH);
    CHECK(sceKernelPollSema(s, 1) == E_ESRCH);
    CHECK(sceKernelCancelSema(s, 0, NULL) == E_ESRCH);

    // Through the guest shims: the handle is written as a 32-bit id.
    Guest g;
    guest_init(&g, 0);
    strcpy((char *)mem + NAME, "guest");
    g.ctx.rcx = 0, g.ctx.r8 = 1, g.ctx.r9 = 0;
    *(int32_t *)(mem + data(&g, 0)) = 0;
    g.ctx.rsp = g.base + 0x8000 - 8;
    g.ctx.rdi = data(&g, 0), g.ctx.rsi = NAME, g.ctx.rdx = 1;
    shim_sceKernelCreateSema(&g.ctx);
    CHECK((int32_t)g.ctx.rax == 0);
    const int32_t id = *(int32_t *)(mem + data(&g, 0));
    CHECK(kcall(&g, shim_sceKernelSignalSema, (uint64_t)id, 1, 0) == 0);
    CHECK(kcall(&g, shim_sceKernelPollSema, (uint64_t)id, 1, 0) == 0);
    CHECK(kcall(&g, shim_sceKernelDeleteSema, (uint64_t)id, 0, 0) == 0);
    printf("PASS kernel-sema\n");
}

static void test_kernel_sema_order(void) {
    // Priority queue (attr 2): a lower priority value is served first, FIFO among equals.
    OrbisKernelSema s = 0;
    CHECK(sceKernelCreateSema(&s, "prio", 2, 0, 10, NULL) == 0);
    SemaWaiter w[3];
    pthread_t th[3];
    start_sema_waiter(&th[0], &w[0], s, 1, 700, 0xFFFFFFFFu);
    start_sema_waiter(&th[1], &w[1], s, 1, 300, 0xFFFFFFFFu);
    start_sema_waiter(&th[2], &w[2], s, 1, 300, 0xFFFFFFFFu);
    CHECK(sceKernelSignalSema(s, 1) == 0);
    pthread_join(th[1], NULL);
    CHECK(w[1].result == 0);
    CHECK(sceKernelSignalSema(s, 1) == 0);
    pthread_join(th[2], NULL);
    CHECK(w[2].result == 0);
    CHECK(sceKernelSignalSema(s, 1) == 0);
    pthread_join(th[0], NULL);
    CHECK(w[0].result == 0);
    CHECK(sceKernelDeleteSema(s) == 0);

    // FIFO queue (attr 1) ignores priorities.
    CHECK(sceKernelCreateSema(&s, "fifo", 1, 0, 10, NULL) == 0);
    start_sema_waiter(&th[0], &w[0], s, 1, 700, 0xFFFFFFFFu);
    start_sema_waiter(&th[1], &w[1], s, 1, 100, 0xFFFFFFFFu);
    CHECK(sceKernelSignalSema(s, 1) == 0);
    pthread_join(th[0], NULL);
    CHECK(w[0].result == 0);
    CHECK(sceKernelSignalSema(s, 1) == 0);
    pthread_join(th[1], NULL);
    CHECK(sceKernelDeleteSema(s) == 0);
    printf("PASS kernel-sema-order\n");
}

// ---------------------------------------------------------------------------------------------
// Event flags

static void test_event_flag(void) {
    OrbisKernelEventFlag ef = 0;
    OrbisKernelEventFlagOptParam opt = {sizeof(opt)};
    CHECK(sceKernelCreateEventFlag(NULL, "e", 0, 0, NULL) == (int32_t)SCE_KERNEL_ERROR_EINVAL);
    CHECK(sceKernelCreateEventFlag(&ef, NULL, 0, 0, NULL) == (int32_t)SCE_KERNEL_ERROR_EINVAL);
    CHECK(sceKernelCreateEventFlag(&ef, "e", 0, 0, &opt) == (int32_t)SCE_KERNEL_ERROR_EINVAL);
    CHECK(sceKernelCreateEventFlag(&ef, "e", 0x23, 0, NULL) == (int32_t)SCE_KERNEL_ERROR_EINVAL);
    CHECK(sceKernelCreateEventFlag(&ef, "e", 0x03, 0, NULL) == (int32_t)SCE_KERNEL_ERROR_EINVAL);
    CHECK(sceKernelCreateEventFlag(&ef, "0123456789abcdef0123456789abcdef", 0, 0, NULL) == E_ENAMETOOLONG);
    CHECK(sceKernelCreateEventFlag(&ef, "0123456789abcdef0123456789abcde", 0x21, 0x5, NULL) == 0);

    uint64_t res = 0;
    // Poll: AND / OR, EBUSY with the current pattern, EINVAL for no bits or a bad mode.
    CHECK(sceKernelPollEventFlag(ef, 0x5, SCE_KERNEL_EVF_WAITMODE_AND, &res) == 0 && res == 0x5);
    CHECK(sceKernelPollEventFlag(ef, 0x3, SCE_KERNEL_EVF_WAITMODE_AND, &res) == E_EBUSY && res == 0x5);
    CHECK(sceKernelPollEventFlag(ef, 0x3, SCE_KERNEL_EVF_WAITMODE_OR, &res) == 0 && res == 0x5);
    CHECK(sceKernelPollEventFlag(ef, 0, SCE_KERNEL_EVF_WAITMODE_OR, &res) == E_EINVAL);
    CHECK(sceKernelPollEventFlag(ef, 1, 0x3, &res) == E_EINVAL);
    CHECK(sceKernelPollEventFlag(ef, 1, 0x31, &res) == E_EINVAL);
    // Clear modes: the pattern bits, or all of them.
    CHECK(sceKernelPollEventFlag(ef, 0x1, SCE_KERNEL_EVF_WAITMODE_OR | SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT, &res) == 0);
    CHECK(res == 0x5);
    CHECK(sceKernelPollEventFlag(ef, 0x4, SCE_KERNEL_EVF_WAITMODE_AND | SCE_KERNEL_EVF_WAITMODE_CLEAR_ALL, &res) == 0);
    CHECK(res == 0x4);
    CHECK(sceKernelPollEventFlag(ef, ~0ull, SCE_KERNEL_EVF_WAITMODE_OR, &res) == E_EBUSY && res == 0);
    // Set ORs, Clear ANDs.
    CHECK(sceKernelSetEventFlag(ef, 0xF0) == 0 && sceKernelClearEventFlag(ef, 0x30) == 0);
    CHECK(sceKernelPollEventFlag(ef, ~0ull, SCE_KERNEL_EVF_WAITMODE_OR, &res) == 0 && res == 0x30);

    // A timed wait that fails returns the pattern and zeroes the timeout.
    uint32_t timeout = 20000;
    const double t0 = mono_s();
    CHECK(sceKernelWaitEventFlag(ef, 0x1, SCE_KERNEL_EVF_WAITMODE_AND, &res, &timeout) == E_ETIMEDOUT);
    CHECK(timeout == 0 && res == 0x30 && mono_s() - t0 >= 0.02);
    // A wait that succeeds at once leaves the timeout alone.
    timeout = 1234;
    CHECK(sceKernelWaitEventFlag(ef, 0x10, SCE_KERNEL_EVF_WAITMODE_AND, &res, &timeout) == 0 && timeout == 1234);

    CHECK(sceKernelDeleteEventFlag(ef) == 0);
    CHECK(sceKernelDeleteEventFlag(ef) == (int32_t)SCE_KERNEL_ERROR_ESRCH);
    CHECK(sceKernelSetEventFlag(ef, 1) == (int32_t)SCE_KERNEL_ERROR_ESRCH);
    CHECK(sceKernelClearEventFlag(ef, 1) == (int32_t)SCE_KERNEL_ERROR_ESRCH);
    CHECK(sceKernelPollEventFlag(ef, 1, 1, &res) == (int32_t)SCE_KERNEL_ERROR_ESRCH);
    CHECK(sceKernelCancelEventFlag(ef, 0, NULL) == (int32_t)SCE_KERNEL_ERROR_ESRCH);

    // Guest shims: the handle is pointer-sized; Open / Close are stubs.
    Guest g;
    guest_init(&g, 0);
    strcpy((char *)mem + NAME, "guest-evf");
    *(uint64_t *)(mem + data(&g, 0)) = ~0ull;
    g.ctx.rsp = g.base + 0x8000 - 8;
    g.ctx.rdi = data(&g, 0), g.ctx.rsi = NAME, g.ctx.rdx = 0, g.ctx.rcx = 0x2, g.ctx.r8 = 0;
    shim_sceKernelCreateEventFlag(&g.ctx);
    CHECK((int32_t)g.ctx.rax == 0);
    const uint64_t id = *(uint64_t *)(mem + data(&g, 0));
    CHECK(id > 0 && id <= 0xFFFF);
    g.ctx.rsp = g.base + 0x8000 - 8;
    g.ctx.rdi = id, g.ctx.rsi = 0x2, g.ctx.rdx = SCE_KERNEL_EVF_WAITMODE_AND, g.ctx.rcx = data(&g, 8);
    shim_sceKernelPollEventFlag(&g.ctx);
    CHECK((int32_t)g.ctx.rax == 0 && *(uint64_t *)(mem + data(&g, 8)) == 0x2);
    CHECK(kcall(&g, shim_sceKernelOpenEventFlag, 0, 0, 0) == 0);
    CHECK(kcall(&g, shim_sceKernelCloseEventFlag, 0, 0, 0) == 0);
    CHECK(kcall(&g, shim_sceKernelDeleteEventFlag, id, 0, 0) == 0);
    printf("PASS event-flag\n");
}

typedef struct {
    OrbisKernelEventFlag ef;
    uint64_t bits;
    uint32_t mode;
    int32_t prio;
    uint32_t timeout; // 0xFFFFFFFF: no timeout
    int32_t result;
    uint64_t pattern;
    atomic_int started;
} EvfWaiter;

static void *evf_waiter(void *p) {
    EvfWaiter *w = p;
    g_self.attr.prio = w->prio;
    atomic_store(&w->started, 1);
    uint32_t timeout = w->timeout;
    w->result = sceKernelWaitEventFlag(w->ef, w->bits, w->mode, &w->pattern,
                                       w->timeout == 0xFFFFFFFFu ? NULL : &timeout);
    return NULL;
}

static void start_evf_waiter(pthread_t *th, EvfWaiter *w, OrbisKernelEventFlag ef, uint64_t bits, uint32_t mode,
                             int32_t prio) {
    memset(w, 0, sizeof(*w));
    w->ef = ef, w->bits = bits, w->mode = mode, w->prio = prio, w->timeout = 0xFFFFFFFFu;
    CHECK(pthread_create(th, NULL, evf_waiter, w) == 0);
    while (!atomic_load(&w->started)) {
        sleep_ms(1);
    }
    sleep_ms(20);
}

static void test_event_flag_threads(void) {
    uint64_t res = 0;
    // SINGLE (default): a second waiter is EPERM, also for a poll.
    OrbisKernelEventFlag ef = 0;
    CHECK(sceKernelCreateEventFlag(&ef, "single", 0, 0, NULL) == 0);
    EvfWaiter a;
    pthread_t ta;
    start_evf_waiter(&ta, &a, ef, 0x1, SCE_KERNEL_EVF_WAITMODE_AND, 0);
    CHECK(sceKernelWaitEventFlag(ef, 0x2, SCE_KERNEL_EVF_WAITMODE_AND, &res, NULL) == E_EPERM);
    CHECK(sceKernelPollEventFlag(ef, 0x2, SCE_KERNEL_EVF_WAITMODE_AND, &res) == E_EPERM);
    CHECK(sceKernelSetEventFlag(ef, 0x3) == 0);
    pthread_join(ta, NULL);
    CHECK(a.result == 0 && a.pattern == 0x3);
    CHECK(sceKernelDeleteEventFlag(ef) == 0);

    // MULTI + FIFO: Set hands the pattern over waiter by waiter, each clear applied in turn.
    CHECK(sceKernelCreateEventFlag(&ef, "multi", SCE_KERNEL_EVF_ATTR_MULTI | SCE_KERNEL_EVF_ATTR_TH_FIFO, 0, NULL) ==
          0);
    EvfWaiter w1, w2, w3;
    pthread_t t1, t2, t3;
    start_evf_waiter(&t1, &w1, ef, 0x1, SCE_KERNEL_EVF_WAITMODE_OR | SCE_KERNEL_EVF_WAITMODE_CLEAR_ALL, 0);
    start_evf_waiter(&t2, &w2, ef, 0x1, SCE_KERNEL_EVF_WAITMODE_OR, 0);
    start_evf_waiter(&t3, &w3, ef, 0x2, SCE_KERNEL_EVF_WAITMODE_AND, 0);
    CHECK(sceKernelSetEventFlag(ef, 0x3) == 0);
    pthread_join(t1, NULL);
    CHECK(w1.result == 0 && w1.pattern == 0x3);
    // w1 cleared everything: w2 and w3 still wait.
    CHECK(sceKernelPollEventFlag(ef, ~0ull, SCE_KERNEL_EVF_WAITMODE_OR, &res) == E_EBUSY && res == 0);
    CHECK(sceKernelSetEventFlag(ef, 0x1) == 0);
    pthread_join(t2, NULL);
    CHECK(w2.result == 0 && w2.pattern == 0x1);
    // Cancel wakes the rest with ECANCELED and the new pattern.
    int32_t waiting = -1;
    CHECK(sceKernelCancelEventFlag(ef, 0x80, &waiting) == 0 && waiting == 1);
    pthread_join(t3, NULL);
    CHECK(w3.result == E_ECANCELED && w3.pattern == 0x80);
    CHECK(sceKernelPollEventFlag(ef, 0x80, SCE_KERNEL_EVF_WAITMODE_AND, &res) == 0);

    // Delete wakes waiters with EACCES.
    start_evf_waiter(&t1, &w1, ef, 0x1, SCE_KERNEL_EVF_WAITMODE_AND, 0);
    CHECK(sceKernelDeleteEventFlag(ef) == 0);
    pthread_join(t1, NULL);
    CHECK(w1.result == E_EACCES);

    // TH_PRIO, in shadPS4's order: a higher priority value is served first.
    CHECK(sceKernelCreateEventFlag(&ef, "prio", SCE_KERNEL_EVF_ATTR_MULTI | SCE_KERNEL_EVF_ATTR_TH_PRIO, 0, NULL) ==
          0);
    start_evf_waiter(&t1, &w1, ef, 0x1, SCE_KERNEL_EVF_WAITMODE_AND | SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT, 300);
    start_evf_waiter(&t2, &w2, ef, 0x1, SCE_KERNEL_EVF_WAITMODE_AND | SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT, 700);
    CHECK(sceKernelSetEventFlag(ef, 0x1) == 0);
    pthread_join(t2, NULL);
    CHECK(w2.result == 0);
    CHECK(sceKernelSetEventFlag(ef, 0x1) == 0);
    pthread_join(t1, NULL);
    CHECK(w1.result == 0);
    CHECK(sceKernelDeleteEventFlag(ef) == 0);
    printf("PASS event-flag-threads\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    setup();
    if (strcmp(argv[1], "posix-sem") == 0) {
        test_posix_sem();
    } else if (strcmp(argv[1], "posix-sem-threads") == 0) {
        test_posix_sem_threads();
    } else if (strcmp(argv[1], "kernel-sema") == 0) {
        test_kernel_sema();
    } else if (strcmp(argv[1], "kernel-sema-order") == 0) {
        test_kernel_sema_order();
    } else if (strcmp(argv[1], "event-flag") == 0) {
        test_event_flag();
    } else if (strcmp(argv[1], "event-flag-threads") == 0) {
        test_event_flag_threads();
    } else {
        fprintf(stderr, "unknown case %s\n", argv[1]);
        return 2;
    }
    return 0;
}
