// SPDX-License-Identifier: GPL-2.0-or-later

// Guest pthreads and their synchronization objects against shadPS4's kernel/threads (pthread.cpp,
// pthread_attr.cpp, pthread_spec.cpp, pthread_clean.cpp, mutex.cpp, condvar.cpp, rwlock.cpp,
// barrier.cpp): attribute defaults and checks, thread creation with attributes (scheduling
// inheritance, the runtime's and the guest's own stacks, names), join and detach, thread-specific
// data and the exit order, deferred cancellation at its cancellation points, mutex, condition
// variable, rwlock and barrier calls, pthread_once, suspension and user contexts. Each guest
// thread is a real host thread running ps4_threading.c's runner; "guest functions" are C
// functions in the dispatch table that return like guest code (popping the return address).
#include "ps4_barrier.h"
#include "ps4_semaphore.h"
#include "ps4_sync.h"
#include "ps4_threading.h"
#include <errno.h>
#include <sched.h>
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

#define SCE(e) (0x80020000u + (uint32_t)(e))
#define MEM_SIZE (1ull << 30)

// ---- The runtime pieces ps4_threading.c and the sync objects use.

static uint8_t *mem;
static _Atomic uint64_t g_bump = 0x100000;

_Thread_local GuestContext *g_current_ctx;
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int prot, int flags, const char *name) {
    (void)ctx, (void)prot, (void)flags, (void)name;
    const uint64_t sz = (size + 0x3fff) & ~0x3fffull;
    const uint64_t a = atomic_fetch_add(&g_bump, sz);
    return a + sz <= MEM_SIZE ? a : (uint64_t)-1;
}

static GuestContext *g_proc;

// A context with its TCB (and, unless the guest gives one, its stack) in the bump area.
static GuestContext *new_context(GuestContext *parent, uint64_t stack_size, uint64_t user_stack) {
    const uint64_t own = user_stack ? 0 : stack_size;
    const uint64_t base = recomp_vm_alloc_named(parent, own + 0x2000, 0, 0, NULL);
    GuestContext *c = (GuestContext *)calloc(1, sizeof(GuestContext));
    c->mem_base = mem;
    c->mem_size = MEM_SIZE;
    c->fs_base = base + own + 0x1000;
    *(uint64_t *)(mem + c->fs_base) = c->fs_base;
    c->process_ctx = parent ? (parent->process_ctx ? parent->process_ctx : parent) : NULL;
    c->stack_base = base;
    c->stack_alloc_size = own + 0x2000;
    c->rsp = user_stack ? (user_stack + stack_size) & ~0xFull : (base + stack_size - 0x100) & ~0xFFull;
    return c;
}

GuestContext *recomp_create_thread_context(GuestContext *parent, uint64_t stack_size) {
    return new_context(parent, stack_size, 0);
}

GuestContext *recomp_create_thread_context_on_stack(GuestContext *parent, uint64_t stack_addr, uint64_t stack_size) {
    return new_context(parent, stack_size, stack_addr);
}

void recomp_free_thread_context(GuestContext *ctx) { free(ctx); }
int32_t sceKernelIsNeoMode(void) { return 0; } // ps4_kernel_time.c's CPU frequency (unused here)
bool recomp_pc_in_guest_code(uint64_t pc) { return (void)pc, false; }
uint64_t recomp_tls_module_offset(uint64_t module_id) { return (void)module_id, 0; }

// ---- Guest functions and calls.

static uint64_t g_next_fn = 0x500000;

static uint64_t guest_fn(recomp_fn_t fn) {
    const uint64_t addr = g_next_fn;
    g_next_fn += 0x10;
    const uint64_t i1 = (addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    const uint64_t i2 = (addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[i1]) g_dispatch_l1[i1] = (recomp_fn_t **)calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    if (!g_dispatch_l1[i1][i2]) g_dispatch_l1[i1][i2] = (recomp_fn_t *)calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    g_dispatch_l1[i1][i2][addr & DISPATCH_LEVEL_MASK] = fn;
    return addr;
}

#define GUEST_RET(c, v)       \
    do {                      \
        (c)->rax = (v);       \
        (c)->rsp += 8;        \
        return;               \
    } while (0)

#define RET_SLOT 0x1234ull

static uint64_t call5(GuestContext *c, void (*shim)(GuestContext *), const uint64_t a[5]) {
    c->rdi = a[0], c->rsi = a[1], c->rdx = a[2], c->rcx = a[3], c->r8 = a[4];
    c->rsp -= 8;
    *(uint64_t *)(mem + c->rsp) = RET_SLOT;
    const uint64_t sp = c->rsp;
    shim(c);
    CHECK(c->rsp == sp + 8);
    return c->rax;
}
#define CALL(c, shim, ...) call5((c), (shim), (const uint64_t[5]){__VA_ARGS__})
#define R32(c, shim, ...) ((uint32_t)CALL(c, shim, __VA_ARGS__))

static uint64_t galloc(uint64_t size) {
    const uint64_t a = recomp_vm_alloc_named(g_proc, size, 0, 0, NULL);
    memset(mem + a, 0, size);
    return a;
}
static uint64_t *u64p(uint64_t a) { return (uint64_t *)(mem + a); }
static int32_t *i32p(uint64_t a) { return (int32_t *)(mem + a); }
static uint64_t gstr(const char *s) {
    const uint64_t a = galloc(strlen(s) + 1);
    strcpy((char *)(mem + a), s);
    return a;
}

static double mono_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }

static void wait_flag(const uint64_t addr) {
    while (!__atomic_load_n(u64p(addr), __ATOMIC_ACQUIRE)) sched_yield();
}
static void set_flag(const uint64_t addr, uint64_t v) { __atomic_store_n(u64p(addr), v, __ATOMIC_RELEASE); }

// Writes a CLOCK_REALTIME (or other host clock) deadline ms from now into a guest timespec.
static uint64_t deadline(clockid_t clock, int ms) {
    struct timespec ts;
    clock_gettime(clock, &ts);
    int64_t ns = ts.tv_nsec + (int64_t)ms * 1000000;
    const uint64_t a = galloc(16);
    u64p(a)[0] = (uint64_t)(ts.tv_sec + ns / 1000000000);
    u64p(a)[1] = (uint64_t)(ns % 1000000000);
    return a;
}

// Creates a guest thread running fn(arg) with the attribute variable attr (0 for none).
static uint64_t spawn_attr(recomp_fn_t fn, uint64_t arg, uint64_t attr) {
    const uint64_t var = galloc(8);
    CHECK(CALL(g_proc, shim_pthread_create, var, attr, guest_fn(fn), arg) == 0);
    return *u64p(var);
}
static uint64_t spawn(recomp_fn_t fn, uint64_t arg) { return spawn_attr(fn, arg, 0); }

