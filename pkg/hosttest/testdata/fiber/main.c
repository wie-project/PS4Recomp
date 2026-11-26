// SPDX-License-Identifier: GPL-2.0-or-later

// libSceFiber against shadPS4's rules: argument checks and their order, the opt param magic,
// context size checking and sceFiberGetInfo's margin, attaching a context, the FPU control words
// a fiber starts with, and Run / Switch / ReturnToThread with real coroutine switches. The guest
// entry functions are C functions in the dispatch table.
#include "ps4_fiber.h"
#include "ps4_threading.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

// Runtime symbols ps4_fiber.c links against.
_Thread_local GuestContext *g_current_ctx;
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];
RecompThread *recomp_get_current_thread(void) { return NULL; }
void recomp_set_current_thread(RecompThread *t) { (void)t; }

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

#define MEM_SIZE 0x200000ull
#define FIBER_A 0x1000ull
#define FIBER_B 0x1100ull
#define NAME_A 0x2000ull
#define NAME_B 0x2040ull
#define OPT 0x3000ull
#define INFO 0x4000ull
#define OUT 0x5000ull
#define CONTEXT_A 0x10000ull
#define CONTEXT_B 0x30000ull
#define CONTEXT_SIZE 0x10000ull
#define THREAD_STACK_TOP 0x100000ull
#define ENTRY_A 0x400000ull
#define ENTRY_B 0x400010ull
#define ENTRY_OVERFLOW 0x400020ull
#define SDK_350 0x3500000u

#define E_NULL ((int32_t)ORBIS_FIBER_ERROR_NULL)
#define E_ALIGNMENT ((int32_t)ORBIS_FIBER_ERROR_ALIGNMENT)
#define E_RANGE ((int32_t)ORBIS_FIBER_ERROR_RANGE)
#define E_INVALID ((int32_t)ORBIS_FIBER_ERROR_INVALID)
#define E_PERMISSION ((int32_t)ORBIS_FIBER_ERROR_PERMISSION)
#define E_STATE ((int32_t)ORBIS_FIBER_ERROR_STATE)

static uint8_t *mem;
static GuestContext *thread_ctx;

static uint64_t *u64_at(uint64_t addr) { return (uint64_t *)(mem + addr); }

// Calls a shim as guest code would: stack arguments above a return address slot.
static int32_t call(GuestContext *c, void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2,
                    uint64_t a3, uint64_t a4, uint64_t a5, const uint64_t *stack, int nstack) {
    c->rsp -= 8 * (uint64_t)nstack;
    for (int i = 0; i < nstack; i++) *u64_at(c->rsp + 8 * (uint64_t)i) = stack[i];
    c->rsp -= 8;
    *u64_at(c->rsp) = 0;
    c->rdi = a0, c->rsi = a1, c->rdx = a2, c->rcx = a3, c->r8 = a4, c->r9 = a5;
    shim(c);
    c->rsp += 8 * (uint64_t)nstack;
    return (int32_t)c->rax;
}

static int32_t initialize(uint64_t fiber, uint64_t name, uint64_t entry, uint64_t arg, uint64_t addr_context,
                          uint64_t size_context, uint64_t opt, uint32_t build_ver) {
    const uint64_t stack[2] = {opt, build_ver};
    return call(thread_ctx, shim_sceFiberInitializeImpl, fiber, name, entry, arg, addr_context, size_context,
                stack, 2);
}

static int32_t initialize_internal(uint64_t fiber, uint64_t entry, uint64_t addr_context, uint64_t size_context,
                                   uint32_t flags, uint32_t build_ver) {
    const uint64_t stack[3] = {0, flags, build_ver};
    return call(thread_ctx, shim_sceFiberInitializeWithInternalOptionImpl, fiber, NAME_A, entry, 0,
                addr_context, size_context, stack, 3);
}

static int32_t call2(GuestContext *c, void (*shim)(GuestContext *), uint64_t a0, uint64_t a1) {
    return call(c, shim, a0, a1, 0, 0, 0, 0, NULL, 0);
}

static int32_t call3(GuestContext *c, void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2) {
    return call(c, shim, a0, a1, a2, 0, 0, 0, NULL, 0);
}

static int32_t call5(GuestContext *c, void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2,
                     uint64_t a3, uint64_t a4) {
    return call(c, shim, a0, a1, a2, a3, a4, 0, NULL, 0);
}

