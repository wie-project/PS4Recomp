// Host test for libSceFiber: a fiber with a context resumes after sceFiberReturnToThread, one
// without a context starts over from its entry on every run, on the running thread's stack.
// Build and run (from the repository root), in both fiber modes:
//   clang -std=c11 -I pkg/runtime/core -I pkg/runtime/kernel/threading -I pkg/runtime/modules/libSceFiber \
//     tests/fiber_test/main.c pkg/runtime/modules/libSceFiber/ps4_fiber.c -o /tmp/fiber_test && \
//     /tmp/fiber_test && PS4_FIBER_THREADS=1 /tmp/fiber_test

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps4_fiber.h"
#include "ps4_threading.h"
#include "recomp_context.h"

recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];
_Thread_local GuestContext *g_current_ctx;
RecompThread *recomp_get_current_thread(void) { return NULL; }
void recomp_set_current_thread(RecompThread *t) { (void)t; }

enum {
  kStack = 0x10000,   // guest thread stack top
  kName = 0x11000,    // fiber name
  kOut = 0x11100,     // argOnReturn / argOnRun out slot
  kFiber = 0x12000,   // fiber with a context
  kBare = 0x12100,    // fiber without a context
  kContext = 0x20000, // context of kFiber
  kContextSize = 0x4000,
  kEntryKept = 0x1000, // guest address of entry_kept
  kEntryBare = 0x1010, // guest address of entry_bare
};

static int g_kept_entries, g_kept_resumes, g_bare_entries;
static uint64_t g_bare_rsp;

// Guest calls a shim: push the return address the shim pops.
static void guest_call(GuestContext *ctx, void (*shim)(GuestContext *), uint64_t a, uint64_t b) {
  ctx->rdi = a;
  ctx->rsi = b;
  ctx->rsp -= 8;
  shim(ctx);
}

// Returns to the thread, then reports every resume to the thread the same way.
static void entry_kept(GuestContext *ctx) {
  g_kept_entries++;
  for (;;) {
    guest_call(ctx, shim_sceFiberReturnToThread, 100 + g_kept_resumes, 0);
    assert((int32_t)ctx->rax == 0);
    g_kept_resumes++;
  }
}

// Returns to the thread once: a fiber without a context never continues after that.
static void entry_bare(GuestContext *ctx) {
  g_bare_entries++;
  g_bare_rsp = ctx->rsp;
  assert(ctx->rdi == 0xb0 && ctx->rsi == 0x200u + (uint64_t)g_bare_entries);
  guest_call(ctx, shim_sceFiberReturnToThread, 200 + g_bare_entries, 0);
  fprintf(stderr, "[fiber-test] FAIL: a fiber without a context was resumed\n");
  abort();
}

static void map_guest_fn(uint64_t addr, recomp_fn_t fn) {
  uint64_t l1 = (addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
  uint64_t l2 = (addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
  if (!g_dispatch_l1[l1]) g_dispatch_l1[l1] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
  if (!g_dispatch_l1[l1][l2]) g_dispatch_l1[l1][l2] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
  g_dispatch_l1[l1][l2][addr & DISPATCH_LEVEL_MASK] = fn;
}

static int32_t call(GuestContext *ctx, void (*shim)(GuestContext *), uint64_t a, uint64_t b, uint64_t c,
                    uint64_t d, uint64_t e, uint64_t f, uint64_t stack0) {
  ctx->rsp = kStack - 16;
  *(uint64_t *)(ctx->mem_base + ctx->rsp + 8) = stack0;
  ctx->rdi = a;
  ctx->rsi = b;
  ctx->rdx = c;
  ctx->rcx = d;
  ctx->r8 = e;
  ctx->r9 = f;
  shim(ctx);
  assert(ctx->rsp == kStack - 8);
  return (int32_t)ctx->rax;
}

static uint64_t out(GuestContext *ctx) { return *(uint64_t *)(ctx->mem_base + kOut); }

int main(void) {
  map_guest_fn(kEntryKept, entry_kept);
  map_guest_fn(kEntryBare, entry_bare);
  GuestContext ctx = {0};
  ctx.mem_size = 1 << 20;
  ctx.mem_base = calloc(1, ctx.mem_size);
  assert(ctx.mem_base);
  g_current_ctx = &ctx;
  strcpy((char *)ctx.mem_base + kName, "test");

  assert(call(&ctx, shim_sceFiberInitializeImpl, kFiber, kName, kEntryKept, 0xa0, kContext, kContextSize, 0) == 0);
  assert(call(&ctx, shim_sceFiberInitializeImpl, kBare, kName, kEntryBare, 0xb0, 0, 0, 0) == 0);

  // With a context: one entry, then every Run resumes after ReturnToThread.
  for (int i = 0; i < 3; i++) {
    assert(call(&ctx, shim_sceFiberRun, kFiber, 0x100, kOut, 0, 0, 0, 0) == 0);
    assert(out(&ctx) == 100u + (uint64_t)i);
  }
  assert(g_kept_entries == 1 && g_kept_resumes == 2);
  printf("[+] a fiber with a context resumes after sceFiberReturnToThread\n");

  // Without a context: every Run starts over at the entry, below the Run caller's stack.
  for (int i = 1; i <= 3; i++) {
    assert(call(&ctx, shim_sceFiberRun, kBare, 0x200u + (uint64_t)i, kOut, 0, 0, 0, 0) == 0);
    assert(out(&ctx) == 200u + (uint64_t)i);
    assert(g_bare_rsp < kStack - 8 && g_bare_rsp > kStack - 0x4000);
  }
  assert(g_bare_entries == 3);
  printf("[+] a fiber without a context starts over on every run\n");

  assert(call(&ctx, shim_sceFiberFinalize, kBare, 0, 0, 0, 0, 0, 0) == 0);
  printf("[fiber-test] all passed (%s)\n", ps4_fiber_threads_mode() ? "threads" : "coroutines");
  return 0;
}