static uint64_t join(uint64_t t) {
    const uint64_t out = galloc(8);
    CHECK(CALL(g_proc, shim_pthread_join, t, out) == 0);
    return *u64p(out);
}

// ---- attr: defaults, setters' checks, affinity, the scePthreadAttr error codes.

static void test_attr(void) {
    GuestContext *m = g_proc;
    const uint64_t a = galloc(8), o = galloc(16);
    CHECK(CALL(m, shim_pthread_attr_init, a) == 0);
    CHECK(CALL(m, shim_pthread_attr_getschedpolicy, a, o) == 0 && *i32p(o) == 1);
    CHECK(CALL(m, shim_pthread_attr_getinheritsched, a, o) == 0 && *i32p(o) == 4);
    CHECK(CALL(m, shim_pthread_attr_getschedparam, a, o) == 0 && *i32p(o) == 700);
    CHECK(CALL(m, shim_pthread_attr_getscope, a, o) == 0 && *i32p(o) == 2);
    CHECK(CALL(m, shim_pthread_attr_getstacksize, a, o) == 0 && *u64p(o) == 0x100000);
    CHECK(CALL(m, shim_pthread_attr_getguardsize, a, o) == 0 && *u64p(o) == 0);
    CHECK(CALL(m, shim_pthread_attr_getdetachstate, a, o) == 0 && *i32p(o) == 0);
    CHECK(CALL(m, shim_pthread_attr_getstack, a, o, o + 8) == 0 && u64p(o)[0] == 0 && u64p(o)[1] == 0x100000);

    CHECK(CALL(m, shim_pthread_attr_setstacksize, a, 0x3fff) == EINVAL);
    CHECK(CALL(m, shim_pthread_attr_setstacksize, a, 0x10000) == 0);
    CHECK(CALL(m, shim_scePthreadAttrGetstacksize, a, o) == 0 && *u64p(o) == 0x10000);
    CHECK(CALL(m, shim_pthread_attr_setschedpolicy, a, 0) == ENOTSUP);
    CHECK(R32(m, shim_scePthreadAttrSetschedpolicy, a, 4) == SCE(ENOTSUP));
    CHECK(CALL(m, shim_pthread_attr_setschedpolicy, a, 2) == 0);
    CHECK(CALL(m, shim_pthread_attr_getschedparam, a, o) == 0 && *i32p(o) == 900); // OTHER's default
    *i32p(o) = 5000;
    CHECK(CALL(m, shim_pthread_attr_setschedparam, a, o) == 0); // OTHER: not range checked
    CHECK(CALL(m, shim_pthread_attr_setschedpolicy, a, 1) == 0);
    *i32p(o) = 255;
    CHECK(CALL(m, shim_pthread_attr_setschedparam, a, o) == ENOTSUP);
    *i32p(o) = 300;
    CHECK(CALL(m, shim_pthread_attr_setschedparam, a, o) == 0);
    CHECK(CALL(m, shim_pthread_attr_setschedparam, a, 0) == ENOTSUP);
    CHECK(CALL(m, shim_pthread_attr_setinheritsched, a, 1) == ENOTSUP);
    CHECK(CALL(m, shim_pthread_attr_setinheritsched, a, 0) == 0);
    CHECK(CALL(m, shim_pthread_attr_setscope, a, 1) == EINVAL);
    CHECK(CALL(m, shim_pthread_attr_setscope, a, 0) == 0);
    CHECK(CALL(m, shim_pthread_attr_getscope, a, o) == 0 && *i32p(o) == 0);
    CHECK(CALL(m, shim_pthread_attr_setdetachstate, a, 2) == EINVAL);
    CHECK(R32(m, shim_scePthreadAttrSetdetachstate, a, 1) == 0);
    CHECK(R32(m, shim_scePthreadAttrGetdetachstate, a, o) == 0 && *i32p(o) == 1);
    CHECK(CALL(m, shim_pthread_attr_setguardsize, a, 0x4000) == 0);
    CHECK(R32(m, shim_scePthreadAttrGetguardsize, a, o) == 0 && *u64p(o) == 0x4000);
    CHECK(CALL(m, shim_pthread_attr_setstack, a, 0, 0x10000) == EINVAL);
    CHECK(CALL(m, shim_pthread_attr_setstack, a, 0x2000000, 0x1000) == EINVAL);
    CHECK(CALL(m, shim_pthread_attr_setstackaddr, a, 0) == EINVAL);
    CHECK(CALL(m, shim_pthread_attr_setcreatesuspend_np, a) == 0);

    // No CPU set: every CPU. A set mask is reported back.
    memset(mem + o, 0, 16);
    CHECK(CALL(m, shim_pthread_attr_getaffinity_np, a, 16, o) == 0 && u64p(o)[0] == ~0ull && u64p(o)[1] == ~0ull);
    CHECK(R32(m, shim_scePthreadAttrSetaffinity, a, 0x5) == 0);
    CHECK(R32(m, shim_scePthreadAttrGetaffinity, a, o) == 0 && *u64p(o) == 0x5);
    CHECK(CALL(m, shim_pthread_attr_setaffinity_np, a, 0, 0) == 0); // clears it
    CHECK(R32(m, shim_scePthreadAttrGetaffinity, a, o) == 0 && *u64p(o) == ~0ull);

    CHECK(CALL(m, shim_pthread_attr_getstacksize, 0, o) == EINVAL);
    CHECK(CALL(m, shim_pthread_attr_destroy, a) == 0 && *u64p(a) == 0);
    CHECK(CALL(m, shim_pthread_attr_destroy, a) == EINVAL);
    CHECK(R32(m, shim_scePthreadAttrDestroy, a) == SCE(EINVAL));
    CHECK(R32(m, shim_scePthreadAttrGetstacksize, a, o) == SCE(EINVAL));
    puts("PASS attr");
}

// ---- thread: creation with attributes, a running thread's view of itself, scheduling,
// affinity and names seen from another thread.

typedef struct {
    uint64_t ready, go;
    uint64_t tid, self, policy, prio, attr, stackaddr, stacksize, rsp;
    char name[32];
} Info;

static void entry_info(GuestContext *c) {
    const uint64_t d = c->rdi;
    Info *in = (Info *)(mem + d);
    in->rsp = c->rsp;
    in->tid = CALL(c, shim_pthread_getthreadid_np);
    in->self = CALL(c, shim_pthread_self);
    CHECK(CALL(c, shim_pthread_getname_np, in->self, d + offsetof(Info, name)) == 0);
    CHECK(CALL(c, shim_pthread_getschedparam, in->self, d + offsetof(Info, policy), d + offsetof(Info, prio)) == 0);
    if (in->attr) {
        CHECK(CALL(c, shim_pthread_attr_get_np, in->self, in->attr) == 0);
        CHECK(CALL(c, shim_pthread_attr_getstack, in->attr, d + offsetof(Info, stackaddr), d + offsetof(Info, stacksize)) == 0);
    }
    set_flag(d + offsetof(Info, ready), 1);
    wait_flag(d + offsetof(Info, go));
    GUEST_RET(c, 0x77);
}

