// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpCommon against shadPS4's np_common.cpp: NpId and online id helpers, the platform tag,
// online id validation and the SDK version string; the mutex / condition wrappers and their
// error mapping; sceNpCreateThread / sceNpJoinThread; and callout contexts, whose worker thread
// calls guest handlers in deadline order. The libkernel pieces ps4_np_common.c calls (guest
// mutexes, condition variables and threads) are provided here on host pthreads, with the same
// errno results; guest functions are C functions in the dispatch table.
#include "ps4_np_common.h"
#include "ps4_kernel_sys.h"
#include "ps4_sync.h"
#include "ps4_threading.h"
#include <errno.h>
#include <pthread.h>
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

#define MEM_SIZE 0x800000ull
#define MAIN_STACK_TOP 0x100000ull
#define THREAD_STACKS 0x200000ull // child stacks: THREAD_STACKS + i * THREAD_STACK_SIZE
#define THREAD_STACK_SIZE 0x40000ull
#define MAX_THREADS 8
#define VARS 0x1000ull     // guest variables (mutex, cond, thread handles)
#define IDS 0x2000ull      // NpIds and online ids
#define CALLOUT 0x3000ull  // OrbisNpCalloutContext
#define ENTRIES 0x3100ull  // OrbisNpCalloutEntry[4]
#define STRINGS 0x4000ull
#define GUEST_THREAD_ENTRY 0x400000ull
#define GUEST_HANDLER 0x400010ull

// ---- Runtime symbols ps4_np_common.c links against.

recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

int32_t sceKernelGetCompiledSdkVersion(const GuestContext *ctx, int32_t *ver) {
    (void)ctx;
    *ver = 0x4500000;
    return 0;
}

int32_t sceKernelGetSystemSwVersion(OrbisKernelSwVersion *version) {
    version->Size = sizeof(*version);
    version->Version = PS4_SYSTEM_SW_VERSION;
    return 0;
}

// Guest sync variables hold an index into these tables (0: not initialized).
#define MAX_OBJS 32
static pthread_mutex_t g_mutexes[MAX_OBJS + 1];
static pthread_cond_t g_conds[MAX_OBJS + 1];
static int g_next_mutex = 1, g_next_cond = 1;
static uint8_t *mem;

static uint64_t *u64_at(uint64_t addr) { return (uint64_t *)(mem + addr); }

static pthread_mutex_t *mutex_of(uint64_t addr) {
    const uint64_t i = *u64_at(addr);
    return i ? &g_mutexes[i] : NULL;
}

int ps4_guest_mutex_init(GuestContext *ctx, uint64_t addr, int type) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, type == 2 ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_ERRORCHECK);
    const int i = g_next_mutex++;
    CHECK(i <= MAX_OBJS);
    const int rc = pthread_mutex_init(&g_mutexes[i], &a);
    pthread_mutexattr_destroy(&a);
    *u64_at(addr) = (uint64_t)i;
    return rc;
}

int ps4_guest_mutex_lock(GuestContext *ctx, uint64_t addr) { return pthread_mutex_lock(mutex_of(addr)); }
int ps4_guest_mutex_trylock(GuestContext *ctx, uint64_t addr) { return pthread_mutex_trylock(mutex_of(addr)); }
int ps4_guest_mutex_unlock(GuestContext *ctx, uint64_t addr) { return pthread_mutex_unlock(mutex_of(addr)); }

int ps4_guest_mutex_destroy(GuestContext *ctx, uint64_t addr) {
    pthread_mutex_t *m = mutex_of(addr);
    *u64_at(addr) = 0;
    return m ? pthread_mutex_destroy(m) : 0;
}

int ps4_guest_cond_init(GuestContext *ctx, uint64_t addr) {
    const int i = g_next_cond++;
    CHECK(i <= MAX_OBJS);
    *u64_at(addr) = (uint64_t)i;
    return pthread_cond_init(&g_conds[i], NULL);
}

int ps4_guest_cond_destroy(GuestContext *ctx, uint64_t addr) {
    const uint64_t i = *u64_at(addr);
    *u64_at(addr) = 0;
    return i ? pthread_cond_destroy(&g_conds[i]) : 0;
}