static int32_t get_info(uint64_t fiber, uint64_t *margin) {
    memset(mem + INFO, 0, 128);
    *u64_at(INFO) = 128;
    int32_t ret = call2(thread_ctx, shim_sceFiberGetInfo, fiber, INFO);
    *margin = *u64_at(INFO + 0x48);
    return ret;
}

static void register_entry(uint64_t addr, recomp_fn_t fn) {
    const uint64_t l1 = (addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    const uint64_t l2 = (addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[l1]) g_dispatch_l1[l1] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    if (!g_dispatch_l1[l1][l2]) g_dispatch_l1[l1][l2] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    g_dispatch_l1[l1][l2][addr & DISPATCH_LEVEL_MASK] = fn;
}

// What the fibers saw, checked by the thread after each transfer.
static struct {
    uint64_t a_init, a_run, a_resumed_with;
    uint16_t a_fpu_cw;
    uint32_t a_mxcsr;
    uint32_t b_mxcsr;
    uint64_t b_run, b_resumed_with;
    int32_t a_run_inside, b_switch_to_running;
    uint64_t self;
} seen;

// A: Run inside a fiber fails, switches to B, then returns to the thread twice.
static void entry_a(GuestContext *c) {
    seen.a_init = c->rdi;
    seen.a_run = c->rsi;
    seen.a_fpu_cw = c->fpu_cw;
    seen.a_mxcsr = c->mxcsr;
    seen.a_run_inside = call3(c, shim_sceFiberRun, FIBER_B, 0, 0);
    CHECK(call2(c, shim_sceFiberGetSelf, OUT, 0) == 0);
    seen.self = *u64_at(OUT);
    CHECK(call3(c, shim_sceFiberSwitch, FIBER_B, 21, OUT + 8) == 0);
    seen.a_resumed_with = *u64_at(OUT + 8);
    CHECK(call2(c, shim_sceFiberReturnToThread, 88, OUT + 16) == 0);
    abort(); // never run again
}

// B: started by A's switch, so it starts with A's FPU control words; switching to itself, which
// is running, fails; returns to the thread twice.
static void entry_b(GuestContext *c) {
    seen.b_mxcsr = c->mxcsr;
    seen.b_run = c->rsi;
    seen.b_switch_to_running = call3(c, shim_sceFiberSwitch, FIBER_B, 0, 0);
    CHECK(call2(c, shim_sceFiberReturnToThread, 77, OUT + 24) == 0);
    seen.b_resumed_with = *u64_at(OUT + 24);
    CHECK(call2(c, shim_sceFiberReturnToThread, 99, OUT + 32) == 0);
    abort();
}

// Overwrites the signature at the bottom of its context, then switches away.
static void entry_overflow(GuestContext *c) {
    *u64_at(CONTEXT_A) = 0;
    call3(c, shim_sceFiberSwitch, FIBER_B, 0, 0);
    exit(3); // the switch must abort first
}

static void setup(void) {
    mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(mem != MAP_FAILED);
    thread_ctx = calloc(1, sizeof(GuestContext));
    thread_ctx->mem_base = mem;
    thread_ctx->mem_size = MEM_SIZE;
    thread_ctx->rsp = THREAD_STACK_TOP;
    thread_ctx->fpu_cw = 0x027F;
    thread_ctx->mxcsr = 0x3F80;
    g_current_ctx = thread_ctx;
    strcpy((char *)mem + NAME_A, "fiber-a");
    strcpy((char *)mem + NAME_B, "fiber-b");
    register_entry(ENTRY_A, entry_a);
    register_entry(ENTRY_B, entry_b);
    register_entry(ENTRY_OVERFLOW, entry_overflow);
}

static void test_init_checks(void) {
    CHECK(initialize(0, NAME_A, ENTRY_A, 0, 0, 0, 0, 0) == E_NULL);
    CHECK(initialize(FIBER_A, 0, ENTRY_A, 0, 0, 0, 0, 0) == E_NULL);
    CHECK(initialize(FIBER_A, NAME_A, 0, 0, 0, 0, 0, 0) == E_NULL);
    CHECK(initialize(FIBER_A + 4, NAME_A, ENTRY_A, 0, 0, 0, 0, 0) == E_ALIGNMENT);
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, CONTEXT_A + 8, CONTEXT_SIZE, 0, 0) == E_ALIGNMENT);
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, CONTEXT_A, CONTEXT_SIZE, OPT + 4, 0) == E_ALIGNMENT);
    // Size checks come before the address/size pairing, as in shadPS4.
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, 0, 256, 0, 0) == E_RANGE);
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, CONTEXT_A, 0x1008, 0, 0) == E_INVALID);
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, 0, CONTEXT_SIZE, 0, 0) == E_INVALID);
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, CONTEXT_A, 0, 0, 0) == E_INVALID);
    *u64_at(OPT) = 0;
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, CONTEXT_A, CONTEXT_SIZE, OPT, 0) == E_INVALID);

    CHECK(call2(thread_ctx, shim_sceFiberOptParamInitialize, 0, 0) == E_NULL);
    CHECK(call2(thread_ctx, shim_sceFiberOptParamInitialize, OPT + 4, 0) == E_ALIGNMENT);
    memset(mem + OPT, 0xAB, 128);
    CHECK(call2(thread_ctx, shim_sceFiberOptParamInitialize, OPT, 0) == 0);
    CHECK(*(uint32_t *)(mem + OPT) == 0xBB40E64Du);
    CHECK(mem[OPT + 4] == 0xAB); // only the magic is written
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, CONTEXT_A, CONTEXT_SIZE, OPT, 0) == 0);
    CHECK(*u64_at(CONTEXT_A) == 0x7149F2CA7149F2CAull); // stack signature
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_A, 0, 0, 0, 0, 0, NULL, 0) == 0);
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_A, 0, 0, 0, 0, 0, NULL, 0) == E_INVALID);
    puts("PASS init-checks");
}