static void test_thread(void) {
    GuestContext *m = g_proc;
    const uint64_t a = galloc(8), o = galloc(64), var = galloc(8);
    CHECK(CALL(m, shim_pthread_attr_init, a) == 0);
    CHECK(CALL(m, shim_pthread_attr_setstacksize, a, 0x10000) == 0);
    CHECK(CALL(m, shim_pthread_attr_setinheritsched, a, 0) == 0);
    *i32p(o) = 300;
    CHECK(CALL(m, shim_pthread_attr_setschedparam, a, o) == 0);

    const uint64_t d = galloc(sizeof(Info));
    Info *in = (Info *)(mem + d);
    in->attr = galloc(8);
    CHECK(CALL(m, shim_pthread_attr_init, in->attr) == 0);
    CHECK(R32(m, shim_scePthreadCreate, var, a, guest_fn(entry_info), d, gstr("Worker")) == 0);
    const uint64_t t = *u64p(var);
    wait_flag(d + offsetof(Info, ready));
    CHECK(in->tid == ((RecompThread *)t)->thread_id && in->self == t);
    CHECK(strcmp(in->name, "Worker") == 0);
    CHECK(in->policy == 1 && in->prio == 300);
    CHECK(in->stackaddr != 0 && in->stacksize == 0x10000 + PS4_THR_STACK_HLE_EXTRA); // room for HLE code
    CHECK(in->rsp > in->stackaddr && in->rsp <= in->stackaddr + in->stacksize);

    // Scheduling, affinity and names from another thread.
    CHECK(R32(m, shim_scePthreadGetprio, t, o) == 0 && *i32p(o) == 300);
    CHECK(R32(m, shim_scePthreadSetprio, t, 400) == 0);
    CHECK(CALL(m, shim_pthread_getschedparam, t, o, o + 4) == 0 && i32p(o)[0] == 1 && i32p(o)[1] == 400);
    *i32p(o + 8) = 500;
    CHECK(R32(m, shim_scePthreadSetschedparam, t, 2, o + 8) == 0);
    CHECK(R32(m, shim_scePthreadGetschedparam, t, o, o + 4) == 0 && i32p(o)[0] == 2 && i32p(o)[1] == 500);
    CHECK(CALL(m, shim_pthread_getschedparam, t, 0, o) == EINVAL);
    CHECK(CALL(m, shim_pthread_getaffinity_np, t, 16, o) == 0 && u64p(o)[0] == ~0ull);
    CHECK(R32(m, shim_scePthreadSetaffinity, t, 3) == 0);
    CHECK(R32(m, shim_scePthreadGetaffinity, t, o) == 0 && *u64p(o) == 3);
    CHECK(R32(m, shim_scePthreadGetname, t, o) == 0 && strcmp((char *)(mem + o), "Worker") == 0);
    CHECK(R32(m, shim_scePthreadRename, t, gstr("Renamed")) == 0);
    CHECK(CALL(m, shim_pthread_getname_np, t, o) == 0 && strcmp((char *)(mem + o), "Renamed") == 0);
    CHECK(CALL(m, shim_scePthreadAttrGet, t, in->attr) == 0);
    CHECK(CALL(m, shim_pthread_attr_getschedparam, in->attr, o) == 0 && *i32p(o) == 500);

    set_flag(d + offsetof(Info, go), 1);
    CHECK(join(t) == 0x77);
    // A joined thread is gone.
    CHECK(CALL(m, shim_pthread_getname_np, t, o) == ESRCH);
    CHECK(R32(m, shim_scePthreadGetprio, t, o) == 0x80020003u);
    CHECK(R32(m, shim_scePthreadJoin, t, 0) == SCE(ESRCH));
    CHECK(CALL(m, shim_pthread_setprio, 0xdead, 1) == ESRCH);

    // Without an attribute: the default one, scheduling inherited from the creator, named after
    // the thread id.
    CHECK(CALL(m, shim_pthread_setprio, CALL(m, shim_pthread_self), 600) == 0);
    memset(in, 0, sizeof(*in));
    const uint64_t t2 = spawn(entry_info, d);
    wait_flag(d + offsetof(Info, ready));
    CHECK(in->policy == 1 && in->prio == 600);
    char want[32];
    snprintf(want, sizeof want, "Thread%d", (int)in->tid);
    CHECK(strcmp(in->name, want) == 0);
    set_flag(d + offsetof(Info, go), 1);
    CHECK(join(t2) == 0x77);

    // The guest's own stack.
    const uint64_t stack = galloc(0x8000);
    CHECK(CALL(m, shim_pthread_attr_setstack, a, stack, 0x8000) == 0);
    memset(in, 0, sizeof(*in));
    in->attr = galloc(8);
    CHECK(CALL(m, shim_pthread_attr_init, in->attr) == 0);
    const uint64_t t3 = spawn_attr(entry_info, d, a);
    wait_flag(d + offsetof(Info, ready));
    CHECK(in->stackaddr == stack && in->stacksize == 0x8000);
    CHECK(in->rsp >= stack && in->rsp < stack + 0x8000);
    set_flag(d + offsetof(Info, go), 1);
    CHECK(join(t3) == 0x77);

    // A suspended-create attribute is not supported.
    CHECK(CALL(m, shim_pthread_attr_setcreatesuspend_np, a) == 0);
    CHECK(R32(m, shim_scePthreadCreate, var, a, guest_fn(entry_info), d, 0) == SCE(ENOTSUP));
    CHECK(CALL(m, shim_sched_get_priority_max, 1) == 767 && CALL(m, shim_sched_get_priority_min, 3) == 256);
    CHECK(CALL(m, shim_sched_get_priority_max, 2) == EINVAL);
    puts("PASS create");
}

// ---- join: argument checks, a timed join that runs out, detached threads.

static void entry_wait_go(GuestContext *c) {
    wait_flag(c->rdi);
    GUEST_RET(c, 0x55);
}

