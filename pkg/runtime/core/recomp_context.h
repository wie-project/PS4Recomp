// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RECOMP_CONTEXT_H
#define RECOMP_CONTEXT_H

#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

// UnwindFrame tracks active call frames for C++ exception unwinding
typedef struct UnwindFrame {
  uint64_t fn_start;
  uint64_t fn_end;
  jmp_buf buf;
  struct UnwindFrame *prev;
} UnwindFrame;

// 128-bit vector union for XMM registers
typedef union {
  uint8_t u8[16];
  uint16_t u16[8];
  uint32_t u32[4];
  uint64_t u64[2];
  int8_t s8[16];
  int16_t s16[8];
  int32_t s32[4];
  int64_t s64[2];
  __uint128_t u128;
  float f32[4];
  double f64[2];
} xmm_reg_t;

// Guest CPU context
typedef struct GuestContext {
  uint64_t rax;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rbx;
  uint64_t rsp;
  uint64_t rbp;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rip;
  uint64_t fs_base;

  // CPU Flags
  uint8_t cf;
  uint8_t zf;
  uint8_t sf;
  uint8_t of;
  uint8_t pf;
  uint8_t af;

  // SIMD Registers
  xmm_reg_t xmm[16];
  xmm_reg_t ymmh[16];

  // x87 FPU state
  double fpu_stack[8];
  int fpu_top;
  uint16_t fpu_cw;
  uint16_t fpu_sw;
  uint32_t mxcsr;
  uint64_t mmx[8];

  // Guest memory mapping
  uint8_t *mem_base;
  size_t mem_size;
  uint64_t heap_ptr;

  // Guest virtual memory extent manager
  struct GuestVMExtent *vm_extents;
  pthread_mutex_t vm_mutex;

  // Threading and TLS
  uint64_t thread_id;
  uint64_t stack_base;
  size_t stack_alloc_size;
  uint64_t tls_keys[128];
  uint64_t tls_destructors[128];

  // Process root context (shared across threads)
  struct GuestContext *process_ctx;

  // Active exception unwinding frame stack
  UnwindFrame *unwind_frame;
} GuestContext;

#define SHIM_RETURN()                                                          \
  do {                                                                         \
    ctx->rsp += 8;                                                             \
    return;                                                                    \
  } while (0)

static inline void set_guest_errno(GuestContext *ctx, int err) {
  if (ctx && ctx->fs_base) {
    *(int *)(ctx->mem_base + ctx->fs_base + 0x100ULL) = err;
  }
}

// Guest VM Extent for tracking mmap/munmap ranges
typedef struct GuestVMExtent {
  uint64_t addr;
  size_t size;
  bool is_free;
  int prot;
  int flags;
  char name[32];
  struct GuestVMExtent *prev;
  struct GuestVMExtent *next;
} GuestVMExtent;

extern _Thread_local GuestContext *g_current_ctx;

// x87 FPU stack helpers
#define FPU_ST(i) ((ctx)->fpu_stack[((ctx)->fpu_top + (i)) & 7])

static inline void fpu_push(GuestContext *ctx, double val) {
  ctx->fpu_top = (ctx->fpu_top - 1) & 7;
  ctx->fpu_stack[ctx->fpu_top] = val;
}

static inline double fpu_pop(GuestContext *ctx) {
  double val = ctx->fpu_stack[ctx->fpu_top];
  ctx->fpu_top = (ctx->fpu_top + 1) & 7;
  return val;
}

// Memory access helpers
#define MEM_U8(addr) (*(uint8_t *)((ctx)->mem_base + (uint64_t)(addr)))
#define MEM_U16(addr) (*(uint16_t *)((ctx)->mem_base + (uint64_t)(addr)))
#define MEM_U32(addr) (*(uint32_t *)((ctx)->mem_base + (uint64_t)(addr)))
#define MEM_U64(addr) (*(uint64_t *)((ctx)->mem_base + (uint64_t)(addr)))

static inline uint64_t recomp_rdtsc(void) {
#if defined(__aarch64__)
  uint64_t val;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
  return val;
#elif defined(__x86_64__)
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
#else
  return 0;
#endif
}

// Parity computation using compiler intrinsic
static inline uint8_t compute_parity(uint8_t val) {
  return (uint8_t)(!__builtin_parity((unsigned int)val));
}

// 64-bit flag setters
static inline void set_flags_add_u64(GuestContext *ctx, uint64_t a, uint64_t b,
                                     uint64_t res) {
  ctx->cf = res < a;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 63) & 1;
  ctx->of = ((~(a ^ b) & (a ^ res)) >> 63) & 1;
}