int ps4_guest_cond_signal(GuestContext *ctx, uint64_t addr) {
    return pthread_cond_signal(&g_conds[*u64_at(addr)]);
}

int ps4_guest_cond_wait(GuestContext *ctx, uint64_t cond_addr, uint64_t mutex_addr, uint64_t usec) {
    pthread_cond_t *c = &g_conds[*u64_at(cond_addr)];
    if (usec == 0) return pthread_cond_wait(c, mutex_of(mutex_addr));
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(usec / 1000000);
    ts.tv_nsec += (long)(usec % 1000000) * 1000;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(c, mutex_of(mutex_addr), &ts);
}

// Threads: the handle stored in the guest variable is an index into g_threads (from 1).
typedef struct TestThread {
    pthread_t host;
    GuestContext ctx;
    uint64_t start, arg, ret;
    void (*host_entry)(GuestContext *, void *);
    void *host_arg;
    PthreadAttr attr;
    char name[32];
} TestThread;
static TestThread g_threads[MAX_THREADS + 1];
static int g_next_thread = 1;

static void *thread_main(void *p) {
    TestThread *t = p;
    if (t->host_entry) {
        t->host_entry(&t->ctx, t->host_arg);
        return NULL;
    }
    t->ret = recomp_shim_call_guest(&t->ctx, t->start, 0, t->arg, 0, 0, 0);
    return NULL;
}

static int start_thread(GuestContext *ctx, uint64_t thread_out, const PthreadAttr *attr, uint64_t start,
                        uint64_t arg, void (*entry)(GuestContext *, void *), void *entry_arg, const char *name) {
    const int i = g_next_thread++;
    CHECK(i <= MAX_THREADS);
    TestThread *t = &g_threads[i];
    t->ctx.mem_base = mem;
    t->ctx.mem_size = MEM_SIZE;
    t->ctx.rsp = THREAD_STACKS + (uint64_t)i * THREAD_STACK_SIZE - 64;
    t->start = start;
    t->arg = arg;
    t->host_entry = entry;
    t->host_arg = entry_arg;
    if (attr) t->attr = *attr;
    if (name) snprintf(t->name, sizeof t->name, "%s", name);
    if (thread_out) *u64_at(thread_out) = (uint64_t)i;
    return pthread_create(&t->host, NULL, thread_main, t);
}

int ps4_create_guest_thread(GuestContext *ctx, uint64_t thread_out, const PthreadAttr *attr,
                            uint64_t start_routine, uint64_t arg, const char *name) {
    return start_thread(ctx, thread_out, attr, start_routine, arg, NULL, NULL, name);
}

int ps4_create_host_entry_thread(GuestContext *ctx, uint64_t thread_out, const PthreadAttr *attr,
                                 void (*entry)(GuestContext *child_ctx, void *arg), void *arg,
                                 const char *name) {
    return start_thread(ctx, thread_out, attr, 0, 0, entry, arg, name);
}

int ps4_join_guest_thread(uint64_t handle, uint64_t *ret) {
    if (handle == 0 || handle >= (uint64_t)g_next_thread) return ESRCH;
    TestThread *t = &g_threads[handle];
    const int rc = pthread_join(t->host, NULL);
    if (rc == 0 && ret) *ret = t->ret;
    return rc;
}

// ---- Test helpers.

static GuestContext main_ctx;

// Calls a shim as guest code would: stack arguments above a return address slot.
static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                    uint64_t a4, uint64_t a5, const uint64_t *stack, int nstack) {
    GuestContext *c = &main_ctx;
    c->rsp -= 8 * (uint64_t)nstack;
    for (int i = 0; i < nstack; i++) *u64_at(c->rsp + 8 * (uint64_t)i) = stack[i];
    c->rsp -= 8;
    *u64_at(c->rsp) = 0;
    const uint64_t rsp = c->rsp;
    c->rdi = a0, c->rsi = a1, c->rdx = a2, c->rcx = a3, c->r8 = a4, c->r9 = a5;
    shim(c);
    CHECK(c->rsp == rsp + 8); // the shim returned like a guest function
    c->rsp += 8 * (uint64_t)nstack;
    return (int32_t)c->rax;
}