static void test_join(void) {
    GuestContext *m = g_proc;
    const uint64_t self = CALL(m, shim_pthread_self);
    CHECK(CALL(m, shim_pthread_join, 0, 0) == EINVAL);
    CHECK(CALL(m, shim_pthread_join, self, 0) == EDEADLK);
    CHECK(CALL(m, shim_pthread_join, 0xdead, 0) == ESRCH);

    const uint64_t go = galloc(8), out = galloc(8);
    const uint64_t t = spawn(entry_wait_go, go);
    const uint64_t bad = galloc(16);
    u64p(bad)[1] = 1000000000;
    CHECK(CALL(m, shim_pthread_timedjoin_np, t, out, bad) == EINVAL);
    CHECK(CALL(m, shim_pthread_timedjoin_np, t, out, 0) == EINVAL);
    const double t0 = mono_s();
    CHECK(CALL(m, shim_pthread_timedjoin_np, t, out, deadline(CLOCK_REALTIME, 50)) == ETIMEDOUT);
    CHECK(mono_s() - t0 >= 0.04);
    set_flag(go, 1);
    CHECK(CALL(m, shim_pthread_timedjoin_np, t, out, deadline(CLOCK_REALTIME, 5000)) == 0 && *u64p(out) == 0x55);

    // A detached thread cannot be joined or detached again.
    const uint64_t a = galloc(8);
    CHECK(CALL(m, shim_pthread_attr_init, a) == 0);
    CHECK(CALL(m, shim_pthread_attr_setdetachstate, a, 1) == 0);
    set_flag(go, 0);
    const uint64_t td = spawn_attr(entry_wait_go, go, a);
    CHECK(CALL(m, shim_pthread_join, td, 0) == EINVAL);
    CHECK(R32(m, shim_scePthreadDetach, td) == SCE(EINVAL));
    const uint64_t go2 = galloc(8);
    const uint64_t t2 = spawn(entry_wait_go, go2);
    CHECK(CALL(m, shim_pthread_detach, t2) == 0);
    CHECK(CALL(m, shim_pthread_detach, t2) == EINVAL);
    CHECK(CALL(m, shim_pthread_detach, 0) == EINVAL);
    set_flag(go, 1);
    set_flag(go2, 1);
    puts("PASS join");
}

// ---- keys: pthread_spec.cpp's key table and the order of a thread's exit work.

static uint64_t g_key, g_order_addr;
static _Atomic int g_order;

static void key_dtor(GuestContext *c) {
    u64p(g_order_addr)[0] = c->rdi;                    // the value
    u64p(g_order_addr)[1] = (uint64_t)++g_order;      // when
    GUEST_RET(c, 0);
}

static void thread_dtors(GuestContext *c) {
    u64p(g_order_addr)[2] = (uint64_t)++g_order;
    GUEST_RET(c, 0);
}

static void cleanup_handler(GuestContext *c) {
    u64p(g_order_addr)[3] = (uint64_t)++g_order;
    u64p(g_order_addr)[4] = c->rdi;
    GUEST_RET(c, 0);
}

static void entry_keys(GuestContext *c) {
    CHECK(CALL(c, shim_pthread_getspecific, g_key) == 0); // values are per thread
    CHECK(CALL(c, shim_pthread_setspecific, g_key, 0x99) == 0);
    CALL(c, shim___pthread_cleanup_push_imp, guest_fn(cleanup_handler), 0x42, 0);
    GUEST_RET(c, 0);
}

static void test_keys(void) {
    GuestContext *m = g_proc;
    const uint64_t k = galloc(8);
    g_order_addr = galloc(64);
    CHECK(CALL(m, shim_pthread_key_create, k, guest_fn(key_dtor)) == 0);
    g_key = *(uint32_t *)(mem + k);
    CHECK(g_key == 0); // the first free slot
    CHECK(CALL(m, shim_pthread_setspecific, g_key, 0x1234) == 0);
    CHECK(CALL(m, shim_pthread_getspecific, g_key) == 0x1234);

    // Cleanup handlers, then the _sceKernelSetThreadDtors routine, then key destructors.
    ps4_thread_set_dtors(guest_fn(thread_dtors));
    join(spawn(entry_keys, 0));
    ps4_thread_set_dtors(0);
    CHECK(u64p(g_order_addr)[3] == 1 && u64p(g_order_addr)[4] == 0x42);
    CHECK(u64p(g_order_addr)[2] == 2);
    CHECK(u64p(g_order_addr)[1] == 3 && u64p(g_order_addr)[0] == 0x99);

    CHECK(R32(m, shim_scePthreadKeyDelete, g_key) == 0);
    CHECK(CALL(m, shim_pthread_key_delete, g_key) == EINVAL);
    CHECK(CALL(m, shim_pthread_key_delete, 300) == EINVAL);
    CHECK(CALL(m, shim_pthread_setspecific, g_key, 1) == EINVAL);
    CHECK(R32(m, shim_scePthreadSetspecific, g_key, 1) == SCE(EINVAL));
    CHECK(CALL(m, shim_pthread_getspecific, g_key) == 0);
    // A new key in the same slot does not see the old key's value.
    CHECK(CALL(m, shim_pthread_key_create, k, 0) == 0 && *(uint32_t *)(mem + k) == g_key);
    CHECK(CALL(m, shim_pthread_getspecific, g_key) == 0);
    CHECK(ps4_pthread_key_destructor(g_key) == 0);
    // 256 keys in all.
    int made = 1;
    while (R32(m, shim_scePthreadKeyCreate, k, 0) == 0) made++;
    CHECK(made == RECOMP_PTHREAD_KEYS_MAX);
    CHECK(CALL(m, shim_pthread_key_create, k, 0) == EAGAIN);
    puts("PASS keys");
}

// ---- cancel: deferred cancellation at its cancellation points.

typedef struct {
    uint64_t mutex, cond, ready, go, mark, sem, target, old;
} CancelData;

static void unlock_handler(GuestContext *c) {
    CancelData *d = (CancelData *)(mem + c->rdi);
    d->mark = 1;
    CHECK(CALL(c, shim_pthread_mutex_unlock, c->rdi + offsetof(CancelData, mutex)) == 0);
    GUEST_RET(c, 0);
}

static void entry_cond_forever(GuestContext *c) {
    const uint64_t d = c->rdi;
    CHECK(CALL(c, shim_pthread_mutex_lock, d + offsetof(CancelData, mutex)) == 0);
    CALL(c, shim_pthread_cleanup_push, guest_fn(unlock_handler), d);
    set_flag(d + offsetof(CancelData, ready), 1);
    for (;;) CALL(c, shim_pthread_cond_wait, d + offsetof(CancelData, cond), d + offsetof(CancelData, mutex));
}

static void entry_disabled(GuestContext *c) {
    const uint64_t d = c->rdi;
    CHECK(CALL(c, shim_pthread_setcancelstate, 1, d + offsetof(CancelData, old)) == 0);
    set_flag(d + offsetof(CancelData, ready), 1);
    wait_flag(d + offsetof(CancelData, go));
    CALL(c, shim_pthread_testcancel); // disabled: no effect
    ((CancelData *)(mem + d))->mark = 1;
    CALL(c, shim_scePthreadSetcancelstate, 0, 0); // enabling acts on the pending cancel
    ((CancelData *)(mem + d))->mark = 2;
    GUEST_RET(c, 0);
}

