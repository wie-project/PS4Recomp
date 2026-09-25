// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RECOMP_RUNTIME_H
#define RECOMP_RUNTIME_H

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
          "FATAL: Unresolved indirect jump/call to 0x%llx (from RIP=0x%llx)\n",
          (unsigned long long)target, (unsigned long long)ctx->rip);
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

// Runtime initialization and memory layout
GuestContext *recomp_init_runtime(size_t guest_mem_sz, const uint8_t *elf_image, size_t image_size, const char *prog_name);
GuestContext *recomp_init_runtime_file(const char *image_filename, size_t requested_mem_sz, const char *prog_name);
void recomp_free_runtime(GuestContext *ctx);
GuestContext *recomp_create_thread_context(GuestContext *parent, uint64_t stack_size);
uint64_t recomp_vm_alloc(GuestContext *ctx, size_t size);
uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int prot, int flags, const char *name);
uint64_t recomp_vm_alloc_fixed(GuestContext *ctx, uint64_t desired_addr, size_t size, int prot, int flags, const char *name);
int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size);
GuestVMExtent *recomp_vm_find(GuestContext *ctx, uint64_t addr);

// Syscall / Libkernel Shim declarations
void shim_sceKernelUsleep(GuestContext *ctx);
void shim_write(GuestContext *ctx);
void shim_writev(GuestContext *ctx);
void shim_memcpy(GuestContext *ctx);
void shim_memmove(GuestContext *ctx);
void shim_memset(GuestContext *ctx);
void shim_strlen(GuestContext *ctx);
void shim_strcpy(GuestContext *ctx);
void shim_strncpy(GuestContext *ctx);
void shim_strcmp(GuestContext *ctx);
void shim_read(GuestContext *ctx);
void shim_readv(GuestContext *ctx);
void shim_open(GuestContext *ctx);
void shim_close(GuestContext *ctx);
void shim_lseek(GuestContext *ctx);
void shim_fcntl(GuestContext *ctx);
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
void shim_syscall(GuestContext *ctx);
void shim___stack_chk_fail(GuestContext *ctx);
void shim_clock_gettime(GuestContext *ctx);
void shim_gettimeofday(GuestContext *ctx);
void shim_getrusage(GuestContext *ctx);
void shim_getrlimit(GuestContext *ctx);
void shim_cpuset_getaffinity(GuestContext *ctx);

// Pthread shims
void shim_pthread_create(GuestContext *ctx);
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
void shim_pthread_key_delete(GuestContext *ctx);
void shim_pthread_setspecific(GuestContext *ctx);
void shim_pthread_getspecific(GuestContext *ctx);
void shim_pthread_exit(GuestContext *ctx);
void shim_pthread_rwlock_init(GuestContext *ctx);
void shim_pthread_rwlock_destroy(GuestContext *ctx);
void shim_pthread_rename_np(GuestContext *ctx);
void shim_pthread_create_name_np(GuestContext *ctx);

// POSIX System & File I/O shims
void shim_stat(GuestContext *ctx);
void shim_chmod(GuestContext *ctx);
void shim_utimes(GuestContext *ctx);
void shim_getdents(GuestContext *ctx);
void shim_unlink(GuestContext *ctx);
void shim_usleep(GuestContext *ctx);
void shim_getpid(GuestContext *ctx);

// BSD Socket shims
void shim_socket(GuestContext *ctx);
void shim_connect(GuestContext *ctx);
void shim_bind(GuestContext *ctx);
void shim_listen(GuestContext *ctx);
void shim_accept(GuestContext *ctx);
void shim_send(GuestContext *ctx);
void shim_recv(GuestContext *ctx);
void shim_sendto(GuestContext *ctx);
void shim_recvfrom(GuestContext *ctx);
void shim_setsockopt(GuestContext *ctx);
void shim_getsockopt(GuestContext *ctx);
void shim_getsockname(GuestContext *ctx);
void shim_getpeername(GuestContext *ctx);
void shim_shutdown(GuestContext *ctx);
void shim_select(GuestContext *ctx);
void shim_inet_pton(GuestContext *ctx);

