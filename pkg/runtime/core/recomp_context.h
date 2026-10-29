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
  uint64_t gs_base;

  // CPU Flags
  uint8_t cf;
  uint8_t zf;
  uint8_t sf;
  uint8_t of;
  uint8_t pf;
  uint8_t af;
  uint8_t df;

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

  // Process parameters address (SceKernelProcessParam)
  uint64_t proc_param_addr;

  // Process arguments address (OrbisEntryParams)
  uint64_t args_addr;

  // Active exception unwinding frame stack
  UnwindFrame *unwind_frame;

  // Size of the static TLS area below the TCB (recomp_init_tls_modules)
  uint64_t static_tls_size;
} GuestContext;

// The TCB slot (guest address fs_base + RECOMP_TCB_CTX_SLOT) that holds the GuestContext
// pointer of the execution context running on the guest thread (contract at
// recomp_enter_guest in the emitter's trampoline).
#define RECOMP_TCB_CTX_SLOT 0x70ULL

// GuestContext offsets that assembly hardcodes: the trampoline (pkg/emitter/asm_trampoline.go)
// and the lifter's inline sequences (pkg/lifter: x87 stack and control/status words, MXCSR,
// the flag bytes, the TCB slot). Changing the layout breaks these at build time; update the
// assembly and pkg/emitter/context_offsets_test.go together with them.
#ifdef __cplusplus
#define RECOMP_CTX_OFFSET(field, off) \
  static_assert(offsetof(GuestContext, field) == (off), "GuestContext." #field " offset is hardcoded in assembly")
#else
#define RECOMP_CTX_OFFSET(field, off) \
  _Static_assert(offsetof(GuestContext, field) == (off), "GuestContext." #field " offset is hardcoded in assembly")
#endif
RECOMP_CTX_OFFSET(rax, 0x00);
RECOMP_CTX_OFFSET(rcx, 0x08);
RECOMP_CTX_OFFSET(rdx, 0x10);
RECOMP_CTX_OFFSET(rbx, 0x18);
RECOMP_CTX_OFFSET(rsp, 0x20);
RECOMP_CTX_OFFSET(rbp, 0x28);
RECOMP_CTX_OFFSET(rsi, 0x30);
RECOMP_CTX_OFFSET(rdi, 0x38);
RECOMP_CTX_OFFSET(r8, 0x40);
RECOMP_CTX_OFFSET(r9, 0x48);
RECOMP_CTX_OFFSET(r10, 0x50);
RECOMP_CTX_OFFSET(r11, 0x58);
RECOMP_CTX_OFFSET(r12, 0x60);
RECOMP_CTX_OFFSET(r13, 0x68);
RECOMP_CTX_OFFSET(r14, 0x70);
RECOMP_CTX_OFFSET(r15, 0x78);
RECOMP_CTX_OFFSET(rip, 0x80);
RECOMP_CTX_OFFSET(fs_base, 0x88);
RECOMP_CTX_OFFSET(cf, 0x98);
RECOMP_CTX_OFFSET(zf, 0x99);
RECOMP_CTX_OFFSET(sf, 0x9a);
RECOMP_CTX_OFFSET(of, 0x9b);
RECOMP_CTX_OFFSET(xmm[0], 0xa0);
RECOMP_CTX_OFFSET(xmm[2], 0xc0);
RECOMP_CTX_OFFSET(xmm[4], 0xe0);
RECOMP_CTX_OFFSET(xmm[6], 0x100);
RECOMP_CTX_OFFSET(fpu_stack, 0x2a0);
RECOMP_CTX_OFFSET(fpu_top, 0x2e0);
RECOMP_CTX_OFFSET(fpu_cw, 0x2e4);
RECOMP_CTX_OFFSET(fpu_sw, 0x2e6);
RECOMP_CTX_OFFSET(mxcsr, 0x2e8);
RECOMP_CTX_OFFSET(mem_base, 0x330);
#ifdef __cplusplus
static_assert(sizeof(xmm_reg_t) == 16 && sizeof(((GuestContext *)0)->fpu_stack[0]) == 8 &&
                  sizeof(((GuestContext *)0)->fpu_top) == 4,
              "element sizes the assembly assumes");
#else
_Static_assert(sizeof(xmm_reg_t) == 16 && sizeof(((GuestContext *)0)->fpu_stack[0]) == 8 &&
                   sizeof(((GuestContext *)0)->fpu_top) == 4,
               "element sizes the assembly assumes");
#endif

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
  int mtype; // memory type set by sceKernelMtypeprotect (0 = write-back onion)
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
// x86 float -> integer conversions: out-of-range and NaN inputs give the "integer
// indefinite" value (INT_MIN), not C's undefined behaviour / ARM saturation. The rounding
// variants use the current rounding mode (round-to-nearest-even by default), like CVTSS2SI.
static inline int32_t x86_cvtt_f32_i32(float f) {
  return (f >= -2147483648.0f && f < 2147483648.0f) ? (int32_t)f : INT32_MIN;
}
static inline int32_t x86_cvtt_f64_i32(double f) {
  return (f > -2147483649.0 && f < 2147483648.0) ? (int32_t)f : INT32_MIN;
}
static inline int64_t x86_cvtt_f32_i64(float f) {
  return (f >= -9223372036854775808.0f && f < 9223372036854775808.0f) ? (int64_t)f : INT64_MIN;
}
static inline int64_t x86_cvtt_f64_i64(double f) {
  return (f >= -9223372036854775808.0 && f < 9223372036854775808.0) ? (int64_t)f : INT64_MIN;
}
static inline int32_t x86_cvt_f32_i32(float f) { return x86_cvtt_f32_i32(rintf(f)); }
static inline int32_t x86_cvt_f64_i32(double f) { return x86_cvtt_f64_i32(rint(f)); }
static inline int64_t x86_cvt_f32_i64(float f) { return x86_cvtt_f32_i64(rintf(f)); }
static inline int64_t x86_cvt_f64_i64(double f) { return x86_cvtt_f64_i64(rint(f)); }

#define MEM_U8(addr) (*(uint8_t *)((ctx)->mem_base + (uint64_t)(addr)))
#define MEM_U16(addr) (*(uint16_t *)((ctx)->mem_base + (uint64_t)(addr)))
#define MEM_U32(addr) (*(uint32_t *)((ctx)->mem_base + (uint64_t)(addr)))
#define MEM_U64(addr) (*(uint64_t *)((ctx)->mem_base + (uint64_t)(addr)))

// Ordered guest memory access for non-stack addresses. x86 is TSO: every load
// has acquire and every store release semantics. arm64 is weakly ordered, so
// lock-free guest code (job queues, spinlocks released with plain stores)
// breaks with plain accesses. Acquire/release atomics give TSO-equivalent
// ordering. LDAPR/STLR fault on accesses crossing a 16-byte boundary even
// with FEAT_LSE2, so those fall back to a plain access plus a fence.
#define RECOMP_ORD_OK(p, n) ((((uintptr_t)(p)) & 15) + (n) <= 16)
static inline uint8_t recomp_ld8(GuestContext *ctx, uint64_t a) {
  return __atomic_load_n((uint8_t *)(ctx->mem_base + a), __ATOMIC_ACQUIRE);
}
#define RECOMP_DEF_LD(bits)                                                    \
  static inline uint##bits##_t recomp_ld##bits(GuestContext *ctx, uint64_t a) { \
    uint8_t *p = ctx->mem_base + a;                                            \
    if (__builtin_expect(RECOMP_ORD_OK(p, bits / 8), 1))                       \
      return __atomic_load_n((uint##bits##_t *)p, __ATOMIC_ACQUIRE);           \
    uint##bits##_t v;                                                          \
    __builtin_memcpy(&v, p, sizeof(v));                                        \
    __atomic_thread_fence(__ATOMIC_ACQUIRE);                                   \
    return v;                                                                  \
  }
RECOMP_DEF_LD(16)
RECOMP_DEF_LD(32)
RECOMP_DEF_LD(64)
static inline void recomp_st8(GuestContext *ctx, uint64_t a, uint8_t v) {
  __atomic_store_n((uint8_t *)(ctx->mem_base + a), v, __ATOMIC_RELEASE);
}
#define RECOMP_DEF_ST(bits)                                                    \
  static inline void recomp_st##bits(GuestContext *ctx, uint64_t a,            \
                                     uint##bits##_t v) {                       \
    uint8_t *p = ctx->mem_base + a;                                            \
    if (__builtin_expect(RECOMP_ORD_OK(p, bits / 8), 1)) {                     \
      __atomic_store_n((uint##bits##_t *)p, v, __ATOMIC_RELEASE);              \
      return;                                                                  \
    }                                                                          \
    __atomic_thread_fence(__ATOMIC_RELEASE);                                   \
    __builtin_memcpy(p, &v, sizeof(v));                                        \
  }
RECOMP_DEF_ST(16)
RECOMP_DEF_ST(32)
RECOMP_DEF_ST(64)
// x86 PAUSE: spin-wait hint. On arm64 ISB stalls the pipeline for a short time like PAUSE does
// (YIELD is a no-op on Apple cores), so guest spin loops do not hammer the cache line at full
// speed and burn through their spin budgets much faster than on the original hardware.
#if defined(__aarch64__)
#define RECOMP_SPIN_PAUSE() __asm__ __volatile__("isb" ::: "memory")
#elif defined(__x86_64__)
#define RECOMP_SPIN_PAUSE() __asm__ __volatile__("pause" ::: "memory")
#else
#define RECOMP_SPIN_PAUSE() __asm__ __volatile__("" ::: "memory")
#endif

#define MEM_LD8(addr) recomp_ld8((ctx), (uint64_t)(addr))
#define MEM_LD16(addr) recomp_ld16((ctx), (uint64_t)(addr))
#define MEM_LD32(addr) recomp_ld32((ctx), (uint64_t)(addr))
#define MEM_LD64(addr) recomp_ld64((ctx), (uint64_t)(addr))
#define MEM_ST8(addr, v) recomp_st8((ctx), (uint64_t)(addr), (v))
#define MEM_ST16(addr, v) recomp_st16((ctx), (uint64_t)(addr), (v))
#define MEM_ST32(addr, v) recomp_st32((ctx), (uint64_t)(addr), (v))
#define MEM_ST64(addr, v) recomp_st64((ctx), (uint64_t)(addr), (v))

// x87 80-bit extended precision <-> host double. The emulated x87 stack holds
// doubles, but guest code (e.g. libc printf's long double path) stores and
// reloads 10-byte m80 values and inspects their sign/exponent word directly,
// so the memory image must use the real extended format.
static inline double fpu_f80_to_double(const uint8_t *p) {
  uint64_t mant;
  uint16_t se;
  memcpy(&mant, p, 8);
  memcpy(&se, p + 8, 2);
  int neg = (se & 0x8000) != 0;
  int exp = se & 0x7fff;
  double r;
  if (exp == 0x7fff) {
    if ((mant << 1) == 0) {
      r = INFINITY;
    } else {
      uint64_t bits = 0x7ff8000000000000ULL | ((mant >> 11) & 0x000fffffffffffffULL);
      memcpy(&r, &bits, 8);
    }
  } else if (mant == 0) {
    r = 0.0;
  } else {
    int e = (exp == 0 ? 1 : exp) - 16383 - 63;
    r = ldexp((double)mant, e);
  }
  return neg ? -r : r;
}

static inline void fpu_double_to_f80(double v, uint8_t *p) {
  uint64_t bits;
  memcpy(&bits, &v, 8);
  uint16_t sign = (bits >> 63) ? 0x8000 : 0;
  int e = (int)((bits >> 52) & 0x7ff);
  uint64_t frac = bits & 0x000fffffffffffffULL;
  uint64_t mant;
  uint16_t exp;
  if (e == 0x7ff) {
    exp = 0x7fff;
    mant = 0x8000000000000000ULL | (frac << 11);
  } else if (e == 0) {
    if (frac == 0) {
      exp = 0;
      mant = 0;
    } else {
      int lz = __builtin_clzll(frac);
      mant = frac << lz;
      exp = (uint16_t)(16383 + 63 - 1074 - lz);
    }
  } else {
    exp = (uint16_t)(e - 1023 + 16383);
    mant = 0x8000000000000000ULL | (frac << 11);
  }
  uint16_t se = sign | exp;
  memcpy(p, &mant, 8);
  memcpy(p + 8, &se, 2);
}

#define FPU_LOAD_M80(addr) fpu_f80_to_double((const uint8_t *)((ctx)->mem_base + (uint64_t)(addr)))
#define FPU_STORE_M80(addr, v) fpu_double_to_f80((v), (uint8_t *)((ctx)->mem_base + (uint64_t)(addr)))

// Round according to the x87 control word rounding-control field (bits 10-11).
static inline double fpu_round_cw(uint16_t cw, double v) {
  switch ((cw >> 10) & 3) {
  case 1: return floor(v);
  case 2: return ceil(v);
  case 3: return trunc(v);
  default: {
    // round to nearest, ties to even (independent of the host rounding mode)
    double r = round(v);
    if (fabs(v - trunc(v)) == 0.5) r = 2.0 * round(v / 2.0);
    return r;
  }
  }
}

// x87 float -> integer store; out-of-range and NaN produce the "integer
// indefinite" value (most negative integer of the destination width).
static inline int64_t fpu_to_i64(double r) {
  if (!(r >= -9223372036854775808.0 && r < 9223372036854775808.0)) return INT64_MIN;
  return (int64_t)r;
}
static inline int32_t fpu_to_i32(double r) {
  if (!(r >= -2147483648.0 && r < 2147483648.0)) return INT32_MIN;
  return (int32_t)r;
}
static inline int16_t fpu_to_i16(double r) {
  if (!(r >= -32768.0 && r < 32768.0)) return INT16_MIN;
  return (int16_t)r;
}

// Guest TSC frequency reported by sceKernelGetTscFrequency. The guest reads
// the TSC both through the RDTSC instruction and sceKernelReadTsc, so both must
// return the same counter in these units.
#define RECOMP_TSC_FREQ 2130000000ULL

static inline uint64_t recomp_rdtsc(void) {
#if defined(__aarch64__)
  uint64_t val, frq;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
  if (frq == 0) return val;
  return (uint64_t)(((unsigned __int128)val * RECOMP_TSC_FREQ) / frq);
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

static inline void recomp_dump_guest_context(GuestContext *ctx) {
  if (!ctx) return;
  fprintf(stderr,
          "Registers:\n"
          "  RAX=0x%016llx RBX=0x%016llx RCX=0x%016llx RDX=0x%016llx\n"
          "  RSI=0x%016llx RDI=0x%016llx RBP=0x%016llx RSP=0x%016llx\n"
          "  R8 =0x%016llx R9 =0x%016llx R10=0x%016llx R11=0x%016llx\n"
          "  R12=0x%016llx R13=0x%016llx R14=0x%016llx R15=0x%016llx\n",
          (unsigned long long)ctx->rax, (unsigned long long)ctx->rbx,
          (unsigned long long)ctx->rcx, (unsigned long long)ctx->rdx,
          (unsigned long long)ctx->rsi, (unsigned long long)ctx->rdi,
          (unsigned long long)ctx->rbp, (unsigned long long)ctx->rsp,
          (unsigned long long)ctx->r8,  (unsigned long long)ctx->r9,
          (unsigned long long)ctx->r10, (unsigned long long)ctx->r11,
          (unsigned long long)ctx->r12, (unsigned long long)ctx->r13,
          (unsigned long long)ctx->r14, (unsigned long long)ctx->r15);
  if (ctx->mem_base && ctx->rsp < ctx->mem_size) {
    fprintf(stderr, "Guest call stack heuristic (RSP=0x%llx):\n", (unsigned long long)ctx->rsp);
    uint64_t sp = ctx->rsp;
    int frame = 0;
    for (int i = 0; i < 64 && sp + 8 <= ctx->mem_size; i++, sp += 8) {
      uint64_t val = *(uint64_t *)(ctx->mem_base + sp);
      if (val >= 0x10000 && val < (1ULL << 48) && val != (uint64_t)-1) {
        fprintf(stderr, "  frame #%d: 0x%016llx (RSP+0x%x)\n", frame++, (unsigned long long)val, (unsigned int)(sp - ctx->rsp));
      }
    }
  }
}

static inline void recomp_dispatch(GuestContext *ctx, uint64_t target) {
  uint64_t caller_rip = ctx->rip;
  ctx->rip = target;
  if (__builtin_expect(target != 0 && target < (1ULL << 48), 1)) {
    uint64_t l1_idx = (target >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    recomp_fn_t **l2 = g_dispatch_l1[l1_idx];
    if (__builtin_expect(l2 != NULL, 1)) {
      uint64_t l2_idx = (target >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
      recomp_fn_t *l3 = l2[l2_idx];
      if (__builtin_expect(l3 != NULL, 1)) {
        recomp_fn_t fn = l3[target & DISPATCH_LEVEL_MASK];
        if (__builtin_expect(fn != NULL, 1)) {
#if defined(RECOMP_ARM64_ASM)
          extern void recomp_enter_guest(GuestContext *ctx, uint64_t target_fn);
          recomp_enter_guest(ctx, (uint64_t)fn);
#else
          fn(ctx);
#endif
          return;
        }
      }
    }
  }
  uint64_t ret_addr = 0;
  if (ctx->mem_base && ctx->rsp < ctx->mem_size) {
    ret_addr = *(uint64_t *)(ctx->mem_base + ctx->rsp);
  }
  if (caller_rip != 0 && caller_rip != target) {
    fprintf(stderr,
            "\nFATAL: Unresolved indirect jump/call to 0x%llx (from RIP=0x%llx, return addr=0x%llx)\n",
            (unsigned long long)target, (unsigned long long)caller_rip, (unsigned long long)ret_addr);
  } else {
    fprintf(stderr,
            "\nFATAL: Unresolved indirect jump/call to 0x%llx (return addr=0x%llx)\n",
            (unsigned long long)target, (unsigned long long)ret_addr);
  }
  recomp_dump_guest_context(ctx);
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

// SIMD helpers. Recompiled code calls recomp_vpcmpistri, recomp_vpcmpe_stri and
// recomp_vaeskeygenassist in the middle of a guest instruction; those entry points are
// assembly (trampoline_arm64.s) that keep every vector register and the flags and call the C
// implementations below. recomp_vpcmpistri/recomp_vpcmpe_stri have the C signature of their
// implementation; recomp_vaeskeygenassist takes the source in v0 and the round constant in
// w0 and returns in v0, as the lifter calls it.
void recomp_vpcmpistri(GuestContext *ctx, const void *src2_ptr, const void *src1_ptr, uint8_t imm8);
void recomp_vpcmpe_stri(GuestContext *ctx, const void *src2_ptr, const void *src1_ptr, uint8_t imm8);
void recomp_vpcmpistri_impl(GuestContext *ctx, const void *src2_ptr, const void *src1_ptr, uint8_t imm8);
void recomp_vpcmpe_stri_impl(GuestContext *ctx, const void *src2_ptr, const void *src1_ptr, uint8_t imm8);

// AES-NI and PCLMULQDQ helpers
xmm_reg_t recomp_pclmulqdq(uint64_t a, uint64_t b);
xmm_reg_t recomp_vaesenc(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesenclast(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesdec(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesdeclast(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesimc(xmm_reg_t s);
xmm_reg_t recomp_vaeskeygenassist_impl(xmm_reg_t s, uint8_t rcon);

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