static void entry_join_target(GuestContext *c) {
    CancelData *d = (CancelData *)(mem + c->rdi);
    set_flag(c->rdi + offsetof(CancelData, ready), 1);
    CALL(c, shim_pthread_join, d->target, 0);
    d->mark = 9; // not reached
    GUEST_RET(c, 0);
}

static void entry_sem_wait(GuestContext *c) {
    set_flag(c->rdi + offsetof(CancelData, ready), 1);
    CALL(c, shim_sem_wait, c->rdi + offsetof(CancelData, sem));
    ((CancelData *)(mem + c->rdi))->mark = 9; // not reached
    GUEST_RET(c, 0);
}

static void test_cancel(void) {
    GuestContext *m = g_proc;
    const uint64_t d = galloc(sizeof(CancelData));
    CancelData *cd = (CancelData *)(mem + d);

    // Blocked in a condition wait: woken, the mutex taken again, the cleanup handler run.
    CHECK(CALL(m, shim_pthread_mutex_init, d + offsetof(CancelData, mutex), 0) == 0);
    CHECK(CALL(m, shim_pthread_cond_init, d + offsetof(CancelData, cond), 0) == 0);
    const uint64_t t1 = spawn(entry_cond_forever, d);
    wait_flag(d + offsetof(CancelData, ready));
    sleep_ms(30);
    CHECK(CALL(m, shim_pthread_cancel, t1) == 0);
    CHECK(join(t1) == PS4_PTHREAD_CANCELED);
    CHECK(cd->mark == 1);
    CHECK(CALL(m, shim_pthread_mutex_trylock, d + offsetof(CancelData, mutex)) == 0);
    CHECK(CALL(m, shim_pthread_mutex_unlock, d + offsetof(CancelData, mutex)) == 0);

    // Disabled: the cancel waits until the thread enables cancellation.
    memset(cd, 0, sizeof(*cd));
    const uint64_t t2 = spawn(entry_disabled, d);
    wait_flag(d + offsetof(CancelData, ready));
    CHECK(R32(m, shim_scePthreadCancel, t2) == 0);
    set_flag(d + offsetof(CancelData, go), 1);
    CHECK(join(t2) == PS4_PTHREAD_CANCELED);
    CHECK(cd->mark == 1 && cd->old == 0);

    // Blocked in a join: backed out, so the target can still be joined.
    memset(cd, 0, sizeof(*cd));
    const uint64_t go = galloc(8);
    cd->target = spawn(entry_wait_go, go);
    const uint64_t t3 = spawn(entry_join_target, d);
    wait_flag(d + offsetof(CancelData, ready));
    sleep_ms(30);
    CHECK(CALL(m, shim_pthread_cancel, t3) == 0);
    CHECK(join(t3) == PS4_PTHREAD_CANCELED && cd->mark == 0);
    set_flag(go, 1);
    CHECK(join(cd->target) == 0x55);

    // Blocked in sem_wait.
    memset(cd, 0, sizeof(*cd));
    CHECK(CALL(m, shim_sem_init, d + offsetof(CancelData, sem), 0, 0) == 0);
    const uint64_t t4 = spawn(entry_sem_wait, d);
    wait_flag(d + offsetof(CancelData, ready));
    sleep_ms(30);
    CHECK(CALL(m, shim_pthread_cancel, t4) == 0);
    CHECK(join(t4) == PS4_PTHREAD_CANCELED && cd->mark == 0);
    CHECK(CALL(m, shim_sem_destroy, d + offsetof(CancelData, sem)) == 0);

    const uint64_t o = galloc(8);
    CHECK(CALL(m, shim_pthread_setcanceltype, 1, o) == EINVAL);
    CHECK(R32(m, shim_scePthreadSetcanceltype, 0, o) == 0 && *i32p(o) == 0);
    CHECK(CALL(m, shim_pthread_setcancelstate, 5, o) == EINVAL);
    CHECK(CALL(m, shim_pthread_cancel, 0xdead) == ESRCH);
    CHECK(R32(m, shim_scePthreadCancel, 0xdead) == SCE(ESRCH));
    puts("PASS cancel");
}

// ---- mutex: attributes, init checks, destroy of a locked mutex, timed locks, per-mutex
// settings.

static void entry_hold(GuestContext *c) {
    const uint64_t d = c->rdi; // [mutex, ready, go]
    CHECK(CALL(c, shim_pthread_mutex_lock, d) == 0);
    set_flag(d + 8, 1);
    wait_flag(d + 16);
    CHECK(CALL(c, shim_pthread_mutex_unlock, d) == 0);
    GUEST_RET(c, 0);
}