// libSceNet shims
void shim_sceNetInit(GuestContext *ctx);
void shim_sceNetTerm(GuestContext *ctx);
void shim_sceNetErrnoLoc(GuestContext *ctx);
void shim_sceNetSocket(GuestContext *ctx);
void shim_sceNetSocketClose(GuestContext *ctx);
void shim_sceNetBind(GuestContext *ctx);
void shim_sceNetListen(GuestContext *ctx);
void shim_sceNetAccept(GuestContext *ctx);
void shim_sceNetConnect(GuestContext *ctx);
void shim_sceNetSend(GuestContext *ctx);
void shim_sceNetRecv(GuestContext *ctx);
void shim_sceNetSendto(GuestContext *ctx);
void shim_sceNetRecvfrom(GuestContext *ctx);
void shim_sceNetSetsockopt(GuestContext *ctx);
void shim_sceNetGetsockopt(GuestContext *ctx);
void shim_sceNetGetsockname(GuestContext *ctx);
void shim_sceNetGetpeername(GuestContext *ctx);
void shim_sceNetShutdown(GuestContext *ctx);
void shim_sceNetHtons(GuestContext *ctx);
void shim_sceNetHtonl(GuestContext *ctx);
void shim_sceNetHtonll(GuestContext *ctx);
void shim_sceNetNtohs(GuestContext *ctx);
void shim_sceNetNtohl(GuestContext *ctx);
void shim_sceNetNtohll(GuestContext *ctx);
void shim_sceNetInetPton(GuestContext *ctx);
void shim_sceNetInetNtop(GuestContext *ctx);
void shim_sceNetPoolCreate(GuestContext *ctx);
void shim_sceNetPoolDestroy(GuestContext *ctx);
void shim_sceNetResolverCreate(GuestContext *ctx);
void shim_sceNetResolverDestroy(GuestContext *ctx);
void shim_sceNetResolverStartNtoa(GuestContext *ctx);
void shim_sceNetResolverStartAton(GuestContext *ctx);
void shim_sceNetResolverStartNtoaMultipleRecords(GuestContext *ctx);
void shim_sceNetEpollCreate(GuestContext *ctx);
void shim_sceNetEpollDestroy(GuestContext *ctx);
void shim_sceNetEpollControl(GuestContext *ctx);
void shim_sceNetEpollWait(GuestContext *ctx);
void shim_sceNetGetMacAddress(GuestContext *ctx);
void shim_sceNetGetSockInfo(GuestContext *ctx);

// Kernel events and lifecycle
void shim_sceKernelTriggerUserEvent(GuestContext *ctx);
void shim_sceKernelAddUserEventEdge(GuestContext *ctx);
void shim_sceKernelStopUnloadModule(GuestContext *ctx);
void shim_sceKernelGetPrtAperture(GuestContext *ctx);
void shim_pthread_self(GuestContext *ctx);
void shim_pthread_equal(GuestContext *ctx);
void shim_pthread_detach(GuestContext *ctx);
void shim_pthread_join(GuestContext *ctx);
void shim_pthread_attr_init(GuestContext *ctx);
void shim_pthread_attr_destroy(GuestContext *ctx);
void shim_pthread_attr_setdetachstate(GuestContext *ctx);
void shim_pthread_attr_setstacksize(GuestContext *ctx);
void shim_pthread_getschedparam(GuestContext *ctx);
void shim_pthread_setschedparam(GuestContext *ctx);
void shim_pthread_setcanceltype(GuestContext *ctx);
void shim_pthread_sigmask(GuestContext *ctx);
void shim_sched_get_priority_max(GuestContext *ctx);
void shim_sched_get_priority_min(GuestContext *ctx);
void recomp_init_main_thread(GuestContext *ctx);

