#ifndef RECOMP_RUNTIME_H
#define RECOMP_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// 128-bit vector union for XMM registers
typedef union {
    uint8_t   u8[16];
    uint16_t  u16[8];
    uint32_t  u32[4];
    uint64_t  u64[2];
    __uint128_t u128;
    float     f32[4];
    double    f64[2];
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

    // Guest memory mapping
    uint8_t *mem_base;
    size_t   mem_size;
} GuestContext;

// Memory access helpers
#define MEM_U8(addr)  (*(uint8_t*)((ctx)->mem_base + (uint64_t)(addr)))
#define MEM_U16(addr) (*(uint16_t*)((ctx)->mem_base + (uint64_t)(addr)))
#define MEM_U32(addr) (*(uint32_t*)((ctx)->mem_base + (uint64_t)(addr)))
#define MEM_U64(addr) (*(uint64_t*)((ctx)->mem_base + (uint64_t)(addr)))

// Parity table computation
static inline uint8_t compute_parity(uint8_t val) {
    val ^= val >> 4;
    val ^= val >> 2;
    val ^= val >> 1;
    return (~val) & 1;
}

// 64-bit flag setters
static inline void set_flags_add_u64(GuestContext *ctx, uint64_t a, uint64_t b, uint64_t res) {
    ctx->cf = res < a;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 63) & 1;
    ctx->of = ((~(a ^ b) & (a ^ res)) >> 63) & 1;
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ b ^ res) & 0x10) != 0;
}

static inline void set_flags_sub_u64(GuestContext *ctx, uint64_t a, uint64_t b, uint64_t res) {
    ctx->cf = a < b;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 63) & 1;
    ctx->of = (((a ^ b) & (a ^ res)) >> 63) & 1;
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ b ^ res) & 0x10) != 0;
}

static inline void set_flags_logic_u64(GuestContext *ctx, uint64_t res) {
    ctx->cf = 0;
    ctx->of = 0;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 63) & 1;
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = 0;
}

static inline void set_flags_inc_u64(GuestContext *ctx, uint64_t a, uint64_t res) {
    ctx->zf = (res == 0);
    ctx->sf = (res >> 63) & 1;
    ctx->of = (res == 0x8000000000000000ULL);
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ 1 ^ res) & 0x10) != 0;
}

static inline void set_flags_dec_u64(GuestContext *ctx, uint64_t a, uint64_t res) {
    ctx->zf = (res == 0);
    ctx->sf = (res >> 63) & 1;
    ctx->of = (a == 0x8000000000000000ULL);
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ 1 ^ res) & 0x10) != 0;
}

// 32-bit flag setters
static inline void set_flags_add_u32(GuestContext *ctx, uint32_t a, uint32_t b, uint32_t res) {
    ctx->cf = res < a;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 31) & 1;
    ctx->of = ((~(a ^ b) & (a ^ res)) >> 31) & 1;
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ b ^ res) & 0x10) != 0;
}

static inline void set_flags_sub_u32(GuestContext *ctx, uint32_t a, uint32_t b, uint32_t res) {
    ctx->cf = a < b;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 31) & 1;
    ctx->of = (((a ^ b) & (a ^ res)) >> 31) & 1;
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ b ^ res) & 0x10) != 0;
}

static inline void set_flags_logic_u32(GuestContext *ctx, uint32_t res) {
    ctx->cf = 0;
    ctx->of = 0;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 31) & 1;
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = 0;
}

static inline void set_flags_inc_u32(GuestContext *ctx, uint32_t a, uint32_t res) {
    ctx->zf = (res == 0);
    ctx->sf = (res >> 31) & 1;
    ctx->of = (res == 0x80000000U);
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ 1 ^ res) & 0x10) != 0;
}

static inline void set_flags_dec_u32(GuestContext *ctx, uint32_t a, uint32_t res) {
    ctx->zf = (res == 0);
    ctx->sf = (res >> 31) & 1;
    ctx->of = (a == 0x80000000U);
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ 1 ^ res) & 0x10) != 0;
}

// 16-bit flag setters
static inline void set_flags_add_u16(GuestContext *ctx, uint16_t a, uint16_t b, uint16_t res) {
    ctx->cf = res < a;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 15) & 1;
    ctx->of = ((~(a ^ b) & (a ^ res)) >> 15) & 1;
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ b ^ res) & 0x10) != 0;
}

static inline void set_flags_sub_u16(GuestContext *ctx, uint16_t a, uint16_t b, uint16_t res) {
    ctx->cf = a < b;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 15) & 1;
    ctx->of = (((a ^ b) & (a ^ res)) >> 15) & 1;
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = ((a ^ b ^ res) & 0x10) != 0;
}

static inline void set_flags_logic_u16(GuestContext *ctx, uint16_t res) {
    ctx->cf = 0;
    ctx->of = 0;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 15) & 1;
    ctx->pf = compute_parity((uint8_t)(res & 0xff));
    ctx->af = 0;
}

