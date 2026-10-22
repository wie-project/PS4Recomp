// Guest-state boundary tests of the ARM64 assembly backend (boundary_test.go builds this with
// the emitted trampoline, the generated shim stubs, guest.s and the runtime sources).
// Usage: harness <case>; prints "PASS <case>" and exits 0, or prints what differs and exits 1.
#include "recomp_runtime.h"
#include "ps4_fiber.h"
#include "ps4_threading.h"
#include "ps4_kernel_sys.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <mach/mach_time.h>

// Guest data layout (guest.s has the same addresses).
#define D 0x1000000ULL
#define FIBER_A (D + 0x000)
#define FIBER_B (D + 0x100)
#define NAME_A (D + 0x200)
#define NAME_B (D + 0x210)
#define ARGRET1 (D + 0x300)
#define ARGRET2 (D + 0x308)
#define ARGRUN_A (D + 0x310)
#define ARGRUN_A2 (D + 0x318)
#define ARGRUN_B (D + 0x320)
#define ARGRUN_A3 (D + 0x328)
#define STACK_A 0x1100000ULL
#define STACK_B 0x1200000ULL
#define FIBER_STACK_SIZE 0x10000ULL

enum {
  RES_M1, RES_A1, RES_A2, RES_B1, RES_M2, RES_A_ARGS, RES_B_ARGS, RES_A_PRE, RES_B_PRE, RES_M1_PRE, RES_M2_PRE,
  RES_EXC_PRE, RES_EXC, RES_EXC_HANDLER, RES_GPRS_PRE, RES_GPRS,
};
// Result block words (guest.s STORE_REGS).
enum { W_RAX, W_RBX, W_RCX, W_RDX, W_RSI, W_RDI, W_RBP, W_RSP, W_R8, W_R15 = W_R8 + 7, W_FS, W_MEM };

// Guest addresses of the guest functions.
#define ADDR_FIBER_MAIN1 0x400000ULL
#define ADDR_FIBER_MAIN2 0x400100ULL
#define ADDR_FIBER_A 0x400200ULL
#define ADDR_FIBER_B 0x400300ULL
#define ADDR_EXC_MAIN 0x400400ULL
#define ADDR_EXC_HANDLER 0x400500ULL
#define ADDR_GPRS 0x400600ULL
#define ADDR_XMM 0x400700ULL
#define ADDR_XMM_CALLBACK 0x400800ULL
#define RES_XMM_ADDR (D + 0x1000 + 0x1000)
#define RES_FPCR_ADDR (D + 0x1000 + 0x1100)
#define RES_FP_ADDR (D + 0x1000 + 0x1200) // M1, A1, A2, B1, M2: {FPCR, ctx->mxcsr}
#define ADDR_FPCR 0x400900ULL

// FPCR values for an MXCSR rounding mode (RMode bits 23:22) and flush to zero (bit 24).
#define FPCR_RP (1ULL << 22)
#define FPCR_RM (2ULL << 22)
#define FPCR_RZ (3ULL << 22)
#define FPCR_FZ (1ULL << 24)

static uint64_t read_fpcr(void) {
  uint64_t v;
  __asm__ volatile("mrs %0, fpcr" : "=r"(v));
  return v;
}

void guest_fiber_main1(void);
void guest_fiber_main2(void);
void guest_fiber_a(void);
void guest_fiber_b(void);
void guest_exc_main(void);
void guest_exc_handler(void);
void guest_gprs(void);
void guest_xmm(void);
void guest_xmm_callback(void);
void guest_clobber_fp(void);
void guest_fpcr(void);
void guest_spin(void);
#define ADDR_SPIN 0x400d00ULL
#define SPIN_FLAG (D + 0x600)
#define SPIN_HANDLE (D + 0x608)
void guest_crash(void);
void guest_helper_loop(void);
void guest_helper_window(void);
#define ADDR_HELPER_WINDOW 0x500300ULL
void guest_shim_bench(void);
#define ADDR_SHIM_BENCH 0x401300ULL
void guest_uc_check(void);
void guest_exc_fiber_main(void);
void guest_exc_fiber_a(void);
#define ADDR_HELPER_LOOP 0x500200ULL
#define ADDR_UC_CHECK 0x401000ULL
#define ADDR_EXC_FIBER_MAIN 0x401100ULL
#define ADDR_EXC_FIBER_A 0x401200ULL
void guest_exc_state_handler(void);
#define ADDR_EXC_STATE_HANDLER 0x401400ULL
#define EXC_KEY_OUT (D + 0x6f0)
#define HELPER_LOOP_FLAG 0x1000800ULL
void guest_longjmp(void);
void recomp_register_guest_modules(void);  // emitted guest_modules.c
void shim_sceKernelGetModuleList(GuestContext *ctx);
void shim_sceKernelGetModuleInfo(GuestContext *ctx);
void shim_sceKernelGetModuleInfo2(GuestContext *ctx);
void shim_sceKernelGetModuleInfoInternal(GuestContext *ctx);
#define ADDR_LONGJMP 0x400f00ULL
#define ADDR_CRASH 0x400e00ULL
void guest_rip(void);
#define ADDR_RIP 0x400c00ULL
// boundary_test.go boundaryStubAddr: guest address of the n-th generated shim stub.
#define STUB_ADDR(n) (0x7f0000000ULL + (uint64_t)(n) * 0x10)
#define STUB_TEST_RIP 12
void guest_thread_main(void);
void guest_thread_entry(void);
#define ADDR_THREAD_MAIN 0x400b00ULL
#define ADDR_THREAD_ENTRY 0x400a00ULL
#define THREAD_RET (D + 0x408)
#define TLS_INDEX (D + 0x500)
#define TLS_TEMPLATE 0x1004000ULL
#define RES_THR_PRE 0x1300
#define RES_THR_MAIN 0x1400
#define RES_THR_MAIN_FP 0x1500
#define RES_THR_CHILD 0x1600
void guest_lifted_vpcmpistri(void);
void guest_lifted_vaeskeygenassist(void);
#define ADDR_LIFTED_VPCMPISTRI 0x500000ULL
#define ADDR_LIFTED_VAESKEYGENASSIST 0x500100ULL
#define RES_LIFTED_VPCMPISTRI 0x1003000ULL
#define RES_LIFTED_VAESKEYGENASSIST 0x1003400ULL
uint64_t host_enter_guest_d8_d15(GuestContext *ctx, uint64_t fn);