// Orbis Pthread threading & synchronization shims
void shim_scePthreadCreate(GuestContext *ctx);
void shim_scePthreadJoin(GuestContext *ctx);
void shim_scePthreadDetach(GuestContext *ctx);
void shim_scePthreadExit(GuestContext *ctx);
void shim_scePthreadSelf(GuestContext *ctx);
void shim_scePthreadEqual(GuestContext *ctx);
void shim_scePthreadYield(GuestContext *ctx);
void shim_scePthreadGetthreadid(GuestContext *ctx);
void shim_scePthreadSetprio(GuestContext *ctx);
void shim_scePthreadGetprio(GuestContext *ctx);
void shim_scePthreadSetaffinity(GuestContext *ctx);
void shim_scePthreadGetaffinity(GuestContext *ctx);
void shim_scePthreadAttrInit(GuestContext *ctx);
void shim_scePthreadAttrDestroy(GuestContext *ctx);
void shim_scePthreadAttrSetstacksize(GuestContext *ctx);
void shim_scePthreadAttrSetdetachstate(GuestContext *ctx);
void shim_scePthreadAttrSetschedpolicy(GuestContext *ctx);
void shim_scePthreadAttrSetschedparam(GuestContext *ctx);
void shim_scePthreadAttrGetschedparam(GuestContext *ctx);
void shim_scePthreadAttrSetinheritsched(GuestContext *ctx);
void shim_scePthreadAttrSetaffinity(GuestContext *ctx);
void shim_scePthreadMutexInit(GuestContext *ctx);
void shim_scePthreadMutexLock(GuestContext *ctx);
void shim_scePthreadMutexTrylock(GuestContext *ctx);
void shim_scePthreadMutexUnlock(GuestContext *ctx);
void shim_scePthreadMutexDestroy(GuestContext *ctx);
void shim_scePthreadMutexattrInit(GuestContext *ctx);
void shim_scePthreadMutexattrDestroy(GuestContext *ctx);
void shim_scePthreadMutexattrSettype(GuestContext *ctx);
void shim_scePthreadMutexattrSetprotocol(GuestContext *ctx);
void shim_scePthreadCondInit(GuestContext *ctx);
void shim_scePthreadCondDestroy(GuestContext *ctx);
void shim_scePthreadCondSignal(GuestContext *ctx);
void shim_scePthreadCondBroadcast(GuestContext *ctx);
void shim_scePthreadCondWait(GuestContext *ctx);
void shim_scePthreadCondTimedwait(GuestContext *ctx);
void shim_scePthreadCondattrInit(GuestContext *ctx);
void shim_scePthreadCondattrDestroy(GuestContext *ctx);
void shim_scePthreadKeyCreate(GuestContext *ctx);
void shim_scePthreadKeyDelete(GuestContext *ctx);
void shim_scePthreadSetspecific(GuestContext *ctx);
void shim_scePthreadGetspecific(GuestContext *ctx);
void shim___tls_get_addr(GuestContext *ctx);

// POSIX Semaphores
void shim_sem_init(GuestContext *ctx);
void shim_sem_destroy(GuestContext *ctx);
void shim_sem_wait(GuestContext *ctx);
void shim_sem_trywait(GuestContext *ctx);
void shim_sem_post(GuestContext *ctx);
void shim_sem_getvalue(GuestContext *ctx);

// Direct Memory & VMM shims
void shim_sceKernelAllocateDirectMemory(GuestContext *ctx);
void shim_sceKernelAllocateMainDirectMemory(GuestContext *ctx);
void shim_sceKernelGetDirectMemorySize(GuestContext *ctx);
void shim_sceKernelAvailableDirectMemorySize(GuestContext *ctx);
void shim_sceKernelMapDirectMemory(GuestContext *ctx);
void shim_sceKernelReleaseDirectMemory(GuestContext *ctx);
void shim_sceKernelMapFlexibleMemory(GuestContext *ctx);
void shim_sceKernelAvailableFlexibleMemorySize(GuestContext *ctx);
void shim_sceKernelVirtualQuery(GuestContext *ctx);
void shim_sceKernelQueryMemoryProtection(GuestContext *ctx);
void shim_sceKernelMemoryPoolReserve(GuestContext *ctx);
void shim_sceKernelMemoryPoolExpand(GuestContext *ctx);
void shim_sceKernelMemoryPoolCommit(GuestContext *ctx);
void shim_sceKernelMemoryPoolDecommit(GuestContext *ctx);

// Orbis Event Flags
void shim_sceKernelCreateEventFlag(GuestContext *ctx);
void shim_sceKernelDeleteEventFlag(GuestContext *ctx);
void shim_sceKernelSetEventFlag(GuestContext *ctx);
void shim_sceKernelClearEventFlag(GuestContext *ctx);
void shim_sceKernelWaitEventFlag(GuestContext *ctx);
void shim_sceKernelPollEventFlag(GuestContext *ctx);