static inline void set_flags_sub_u64(GuestContext *ctx, uint64_t a, uint64_t b,
                                     uint64_t res) {
  ctx->cf = a < b;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 63) & 1;
  ctx->of = (((a ^ b) & (a ^ res)) >> 63) & 1;
}

static inline void set_flags_logic_u64(GuestContext *ctx, uint64_t res) {
  ctx->cf = 0;
  ctx->of = 0;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 63) & 1;
}

static inline void set_flags_inc_u64(GuestContext *ctx, uint64_t a,
                                     uint64_t res) {
  (void)a;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 63) & 1;
  ctx->of = (res == 0x8000000000000000ULL);
}

static inline void set_flags_dec_u64(GuestContext *ctx, uint64_t a,
                                     uint64_t res) {
  (void)res;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 63) & 1;
  ctx->of = (a == 0x8000000000000000ULL);
}

// 32-bit flag setters
static inline void set_flags_add_u32(GuestContext *ctx, uint32_t a, uint32_t b,
                                     uint32_t res) {
  ctx->cf = res < a;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 31) & 1;
  ctx->of = ((~(a ^ b) & (a ^ res)) >> 31) & 1;
}

static inline void set_flags_sub_u32(GuestContext *ctx, uint32_t a, uint32_t b,
                                     uint32_t res) {
  ctx->cf = a < b;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 31) & 1;
  ctx->of = (((a ^ b) & (a ^ res)) >> 31) & 1;
}

static inline void set_flags_logic_u32(GuestContext *ctx, uint32_t res) {
  ctx->cf = 0;
  ctx->of = 0;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 31) & 1;
}

static inline void set_flags_inc_u32(GuestContext *ctx, uint32_t a,
                                     uint32_t res) {
  (void)a;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 31) & 1;
  ctx->of = (res == 0x80000000U);
}

static inline void set_flags_dec_u32(GuestContext *ctx, uint32_t a,
                                     uint32_t res) {
  ctx->zf = (res == 0);
  ctx->sf = (res >> 31) & 1;
  ctx->of = (a == 0x80000000U);
}

// 16-bit flag setters
static inline void set_flags_add_u16(GuestContext *ctx, uint16_t a, uint16_t b,
                                     uint16_t res) {
  ctx->cf = res < a;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 15) & 1;
  ctx->of = ((~(a ^ b) & (a ^ res)) >> 15) & 1;
}

static inline void set_flags_sub_u16(GuestContext *ctx, uint16_t a, uint16_t b,
                                     uint16_t res) {
  ctx->cf = a < b;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 15) & 1;
  ctx->of = (((a ^ b) & (a ^ res)) >> 15) & 1;
}

static inline void set_flags_logic_u16(GuestContext *ctx, uint16_t res) {
  ctx->cf = 0;
  ctx->of = 0;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 15) & 1;
}

static inline void set_flags_inc_u16(GuestContext *ctx, uint16_t a,
                                     uint16_t res) {
  (void)a;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 15) & 1;
  ctx->of = (res == 0x8000);
}

static inline void set_flags_dec_u16(GuestContext *ctx, uint16_t a,
                                     uint16_t res) {
  ctx->zf = (res == 0);
  ctx->sf = (res >> 15) & 1;
  ctx->of = (a == 0x8000);
}

// 8-bit flag setters
static inline void set_flags_add_u8(GuestContext *ctx, uint8_t a, uint8_t b,
                                    uint8_t res) {
  ctx->cf = res < a;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 7) & 1;
  ctx->of = ((~(a ^ b) & (a ^ res)) >> 7) & 1;
}

static inline void set_flags_sub_u8(GuestContext *ctx, uint8_t a, uint8_t b,
                                    uint8_t res) {
  ctx->cf = a < b;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 7) & 1;
  ctx->of = (((a ^ b) & (a ^ res)) >> 7) & 1;
}

static inline void set_flags_logic_u8(GuestContext *ctx, uint8_t res) {
  ctx->cf = 0;
  ctx->of = 0;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 7) & 1;
}

static inline void set_flags_inc_u8(GuestContext *ctx, uint8_t a, uint8_t res) {
  (void)a;
  ctx->zf = (res == 0);
  ctx->sf = (res >> 7) & 1;
  ctx->of = (res == 0x80);
}

static inline void set_flags_dec_u8(GuestContext *ctx, uint8_t a, uint8_t res) {
  ctx->zf = (res == 0);
  ctx->sf = (res >> 7) & 1;
  ctx->of = (a == 0x80);
}

// Function pointer type for recompiled functions
typedef void (*recomp_fn_t)(GuestContext *__restrict__ ctx);