static void test_mutex(void) {
    GuestContext *m = g_proc;
    const uint64_t a = galloc(8), o = galloc(8), mx = galloc(8);
    CHECK(R32(m, shim_scePthreadMutexattrInit, a) == 0);
    CHECK(CALL(m, shim_pthread_mutexattr_gettype, a, o) == 0 && *i32p(o) == 1);
    CHECK(CALL(m, shim_pthread_mutexattr_settype, a, 0) == EINVAL);
    CHECK(R32(m, shim_scePthreadMutexattrSettype, a, 5) == SCE(EINVAL));
    CHECK(CALL(m, shim_pthread_mutexattr_settype, a, 2) == 0);
    CHECK(R32(m, shim_scePthreadMutexattrGettype, a, o) == 0 && *i32p(o) == 2);
    CHECK(CALL(m, shim_pthread_mutexattr_getprotocol, a, o) == 0 && *i32p(o) == 0);
    CHECK(CALL(m, shim_pthread_mutexattr_getprioceiling, a, o) == EINVAL);
    CHECK(CALL(m, shim_pthread_mutexattr_setprotocol, a, 3) == EINVAL);
    CHECK(CALL(m, shim_pthread_mutexattr_setprotocol, a, 2) == 0);
    CHECK(CALL(m, shim_pthread_mutexattr_getprioceiling, a, o) == 0 && *i32p(o) == 767);
    CHECK(CALL(m, shim_pthread_mutexattr_setprioceiling, a, 255) == EINVAL);
    CHECK(R32(m, shim_scePthreadMutexattrSetprioceiling, a, 300) == 0);
    CHECK(R32(m, shim_scePthreadMutexattrGetprioceiling, a, o) == 0 && *i32p(o) == 300);
    CHECK(CALL(m, shim_pthread_mutexattr_getpshared, a, o) == 0 && *i32p(o) == 0);
    CHECK(CALL(m, shim_pthread_mutexattr_setpshared, a, 1) == EINVAL);
    CHECK(CALL(m, shim_pthread_mutexattr_setprotocol, a, 0) == 0);
    // getkind_np takes the attribute itself; ORBIS() turns the kind into an error code.
    CHECK(CALL(m, shim_pthread_mutexattr_getkind_np, *u64p(a)) == 2);
    CHECK(R32(m, shim_scePthreadMutexattrGetkind, *u64p(a)) == SCE(2));
    CHECK((int32_t)CALL(m, shim_pthread_mutexattr_getkind_np, 0) == -1);
    CHECK(*(int *)(mem + m->fs_base + RECOMP_TCB_ERRNO_SLOT) == EINVAL);
    CHECK((int32_t)CALL(m, shim_pthread_mutexattr_setkind_np, 0, 2) == -1);

    // A recursive mutex from the attribute; a locked one cannot be destroyed.
    CHECK(R32(m, shim_scePthreadMutexInit, mx, a, gstr("TestMutex")) == 0);
    CHECK(CALL(m, shim_pthread_mutex_lock, mx) == 0);
    CHECK(CALL(m, shim_pthread_mutex_lock, mx) == 0);
    CHECK(CALL(m, shim_pthread_mutex_isowned_np, mx) == 1);
    CHECK(R32(m, shim_scePthreadMutexIsownedNp, mx) == SCE(1));
    CHECK(CALL(m, shim_pthread_mutex_destroy, mx) == EBUSY);
    CHECK(CALL(m, shim_pthread_mutex_unlock, mx) == 0);
    CHECK(CALL(m, shim_pthread_mutex_unlock, mx) == 0);
    CHECK(CALL(m, shim_pthread_mutex_isowned_np, mx) == 0);
    CHECK(R32(m, shim_scePthreadMutexDestroy, mx) == 0);

    // An attribute with an unknown kind is rejected at init.
    CHECK(CALL(m, shim_pthread_mutexattr_setkind_np, a, 9) == 0);
    CHECK(CALL(m, shim_pthread_mutex_init, mx, a) == EINVAL);
    CHECK(R32(m, shim_scePthreadMutexInitForInternalLibc, mx, a) == SCE(EINVAL));
    CHECK(CALL(m, shim_pthread_mutexattr_destroy, a) == 0);
    CHECK(R32(m, shim_scePthreadMutexattrDestroy, a) == SCE(EINVAL));
    CHECK(R32(m, shim_scePthreadMutexattrInitForInternalLibc, a) == 0);

    // Timed locks of a mutex another thread holds.
    const uint64_t h = galloc(24);
    CHECK(CALL(m, shim_pthread_mutex_init, h, 0) == 0);
    const uint64_t t = spawn(entry_hold, h);
    wait_flag(h + 8);
    CHECK(CALL(m, shim_pthread_mutex_reltimedlock_np, h, 0) == ETIMEDOUT);
    CHECK(CALL(m, shim_pthread_mutex_reltimedlock_np, h, 20000) == ETIMEDOUT);
    CHECK(R32(m, shim_scePthreadMutexTimedlock, h, 0) == SCE(ETIMEDOUT));
    const uint64_t bad = galloc(16);
    u64p(bad)[1] = 2000000000;
    CHECK(CALL(m, shim_pthread_mutex_timedlock, h, bad) == EINVAL);
    CHECK(CALL(m, shim_pthread_mutex_timedlock, h, deadline(CLOCK_REALTIME, 20)) == ETIMEDOUT);
    set_flag(h + 16, 1);
    join(t);
    CHECK(CALL(m, shim_pthread_mutex_reltimedlock_np, h, 1000000) == 0);
    CHECK(CALL(m, shim_pthread_mutex_unlock, h) == 0);

    // Spin and yield counts: 0 for a default mutex, 2000 spins for the adaptive initializer.
    CHECK(CALL(m, shim_pthread_mutex_getspinloops_np, h, o) == 0 && *i32p(o) == 0);
    CHECK(R32(m, shim_scePthreadMutexSetspinloops, h, 77) == 0);
    CHECK(R32(m, shim_scePthreadMutexGetspinloops, h, o) == 0 && *i32p(o) == 77);
    CHECK(CALL(m, shim_pthread_mutex_setyieldloops_np, h, 5) == 0);
    CHECK(CALL(m, shim_pthread_mutex_getyieldloops_np, h, o) == 0 && *i32p(o) == 5);
    const uint64_t adaptive = galloc(8);
    *u64p(adaptive) = 1; // PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
    CHECK(CALL(m, shim_pthread_mutex_getspinloops_np, adaptive, o) == 0 && *i32p(o) == 2000);
    CHECK(CALL(m, shim_pthread_mutex_setname_np, h, 0) == 0);
    puts("PASS mutex");
}

// ---- cond: attribute clocks, timed waits on the condition's clock, reltimed waits,
// signalto_np.

static void entry_cond_once(GuestContext *c) {
    const uint64_t d = c->rdi; // [mutex, cond, ready, woke]
    CHECK(CALL(c, shim_pthread_mutex_lock, d) == 0);
    set_flag(d + 16, 1);
    CHECK(CALL(c, shim_pthread_cond_wait, d + 8, d) == 0);
    set_flag(d + 24, 1);
    CHECK(CALL(c, shim_pthread_mutex_unlock, d) == 0);
    GUEST_RET(c, 0);
}