// Kernel High-Resolution Timers & CPU Configuration
void shim_sceKernelGetProcessTimeCounter(GuestContext *ctx);
void shim_sceKernelGetProcessTimeCounterFrequency(GuestContext *ctx);
void shim_sceKernelGetProcessTime(GuestContext *ctx);
void shim_sceKernelGetTscFrequency(GuestContext *ctx);
void shim_sceKernelReadTsc(GuestContext *ctx);
void shim_sceKernelGettimeofday(GuestContext *ctx);
void shim_sceKernelGetCurrentCpu(GuestContext *ctx);
void shim_sceKernelGetCpumode(GuestContext *ctx);
void shim_sceKernelIsNeoMode(GuestContext *ctx);
void shim_sceKernelHasNeoMode(GuestContext *ctx);
void shim_sceKernelIsAuthenticNeo(GuestContext *ctx);
void shim_sceKernelGetSystemSwVersion(GuestContext *ctx);

// Kernel Equeue shims
void shim_sceKernelCreateEqueue(GuestContext *ctx);
void shim_sceKernelDeleteEqueue(GuestContext *ctx);
void shim_sceKernelWaitEqueue(GuestContext *ctx);

// AudioOut shims
void shim_sceAudioOutInit(GuestContext *ctx);
void shim_sceAudioOutOpen(GuestContext *ctx);
void shim_sceAudioOutOutput(GuestContext *ctx);
void shim_sceAudioOutClose(GuestContext *ctx);

// VideoOut display shims
void shim_sceVideoOutOpen(GuestContext *ctx);
void shim_sceVideoOutClose(GuestContext *ctx);
void shim_sceVideoOutSetBufferAttribute(GuestContext *ctx);
void shim_sceVideoOutRegisterBuffers(GuestContext *ctx);
void shim_sceVideoOutSetFlipRate(GuestContext *ctx);
void shim_sceVideoOutAddFlipEvent(GuestContext *ctx);
void shim_sceVideoOutSubmitFlip(GuestContext *ctx);
void shim_sceVideoOutGetFlipStatus(GuestContext *ctx);
void shim_sceVideoOutGetResolutionStatus(GuestContext *ctx);
void shim_sceVideoOutIsFlipPending(GuestContext *ctx);
void shim_sceVideoOutUnregisterBuffers(GuestContext *ctx);
void shim_sceVideoOutGetVblankStatus(GuestContext *ctx);
void shim_sceVideoOutGetDeviceCapabilityInfo_(GuestContext *ctx);
void shim_sceVideoOutModeSetAny_(GuestContext *ctx);

// Pad shims
void shim_scePadGetHandle(GuestContext *ctx);

// User Service shims
void shim_sceUserServiceInitialize(GuestContext *ctx);
void shim_sceUserServiceGetInitialUser(GuestContext *ctx);
void shim_sceUserServiceGetLoginUserIdList(GuestContext *ctx);
void shim_sceUserServiceGetUserName(GuestContext *ctx);
void shim_sceUserServiceGetEvent(GuestContext *ctx);
void shim_sceUserServiceTerminate(GuestContext *ctx);

// PlayGo shims
void shim_scePlayGoInitialize(GuestContext *ctx);
void shim_scePlayGoTerminate(GuestContext *ctx);
void shim_scePlayGoOpen(GuestContext *ctx);
void shim_scePlayGoClose(GuestContext *ctx);
void shim_scePlayGoGetProgress(GuestContext *ctx);
void shim_scePlayGoGetLocus(GuestContext *ctx);
void shim_scePlayGoGetLanguageMask(GuestContext *ctx);
void shim_scePlayGoSetLanguageMask(GuestContext *ctx);
void shim_scePlayGoGetInstallSpeed(GuestContext *ctx);
void shim_scePlayGoSetInstallSpeed(GuestContext *ctx);
void shim_scePlayGoGetToDoList(GuestContext *ctx);
void shim_scePlayGoGetEta(GuestContext *ctx);
void shim_scePlayGoPrefetch(GuestContext *ctx);

// RTC shims
void shim_sceRtcGetCurrentTick(GuestContext *ctx);
void shim_sceRtcGetTick(GuestContext *ctx);
void shim_sceRtcSetTick(GuestContext *ctx);
void shim_sceRtcGetTickResolution(GuestContext *ctx);
void shim_sceRtcGetCurrentClockLocalTime(GuestContext *ctx);
void shim_sceRtcGetDayOfWeek(GuestContext *ctx);
void shim_sceRtcIsLeapYear(GuestContext *ctx);

// Random shims
void shim_sceRandomGetRandomNumber(GuestContext *ctx);