// 8-bit flag setters
static inline void set_flags_add_u8(GuestContext *ctx, uint8_t a, uint8_t b, uint8_t res) {
    ctx->cf = res < a;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 7) & 1;
    ctx->of = ((~(a ^ b) & (a ^ res)) >> 7) & 1;
    ctx->pf = compute_parity(res);
    ctx->af = ((a ^ b ^ res) & 0x10) != 0;
}

static inline void set_flags_sub_u8(GuestContext *ctx, uint8_t a, uint8_t b, uint8_t res) {
    ctx->cf = a < b;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 7) & 1;
    ctx->of = (((a ^ b) & (a ^ res)) >> 7) & 1;
    ctx->pf = compute_parity(res);
    ctx->af = ((a ^ b ^ res) & 0x10) != 0;
}

static inline void set_flags_logic_u8(GuestContext *ctx, uint8_t res) {
    ctx->cf = 0;
    ctx->of = 0;
    ctx->zf = (res == 0);
    ctx->sf = (res >> 7) & 1;
    ctx->pf = compute_parity(res);
    ctx->af = 0;
}

// Function pointer type for recompiled functions
typedef void (*recomp_fn_t)(GuestContext *ctx);

// Dispatch table: maps guest virtual address to compiled function pointer
#define DISPATCH_TABLE_SIZE (0x200000) // 2MB range for code
extern recomp_fn_t g_dispatch_table[DISPATCH_TABLE_SIZE];

void recomp_register_fn(uint64_t guest_addr, recomp_fn_t fn);

static inline void recomp_dispatch(GuestContext *ctx, uint64_t target) {
    ctx->rip = target;
    if (target < DISPATCH_TABLE_SIZE && g_dispatch_table[target]) {
        g_dispatch_table[target](ctx);
        return;
    }
    fprintf(stderr, "FATAL: Unresolved indirect jump/call to 0x%llx (from RIP=0x%llx)\n",
            (unsigned long long)target, (unsigned long long)ctx->rip);
    abort();
}

// Runtime initialization and memory layout
GuestContext *recomp_init_runtime(const uint8_t *elf_image, size_t image_size);
void recomp_free_runtime(GuestContext *ctx);

// Syscall / Libkernel Shim declarations
void shim_sceKernelUsleep(GuestContext *ctx);
void shim_write(GuestContext *ctx);
void shim_writev(GuestContext *ctx);
void shim_open(GuestContext *ctx);
void shim_close(GuestContext *ctx);
void shim_lseek(GuestContext *ctx);
void shim_fstat(GuestContext *ctx);
void shim_ioctl(GuestContext *ctx);
void shim_mmap(GuestContext *ctx);
void shim_munmap(GuestContext *ctx);
void shim_madvise(GuestContext *ctx);
void shim_nanosleep(GuestContext *ctx);
void shim_sysconf(GuestContext *ctx);
void shim_sched_yield(GuestContext *ctx);
void shim_exit(GuestContext *ctx);
void shim_error(GuestContext *ctx);
void shim_sigaction(GuestContext *ctx);
void shim_sigprocmask(GuestContext *ctx);
void shim_raise(GuestContext *ctx);
void shim_poll(GuestContext *ctx);

// Pthread shims
void shim_pthread_mutex_init(GuestContext *ctx);
void shim_pthread_mutex_lock(GuestContext *ctx);
void shim_pthread_mutex_trylock(GuestContext *ctx);
void shim_pthread_mutex_unlock(GuestContext *ctx);
void shim_pthread_mutex_destroy(GuestContext *ctx);
void shim_pthread_mutexattr_init(GuestContext *ctx);
void shim_pthread_mutexattr_settype(GuestContext *ctx);
void shim_pthread_mutexattr_destroy(GuestContext *ctx);
void shim_pthread_cond_init(GuestContext *ctx);
void shim_pthread_cond_wait(GuestContext *ctx);
void shim_pthread_cond_timedwait(GuestContext *ctx);
void shim_pthread_cond_signal(GuestContext *ctx);
void shim_pthread_cond_broadcast(GuestContext *ctx);
void shim_pthread_cond_destroy(GuestContext *ctx);
void shim_pthread_rwlock_rdlock(GuestContext *ctx);
void shim_pthread_rwlock_wrlock(GuestContext *ctx);
void shim_pthread_rwlock_unlock(GuestContext *ctx);
void shim_pthread_once(GuestContext *ctx);
void shim_pthread_key_create(GuestContext *ctx);
void shim_pthread_setspecific(GuestContext *ctx);
void shim_pthread_getspecific(GuestContext *ctx);
void shim_pthread_self(GuestContext *ctx);
void shim_pthread_equal(GuestContext *ctx);
void shim_pthread_detach(GuestContext *ctx);
void shim_pthread_join(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // RECOMP_RUNTIME_H