static void test_cond(void) {
    GuestContext *m = g_proc;
    const uint64_t a = galloc(8), o = galloc(8), mx = galloc(8), cv = galloc(8);
    CHECK(R32(m, shim_scePthreadCondattrInit, a) == 0);
    CHECK(CALL(m, shim_pthread_condattr_getclock, a, o) == 0 && *i32p(o) == 0);
    CHECK(CALL(m, shim_pthread_condattr_setclock, a, 3) == EINVAL);
    CHECK(R32(m, shim_scePthreadCondattrSetclock, a, 4) == 0);
    CHECK(R32(m, shim_scePthreadCondattrGetclock, a, o) == 0 && *i32p(o) == 4);
    CHECK(CALL(m, shim_pthread_condattr_getpshared, a, o) == 0 && *i32p(o) == 0);
    CHECK(R32(m, shim_scePthreadCondattrSetpshared, a, 1) == SCE(EINVAL));

    // A monotonic-clock condition: the deadline is on CLOCK_MONOTONIC.
    CHECK(CALL(m, shim_pthread_cond_init, cv, a) == 0);
    CHECK(CALL(m, shim_pthread_condattr_destroy, a) == 0);
    CHECK(CALL(m, shim_pthread_condattr_destroy, a) == EINVAL);
    CHECK(CALL(m, shim_pthread_mutex_init, mx, 0) == 0);
    CHECK(CALL(m, shim_pthread_cond_wait, cv, mx) == EPERM);
    CHECK(CALL(m, shim_pthread_mutex_lock, mx) == 0);
    double t0 = mono_s();
    CHECK(CALL(m, shim_pthread_cond_timedwait, cv, mx, deadline(CLOCK_MONOTONIC, 80)) == ETIMEDOUT);
    double dt = mono_s() - t0;
    CHECK(dt >= 0.07 && dt < 2.0);
    const uint64_t bad = galloc(16);
    u64p(bad)[1] = 1000000000;
    CHECK(CALL(m, shim_pthread_cond_timedwait, cv, mx, bad) == EINVAL);
    CHECK(CALL(m, shim_pthread_cond_timedwait, cv, mx, 0) == EINVAL);
    t0 = mono_s();
    CHECK(CALL(m, shim_pthread_cond_reltimedwait_np, cv, mx, 30000) == ETIMEDOUT);
    CHECK(mono_s() - t0 >= 0.025);
    CHECK(R32(m, shim_scePthreadCondTimedwait, cv, mx, 1000) == SCE(ETIMEDOUT));
    CHECK(CALL(m, shim_pthread_mutex_unlock, mx) == 0);
    CHECK(R32(m, shim_scePthreadCondDestroy, cv) == 0);

    // A realtime condition (no attribute).
    CHECK(R32(m, shim_scePthreadCondInit, cv, 0, gstr("cv")) == 0);
    CHECK(CALL(m, shim_pthread_mutex_lock, mx) == 0);
    CHECK(CALL(m, shim_pthread_cond_timedwait, cv, mx, deadline(CLOCK_REALTIME, 30)) == ETIMEDOUT);
    CHECK(CALL(m, shim_pthread_mutex_unlock, mx) == 0);

    // signalto_np: 1 while the thread does not wait on the condition, then it wakes it.
    const uint64_t d = galloc(32);
    CHECK(CALL(m, shim_pthread_mutex_init, d, 0) == 0);
    CHECK(CALL(m, shim_pthread_cond_init, d + 8, 0) == 0);
    CHECK(CALL(m, shim_pthread_cond_signalto_np, d + 8, 0xdead) == ESRCH);
    CHECK(CALL(m, shim_pthread_cond_signalto_np, d + 8, CALL(m, shim_pthread_self)) == 1);
    CHECK(R32(m, shim_scePthreadCondSignalto, d + 8, CALL(m, shim_pthread_self)) == SCE(1));
    const uint64_t t = spawn(entry_cond_once, d);
    wait_flag(d + 16);
    const double until = mono_s() + 5;
    uint64_t r;
    while ((r = CALL(m, shim_pthread_cond_signalto_np, d + 8, t)) == 1 && mono_s() < until) sched_yield();
    CHECK(r == 0);
    join(t);
    CHECK(*u64p(d + 24) == 1);
    CHECK(CALL(m, shim_pthread_cond_setname_np, d + 8, 0) == EINVAL);
    CHECK(CALL(m, shim_pthread_cond_setname_np, d + 8, gstr("x")) == 0);
    puts("PASS cond");
}

// ---- rwlock: timed and reltimed locks against a holder, the scePthreadRwlock codes.

static void entry_rw_hold(GuestContext *c) {
    const uint64_t d = c->rdi; // [rwlock, ready, go, write]
    if (*u64p(d + 24)) {
        CHECK(CALL(c, shim_pthread_rwlock_wrlock, d) == 0);
    } else {
        CHECK(CALL(c, shim_pthread_rwlock_rdlock, d) == 0);
    }
    set_flag(d + 8, 1);
    wait_flag(d + 16);
    CHECK(CALL(c, shim_pthread_rwlock_unlock, d) == 0);
    GUEST_RET(c, 0);
}

static void test_rwlock(void) {
    GuestContext *m = g_proc;
    const uint64_t d = galloc(32);
    CHECK(R32(m, shim_scePthreadRwlockInit, d, 0) == 0);
    *u64p(d + 24) = 1;
    uint64_t t = spawn(entry_rw_hold, d);
    wait_flag(d + 8);
    CHECK(CALL(m, shim_pthread_rwlock_reltimedrdlock_np, d, 20000) == ETIMEDOUT);
    CHECK(R32(m, shim_scePthreadRwlockTimedrdlock, d, 1000) == SCE(ETIMEDOUT));
    CHECK(R32(m, shim_scePthreadRwlockTimedwrlock, d, 1000) == SCE(ETIMEDOUT));
    const uint64_t bad = galloc(16);
    u64p(bad)[1] = 1000000000;
    CHECK(CALL(m, shim_pthread_rwlock_timedrdlock, d, bad) == EINVAL);
    CHECK(CALL(m, shim_pthread_rwlock_timedwrlock, d, deadline(CLOCK_REALTIME, 20)) == ETIMEDOUT);
    CHECK(R32(m, shim_scePthreadRwlockTryrdlock, d) == SCE(EBUSY));
    set_flag(d + 16, 1);
    join(t);

    // A writer that times out behind a reader does not hold back later readers.
    *u64p(d + 8) = *u64p(d + 16) = *u64p(d + 24) = 0;
    t = spawn(entry_rw_hold, d);
    wait_flag(d + 8);
    CHECK(CALL(m, shim_pthread_rwlock_reltimedwrlock_np, d, 20000) == ETIMEDOUT);
    CHECK(CALL(m, shim_pthread_rwlock_timedrdlock, d, deadline(CLOCK_REALTIME, 1000)) == 0);
    CHECK(CALL(m, shim_pthread_rwlock_unlock, d) == 0);
    set_flag(d + 16, 1);
    join(t);

    CHECK(R32(m, shim_scePthreadRwlockWrlock, d) == 0);
    CHECK(R32(m, shim_scePthreadRwlockWrlock, d) == SCE(EDEADLK));
    CHECK(R32(m, shim_scePthreadRwlockUnlock, d) == 0);
    CHECK(R32(m, shim_scePthreadRwlockUnlock, d) == SCE(EPERM));
    CHECK(CALL(m, shim_pthread_rwlock_setname_np, d, 0) == EINVAL);
    CHECK(R32(m, shim_scePthreadRwlockDestroy, d) == 0);
    puts("PASS rwlock");
}

// ---- barrier.

static void entry_barrier(GuestContext *c) {
    GUEST_RET(c, (uint32_t)CALL(c, shim_scePthreadBarrierWait, c->rdi));
}