// Pad shims
void shim_scePadInit(GuestContext *ctx);
void shim_scePadOpen(GuestContext *ctx);
void shim_scePadClose(GuestContext *ctx);
void shim_scePadReadState(GuestContext *ctx);
void shim_scePadRead(GuestContext *ctx);
void shim_scePadSetVibration(GuestContext *ctx);
void shim_scePadGetControllerInformation(GuestContext *ctx);
void shim_scePadSetLightBar(GuestContext *ctx);
void shim_scePadResetOrientation(GuestContext *ctx);
void shim_scePadResetLightBar(GuestContext *ctx);

// Keyboard shims
void shim_sceKeyboardInit(GuestContext *ctx);
void shim_sceKeyboardOpen(GuestContext *ctx);
void shim_sceKeyboardClose(GuestContext *ctx);
void shim_sceKeyboardReadState(GuestContext *ctx);
void shim_sceKeyboardGetKey2Char(GuestContext *ctx);
void shim_sceKeyboardGetHandle(GuestContext *ctx);

// Sysmodule shims
void shim_sceSysmoduleLoadModule(GuestContext *ctx);
void shim_sceSysmoduleIsLoaded(GuestContext *ctx);
void shim_sceSysmoduleUnloadModule(GuestContext *ctx);
void shim_sceSysmoduleLoadModuleInternal(GuestContext *ctx);
void shim_sceSysmoduleUnloadModuleInternal(GuestContext *ctx);

// FreeType shims
void shim_FT_Init_FreeType(GuestContext *ctx);
void shim_FT_New_Face(GuestContext *ctx);
void shim_FT_Set_Pixel_Sizes(GuestContext *ctx);
void shim_FT_Get_Char_Index(GuestContext *ctx);
void shim_FT_Load_Glyph(GuestContext *ctx);
void shim_FT_Render_Glyph(GuestContext *ctx);

// CommonDialog & MsgDialog shims
void shim_sceCommonDialogInitialize(GuestContext *ctx);
void shim_sceCommonDialogIsUsed(GuestContext *ctx);
void shim_sceMsgDialogInitialize(GuestContext *ctx);
void shim_sceMsgDialogOpen(GuestContext *ctx);
void shim_sceMsgDialogGetResult(GuestContext *ctx);
void shim_sceMsgDialogGetStatus(GuestContext *ctx);
void shim_sceMsgDialogUpdateStatus(GuestContext *ctx);
void shim_sceMsgDialogClose(GuestContext *ctx);
void shim_sceMsgDialogTerminate(GuestContext *ctx);
void shim_sceMsgDialogProgressBarInc(GuestContext *ctx);
void shim_sceMsgDialogProgressBarSetMsg(GuestContext *ctx);
void shim_sceMsgDialogProgressBarSetValue(GuestContext *ctx);

// Group 2: Dialogs, System UI & Media Services
void shim_sceImeDialogInit(GuestContext *ctx);
void shim_sceImeDialogGetStatus(GuestContext *ctx);
void shim_sceImeDialogGetResult(GuestContext *ctx);
void shim_sceImeDialogAbort(GuestContext *ctx);
void shim_sceImeDialogTerm(GuestContext *ctx);
void shim_sceImeDialogGetPanelSizeExtended(GuestContext *ctx);

void shim_sceSaveDataDialogInitialize(GuestContext *ctx);
void shim_sceSaveDataDialogOpen(GuestContext *ctx);
void shim_sceSaveDataDialogUpdateStatus(GuestContext *ctx);
void shim_sceSaveDataDialogGetResult(GuestContext *ctx);
void shim_sceSaveDataDialogTerminate(GuestContext *ctx);

void shim_sceErrorDialogInitialize(GuestContext *ctx);
void shim_sceErrorDialogOpen(GuestContext *ctx);
void shim_sceErrorDialogUpdateStatus(GuestContext *ctx);
void shim_sceErrorDialogGetStatus(GuestContext *ctx);
void shim_sceErrorDialogTerminate(GuestContext *ctx);

void shim_sceInvitationDialogInitialize(GuestContext *ctx);
void shim_sceInvitationDialogOpenA(GuestContext *ctx);
void shim_sceInvitationDialogUpdateStatus(GuestContext *ctx);
void shim_sceInvitationDialogGetStatus(GuestContext *ctx);
void shim_sceInvitationDialogGetResultA(GuestContext *ctx);
void shim_sceInvitationDialogTerminate(GuestContext *ctx);