static GuestContext *g_proc;
static int g_failures;

static uint64_t *gmem64(uint64_t addr) { return (uint64_t *)(g_proc->mem_base + addr); }
static uint64_t *res(int n) { return gmem64(D + 0x1000 + (uint64_t)n * 0x100); }

static void fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  printf("FAIL: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  g_failures++;
}

static void expect(const char *what, uint64_t got, uint64_t want) {
  if (got != want) fail("%s = 0x%llx, want 0x%llx", what, (unsigned long long)got, (unsigned long long)want);
}

// The registers guest.s SET_PATTERN set (only those a call must preserve are checked) and the
// guest stack pointer, FS base and memory base of block n.
static void check_pattern(const char *name, int n, uint64_t p, uint64_t rsp, uint64_t fs) {
  char what[64];
  const uint64_t *r = res(n);
  snprintf(what, sizeof what, "%s rbx", name);
  expect(what, r[W_RBX], p + 0x01);
  snprintf(what, sizeof what, "%s rbp", name);
  expect(what, r[W_RBP], p + 0x02);
  for (int i = 12; i <= 15; i++) {
    snprintf(what, sizeof what, "%s r%d", name, i);
    expect(what, r[W_R8 + i - 8], p + (uint64_t)i);
  }
  snprintf(what, sizeof what, "%s rsp", name);
  expect(what, r[W_RSP], rsp);
  snprintf(what, sizeof what, "%s FS base (x27)", name);
  expect(what, r[W_FS], fs);
  snprintf(what, sizeof what, "%s memory base (x28)", name);
  expect(what, r[W_MEM], (uint64_t)(uintptr_t)g_proc->mem_base);
}

// Calls a shim from C the way guest code does: return address and a stack argument on the
// guest stack, integer arguments in the System V registers.
static uint64_t call_shim(GuestContext *ctx, void (*shim)(GuestContext *), uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6, uint64_t stack7) {
  const uint64_t rsp = ctx->rsp;
  ctx->rsp -= 16;
  *gmem64(ctx->rsp) = 0;
  *gmem64(ctx->rsp + 8) = stack7;
  ctx->rdi = a1;
  ctx->rsi = a2;
  ctx->rdx = a3;
  ctx->rcx = a4;
  ctx->r8 = a5;
  ctx->r9 = a6;
  shim(ctx);
  ctx->rsp = rsp;
  return ctx->rax;
}

// A shim that does nothing: it only makes the bridge store and reload the guest registers.
void shim_test_noop(GuestContext *ctx) { SHIM_RETURN(); }

static uint64_t tcb_slot(const GuestContext *t) { return *gmem64(t->fs_base + 0x70); }

static void *run_fiber_main2(void *arg) {
  GuestContext *t2 = (GuestContext *)arg;
  g_current_ctx = t2;
  recomp_call_guest(t2, ADDR_FIBER_MAIN2);
  return NULL;
}

// sceFiberRun/Switch/ReturnToThread: every context resumes with its own registers, also
// when a fiber resumes on another guest thread (FS base of that thread).
static void test_fiber(void) {
  GuestContext *t1 = g_proc;
  strcpy((char *)g_proc->mem_base + NAME_A, "A");
  strcpy((char *)g_proc->mem_base + NAME_B, "B");
  expect("init A", call_shim(t1, shim_sceFiberInitializeImpl, FIBER_A, NAME_A, ADDR_FIBER_A, 0xa1a1, STACK_A,
                             FIBER_STACK_SIZE, 0), 0);
  expect("init B", call_shim(t1, shim_sceFiberInitializeImpl, FIBER_B, NAME_B, ADDR_FIBER_B, 0xb1b1, STACK_B,
                             FIBER_STACK_SIZE, 0), 0);
  recomp_call_guest(t1, ADDR_FIBER_MAIN1);

  GuestContext *t2 = recomp_create_thread_context(t1, 0x10000);
  pthread_t th;
  pthread_create(&th, NULL, run_fiber_main2, t2);
  pthread_join(th, NULL);

  const uint64_t fs1 = t1->fs_base, fs2 = t2->fs_base;
  expect("A argOnInitialize", res(RES_A_ARGS)[W_RDI], 0xa1a1);
  expect("A argOnRun", res(RES_A_ARGS)[W_RSI], 0x5150);
  expect("B argOnInitialize", res(RES_B_ARGS)[W_RDI], 0xb1b1);
  expect("B argOnRun", res(RES_B_ARGS)[W_RSI], 0x5151);
  check_pattern("A after Switch(B) on thread 1", RES_A1, 0xa000, res(RES_A_PRE)[W_RSP], fs1);
  expect("A argOnRun from B", *gmem64(ARGRUN_A), 0x5353);
  expect("A rax after Switch", res(RES_A1)[W_RAX], 0);
  check_pattern("thread 1 after Run(A)", RES_M1, 0x1100, res(RES_M1_PRE)[W_RSP], fs1);
  expect("thread 1 argOnReturn", *gmem64(ARGRET1), 0x77);
  check_pattern("A after ReturnToThread, resumed on thread 2", RES_A2, 0xa000, res(RES_A_PRE)[W_RSP], fs2);
  expect("A argOnRun from thread 2", *gmem64(ARGRUN_A2), 0x5250);
  check_pattern("B after Switch(A), resumed on thread 2", RES_B1, 0xb000, res(RES_B_PRE)[W_RSP], fs2);
  expect("B argOnRun from A", *gmem64(ARGRUN_B), 0x5252);
  check_pattern("thread 2 after Run(A)", RES_M2, 0x2200, res(RES_M2_PRE)[W_RSP], fs2);
  expect("thread 2 argOnReturn", *gmem64(ARGRET2), 0x88);
  static const char *fp_names[] = {"thread 1 after Run(A)", "A after Switch(B)", "A resumed on thread 2",
                                   "B resumed on thread 2", "thread 2 after Run(A)"};
  static const uint64_t fp_fpcr[] = {FPCR_RZ, FPCR_FZ, FPCR_FZ, FPCR_RP, 0};
  static const uint64_t fp_mxcsr[] = {0x7f80, 0x9f80, 0x9f80, 0x5f80, 0x1f80};
  for (int i = 0; i < 5; i++) {
    char what[96];
    const uint64_t *fp = gmem64(RES_FP_ADDR + (uint64_t)i * 16);
    snprintf(what, sizeof what, "%s: FPCR", fp_names[i]);
    expect(what, fp[0], fp_fpcr[i]);
    snprintf(what, sizeof what, "%s: MXCSR (STMXCSR)", fp_names[i]);
    expect(what, fp[1], fp_mxcsr[i]);
  }
  expect("thread 1 TCB slot", tcb_slot(t1), (uint64_t)(uintptr_t)t1);
  expect("thread 2 TCB slot", tcb_slot(t2), (uint64_t)(uintptr_t)t2);
}