static void test_context_size_check(void) {
    uint64_t margin = 0;
    CHECK(call2(thread_ctx, shim_sceFiberStopContextSizeCheck, 0, 0) == E_STATE);
    CHECK(call2(thread_ctx, shim_sceFiberStartContextSizeCheck, 1, 0) == E_INVALID);
    CHECK(call2(thread_ctx, shim_sceFiberStartContextSizeCheck, 0, 0) == 0);
    CHECK(call2(thread_ctx, shim_sceFiberStartContextSizeCheck, 0, 0) == E_STATE);
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, CONTEXT_A, CONTEXT_SIZE, 0, 0) == 0);
    CHECK(*u64_at(CONTEXT_A + 8) == 0xDEADBEEFDEADBEEFull);
    CHECK(*u64_at(CONTEXT_A + CONTEXT_SIZE - 8) == 0xDEADBEEFDEADBEEFull);
    CHECK(get_info(FIBER_A, &margin) == 0);
    CHECK(margin == CONTEXT_SIZE - 8);
    CHECK(strcmp((char *)mem + INFO + 0x28, "fiber-a") == 0);
    *u64_at(CONTEXT_A + 0x800) = 1; // the stack reached this word
    CHECK(get_info(FIBER_A, &margin) == 0);
    CHECK(margin == 0x800 - 8);
    *u64_at(INFO) = 64;
    CHECK(call2(thread_ctx, shim_sceFiberGetInfo, FIBER_A, INFO) == E_INVALID);
    CHECK(call2(thread_ctx, shim_sceFiberStopContextSizeCheck, 0, 0) == 0);
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_A, 0, 0, 0, 0, 0, NULL, 0) == 0);

    // Without context size checking no margin is measured, unless the internal option flags ask.
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, CONTEXT_A, CONTEXT_SIZE, 0, 0) == 0);
    CHECK(get_info(FIBER_A, &margin) == 0);
    CHECK(margin == UINT64_MAX);
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_A, 0, 0, 0, 0, 0, NULL, 0) == 0);
    CHECK(initialize_internal(FIBER_A, ENTRY_A, CONTEXT_A, CONTEXT_SIZE, 0x10, 0) == 0);
    CHECK(get_info(FIBER_A, &margin) == 0);
    CHECK(margin == CONTEXT_SIZE - 8);
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_A, 0, 0, 0, 0, 0, NULL, 0) == 0);
    puts("PASS context-size-check");
}