void shim_sceNpProfileDialogInitialize(GuestContext *ctx);
void shim_sceNpProfileDialogOpenA(GuestContext *ctx);
void shim_sceNpProfileDialogUpdateStatus(GuestContext *ctx);
void shim_sceNpProfileDialogGetStatus(GuestContext *ctx);
void shim_sceNpProfileDialogGetResult(GuestContext *ctx);
void shim_sceNpProfileDialogTerminate(GuestContext *ctx);

void shim_sceVideoRecordingQueryMemSize2(GuestContext *ctx);
void shim_sceVideoRecordingOpen2(GuestContext *ctx);
void shim_sceVideoRecordingClose(GuestContext *ctx);
void shim_sceVideoRecordingStart(GuestContext *ctx);
void shim_sceVideoRecordingStop(GuestContext *ctx);
void shim_sceVideoRecordingGetStatus(GuestContext *ctx);
void shim_sceVideoRecordingSetInfo(GuestContext *ctx);

void shim_sceScreenShotEnable(GuestContext *ctx);
void shim_sceScreenShotDisable(GuestContext *ctx);

void shim_sceSharePlayInitialize(GuestContext *ctx);
void shim_sceSharePlayTerminate(GuestContext *ctx);
void shim_sceSharePlaySetProhibition(GuestContext *ctx);

void shim_sceMouseInit(GuestContext *ctx);
void shim_sceMouseOpen(GuestContext *ctx);
void shim_sceMouseClose(GuestContext *ctx);
void shim_sceMouseRead(GuestContext *ctx);

// Trophy shims
void shim_sceNpTrophyInit(GuestContext *ctx);
void shim_sceNpTrophyTerm(GuestContext *ctx);
void shim_sceNpTrophyCreateContext(GuestContext *ctx);
void shim_sceNpTrophyDestroyContext(GuestContext *ctx);
void shim_sceNpTrophyCreateHandle(GuestContext *ctx);
void shim_sceNpTrophyDestroyHandle(GuestContext *ctx);
void shim_sceNpTrophyRegisterContext(GuestContext *ctx);
void shim_sceNpTrophyUnlockTrophy(GuestContext *ctx);
void shim_sceNpTrophyShowTrophyList(GuestContext *ctx);

// libSceHttp & libSceHttp2
void shim_sceHttpInit(GuestContext *ctx);
void shim_sceHttpTerm(GuestContext *ctx);
void shim_sceHttpCreateTemplate(GuestContext *ctx);
void shim_sceHttpDeleteTemplate(GuestContext *ctx);
void shim_sceHttpCreateConnectionWithURL(GuestContext *ctx);
void shim_sceHttpDeleteConnection(GuestContext *ctx);
void shim_sceHttpCreateRequestWithURL(GuestContext *ctx);
void shim_sceHttpCreateRequestWithURL2(GuestContext *ctx);
void shim_sceHttpDeleteRequest(GuestContext *ctx);
void shim_sceHttpSendRequest(GuestContext *ctx);
void shim_sceHttpAbortRequest(GuestContext *ctx);
void shim_sceHttpWaitRequest(GuestContext *ctx);
void shim_sceHttpReadData(GuestContext *ctx);
void shim_sceHttpGetStatusCode(GuestContext *ctx);
void shim_sceHttpGetResponseContentLength(GuestContext *ctx);
void shim_sceHttpGetAllResponseHeaders(GuestContext *ctx);
void shim_sceHttpAddRequestHeader(GuestContext *ctx);
void shim_sceHttpSetNonblock(GuestContext *ctx);
void shim_sceHttpGetLastErrno(GuestContext *ctx);
void shim_sceHttpUriParse(GuestContext *ctx);
void shim_sceHttpUriBuild(GuestContext *ctx);
void shim_sceHttpCreateEpoll(GuestContext *ctx);
void shim_sceHttpSetEpoll(GuestContext *ctx);
void shim_sceHttpDestroyEpoll(GuestContext *ctx);
void shim_sceHttp2Init(GuestContext *ctx);
void shim_sceHttp2Term(GuestContext *ctx);

// libSceSsl
void shim_sceSslInit(GuestContext *ctx);
void shim_sceSslTerm(GuestContext *ctx);
void shim_sceSslGetCaCerts(GuestContext *ctx);