// 0: the guest thread has not reached shim_test_wait_signal; 1: it waits in it; 2: released.
static atomic_int g_wait_state;

// Blocks until released, the way a shim blocks in a wait. Host code keeps host values in the
// registers that hold the guest registers in guest code: x8-x15 and x19-x25 hold other values
// while the signal arrives, x26 and x28 hold the guest stack pointer and memory base, as they
// do in any shim that does not reuse them.
void shim_test_wait_signal(GuestContext *ctx) {
  atomic_store(&g_wait_state, 1);
  __asm__ volatile("mov x26, %1\n"
                   "mov x28, %2\n"
                   "mov x8, #0xdead\n mov x9, #0xdead\n mov x10, #0xdead\n mov x11, #0xdead\n"
                   "mov x12, #0xdead\n mov x13, #0xdead\n mov x14, #0xdead\n mov x15, #0xdead\n"
                   "mov x19, #0xdead\n mov x20, #0xdead\n mov x21, #0xdead\n mov x22, #0xdead\n"
                   "mov x23, #0xdead\n mov x24, #0xdead\n mov x25, #0xdead\n"
                   "1: ldar w16, [%0]\n"
                   "cmp w16, #2\n"
                   "b.ne 1b\n"
                   :
                   : "r"(&g_wait_state), "r"(ctx->rsp), "r"(ctx->mem_base)
                   : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x19", "x20", "x21", "x22",
                     "x23", "x24", "x25", "x26", "x28", "cc", "memory");
  ctx->rax = 0;
  SHIM_RETURN();
}

static void *raise_exception_in_wait(void *arg) {
  (void)arg;
  while (atomic_load(&g_wait_state) != 1) usleep(100);
  expect("sceKernelRaiseException", (uint64_t)(int64_t)sceKernelRaiseException(1000, 30), 0);
  volatile uint64_t *signum = &res(RES_EXC_HANDLER)[W_RDI];
  for (int i = 0; i < 20000 && *signum != 30; i++) usleep(100);
  atomic_store(&g_wait_state, 2);
  return NULL;
}

// A guest exception handler (sceKernelRaiseException, SIGUSR1) runs while the thread waits
// in a shim: it sees the guest registers of the interrupted call, and the guest registers the
// shim returns with are unchanged.
static void test_exception(void) {
  GuestContext *t = g_proc;
  expect("sceKernelInstallExceptionHandler", (uint64_t)(int64_t)sceKernelInstallExceptionHandler(30, ADDR_EXC_HANDLER), 0);
  pthread_t th;
  pthread_create(&th, NULL, raise_exception_in_wait, NULL);
  recomp_call_guest(t, ADDR_EXC_MAIN);
  pthread_join(th, NULL);
  expect("handler signum", res(RES_EXC_HANDLER)[W_RDI], 30);
  const uint64_t uc = res(RES_EXC_HANDLER)[W_RSI];
  expect("ucontext r12", *gmem64(uc + 0xa0), 0xe00c);
  expect("ucontext rbx", *gmem64(uc + 0x80), 0xe001);
  expect("ucontext rbp", *gmem64(uc + 0x88), 0xe002);
  check_pattern("after the shim the handler interrupted", RES_EXC, 0xe000, res(RES_EXC_PRE)[W_RSP], t->fs_base);
}

void shim_test_heap_bump(GuestContext *ctx) {
  ctx->heap_ptr += 0x10000;
  SHIM_RETURN();
}

// A guest exception handler that changes the thread's state: a TLS value, a new key's
// destructor (in the process context) and the heap pointer. All of it stays after the handler,
// and the guest registers the interrupted shim returns with are unchanged.
static void test_exception_state(void) {
  GuestContext *t = g_proc;
  GuestContext *proc = t->process_ctx ? t->process_ctx : t;
  t->tls_keys[5] = 0x1111;
  const uint64_t heap = t->heap_ptr;
  expect("sceKernelInstallExceptionHandler",
         (uint64_t)(int64_t)sceKernelInstallExceptionHandler(30, ADDR_EXC_STATE_HANDLER), 0);
  pthread_t th;
  pthread_create(&th, NULL, raise_exception_in_wait, NULL);
  recomp_call_guest(t, ADDR_EXC_MAIN);
  pthread_join(th, NULL);
  expect("TLS key 5 (set by the handler)", t->tls_keys[5], 0x7777);
  const uint32_t key = *(uint32_t *)(g_proc->mem_base + EXC_KEY_OUT);
  if (key == 0 || key >= 128)
    fail("pthread_key_create in the handler returned key %u", key);
  else
    expect("destructor of the key the handler created", proc->tls_destructors[key], 0x7abc);
  expect("heap pointer (moved by the handler)", t->heap_ptr, heap + 0x10000);
  check_pattern("after the shim the handler interrupted", RES_EXC, 0xe000, res(RES_EXC_PRE)[W_RSP], t->fs_base);
}

// Sets every GPR but rsp to 0x5100 + its result-block index; rsp gets the usual return.
void shim_test_set_gprs(GuestContext *ctx) {
  uint64_t *gpr[] = {&ctx->rax, &ctx->rbx, &ctx->rcx, &ctx->rdx, &ctx->rsi, &ctx->rdi, &ctx->rbp, NULL,
                     &ctx->r8,  &ctx->r9,  &ctx->r10, &ctx->r11, &ctx->r12, &ctx->r13, &ctx->r14, &ctx->r15};
  for (int i = 0; i < 16; i++)
    if (gpr[i]) *gpr[i] = 0x5100 + (uint64_t)i;
  SHIM_RETURN();
}