static void test_attach(void) {
    // A fiber without a context. AttachContextAndRun without a context behaves as Run; the
    // checks of the context to attach come before the state check.
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, 0, 0, 0, 0) == 0);
    CHECK(call5(thread_ctx, shim_sceFiberAttachContextAndRun, FIBER_A, CONTEXT_A + 8, CONTEXT_SIZE, 0, 0) ==
          E_ALIGNMENT);
    CHECK(call5(thread_ctx, shim_sceFiberAttachContextAndRun, FIBER_A, CONTEXT_A, 256, 0, 0) == E_RANGE);
    CHECK(call5(thread_ctx, shim_sceFiberAttachContextAndRun, FIBER_A, CONTEXT_A, 0x1008, 0, 0) == E_INVALID);
    CHECK(call5(thread_ctx, shim_sceFiberAttachContextAndRun, FIBER_A, 0, CONTEXT_SIZE, 0, 0) == E_INVALID);
    CHECK(call5(thread_ctx, shim_sceFiberAttachContextAndSwitch, FIBER_A, 0, 0, 0, 0) == E_PERMISSION);
    CHECK(call5(thread_ctx, shim_sceFiberAttachContextAndSwitch, FIBER_A + 8, 0, 0, 0, 0) == E_INVALID);
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_A, 0, 0, 0, 0, 0, NULL, 0) == 0);

    // A fiber that has a context cannot get another one.
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 0, CONTEXT_A, CONTEXT_SIZE, 0, 0) == 0);
    CHECK(call5(thread_ctx, shim_sceFiberAttachContextAndRun, FIBER_A, CONTEXT_B, CONTEXT_SIZE, 0, 0) ==
          E_INVALID);
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_A, 0, 0, 0, 0, 0, NULL, 0) == 0);
    puts("PASS attach");
}

static void test_run_switch(void) {
    // A starts from an SDK 3.50 build: the default FPU control words. B is older and is started
    // by A's switch, so it keeps A's.
    CHECK(initialize(FIBER_A, NAME_A, ENTRY_A, 5, CONTEXT_A, CONTEXT_SIZE, 0, SDK_350) == 0);
    CHECK(initialize(FIBER_B, NAME_B, ENTRY_B, 6, CONTEXT_B, CONTEXT_SIZE, 0, 0) == 0);
    memset(&seen, 0, sizeof(seen));
    CHECK(call3(thread_ctx, shim_sceFiberSwitch, FIBER_A, 0, 0) == E_PERMISSION);
    CHECK(call2(thread_ctx, shim_sceFiberGetSelf, OUT, 0) == E_PERMISSION);

    // Thread -> A -> B -> thread.
    CHECK(call3(thread_ctx, shim_sceFiberRun, FIBER_A, 11, OUT + 40) == 0);
    CHECK(*u64_at(OUT + 40) == 77);
    CHECK(seen.a_init == 5 && seen.a_run == 11);
    CHECK(seen.a_fpu_cw == 0x037F && seen.a_mxcsr == 0x9FC0);
    CHECK(seen.b_mxcsr == 0x9FC0);
    CHECK(seen.b_run == 21);
    CHECK(seen.a_run_inside == E_PERMISSION);
    CHECK(seen.b_switch_to_running == E_STATE);
    CHECK(seen.self == FIBER_A);
    CHECK(thread_ctx->rsp == THREAD_STACK_TOP);

    // A waits in its switch, B in its return: both are idle and resume where they left off.
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_A + 8, 0, 0, 0, 0, 0, NULL, 0) == E_INVALID);
    CHECK(call3(thread_ctx, shim_sceFiberRun, FIBER_A, 12, OUT + 40) == 0);
    CHECK(seen.a_resumed_with == 12);
    CHECK(*u64_at(OUT + 40) == 88);
    CHECK(call3(thread_ctx, shim_sceFiberRun, FIBER_B, 13, OUT + 40) == 0);
    CHECK(seen.b_resumed_with == 13);
    CHECK(*u64_at(OUT + 40) == 99);
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_A, 0, 0, 0, 0, 0, NULL, 0) == 0);
    CHECK(call(thread_ctx, shim_sceFiberFinalize, FIBER_B, 0, 0, 0, 0, 0, NULL, 0) == 0);
    puts("PASS run-switch");
}

static void test_overflow(void) {
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        CHECK(initialize(FIBER_A, NAME_A, ENTRY_OVERFLOW, 0, CONTEXT_A, CONTEXT_SIZE, 0, 0) == 0);
        CHECK(initialize(FIBER_B, NAME_B, ENTRY_B, 0, CONTEXT_B, CONTEXT_SIZE, 0, 0) == 0);
        call3(thread_ctx, shim_sceFiberRun, FIBER_A, 0, 0);
        exit(4);
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    puts("PASS overflow");
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    setup();
    if (strcmp(argv[1], "init-checks") == 0) test_init_checks();
    else if (strcmp(argv[1], "context-size-check") == 0) test_context_size_check();
    else if (strcmp(argv[1], "attach") == 0) test_attach();
    else if (strcmp(argv[1], "run-switch") == 0) test_run_switch();
    else if (strcmp(argv[1], "overflow") == 0) test_overflow();
    else return 2;
    return 0;
}