// libSceJson2
void shim__ZN3sce4Json12MemAllocatorC2Ev(GuestContext *ctx);
void shim__ZN3sce4Json12MemAllocatorD2Ev(GuestContext *ctx);
void shim__ZN3sce4Json14InitParameter2C1Ev(GuestContext *ctx);
void shim__ZN3sce4Json14InitParameter212setAllocatorEPNS0_12MemAllocatorEPv(GuestContext *ctx);
void shim__ZN3sce4Json14InitParameter217setFileBufferSizeEm(GuestContext *ctx);
void shim__ZN3sce4Json11InitializerC1Ev(GuestContext *ctx);
void shim__ZN3sce4Json11InitializerD1Ev(GuestContext *ctx);
void shim__ZN3sce4Json11Initializer10initializeEPKNS0_14InitParameter2E(GuestContext *ctx);
void shim__ZN3sce4Json11Initializer9terminateEv(GuestContext *ctx);
void shim__ZN3sce4Json6StringC1EPKc(GuestContext *ctx);
void shim__ZN3sce4Json6StringD1Ev(GuestContext *ctx);
void shim__ZNK3sce4Json6String5c_strEv(GuestContext *ctx);
void shim__ZN3sce4Json5ValueC1Ev(GuestContext *ctx);
void shim__ZN3sce4Json5ValueD1Ev(GuestContext *ctx);
void shim__ZN3sce4Json5ValueaSERKS1_(GuestContext *ctx);
void shim__ZN3sce4Json5ValueC1ERKNS0_6StringE(GuestContext *ctx);
void shim__ZNK3sce4Json5Value9getStringEv(GuestContext *ctx);
void shim__ZNK3sce4Json5ValueixEPKc(GuestContext *ctx);
void shim__ZN3sce4Json6ObjectC1Ev(GuestContext *ctx);
void shim__ZN3sce4Json6ObjectC1ERKS1_(GuestContext *ctx);
void shim__ZN3sce4Json6ObjectD1Ev(GuestContext *ctx);
void shim__ZN3sce4Json6ObjectixERKNS0_6StringE(GuestContext *ctx);
void shim__ZN3sce4Json6Parser5parseERNS0_5ValueEPKcm(GuestContext *ctx);

// libSceNp
void shim_sceNpCheckNpAvailability(GuestContext *ctx);
void shim_sceNpCheckNpAvailabilityA(GuestContext *ctx);
void shim_sceNpCheckPlus(GuestContext *ctx);
void shim_sceNpCreateAsyncRequest(GuestContext *ctx);
void shim_sceNpCreateRequest(GuestContext *ctx);
void shim_sceNpDeleteRequest(GuestContext *ctx);
void shim_sceNpGetAccountCountry(GuestContext *ctx);
void shim_sceNpGetAccountCountryA(GuestContext *ctx);
void shim_sceNpGetAccountDateOfBirth(GuestContext *ctx);
void shim_sceNpGetAccountDateOfBirthA(GuestContext *ctx);
void shim_sceNpGetAccountId(GuestContext *ctx);
void shim_sceNpGetAccountIdA(GuestContext *ctx);
void shim_sceNpGetAccountLanguage(GuestContext *ctx);
void shim_sceNpGetAccountLanguageA(GuestContext *ctx);
void shim_sceNpGetGamePresenceStatus(GuestContext *ctx);
void shim_sceNpGetGamePresenceStatusA(GuestContext *ctx);
void shim_sceNpGetNpId(GuestContext *ctx);
void shim_sceNpGetNpReachabilityState(GuestContext *ctx);
void shim_sceNpGetOnlineId(GuestContext *ctx);
void shim_sceNpGetState(GuestContext *ctx);
void shim_sceNpHasSignedUp(GuestContext *ctx);
void shim_sceNpIsPlusMember(GuestContext *ctx);
void shim_sceNpPollAsync(GuestContext *ctx);
void shim_sceNpWaitAsync(GuestContext *ctx);
void shim_sceNpRegisterStateCallback(GuestContext *ctx);
void shim_sceNpRegisterStateCallbackA(GuestContext *ctx);
void shim_sceNpUnregisterStateCallback(GuestContext *ctx);
void shim_sceNpUnregisterStateCallbackA(GuestContext *ctx);

void shim_sceNpAuthCreateRequest(GuestContext *ctx);
void shim_sceNpAuthCreateAsyncRequest(GuestContext *ctx);
void shim_sceNpAuthDeleteRequest(GuestContext *ctx);
void shim_sceNpAuthGetAuthorizationCode(GuestContext *ctx);
void shim_sceNpAuthGetAuthorizationCodeA(GuestContext *ctx);
void shim_sceNpAuthPollAsync(GuestContext *ctx);
void shim_sceNpAuthWaitAsync(GuestContext *ctx);