// Guest code continues with every GPR a shim wrote to ctx.
static void test_gprs(void) {
  static const char *names[] = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
                                "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
  recomp_call_guest(g_proc, ADDR_GPRS);
  for (int i = 0; i < 16; i++) {
    char what[32];
    snprintf(what, sizeof what, "%s after the shim", names[i]);
    expect(what, res(RES_GPRS)[i], i == W_RSP ? res(RES_GPRS_PRE)[W_RSP] : 0x5100 + (uint64_t)i);
  }
  expect("FS base after the shim", res(RES_GPRS)[W_FS], g_proc->fs_base);
}

static xmm_reg_t g_shim_saw_xmm[8];
static float g_callback_result;

// Records xmm0-7 as the shim sees them, calls a guest callback(float) through ctx->xmm[0] and
// returns the double 2.5 in xmm0.
void shim_test_xmm(GuestContext *ctx) {
  memcpy(g_shim_saw_xmm, ctx->xmm, sizeof g_shim_saw_xmm);
  ctx->xmm[0].f32[0] = 3.0f;
  recomp_call_guest(ctx, ADDR_XMM_CALLBACK);
  g_callback_result = ctx->xmm[0].f32[0];
  memset(&ctx->xmm[0], 0, sizeof ctx->xmm[0]);
  ctx->xmm[0].f64[0] = 2.5;
  SHIM_RETURN();
}

static const uint8_t kXmmByte[8] = {0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78};

static void expect_xmm(const char *what, int i, const xmm_reg_t *got, uint8_t byte) {
  for (int b = 0; b < 16; b++) {
    if (got->u8[b] != byte) {
      fail("%s xmm%d byte %d = 0x%02x, want 0x%02x", what, i, b, got->u8[b], byte);
      return;
    }
  }
}

// A shim sees the guest's xmm0-7, a callback gets its float argument and returns its float
// result through ctx, and the guest gets the shim's xmm0 result and its other xmm1-7 back.
static void test_xmm(void) {
  recomp_call_guest(g_proc, ADDR_XMM);
  for (int i = 0; i < 8; i++) expect_xmm("shim argument", i, &g_shim_saw_xmm[i], kXmmByte[i]);
  if (g_callback_result != 6.0f) fail("callback(3.0f) result in ctx->xmm[0] = %g, want 6", g_callback_result);
  const xmm_reg_t *after = (const xmm_reg_t *)(g_proc->mem_base + RES_XMM_ADDR);
  if (after[0].f64[0] != 2.5 || after[0].u64[1] != 0)
    fail("xmm0 after the shim = %g (0x%016llx 0x%016llx), want 2.5", after[0].f64[0],
         (unsigned long long)after[0].u64[0], (unsigned long long)after[0].u64[1]);
  for (int i = 1; i < 8; i++) expect_xmm("after the shim", i, &after[i], kXmmByte[i]);
}

// C code that keeps doubles in d8-d15 across a guest call keeps them.
static void test_callee_saved_fp(void) {
  g_proc->rsp -= 8; // the return address recomp_call_guest pushes
  *gmem64(g_proc->rsp) = 0;
  const uint64_t changed = host_enter_guest_d8_d15(g_proc, (uint64_t)(uintptr_t)guest_clobber_fp);
  if (changed) fail("recomp_enter_guest changed callee-saved d8-d15 (mask 0x%llx of bits 8-15)", (unsigned long long)changed);
}

static uint64_t g_shim_fpcr = ~0ULL;

void shim_test_fpcr(GuestContext *ctx) {
  g_shim_fpcr = read_fpcr();
  SHIM_RETURN();
}

// Guest code starts with the FPCR of ctx->mxcsr, a shim runs with round to nearest, the guest
// gets its own back, and the host gets its own back from recomp_enter_guest.
static void test_fpcr(void) {
  g_proc->mxcsr = 0x5f80; // round up
  recomp_call_guest(g_proc, ADDR_FPCR);
  const uint64_t host_after = read_fpcr();
  const uint64_t *r = gmem64(RES_FPCR_ADDR);
  expect("guest FPCR at entry (MXCSR 0x5f80)", r[0], FPCR_RP);
  expect("FPCR in the shim (round to nearest, the guest's FZ)", g_shim_fpcr, FPCR_FZ);
  expect("guest FPCR after the shim", r[1], FPCR_RM | FPCR_FZ);
  expect("host FPCR after recomp_enter_guest", host_after, 0);
  expect("ctx->mxcsr after LDMXCSR", g_proc->mxcsr, 0xbf80);
}

// A lifted instruction that calls a runtime helper (the lifter saves x8-x15 itself) keeps
// every vector register it does not write and the flags it does not define.
// boundary_test.go liftedHelperFns sets v<n> = 0x40+n in all bytes and NZCV = Z|C first.
// written: the vector register the instruction writes (want: its value), zeroed: one it
// clears, skip: one not checked; -1 for none.
static void check_lifted(const char *what, uint64_t res_addr, int written, const xmm_reg_t *written_want,
                         int zeroed, int skip, uint64_t nzcv_want) {
  const xmm_reg_t *v = (const xmm_reg_t *)(g_proc->mem_base + res_addr);
  static const xmm_reg_t zero;
  for (int i = 0; i < 32; i++) {
    if (i == skip) continue;
    char name[96];
    snprintf(name, sizeof name, "%s: v%d", what, i);
    if (i == written || i == zeroed) {
      if (i == zeroed) written_want = &zero;
      if (memcmp(&v[i], written_want, 16) != 0)
        fail("%s = %016llx%016llx, want %016llx%016llx", name, (unsigned long long)v[i].u64[1],
             (unsigned long long)v[i].u64[0], (unsigned long long)written_want->u64[1],
             (unsigned long long)written_want->u64[0]);
      continue;
    }
    xmm_reg_t want;
    memset(&want, 0x40 + i, sizeof want);
    if (memcmp(&v[i], &want, 16) != 0)
      fail("%s changed to %016llx%016llx", name, (unsigned long long)v[i].u64[1], (unsigned long long)v[i].u64[0]);
  }
  if (nzcv_want != ~0ULL) expect(what, ((const uint64_t *)&v[32])[1], nzcv_want);
}