static void test_barrier(void) {
    GuestContext *m = g_proc;
    const uint64_t b = galloc(8), a = galloc(8), o = galloc(8);
    CHECK(CALL(m, shim_pthread_barrier_init, b, 0, 0) == EINVAL);
    CHECK(CALL(m, shim_pthread_barrier_init, 0, 0, 2) == EINVAL);
    CHECK(R32(m, shim_scePthreadBarrierattrInit, a) == 0);
    CHECK(CALL(m, shim_pthread_barrierattr_getpshared, a, o) == 0 && *i32p(o) == 0);
    CHECK(CALL(m, shim_pthread_barrierattr_setpshared, a, 1) == EINVAL);
    CHECK(R32(m, shim_scePthreadBarrierInit, b, a, 3, gstr("b")) == 0);
    CHECK(CALL(m, shim_pthread_barrierattr_destroy, a) == 0);
    CHECK(R32(m, shim_scePthreadBarrierattrDestroy, a) == SCE(EINVAL));

    // Three waiters, twice: one of them is the serial thread each time.
    for (int round = 0; round < 2; round++) {
        const uint64_t t1 = spawn(entry_barrier, b), t2 = spawn(entry_barrier, b);
        const uint32_t r0 = (uint32_t)CALL(m, shim_pthread_barrier_wait, b);
        const uint32_t r1 = (uint32_t)join(t1), r2 = (uint32_t)join(t2);
        CHECK((r0 == 0xffffffffu) + (r1 == 0xffffffffu) + (r2 == 0xffffffffu) == 1);
        CHECK((r0 == 0) + (r1 == 0) + (r2 == 0) == 2);
    }
    CHECK(CALL(m, shim_pthread_barrier_destroy, b) == 0);
    CHECK(CALL(m, shim_pthread_barrier_wait, b) == EINVAL);
    CHECK(R32(m, shim_scePthreadBarrierWait, b) == SCE(EINVAL));

    // A barrier with a waiter is busy.
    CHECK(CALL(m, shim_pthread_barrier_init, b, 0, 2) == 0);
    const uint64_t t = spawn(entry_barrier, b);
    sleep_ms(30);
    CHECK(R32(m, shim_scePthreadBarrierDestroy, b) == SCE(EBUSY));
    CALL(m, shim_pthread_barrier_wait, b);
    join(t);
    CHECK(R32(m, shim_scePthreadBarrierDestroy, b) == 0);
    puts("PASS barrier");
}

// ---- once.

static _Atomic int g_once_runs;

static void once_init(GuestContext *c) {
    atomic_fetch_add(&g_once_runs, 1);
    sleep_ms(30);
    GUEST_RET(c, 0);
}

static uint64_t g_once_fn;

static void entry_once(GuestContext *c) {
    GUEST_RET(c, CALL(c, shim_pthread_once, c->rdi, g_once_fn));
}

static void test_once(void) {
    GuestContext *m = g_proc;
    const uint64_t ctl = galloc(8);
    g_once_fn = guest_fn(once_init);
    uint64_t t[4];
    for (int i = 0; i < 4; i++) t[i] = spawn(entry_once, ctl);
    CHECK(R32(m, shim_scePthreadOnce, ctl, g_once_fn) == 0);
    for (int i = 0; i < 4; i++) CHECK(join(t[i]) == 0);
    CHECK(g_once_runs == 1 && *i32p(ctl) == 1);
    *i32p(ctl) = 7;
    CHECK(CALL(m, shim_pthread_once, ctl, g_once_fn) == EINVAL);
    CHECK(R32(m, shim_scePthreadOnce, ctl, g_once_fn) == SCE(EINVAL));
    puts("PASS once");
}

// ---- suspend: suspension stops a thread, user contexts.

static void entry_count(GuestContext *c) {
    const uint64_t d = c->rdi; // [counter, stop]
    while (!__atomic_load_n(u64p(d + 8), __ATOMIC_ACQUIRE)) __atomic_fetch_add(u64p(d), 1, __ATOMIC_RELAXED);
    GUEST_RET(c, 0);
}

static void test_suspend(void) {
    GuestContext *m = g_proc;
    const uint64_t d = galloc(16), uc = galloc(0x500);
    const uint64_t t = spawn(entry_count, d);
    while (__atomic_load_n(u64p(d), __ATOMIC_ACQUIRE) == 0) sched_yield();
    CHECK(CALL(m, shim_pthread_suspend_np, t) == 0);
    const uint64_t c1 = __atomic_load_n(u64p(d), __ATOMIC_ACQUIRE);
    sleep_ms(30);
    CHECK(__atomic_load_n(u64p(d), __ATOMIC_ACQUIRE) == c1);
    // A thread stopped outside recompiled code: the registers of its context, on its stack.
    CHECK(CALL(m, shim_pthread_get_user_context_np, t, uc) == 0);
    const RecompThread *rt = (const RecompThread *)t;
    const uint64_t rsp = *u64p(uc + 0x40 + 0xb8);
    CHECK(rsp >= (uint64_t)(uintptr_t)rt->attr.stackaddr_attr &&
          rsp < (uint64_t)(uintptr_t)rt->attr.stackaddr_attr + rt->attr.stacksize_attr);
    CHECK(CALL(m, shim_pthread_resume_user_context_np, t) == 0);
    sleep_ms(10);
    CHECK(__atomic_load_n(u64p(d), __ATOMIC_ACQUIRE) > c1);
    CHECK(CALL(m, shim_pthread_suspend_user_context_np, t) == 0);
    CHECK(CALL(m, shim_pthread_resume_np, t) == 0);
    set_flag(d + 8, 1);
    join(t);
    CHECK(CALL(m, shim_pthread_suspend_np, t) == ESRCH);
    CHECK(CALL(m, shim_pthread_suspend_np, 0) == EINVAL);

    // The calling thread: the state at its call.
    const uint64_t sp = m->rsp;
    CHECK(CALL(m, shim_pthread_get_user_context_np, CALL(m, shim_pthread_self), uc) == 0);
    CHECK(*u64p(uc + 0x40 + 0xa0) == RET_SLOT && *u64p(uc + 0x40 + 0xb8) == sp);
    CHECK(CALL(m, shim_pthread_get_user_context_np, CALL(m, shim_pthread_self), 0) == EINVAL);
    CHECK(CALL(m, shim_pthread_set_user_context_np, CALL(m, shim_pthread_self), uc) == 0);
    CHECK(CALL(m, shim_pthread_set_user_context_np, 0, uc) == EINVAL);
    puts("PASS suspend");
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    CHECK(mem != MAP_FAILED);
    g_proc = new_context(NULL, 0x40000, 0);
    g_proc->thread_id = 1000;
    g_current_ctx = g_proc;
    recomp_init_main_thread(g_proc);
    const char *c = argv[1];
    if (!strcmp(c, "attr")) test_attr();
    else if (!strcmp(c, "create")) test_thread();
    else if (!strcmp(c, "join")) test_join();
    else if (!strcmp(c, "keys")) test_keys();
    else if (!strcmp(c, "cancel")) test_cancel();
    else if (!strcmp(c, "mutex")) test_mutex();
    else if (!strcmp(c, "cond")) test_cond();
    else if (!strcmp(c, "rwlock")) test_rwlock();
    else if (!strcmp(c, "barrier")) test_barrier();
    else if (!strcmp(c, "once")) test_once();
    else if (!strcmp(c, "suspend")) test_suspend();
    else return 2;
    return 0;
}