void shim_sceNpMatching2Initialize(GuestContext *ctx);
void shim_sceNpMatching2Terminate(GuestContext *ctx);
void shim_sceNpMatching2CreateContext(GuestContext *ctx);
void shim_sceNpMatching2CreateContextA(GuestContext *ctx);

void shim_sceNpSignalingInitialize(GuestContext *ctx);
void shim_sceNpSignalingTerminate(GuestContext *ctx);
void shim_sceNpSignalingCreateContext(GuestContext *ctx);
void shim_sceNpSignalingCreateContextA(GuestContext *ctx);

void shim_sceNpWebApiInitialize(GuestContext *ctx);
void shim_sceNpWebApiTerminate(GuestContext *ctx);
void shim_sceNpWebApiCreateContext(GuestContext *ctx);
void shim_sceNpWebApiCreateContextA(GuestContext *ctx);
void shim_sceNpWebApiDeleteContext(GuestContext *ctx);
void shim_sceNpWebApiCreateRequest(GuestContext *ctx);
void shim_sceNpWebApiSendRequest(GuestContext *ctx);
void shim_sceNpWebApiSendRequest2(GuestContext *ctx);
void shim_sceNpWebApiDeleteRequest(GuestContext *ctx);

void shim_sceNpScoreCreateNpTitleCtx(GuestContext *ctx);
void shim_sceNpScoreCreateNpTitleCtxA(GuestContext *ctx);
void shim_sceNpScoreDeleteNpTitleCtx(GuestContext *ctx);
void shim_sceNpScoreCreateRequest(GuestContext *ctx);
void shim_sceNpScoreDeleteRequest(GuestContext *ctx);
void shim_sceNpScorePollAsync(GuestContext *ctx);
void shim_sceNpScoreWaitAsync(GuestContext *ctx);

void shim_sceNpTusCreateNpTitleCtx(GuestContext *ctx);
void shim_sceNpTusCreateNpTitleCtxA(GuestContext *ctx);
void shim_sceNpTusDeleteNpTitleCtx(GuestContext *ctx);
void shim_sceNpTusCreateRequest(GuestContext *ctx);
void shim_sceNpTusDeleteRequest(GuestContext *ctx);
void shim_sceNpTusPollAsync(GuestContext *ctx);
void shim_sceNpTusWaitAsync(GuestContext *ctx);

void shim_sceNpCommerceDialogInitialize(GuestContext *ctx);
void shim_sceNpCommerceDialogTerminate(GuestContext *ctx);
void shim_sceNpCommerceDialogClose(GuestContext *ctx);
void shim_sceNpCommerceDialogGetStatus(GuestContext *ctx);
void shim_sceNpCommerceDialogGetResult(GuestContext *ctx);
void shim_sceNpCommerceDialogOpen(GuestContext *ctx);

void shim_sceNpUtilityInit(GuestContext *ctx);
void shim_sceNpUtilityTerm(GuestContext *ctx);
void shim_sceNpGameIntentInitialize(GuestContext *ctx);
void shim_sceNpGameIntentTerminate(GuestContext *ctx);

// libSceVoiceQoS
void shim_sceVoiceQoSInit(GuestContext *ctx);
void shim_sceVoiceQoSInitHQ(GuestContext *ctx);
void shim_sceVoiceQoSEnd(GuestContext *ctx);
void shim_sceVoiceQoSConnect(GuestContext *ctx);
void shim_sceVoiceQoSDisconnect(GuestContext *ctx);
void shim_sceVoiceQoSGetStatus(GuestContext *ctx);

// Dynamic module loading shims
void shim_sceKernelLoadStartModule(GuestContext *ctx);
void shim_sceKernelDlsym(GuestContext *ctx);

// Subsystem teardown and lifecycle
void recomp_free_thread_context(GuestContext *ctx);
void ps4_direct_mem_destroy(void);
void ps4_event_flag_destroy(void);
void ps4_sync_destroy(void);
void ps4_videoout_destroy(void);
void ps4_equeue_destroy(void);
void ps4_metal_screen_destroy(void);
void ps4_keyboard_destroy(void);
void ps4_vfs_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // RECOMP_RUNTIME_H