static void test_helpers(void) {
  // vpcmpistri xmm2, xmm3, 0x38 (equal each, masked negative: the helper's vectorized path):
  // the strings differ in byte 0, so ECX = 0.
  recomp_call_guest(g_proc, ADDR_LIFTED_VPCMPISTRI);
  check_lifted("vpcmpistri", RES_LIFTED_VPCMPISTRI, -1, NULL, -1, -1, ~0ULL);
  expect("vpcmpistri rcx", ((const uint64_t *)(g_proc->mem_base + RES_LIFTED_VPCMPISTRI + 512))[0], 0);

  // vaeskeygenassist xmm4, xmm5, 1: xmm4 = the C implementation's result, ymm4's upper half is
  // zeroed (VEX.128), NZCV stays Z|C. The lowering moves the source into v0 before the call
  // (a lifter issue, reported), so v0 is not checked.
  xmm_reg_t src;
  memset(&src, 0x45, sizeof src);
  const xmm_reg_t want = recomp_vaeskeygenassist_impl(src, 1);
  recomp_call_guest(g_proc, ADDR_LIFTED_VAESKEYGENASSIST);
  check_lifted("vaeskeygenassist", RES_LIFTED_VAESKEYGENASSIST, 4, &want, 20, 0, 0x60000000ULL);
}

static struct {
  GuestContext *ctx, *current_ctx;
  uint64_t fs_base;
} g_child_seen;

void shim_test_record_ctx(GuestContext *ctx) {
  g_child_seen.ctx = ctx;
  g_child_seen.current_ctx = g_current_ctx;
  g_child_seen.fs_base = ctx->fs_base;
  SHIM_RETURN();
}

static uint64_t res_word(uint64_t off, int i) { return gmem64(D + 0x1000 + off)[i]; }

// A guest thread created with pthread_create starts with a fresh context: its own TCB at its
// FS base, the TCB slot naming its GuestContext, the default MXCSR, and its static TLS laid out
// as the eboot's PT_TLS expects (variant II: the block ends at FS), which FS-relative accesses
// and __tls_get_addr agree on. The creating thread is unaffected.
static void test_thread(void) {
  memcpy(g_proc->mem_base + TLS_TEMPLATE, "TLS-TEMPLATE-OK!", 16);
  recomp_init_tls(g_proc, TLS_TEMPLATE, 16, 32, 16);
  gmem64(TLS_INDEX)[0] = 1;
  gmem64(TLS_INDEX)[1] = 8;
  uint64_t tdata0, tdata1;
  memcpy(&tdata0, "TLS-TEMP", 8);
  memcpy(&tdata1, "LATE-OK!", 8);
  const uint64_t fs1 = g_proc->fs_base;

  recomp_call_guest(g_proc, ADDR_THREAD_MAIN);

  const uint64_t fs2 = res_word(RES_THR_CHILD, 0);
  if (fs2 == fs1 || fs2 == 0) fail("new thread FS base 0x%llx (creator 0x%llx)", (unsigned long long)fs2, (unsigned long long)fs1);
  expect("new thread fs:[0] (TCB self pointer)", res_word(RES_THR_CHILD, 1), fs2);
  expect("new thread TCB slot = the ctx its shims get", res_word(RES_THR_CHILD, 2), (uint64_t)(uintptr_t)g_child_seen.ctx);
  expect("new thread g_current_ctx = its ctx", (uint64_t)(uintptr_t)g_child_seen.current_ctx,
         (uint64_t)(uintptr_t)g_child_seen.ctx);
  expect("new thread ctx->fs_base", g_child_seen.fs_base, fs2);
  expect("new thread FPCR (default MXCSR)", res_word(RES_THR_CHILD, 3), 0);
  expect("new thread .tdata word at FS - 32", res_word(RES_THR_CHILD, 4), tdata0);
  expect("new thread .tbss word at FS - 16", res_word(RES_THR_CHILD, 5), 0);
  expect("new thread argument", res_word(RES_THR_CHILD, 6), 0x1234);
  expect("__tls_get_addr({1, 8})", res_word(RES_THR_CHILD, 8), fs2 - 32 + 8);
  expect("__tls_get_addr word", res_word(RES_THR_CHILD, 9), tdata1);
  expect("join return value", *gmem64(THREAD_RET), 0x1235);
  check_pattern("creator after pthread_create + join", (int)(RES_THR_MAIN / 0x100), 0x7000, res_word(RES_THR_PRE, W_RSP), fs1);
  expect("creator FPCR after pthread_create + join", res_word(RES_THR_MAIN_FP, 0), FPCR_RM);
  expect("creator .tdata word", *gmem64(fs1 - 32), tdata0);
  expect("creator .tbss word (the new thread wrote its own)", *gmem64(fs1 - 16), 0);
}

static uint64_t g_shim_rip;

void shim_test_rip(GuestContext *ctx) {
  g_shim_rip = ctx->rip;
  SHIM_RETURN();
}

// ctx->rip names the shim (its stub's guest address) while the shim runs.
static void test_rip(void) {
  recomp_call_guest(g_proc, ADDR_RIP);
  expect("ctx->rip in the shim", g_shim_rip, STUB_ADDR(STUB_TEST_RIP));
}

// A stall while a guest thread is busy in guest code (PS4_STALL_PROFILE=20): the profile
// attributes its samples to the guest function and notes once that guest code samples have no
// frame chain (boundary_test.go checks the note).
static void test_profile(void) {
  GuestContext *t = g_proc;
  expect("pthread_create", call_shim(t, shim_pthread_create, SPIN_HANDLE, 0, ADDR_SPIN, 0, 0, 0, 0), 0);
  ps4_stall_watchdog_note_present();
  usleep(150 * 1000);
  ps4_stall_watchdog_note_present(); // ends the stall: the watchdog prints its profile
  usleep(50 * 1000);
  *gmem64(SPIN_FLAG) = 1;
  call_shim(t, shim_pthread_join, *gmem64(SPIN_HANDLE), 0, 0, 0, 0, 0, 0);
}

// Exceptions raised while guest code runs a helper call (the lifted vpcmpistri loop): every
// handler invocation must see the live guest registers in its ucontext, with the ucontext
// below the red zone of the live guest stack, and no helper result in ctx (rcx) may be lost.
static _Atomic int g_uc_seen, g_uc_bad;
static uint64_t g_uc_rsp;