#define CALL(shim, ...) call_n(shim, (uint64_t[]){__VA_ARGS__}, sizeof((uint64_t[]){__VA_ARGS__}) / 8)
static int32_t call_n(void (*shim)(GuestContext *), const uint64_t *a, size_t n) {
    uint64_t r[6] = {0};
    for (size_t i = 0; i < n && i < 6; i++) r[i] = a[i];
    return call(shim, r[0], r[1], r[2], r[3], r[4], r[5], n > 6 ? a + 6 : NULL, n > 6 ? (int)(n - 6) : 0);
}

static void register_entry(uint64_t addr, recomp_fn_t fn) {
    const uint64_t l1 = (addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    const uint64_t l2 = (addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[l1]) g_dispatch_l1[l1] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    if (!g_dispatch_l1[l1][l2]) g_dispatch_l1[l1][l2] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    g_dispatch_l1[l1][l2][addr & DISPATCH_LEVEL_MASK] = fn;
}

static uint64_t put_string(uint64_t addr, const char *s) {
    memcpy(mem + addr, s, strlen(s) + 1);
    return addr;
}

static OrbisNpId *np_id_at(uint64_t addr, const char *handle, const char *opt, const char *reserved) {
    OrbisNpId *id = (OrbisNpId *)(mem + addr);
    memset(id, 0, sizeof(*id));
    strncpy(id->handle.data, handle, sizeof(id->handle.data));
    if (opt) memcpy(id->opt, opt, 8);
    if (reserved) memcpy(id->reserved, reserved, 8);
    return id;
}

// ---- Guest code.

// Thread entry: returns its argument + 1 and records its stack pointer.
static uint64_t g_thread_saw_arg;
static void guest_thread_entry(GuestContext *c) {
    g_thread_saw_arg = c->rdi;
    c->rax = c->rdi + 1;
    c->rsp += 8; // RET
}

// Callout handler: records the order of its arguments.
static pthread_mutex_t g_calls_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_calls[8];
static int g_ncalls;
static void guest_handler(GuestContext *c) {
    pthread_mutex_lock(&g_calls_mu);
    if (g_ncalls < 8) g_calls[g_ncalls++] = c->rdi;
    pthread_mutex_unlock(&g_calls_mu);
    c->rsp += 8; // RET
}

static int calls_seen(void) {
    pthread_mutex_lock(&g_calls_mu);
    const int n = g_ncalls;
    pthread_mutex_unlock(&g_calls_mu);
    return n;
}

static void setup(void) {
    mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(mem != MAP_FAILED);
    main_ctx.mem_base = mem;
    main_ctx.mem_size = MEM_SIZE;
    main_ctx.rsp = MAIN_STACK_TOP;
    register_entry(GUEST_THREAD_ENTRY, guest_thread_entry);
    register_entry(GUEST_HANDLER, guest_handler);
}

// ---- Cases.

#define E_INVALID_ARGUMENT ((int32_t)ORBIS_NP_ERROR_INVALID_ARGUMENT)
#define E_NOT_MATCH ((int32_t)ORBIS_NP_UTIL_ERROR_NOT_MATCH)

static void test_ids(void) {
    const uint64_t a = IDS, b = IDS + 0x40, out = IDS + 0x80;
    np_id_at(a, "alpha", "\0\0\0\0ps4\0", NULL);
    np_id_at(b, "alpha", "\0\0\0\0ps4\0", NULL);
    CHECK(CALL(shim_sceNpCmpNpId, a, b) == 0);
    CHECK(CALL(shim_sceNpCmpOnlineId, a, b) == 0);
    CHECK(CALL(shim_sceNpCmpNpIdInOrder, a, b, out) == 0 && *(uint32_t *)(mem + out) == 0);
    CHECK(CALL(shim_sceNpCmpNpId, a, 0) == E_INVALID_ARGUMENT);
    CHECK(CALL(shim_sceNpCmpOnlineId, 0, b) == E_INVALID_ARGUMENT);
    CHECK(CALL(shim_sceNpCmpNpIdInOrder, a, b, 0) == E_INVALID_ARGUMENT);

    // Handles compare first, then opt, then reserved, as unsigned bytes.
    np_id_at(b, "beta", NULL, NULL);
    CHECK(CALL(shim_sceNpCmpNpId, a, b) == E_NOT_MATCH);
    CHECK(CALL(shim_sceNpCmpOnlineId, a, b) == E_NOT_MATCH);
    CHECK(CALL(shim_sceNpCmpNpIdInOrder, a, b, out) == 0 && *(int32_t *)(mem + out) == -1);
    CHECK(CALL(shim_sceNpCmpNpIdInOrder, b, a, out) == 0 && *(int32_t *)(mem + out) == 1);
    np_id_at(b, "alpha", "\0\0\0\0ps3\0", NULL);
    CHECK(CALL(shim_sceNpCmpOnlineId, a, b) == 0);
    CHECK(CALL(shim_sceNpCmpNpId, a, b) == E_NOT_MATCH);
    CHECK(CALL(shim_sceNpCmpNpIdInOrder, a, b, out) == 0 && *(int32_t *)(mem + out) == 1);
    np_id_at(b, "alpha", "\0\0\0\0ps4\0", "\xff");
    CHECK(CALL(shim_sceNpCmpNpId, a, b) == E_NOT_MATCH);
    CHECK(CALL(shim_sceNpCmpNpIdInOrder, a, b, out) == 0 && *(int32_t *)(mem + out) == -1);

    // Platform tags in opt[4..7].
    CHECK(CALL(shim_sceNpGetPlatformType, a) == ORBIS_NP_PLATFORM_TYPE_PS4);
    np_id_at(b, "x", "\0\0\0\0ps3\0", NULL);
    CHECK(CALL(shim_sceNpGetPlatformType, b) == ORBIS_NP_PLATFORM_TYPE_PS3);
    np_id_at(b, "x", "\0\0\0\0psp2", NULL);
    CHECK(CALL(shim_sceNpGetPlatformType, b) == ORBIS_NP_PLATFORM_TYPE_VITA);
    np_id_at(b, "x", NULL, NULL);
    CHECK(CALL(shim_sceNpGetPlatformType, b) == ORBIS_NP_PLATFORM_TYPE_NONE);
    np_id_at(b, "x", "\0\0\0\0ps5\0", NULL);
    CHECK(CALL(shim_sceNpGetPlatformType, b) == ORBIS_NP_ERROR_INVALID_PLATFORM_TYPE);
    CHECK(CALL(shim_sceNpGetPlatformType, 0) == E_INVALID_ARGUMENT);

    // Online ids: 3..16 characters, alphanumeric first, then alphanumerics, '_' and '-'.
    static const struct {
        const char *id;
        int valid;
    } cases[] = {
        {"abc", 1}, {"A1_b-c", 1}, {"0123456789abcdef", 1}, {"ab", 0},
        {"_abc", 0}, {"-abc", 0}, {"ab.c", 0}, {"ab c", 0},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        np_id_at(a, cases[i].id, NULL, NULL);
        CHECK(CALL(shim_sceNpIntIsValidOnlineId, a) == cases[i].valid);
    }
    np_id_at(a, "abc", NULL, NULL);
    ((OrbisNpId *)(mem + a))->handle.term = 'x';
    CHECK(CALL(shim_sceNpIntIsValidOnlineId, a) == 0);
    CHECK(CALL(shim_sceNpIntIsValidOnlineId, 0) == 0);

    // "<major>.<minor>" of the system software version.
    memset(mem + STRINGS, 'z', 16);
    CALL(shim_sceNpGetSdkVersion, STRINGS);
    CHECK(strcmp((char *)(mem + STRINGS), "9.00") == 0);

    CHECK(CALL(shim_sceNpGetSystemClockUsec, out) == 0);
    const int64_t t0 = *(int64_t *)(mem + out);
    usleep(2000);
    CHECK(CALL(shim_sceNpGetSystemClockUsec, out) == 0);
    CHECK(*(int64_t *)(mem + out) - t0 >= 2000);
    puts("PASS id-helpers");
}

static void test_mutex(void) {
    const uint64_t m = VARS, r = VARS + 8, cv = VARS + 16;
    // Error-check mutex: a second lock by the owner fails, trylock reports NP busy.
    CHECK(CALL(shim_sceNpMutexInit, m, put_string(STRINGS, "m"), 0) == 0);
    CHECK(CALL(shim_sceNpMutexLock, m) == 0);
    CHECK((uint32_t)CALL(shim_sceNpMutexLock, m) == 0x80020000u + EDEADLK);
    CHECK(CALL(shim_sceNpMutexTryLock, m) == ORBIS_NP_LW_MUTEX_ERROR_BUSY);
    CHECK(CALL(shim_sceNpLwMutexTryLock, m) == ORBIS_NP_LW_MUTEX_ERROR_BUSY);
    CHECK(CALL(shim_sceNpMutexUnlock, m) == 0);
    CHECK((uint32_t)CALL(shim_sceNpMutexUnlock, m) == 0x80020000u + EPERM);
    CHECK(CALL(shim_sceNpMutexDestroy, m) == 0);

    // Flag bit 0: recursive.
    CHECK(CALL(shim_sceNpLwMutexInit, r, 0, 1) == 0);
    CHECK(CALL(shim_sceNpLwMutexLock, r) == 0);
    CHECK(CALL(shim_sceNpLwMutexTryLock, r) == 0);
    CHECK(CALL(shim_sceNpLwMutexUnlock, r) == 0);
    CHECK(CALL(shim_sceNpLwMutexUnlock, r) == 0);

    // A timed wait that runs out reports the NP timeout code.
    CHECK(CALL(shim_sceNpCondInit, cv, 0, 0) == 0);
    CHECK(CALL(shim_sceNpCondSignal, cv) == 0);
    CHECK(CALL(shim_sceNpLwMutexLock, r) == 0);
    CHECK(CALL(shim_sceNpCondTimedwait, cv, r, 1000) == ORBIS_NP_LW_COND_ERROR_TIMEDOUT);
    CHECK(CALL(shim_sceNpLwMutexUnlock, r) == 0);
    CHECK(CALL(shim_sceNpCondDestroy, cv) == 0);
    CHECK(CALL(shim_sceNpLwMutexDestroy, r) == 0);
    puts("PASS mutex");
}

static void test_thread(void) {
    const uint64_t th = VARS + 32, ret = VARS + 40;
    const uint64_t name = put_string(STRINGS, "NpWorker");
    // Stacks below libkernel's 16 KiB minimum are EINVAL.
    CHECK((uint32_t)CALL(shim_sceNpCreateThread, th, GUEST_THREAD_ENTRY, 41, 0, 0x1000, 0, name) ==
          0x80020000u + EINVAL);
    CHECK(CALL(shim_sceNpCreateThread, th, GUEST_THREAD_ENTRY, 41, 500, 0x8000, 1, name) == 0);
    const uint64_t handle = *u64_at(th);
    CHECK(handle != 0);
    const TestThread *t = &g_threads[handle];
    CHECK(strcmp(t->name, "NpWorker") == 0);
    CHECK(t->attr.stacksize_attr == 0x8000);
    CHECK(t->attr.prio == 500 && t->attr.sched_policy == 1); // SDK 4.50: FIFO at the priority
    CHECK(CALL(shim_sceNpJoinThread, handle, ret) == 0);
    CHECK(g_thread_saw_arg == 41 && *u64_at(ret) == 42);
    CHECK((uint32_t)CALL(shim_sceNpJoinThread, 99, 0) == 0x80020000u + ESRCH);

    // Without a priority the thread keeps the default scheduling.
    CHECK(CALL(shim_sceNpCreateThread, th, GUEST_THREAD_ENTRY, 1, 0, 0x4000, 0, 0) == 0);
    const TestThread *t2 = &g_threads[*u64_at(th)];
    CHECK(t2->attr.prio == 700 && t2->attr.sched_policy == 2);
    CHECK(CALL(shim_sceNpJoinThread, *u64_at(th), 0) == 0);
    puts("PASS thread");
}

static void wait_calls(int n) {
    for (int i = 0; i < 400 && calls_seen() < n; i++) usleep(5000);
}

static void test_callout(void) {
    const uint64_t e0 = ENTRIES, e1 = ENTRIES + 0x20, e2 = ENTRIES + 0x40, e3 = ENTRIES + 0x60;
    const uint64_t removed = VARS + 48;
    const uint64_t name = put_string(STRINGS, "NpCallout");
    const OrbisNpCalloutContext *cc = (const OrbisNpCalloutContext *)(mem + CALLOUT);

    CHECK(CALL(shim_sceNpCalloutStartOnCtx, CALLOUT, e0, 1000, GUEST_HANDLER, 1) ==
          ORBIS_NP_CALLOUT_ERROR_NOT_INITIALIZED);
    CHECK(CALL(shim_sceNpCalloutInitCtx, CALLOUT, name, 0x10000, 0, 0) == 0);
    CHECK(cc->active == 1 && cc->thread != 0);
    CHECK(strcmp(g_threads[cc->thread].name, "NpCallout") == 0);
    CHECK(CALL(shim_sceNpCalloutInitCtx, CALLOUT, name, 0x10000, 0, 0) == ORBIS_NP_CALLOUT_ERROR_ALREADY_INITIALIZED);

    // Starting a queued entry again fails. As in shadPS4, StartOnCtx rewrites the entry (next,
    // handler, deadline) before it looks for the duplicate, so the check is made on a lone entry.
    CHECK(CALL(shim_sceNpCalloutStartOnCtx64, CALLOUT, e3, 10000000, GUEST_HANDLER, 99) == 0);
    CHECK(CALL(shim_sceNpCalloutStartOnCtx, CALLOUT, e3, 10000000, GUEST_HANDLER, 99) ==
          ORBIS_NP_CALLOUT_ERROR_DUPLICATE_ENTRY);
    // A stopped entry never runs.
    CHECK(CALL(shim_sceNpCalloutStopOnCtx, CALLOUT, e3, removed) == 0 && *(uint32_t *)(mem + removed) == 1);
    CHECK(CALL(shim_sceNpCalloutStopOnCtx, CALLOUT, e3, removed) == 0 && *(uint32_t *)(mem + removed) == 0);
    CHECK(CALL(shim_sceNpCalloutStopOnCtx, CALLOUT, e3, 0) == 0);
    CHECK(cc->head == 0);

    // Entries run in deadline order whatever order they were started in.
    CHECK(CALL(shim_sceNpCalloutStartOnCtx, CALLOUT, e0, 90000, GUEST_HANDLER, 30) == 0);
    CHECK(CALL(shim_sceNpCalloutStartOnCtx64, CALLOUT, e1, 30000, GUEST_HANDLER, 10) == 0);
    CHECK(CALL(shim_sceNpCalloutStartOnCtx, CALLOUT, e2, 60000, GUEST_HANDLER, 20) == 0);
    CHECK(calls_seen() == 0);
    wait_calls(3);
    CHECK(calls_seen() == 3);
    CHECK(g_calls[0] == 10 && g_calls[1] == 20 && g_calls[2] == 30);
    CHECK(cc->head == 0);

    // An entry can be started again once it ran; a pending one is dropped by TermCtx.
    CHECK(CALL(shim_sceNpCalloutStartOnCtx, CALLOUT, e0, 0, GUEST_HANDLER, 40) == 0);
    wait_calls(4);
    CHECK(calls_seen() == 4 && g_calls[3] == 40);
    CHECK(CALL(shim_sceNpCalloutStartOnCtx, CALLOUT, e1, 10000000, GUEST_HANDLER, 50) == 0);
    CALL(shim_sceNpCalloutTermCtx, CALLOUT);
    CHECK(cc->active == 0);
    CHECK(calls_seen() == 4);
    CHECK(CALL(shim_sceNpCalloutStopOnCtx, CALLOUT, e1, removed) == ORBIS_NP_CALLOUT_ERROR_NOT_INITIALIZED);
    CALL(shim_sceNpCalloutTermCtx, CALLOUT); // a second Term does nothing
    // Initialized again after Term.
    CHECK(CALL(shim_sceNpCalloutInitCtx, CALLOUT, name, 0x10000, 0, 0) == 0);
    CALL(shim_sceNpCalloutTermCtx, CALLOUT);
    puts("PASS callout");
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    setup();
    if (strcmp(argv[1], "id-helpers") == 0) test_ids();
    else if (strcmp(argv[1], "mutex") == 0) test_mutex();
    else if (strcmp(argv[1], "thread") == 0) test_thread();
    else if (strcmp(argv[1], "callout") == 0) test_callout();
    else return 2;
    return 0;
}