// Dispatch table: maps guest virtual address to compiled function pointer
// Uses a 3-level page table covering the full 48-bit x86-64 user address space
// (16 bits L1, 16 bits L2, 16 bits L3) with on-demand L2 and L3 allocation.
#define DISPATCH_L1_SHIFT 32
#define DISPATCH_L2_SHIFT 16
#define DISPATCH_LEVEL_BITS 16
#define DISPATCH_LEVEL_SIZE (65536) // 2^16 entries per level
#define DISPATCH_LEVEL_MASK (0xFFFFULL)

extern recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

void recomp_register_fn(uint64_t guest_addr, recomp_fn_t fn);

static inline void recomp_dispatch(GuestContext *ctx, uint64_t target) {
  ctx->rip = target;
  if (__builtin_expect(target < (1ULL << 48), 1)) {
    uint64_t l1_idx = (target >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    recomp_fn_t **l2 = g_dispatch_l1[l1_idx];
    if (__builtin_expect(l2 != NULL, 1)) {
      uint64_t l2_idx = (target >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
      recomp_fn_t *l3 = l2[l2_idx];
      if (__builtin_expect(l3 != NULL, 1)) {
        recomp_fn_t fn = l3[target & DISPATCH_LEVEL_MASK];
        if (__builtin_expect(fn != NULL, 1)) {
          fn(ctx);
          return;
        }
      }
    }
  }
  fprintf(stderr,
          "\nFATAL: Unresolved indirect jump/call to 0x%llx (from RIP=0x%llx)\n"
          "Registers:\n"
          "  RAX=0x%016llx RBX=0x%016llx RCX=0x%016llx RDX=0x%016llx\n"
          "  RSI=0x%016llx RDI=0x%016llx RBP=0x%016llx RSP=0x%016llx\n"
          "  R8 =0x%016llx R9 =0x%016llx R10=0x%016llx R11=0x%016llx\n"
          "  R12=0x%016llx R13=0x%016llx R14=0x%016llx R15=0x%016llx\n",
          (unsigned long long)target, (unsigned long long)ctx->rip,
          (unsigned long long)ctx->rax, (unsigned long long)ctx->rbx,
          (unsigned long long)ctx->rcx, (unsigned long long)ctx->rdx,
          (unsigned long long)ctx->rsi, (unsigned long long)ctx->rdi,
          (unsigned long long)ctx->rbp, (unsigned long long)ctx->rsp,
          (unsigned long long)ctx->r8,  (unsigned long long)ctx->r9,
          (unsigned long long)ctx->r10, (unsigned long long)ctx->r11,
          (unsigned long long)ctx->r12, (unsigned long long)ctx->r13,
          (unsigned long long)ctx->r14, (unsigned long long)ctx->r15);
  fflush(stderr);
  abort();
}

// Invoke a guest function that returns with RET. Guest RET pops a return
// address, so a dummy slot must be present or the caller's stack is corrupted.
static inline void recomp_call_guest(GuestContext *ctx, uint64_t target) {
  ctx->rsp -= 8;
  MEM_U64(ctx->rsp) = 0;
  recomp_dispatch(ctx, target);
}

// Exception unwinding helpers
#define RECOMP_POP_UNWIND() do { if (__cur_unwind_frame) ctx->unwind_frame = __cur_unwind_frame->prev; } while (0)
void recomp_unwind_to(GuestContext *ctx, uint64_t target_ip);

// SIMD helpers
void recomp_vpcmpistri(GuestContext *ctx, const void *src2_ptr, const void *src1_ptr, uint8_t imm8);
void recomp_vpcmpe_stri(GuestContext *ctx, const void *src2_ptr, const void *src1_ptr, uint8_t imm8);

// CRC32 helpers (Castagnoli 0x1EDC6F41 polynomial)
static inline uint32_t recomp_crc32_u8(uint32_t crc, uint8_t val) {
  crc ^= val;
  for (int i = 0; i < 8; i++) {
    crc = (crc >> 1) ^ (0x82F63B78U & (-(crc & 1)));
  }
  return crc;
}

static inline uint32_t recomp_crc32_u16(uint32_t crc, uint16_t val) {
  crc = recomp_crc32_u8(crc, (uint8_t)val);
  return recomp_crc32_u8(crc, (uint8_t)(val >> 8));
}

static inline uint32_t recomp_crc32_u32(uint32_t crc, uint32_t val) {
  crc = recomp_crc32_u16(crc, (uint16_t)val);
  return recomp_crc32_u16(crc, (uint16_t)(val >> 16));
}

static inline uint64_t recomp_crc32_u64(uint64_t crc, uint64_t val) {
  uint32_t c = recomp_crc32_u32((uint32_t)crc, (uint32_t)val);
  return (uint64_t)recomp_crc32_u32(c, (uint32_t)(val >> 32));
}

#ifdef __cplusplus
}
#endif

#endif // RECOMP_CONTEXT_H