void shim_test_check_uc(GuestContext *ctx) {
  const uint64_t uc = ctx->rsi;
  static const struct {
    uint32_t off;
    uint64_t want;
  } regs[] = {{0x80, 0xf001}, {0x88, 0xf002}, {0xa0, 0xf00c}, {0xa8, 0xf00d}, {0xb0, 0xf00e}, {0xb8, 0xf00f}};
  int bad = 0;
  for (unsigned i = 0; i < sizeof regs / sizeof regs[0]; i++)
    if (*gmem64(uc + regs[i].off) != regs[i].want) bad = 1;
  if (*gmem64(uc + 0xf8) != g_uc_rsp || uc + 0x200 > g_uc_rsp - 128) bad = 1;
  if (bad && atomic_load(&g_uc_bad) == 0)
    printf("first bad ucontext: rbx 0x%llx r12 0x%llx rsp 0x%llx (want 0x%llx) at 0x%llx\n",
           (unsigned long long)*gmem64(uc + 0x80), (unsigned long long)*gmem64(uc + 0xa0),
           (unsigned long long)*gmem64(uc + 0xf8), (unsigned long long)g_uc_rsp, (unsigned long long)uc);
  atomic_fetch_add(&g_uc_bad, bad);
  atomic_fetch_add(&g_uc_seen, 1);
  SHIM_RETURN();
}

static void *raise_during_helper_loop(void *arg) {
  (void)arg;
  usleep(20 * 1000);
  for (int i = 0; i < 3000; i++) {
    const int before = atomic_load(&g_uc_seen);
    if (sceKernelRaiseException(1000, 30) != 0) break;
    for (int k = 0; k < 2000 && atomic_load(&g_uc_seen) == before; k++) usleep(50);
  }
  *gmem64(HELPER_LOOP_FLAG) = 1;
  return NULL;
}

static void test_signal_in_helper(void) {
  expect("sceKernelInstallExceptionHandler", (uint64_t)(int64_t)sceKernelInstallExceptionHandler(30, ADDR_UC_CHECK), 0);
  g_uc_rsp = g_proc->rsp - 8; // the return address recomp_call_guest pushes
  pthread_t th;
  pthread_create(&th, NULL, raise_during_helper_loop, NULL);
  recomp_call_guest(g_proc, ADDR_HELPER_LOOP);
  pthread_join(th, NULL);
  if (atomic_load(&g_uc_seen) < 1000) fail("only %d exceptions handled", atomic_load(&g_uc_seen));
  expect("helper calls whose rcx result was lost", *gmem64(HELPER_LOOP_FLAG + 8), 0);
  if (atomic_load(&g_uc_bad))
    fail("%d of %d handler invocations saw a stale interrupted state", atomic_load(&g_uc_bad), atomic_load(&g_uc_seen));
}

// The Mach port of the thread _test_signal_self (boundary_test.go) sends SIGUSR1 to.
uint32_t g_signal_self_port;

// An exception delivered after a helper returned and before the lifted code read its result
// from ctx: the handler must leave ctx->rcx as the helper wrote it.
static void test_signal_after_helper(void) {
  expect("sceKernelInstallExceptionHandler", (uint64_t)(int64_t)sceKernelInstallExceptionHandler(30, ADDR_UC_CHECK), 0);
  g_signal_self_port = pthread_mach_thread_np(pthread_self());
  g_uc_rsp = g_proc->rsp - 8;
  recomp_call_guest(g_proc, ADDR_HELPER_WINDOW);
  expect("exceptions handled", (uint64_t)atomic_load(&g_uc_seen), 200);
  expect("handler invocations with a stale interrupted state", (uint64_t)atomic_load(&g_uc_bad), 0);
  expect("helper calls whose rcx result was lost", *gmem64(HELPER_LOOP_FLAG + 8), 0);
}

// An exception raised on a guest thread while it runs a fiber that waits in a shim: the
// handler interrupts the fiber (in fiber thread mode on the fiber's own host thread), sees the
// fiber's registers, and the fiber, the thread and the TCB slot are unchanged afterwards.
static void test_exception_fiber(void) {
  GuestContext *t1 = g_proc;
  strcpy((char *)g_proc->mem_base + NAME_A, "A");
  expect("init A", call_shim(t1, shim_sceFiberInitializeImpl, FIBER_A, NAME_A, ADDR_EXC_FIBER_A, 0, STACK_A,
                             FIBER_STACK_SIZE, 0), 0);
  expect("sceKernelInstallExceptionHandler", (uint64_t)(int64_t)sceKernelInstallExceptionHandler(30, ADDR_EXC_HANDLER), 0);
  pthread_t th;
  pthread_create(&th, NULL, raise_exception_in_wait, NULL);
  recomp_call_guest(t1, ADDR_EXC_FIBER_MAIN);
  pthread_join(th, NULL);
  expect("handler signum", res(RES_EXC_HANDLER)[W_RDI], 30);
  const uint64_t uc = res(RES_EXC_HANDLER)[W_RSI];
  expect("ucontext r12 (the fiber's)", *gmem64(uc + 0xa0), 0xe50c);
  expect("ucontext rbx (the fiber's)", *gmem64(uc + 0x80), 0xe501);
  check_pattern("fiber after the shim the handler interrupted", RES_A1, 0xe500, res(RES_A_PRE)[W_RSP], t1->fs_base);
  check_pattern("thread after Run", RES_M1, 0x1500, res(RES_M1_PRE)[W_RSP], t1->fs_base);
  expect("TCB slot", tcb_slot(t1), (uint64_t)(uintptr_t)t1);
}


