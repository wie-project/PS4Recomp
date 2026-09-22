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
// Uses a 2-level page table covering up to 4GB of guest code space with
// on-demand L2 allocation
#define DISPATCH_L1_SHIFT 16
#define DISPATCH_L1_SIZE (65536) // 2^16 entries
#define DISPATCH_L2_SIZE (65536) // 2^16 entries
#define DISPATCH_L2_MASK (0xFFFFULL)

extern recomp_fn_t *g_dispatch_l1[DISPATCH_L1_SIZE];

void recomp_register_fn(uint64_t guest_addr, recomp_fn_t fn);

static inline void recomp_dispatch(GuestContext *ctx, uint64_t target) {
  ctx->rip = target;
  uint64_t l1_idx = target >> DISPATCH_L1_SHIFT;
  if (l1_idx < DISPATCH_L1_SIZE) {
    recomp_fn_t *l2 = g_dispatch_l1[l1_idx];
    if (l2) {
      recomp_fn_t fn = l2[target & DISPATCH_L2_MASK];
      if (fn) {
        fn(ctx);
        return;
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
int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size);

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
void shim_pthread_setspecific(GuestContext *ctx);
void shim_pthread_getspecific(GuestContext *ctx);
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

// POSIX Semaphores
void shim_sem_init(GuestContext *ctx);
void shim_sem_destroy(GuestContext *ctx);
void shim_sem_wait(GuestContext *ctx);
void shim_sem_trywait(GuestContext *ctx);
void shim_sem_post(GuestContext *ctx);
void shim_sem_getvalue(GuestContext *ctx);

// Direct Memory shims
void shim_sceKernelAllocateDirectMemory(GuestContext *ctx);
void shim_sceKernelGetDirectMemorySize(GuestContext *ctx);
void shim_sceKernelMapDirectMemory(GuestContext *ctx);
void shim_sceKernelReleaseDirectMemory(GuestContext *ctx);

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

// Pad shims
void shim_scePadGetHandle(GuestContext *ctx);

// User Service shims
void shim_sceUserServiceInitialize(GuestContext *ctx);
void shim_sceUserServiceGetInitialUser(GuestContext *ctx);
void shim_sceUserServiceGetLoginUserIdList(GuestContext *ctx);
void shim_sceUserServiceGetUserName(GuestContext *ctx);
void shim_sceUserServiceTerminate(GuestContext *ctx);

// Pad shims
void shim_scePadInit(GuestContext *ctx);
void shim_scePadOpen(GuestContext *ctx);
void shim_scePadClose(GuestContext *ctx);
void shim_scePadReadState(GuestContext *ctx);
void shim_scePadRead(GuestContext *ctx);

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

// Dynamic module loading shims
void shim_sceKernelLoadStartModule(GuestContext *ctx);
void shim_sceKernelDlsym(GuestContext *ctx);

// Subsystem teardown and lifecycle
void recomp_free_thread_context(GuestContext *ctx);
void ps4_direct_mem_destroy(void);
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