// Module info of the image boundary_test.go emits (unresolvedTestELF plus a recompiled PRX): the
// eboot reports its own three segments with the ELF flags (not the merged companion module's),
// and every handle sceKernelGetModuleList returns works with GetModuleInfo, GetModuleInfo2 and
// GetModuleInfoInternal.
#define MI_LIST (D + 0x8000)
#define MI_COUNT (D + 0x8100)
#define MI_INFO (D + 0x8200)
typedef struct { uint64_t address; uint32_t size; int32_t prot; } MiSeg;
static void expect_seg(const char *what, const MiSeg *s, uint64_t addr, uint32_t size, int32_t prot) {
  expect(what, s->address, addr);
  expect(what, s->size, size);
  expect(what, (uint64_t)s->prot, (uint64_t)prot);
}
static void test_module_info(void) {
  recomp_register_guest_modules();
  GuestContext *ctx = g_proc;
  expect("GetModuleList", call_shim(ctx, shim_sceKernelGetModuleList, MI_LIST, 16, MI_COUNT, 0, 0, 0, 0), 0);
  const uint64_t n = *gmem64(MI_COUNT);
  const int32_t *list = (const int32_t *)(g_proc->mem_base + MI_LIST);
  if (n < 2) fail("GetModuleList returned %llu modules, want the eboot and PRXs", (unsigned long long)n);
  expect("main module handle", (uint64_t)list[0], 0);
  void (*const shims[3])(GuestContext *) = {shim_sceKernelGetModuleInfo, shim_sceKernelGetModuleInfo2,
                                            shim_sceKernelGetModuleInfoInternal};
  const char *const names[3] = {"GetModuleInfo", "GetModuleInfo2", "GetModuleInfoInternal"};
  for (uint64_t i = 0; i < n; i++) {
    for (int k = 0; k < 3; k++) {
      memset(g_proc->mem_base + MI_INFO, 0xee, 1024);
      *gmem64(MI_INFO) = k == 2 ? 0x1c0 : 0x160;  // st_size, ignored by the shims
      char what[96];
      snprintf(what, sizeof what, "%s(handle %d from GetModuleList)", names[k], list[i]);
      expect(what, call_shim(ctx, shims[k], (uint64_t)(int64_t)list[i], MI_INFO, 0, 0, 0, 0, 0), 0);
    }
  }
  // The eboot: info layout is st_size u64, name[256], segments[4] {u64 addr, u32 size, s32 prot}, u32 count.
  call_shim(ctx, shim_sceKernelGetModuleInfo, 0, MI_INFO, 0, 0, 0, 0, 0);
  const MiSeg *seg = (const MiSeg *)(g_proc->mem_base + MI_INFO + 8 + 256);
  const uint32_t count = *(const uint32_t *)(g_proc->mem_base + MI_INFO + 8 + 256 + 4 * sizeof(MiSeg));
  expect("eboot segment count", count, 3);
  expect_seg("eboot text", &seg[0], 0x400000, 0x180000, 5);
  expect_seg("eboot rodata", &seg[1], 0x580000, 0x40000, 4);
  expect_seg("eboot data", &seg[2], 0x5c0000, 0x40000, 6);
  // GetModuleInfoInternal: id, then segments[4] and the count after the eh_frame fields.
  call_shim(ctx, shim_sceKernelGetModuleInfoInternal, 0, MI_INFO, 0, 0, 0, 0, 0);
  const uint8_t *ex = g_proc->mem_base + MI_INFO;
  const MiSeg *xseg = (const MiSeg *)(ex + 8 + 256 + 4 + 4 + 8 + 4 + 4 + 4 + 4 + 8 + 8 + 8 + 8 + 8 + 8 + 4 + 4);
  expect_seg("Internal eboot text", &xseg[0], 0x400000, 0x180000, 5);
  expect_seg("Internal eboot data", &xseg[2], 0x5c0000, 0x40000, 6);

  // GetModuleInfoFromAddr (same layout as Internal): the id is the module's handle, the segments
  // its own; an address in no module is ESRCH, as on the console.
  int32_t prx_handle = -1;
  for (uint64_t i = 0; i < n; i++) {
    call_shim(ctx, shim_sceKernelGetModuleInfo, (uint64_t)(int64_t)list[i], MI_INFO, 0, 0, 0, 0, 0);
    if (strcmp((const char *)ex + 8, "libFake.prx") == 0) prx_handle = list[i];
  }
  if (prx_handle <= 0) fail("GetModuleList has no libFake.prx with a PRX handle (got %d)", prx_handle);
  memset(g_proc->mem_base + MI_INFO, 0xee, 1024);
  expect("FromAddr(eboot text)", call_shim(ctx, shim_sceKernelGetModuleInfoFromAddr, 0x400100, 0, MI_INFO, 0, 0, 0, 0), 0);
  expect("FromAddr(eboot) id", (uint64_t)(int64_t)*(const int32_t *)(ex + 8 + 256), 0);
  expect("FromAddr(eboot) segment count", *(const uint32_t *)((const uint8_t *)xseg + 4 * sizeof(MiSeg)), 3);
  expect_seg("FromAddr eboot rodata", &xseg[1], 0x580000, 0x40000, 4);
  memset(g_proc->mem_base + MI_INFO, 0xee, 1024);
  expect("FromAddr(PRX text)", call_shim(ctx, shim_sceKernelGetModuleInfoFromAddr, 0x800100, 0, MI_INFO, 0, 0, 0, 0), 0);
  expect("FromAddr(PRX) id", (uint64_t)(int64_t)*(const int32_t *)(ex + 8 + 256), (uint64_t)(int64_t)prx_handle);
  if (strcmp((const char *)ex + 8, "libFake.prx") != 0) fail("FromAddr(PRX) name %.32s", (const char *)ex + 8);
  expect("FromAddr(PRX) segment count", *(const uint32_t *)((const uint8_t *)xseg + 4 * sizeof(MiSeg)), 1);
  expect_seg("FromAddr PRX text", &xseg[0], 0x800000, 0x10000, 5);
  expect("FromAddr(no module)", call_shim(ctx, shim_sceKernelGetModuleInfoFromAddr, 0x30000000, 0, MI_INFO, 0, 0, 0, 0),
         (uint64_t)(int64_t)(int32_t)0x80020003);
  memset(g_proc->mem_base + MI_INFO, 0xee, 1024);
  expect("SegmentInfo(PRX)", call_shim(ctx, shim_sceKernelInternalMemoryGetModuleSegmentInfo, 0, 0x800100, MI_INFO, 0, 0, 0, 0), 0);
  expect_seg("SegmentInfo PRX text", (const MiSeg *)ex, 0x800000, 0x10000, 5);
  expect("SegmentInfo(no module)",
         call_shim(ctx, shim_sceKernelInternalMemoryGetModuleSegmentInfo, 0, 0x30000000, MI_INFO, 0, 0, 0, 0),
         (uint64_t)(int64_t)(int32_t)0x80020003);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <case>\n", argv[0]);
    return 2;
  }
  g_proc = recomp_init_runtime(256ULL << 20, NULL, 0, "boundary_test");
  if (!g_proc) return 2;
  recomp_init_main_thread(g_proc);
  recomp_register_fn(ADDR_FIBER_MAIN1, (recomp_fn_t)guest_fiber_main1);
  recomp_register_fn(ADDR_FIBER_MAIN2, (recomp_fn_t)guest_fiber_main2);
  recomp_register_fn(ADDR_FIBER_A, (recomp_fn_t)guest_fiber_a);
  recomp_register_fn(ADDR_FIBER_B, (recomp_fn_t)guest_fiber_b);
  recomp_register_fn(ADDR_EXC_MAIN, (recomp_fn_t)guest_exc_main);
  recomp_register_fn(ADDR_EXC_HANDLER, (recomp_fn_t)guest_exc_handler);
  recomp_register_fn(ADDR_GPRS, (recomp_fn_t)guest_gprs);
  recomp_register_fn(ADDR_XMM, (recomp_fn_t)guest_xmm);
  recomp_register_fn(ADDR_XMM_CALLBACK, (recomp_fn_t)guest_xmm_callback);
  recomp_register_fn(ADDR_FPCR, (recomp_fn_t)guest_fpcr);
  recomp_register_fn(ADDR_RIP, (recomp_fn_t)guest_rip);
  recomp_register_fn(ADDR_SPIN, (recomp_fn_t)guest_spin);
  recomp_register_fn(ADDR_CRASH, (recomp_fn_t)guest_crash);
  recomp_register_fn(ADDR_HELPER_LOOP, (recomp_fn_t)guest_helper_loop);
  recomp_register_fn(ADDR_HELPER_WINDOW, (recomp_fn_t)guest_helper_window);
  recomp_register_fn(ADDR_SHIM_BENCH, (recomp_fn_t)guest_shim_bench);
  recomp_register_fn(ADDR_UC_CHECK, (recomp_fn_t)guest_uc_check);
  recomp_register_fn(ADDR_EXC_FIBER_MAIN, (recomp_fn_t)guest_exc_fiber_main);
  recomp_register_fn(ADDR_EXC_FIBER_A, (recomp_fn_t)guest_exc_fiber_a);
  recomp_register_fn(ADDR_EXC_STATE_HANDLER, (recomp_fn_t)guest_exc_state_handler);
  recomp_register_fn(ADDR_LONGJMP, (recomp_fn_t)guest_longjmp);
  recomp_register_fn(ADDR_THREAD_MAIN, (recomp_fn_t)guest_thread_main);
  recomp_register_fn(ADDR_THREAD_ENTRY, (recomp_fn_t)guest_thread_entry);
  recomp_register_fn(ADDR_LIFTED_VPCMPISTRI, (recomp_fn_t)guest_lifted_vpcmpistri);
  recomp_register_fn(ADDR_LIFTED_VAESKEYGENASSIST, (recomp_fn_t)guest_lifted_vaeskeygenassist);

  const char *c = argv[1];
  if (strcmp(c, "fiber") == 0) {
    test_fiber();
  } else if (strcmp(c, "exception") == 0) {
    test_exception();
  } else if (strcmp(c, "exception-state") == 0) {
    test_exception_state();
  } else if (strcmp(c, "gprs") == 0) {
    test_gprs();
  } else if (strcmp(c, "xmm") == 0) {
    test_xmm();
  } else if (strcmp(c, "callee-saved-fp") == 0) {
    test_callee_saved_fp();
  } else if (strcmp(c, "fpcr") == 0) {
    test_fpcr();
  } else if (strcmp(c, "helpers") == 0) {
    test_helpers();
  } else if (strcmp(c, "thread") == 0) {
    test_thread();
  } else if (strcmp(c, "rip") == 0) {
    test_rip();
  } else if (strcmp(c, "profile") == 0) {
    test_profile();
  } else if (strcmp(c, "bench-shim") == 0) {
    // bench-shim <mxcsr>: ns per guest call of a shim that does nothing, guest MXCSR <mxcsr>.
    g_proc->mxcsr = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 0x1f80;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    const uint64_t t0 = mach_absolute_time();
    recomp_call_guest(g_proc, ADDR_SHIM_BENCH);
    const double ns = (double)(mach_absolute_time() - t0) * tb.numer / tb.denom;
    printf("bench-shim mxcsr 0x%x: %.2f ns per shim call\n", g_proc->mxcsr, ns / 1e7);
  } else if (strcmp(c, "signal-after-helper") == 0) {
    test_signal_after_helper();
  } else if (strcmp(c, "signal-in-helper") == 0) {
    test_signal_in_helper();
  } else if (strcmp(c, "exception-fiber") == 0) {
    test_exception_fiber();
  } else if (strcmp(c, "longjmp") == 0) {
    // shim_guest_nonlocal_jump reports and aborts; boundary_test.go checks the report.
    recomp_call_guest(g_proc, ADDR_LONGJMP);
    fail("longjmp returned");
  } else if (strcmp(c, "unresolved-plt") == 0 || strcmp(c, "unresolved-got") == 0 || strcmp(c, "unresolved-named") == 0) {
    // The generated guest_modules.c binds the unresolved import (unresolvedTestELF in
    // unresolved_test.go) to shim_unresolved_stub, which reports its NID and library and aborts;
    // boundary_test.go checks the report.
    recomp_register_guest_modules();
    recomp_call_guest(g_proc, strcmp(c, "unresolved-plt") == 0   ? 0x540000ULL
                             : strcmp(c, "unresolved-got") == 0 ? 0x560000ULL
                                                                : 0x540030ULL);
    fail("the unresolved import returned");
  } else if (strcmp(c, "module-info") == 0) {
    test_module_info();
  } else if (strcmp(c, "crash") == 0) {
    // The crash handler reports and exits 1; boundary_test.go checks its report.
    recomp_call_guest(g_proc, ADDR_CRASH);
    fail("guest_crash returned");
  } else {
    fprintf(stderr, "unknown case %s\n", c);
    return 2;
  }
  if (g_failures) return 1;
  printf("PASS %s\n", c);
  return 0;
}
