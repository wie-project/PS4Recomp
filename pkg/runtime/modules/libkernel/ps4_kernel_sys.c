#include "ps4_kernel_sys.h"
#include "ps4_sysmodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <uuid/uuid.h>

void shim_clock_gettime(GuestContext *ctx);

#if defined(__APPLE__)
#include <mach/mach_time.h>
static mach_timebase_info_data_t g_timebase = {0};
static uint64_t g_start_time = 0;

static void init_time(void) {
    if (g_timebase.denom == 0) {
        mach_timebase_info(&g_timebase);
        g_start_time = mach_absolute_time();
    }
}

uint64_t sceKernelGetProcessTimeCounter(void) {
    init_time();
    uint64_t elapsed = mach_absolute_time() - g_start_time;
    return (elapsed * g_timebase.numer) / g_timebase.denom;
}
#else
#include <time.h>
static uint64_t g_start_time_ns = 0;
static void init_time(void) {
    if (g_start_time_ns == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        g_start_time_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    }
}

uint64_t sceKernelGetProcessTimeCounter(void) {
    init_time();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    return now_ns - g_start_time_ns;
}
#endif

uint64_t sceKernelGetProcessTimeCounterFrequency(void) {
    return 1000000000ULL; // 1 GHz (1 ns per tick)
}

uint64_t sceKernelGetProcessTime(void) {
    return sceKernelGetProcessTimeCounter() / 1000ULL; // Microseconds
}

uint64_t sceKernelGetTscFrequency(void) {
    return 2130000000ULL; // 2.13 GHz (PS4 Pro / Neo clock frequency)
}

uint64_t sceKernelReadTsc(void) {
    // 2.13 GHz ticks = (ns * 213) / 100
    return (sceKernelGetProcessTimeCounter() * 213ULL) / 100ULL;
}

int32_t sceKernelGettimeofday(OrbisKernelTimeval *tv) {
    if (!tv) return -EINVAL;
    struct timeval host_tv;
    int ret = gettimeofday(&host_tv, NULL);
    if (ret == 0) {
        tv->tv_sec = (int64_t)host_tv.tv_sec;
        tv->tv_usec = (int64_t)host_tv.tv_usec;
    }
    return ret;
}

int32_t sceKernelGetCurrentCpu(void) {
    if (g_current_ctx) {
        return (int32_t)(g_current_ctx->thread_id % 8);
    }
    return 0;
}

int32_t sceKernelGetCpumode(void) {
    return 1; // 7-core high-performance gaming mode
}

int32_t sceKernelIsNeoMode(void) {
    return 1; // PlayStation 4 Pro (Neo) mode enabled
}

int32_t sceKernelHasNeoMode(void) {
    return 1;
}

int32_t sceKernelIsAuthenticNeo(void) {
    return 1;
}

int32_t sceKernelGetSystemSwVersion(OrbisKernelSwVersion *version) {
    if (!version) return -EINVAL;
    version->Size = sizeof(OrbisKernelSwVersion);
    strncpy(version->VersionString, " 9.000.001", sizeof(version->VersionString) - 1);
    version->Version = 0x09000001;
    return 0;
}

// Guest execution shims
void shim_sceKernelGetProcessTimeCounter(GuestContext *ctx) {
    ctx->rax = sceKernelGetProcessTimeCounter();
    SHIM_RETURN();
}

void shim_sceKernelGetProcessTimeCounterFrequency(GuestContext *ctx) {
    ctx->rax = sceKernelGetProcessTimeCounterFrequency();
    SHIM_RETURN();
}

void shim_sceKernelGetProcessTime(GuestContext *ctx) {
    ctx->rax = sceKernelGetProcessTime();
    SHIM_RETURN();
}

void shim_sceKernelGetTscFrequency(GuestContext *ctx) {
    ctx->rax = sceKernelGetTscFrequency();
    SHIM_RETURN();
}

void shim_sceKernelReadTsc(GuestContext *ctx) {
    ctx->rax = sceKernelReadTsc();
    SHIM_RETURN();
}

void shim_sceKernelGettimeofday(GuestContext *ctx) {
    uint64_t tv_guest = ctx->rdi;
    if (!tv_guest) {
        ctx->rax = (uint64_t)(int64_t)-EINVAL;
        SHIM_RETURN();
    }
    OrbisKernelTimeval tv = {0};
    int32_t rc = sceKernelGettimeofday(&tv);
    if (rc == 0) {
        memcpy(ctx->mem_base + tv_guest, &tv, sizeof(tv));
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelGetCurrentCpu(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKernelGetCurrentCpu();
    SHIM_RETURN();
}

void shim_sceKernelGetCpumode(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKernelGetCpumode();
    SHIM_RETURN();
}

void shim_sceKernelIsNeoMode(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKernelIsNeoMode();
    SHIM_RETURN();
}

void shim_sceKernelHasNeoMode(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKernelHasNeoMode();
    SHIM_RETURN();
}

void shim_sceKernelIsAuthenticNeo(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKernelIsAuthenticNeo();
    SHIM_RETURN();
}

void shim_sceKernelGetSystemSwVersion(GuestContext *ctx) {
    uint64_t ver_guest = ctx->rdi;
    if (!ver_guest) {
        ctx->rax = (uint64_t)(int64_t)-EINVAL;
        SHIM_RETURN();
    }
    OrbisKernelSwVersion ver = {0};
    int32_t rc = sceKernelGetSystemSwVersion(&ver);
    if (rc == 0) {
        memcpy(ctx->mem_base + ver_guest, &ver, sizeof(ver));
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

int32_t sceKernelUuidCreate(void *uuid) {
    if (!uuid) return 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
    uuid_t u;
    uuid_generate(u);
    memcpy(uuid, u, sizeof(u));
    return 0;
}

int32_t sceKernelSetGPO(uint32_t gpo) {
    (void)gpo;
    // GPO is devkit GPIO pins; retail PS4 returns ORBIS_OK (0).
    return 0;
}

int32_t sceKernelIsProspero(void) {
    // Returns 0 (false): we are running in PS4 (Orbis) mode, not PS5 (Prospero).
    return 0;
}

void shim_sceKernelClockGettime(GuestContext *ctx) {
    shim_clock_gettime(ctx);
}

void shim_sceKernelUuidCreate(GuestContext *ctx) {
    uint64_t uuidGuest = ctx->rdi;
    if (!uuidGuest || !ctx->mem_base) {
        ctx->rax = 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
        SHIM_RETURN();
    }
    uint8_t u[16];
    int32_t rc = sceKernelUuidCreate(u);
    if (rc == 0) {
        memcpy(ctx->mem_base + uuidGuest, u, sizeof(u));
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelSetGPO(GuestContext *ctx) {
    uint32_t gpo = (uint32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceKernelSetGPO(gpo);
    SHIM_RETURN();
}

void shim_sceKernelIsProspero(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelIsProspero();
    SHIM_RETURN();
}

struct OrbisTimesec {
    int64_t t;
    int32_t west_sec;
    int32_t dst_sec;
};

void shim_sceKernelConvertUtcToLocaltime(GuestContext *ctx) {
    time_t utc_sec = (time_t)ctx->rdi;
    uint64_t local_out = ctx->rsi;
    uint64_t st_out = ctx->rdx;
    uint64_t dst_out = ctx->rcx;

    struct tm tm_loc;
    localtime_r(&utc_sec, &tm_loc);
    time_t local_sec = utc_sec + tm_loc.tm_gmtoff;

    if (local_out && ctx->mem_base) {
        *(int64_t *)(ctx->mem_base + local_out) = (int64_t)local_sec;
    }
    if (st_out && ctx->mem_base) {
        struct OrbisTimesec *st = (struct OrbisTimesec *)(ctx->mem_base + st_out);
        st->t = (int64_t)utc_sec;
        st->west_sec = -(int32_t)tm_loc.tm_gmtoff;
        st->dst_sec = tm_loc.tm_isdst > 0 ? 3600 : 0;
    }
    if (dst_out && ctx->mem_base) {
        *(uint64_t *)(ctx->mem_base + dst_out) = tm_loc.tm_isdst > 0 ? 3600ULL : 0ULL;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelConvertLocaltimeToUtc(GuestContext *ctx) {
    time_t loc_sec = (time_t)ctx->rdi;
    uint64_t utc_out = ctx->rsi;
    uint64_t st_out = ctx->rdx;
    uint64_t dst_out = ctx->rcx;

    struct tm tm_loc;
    localtime_r(&loc_sec, &tm_loc);
    time_t utc_sec = loc_sec - tm_loc.tm_gmtoff;

    if (utc_out && ctx->mem_base) {
        *(int64_t *)(ctx->mem_base + utc_out) = (int64_t)utc_sec;
    }
    if (st_out && ctx->mem_base) {
        struct OrbisTimesec *st = (struct OrbisTimesec *)(ctx->mem_base + st_out);
        st->t = (int64_t)utc_sec;
        st->west_sec = -(int32_t)tm_loc.tm_gmtoff;
        st->dst_sec = tm_loc.tm_isdst > 0 ? 3600 : 0;
    }
    if (dst_out && ctx->mem_base) {
        *(uint64_t *)(ctx->mem_base + dst_out) = tm_loc.tm_isdst > 0 ? 3600ULL : 0ULL;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim___pthread_cxa_finalize(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__sceKernelRtldThreadAtexitDecrement(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__sceKernelRtldThreadAtexitIncrement(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__sceKernelSetThreadAtexitReport(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__sceKernelSetThreadAtexitCount(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

static uint64_t g_thread_dtors = 0;
void shim__sceKernelSetThreadDtors(GuestContext *ctx) {
    g_thread_dtors = ctx->rdi;
    ctx->rax = 0;
    SHIM_RETURN();
}

static uint64_t g_app_heap_api = 0;
void shim__sceKernelRtldSetApplicationHeapAPI(GuestContext *ctx) {
    g_app_heap_api = ctx->rdi;
    fprintf(stderr, "[ps4-kernel] _sceKernelRtldSetApplicationHeapAPI: table at 0x%llx\n",
            (unsigned long long)ctx->rdi);
    if (ctx->rdi && ctx->mem_base) {
        uint64_t *table = (uint64_t *)(ctx->mem_base + ctx->rdi);
        fprintf(stderr, "[ps4-kernel] heap_malloc=0x%llx, heap_free=0x%llx\n",
                (unsigned long long)table[0], (unsigned long long)table[1]);
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim___elf_phdr_match_addr(GuestContext *ctx) {
    uint64_t addr = ctx->rsi;
    // Return 1 if inside guest memory space, else 0
    ctx->rax = (addr != 0) ? 1 : 0;
    SHIM_RETURN();
}

void shim_signal(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__is_signal_return(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceDiscMapIsRequestOnHDD(GuestContext *ctx) {
    ctx->rax = 1;
    SHIM_RETURN();
}

void shim_Func_7C980FFB0AA27E7A(GuestContext *ctx) {
    uint64_t flags_guest = ctx->rcx;
    uint64_t ret1_guest = ctx->r8;
    uint64_t ret2_guest = ctx->r9;
    if (flags_guest && ctx->mem_base) {
        *(int32_t *)(ctx->mem_base + flags_guest) = 0;
    }
    if (ret1_guest && ctx->mem_base) {
        *(int32_t *)(ctx->mem_base + ret1_guest) = 0;
    }
    if (ret2_guest && ctx->mem_base) {
        *(int32_t *)(ctx->mem_base + ret2_guest) = 0;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceDiscMapGetPackageSize(GuestContext *ctx) {
    uint64_t ret1_guest = ctx->rsi;
    uint64_t ret2_guest = ctx->rdx;
    if (ret1_guest && ctx->mem_base) {
        *(int32_t *)(ctx->mem_base + ret1_guest) = 0;
    }
    if (ret2_guest && ctx->mem_base) {
        *(int32_t *)(ctx->mem_base + ret2_guest) = 0;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_Func_8A828CAEE7EDD5E9(GuestContext *ctx) {
    ctx->rax = 0x80820001; // ORBIS_DISC_MAP_ERROR_NO_BITMAP_INFO
    SHIM_RETURN();
}

void shim_Func_E7EBCE96E92F91F8(GuestContext *ctx) {
    ctx->rax = 0x80820001; // ORBIS_DISC_MAP_ERROR_NO_BITMAP_INFO
    SHIM_RETURN();
}

void shim_sceKernelGetSanitizerNewReplaceExternal(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelGetSanitizerMallocReplaceExternal(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelIsAddressSanitizerEnabled(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

#include <signal.h>
#include <pthread.h>
#include "ps4_threading.h"

#define ORBIS_KERNEL_ERROR_ESRCH  0x80020003
#define ORBIS_KERNEL_ERROR_EINVAL 0x80020016
#define ORBIS_KERNEL_ERROR_EAGAIN 0x80020023
#define ORBIS_KERNEL_ERROR_EFAULT 0x8002000e

static uint64_t g_exception_handlers[64] = {0};
static pthread_mutex_t g_exception_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_sigusr1_installed = false;

static void host_sigusr1_handler(int sig, siginfo_t *si, void *ucontext_raw) {
    (void)si;
    (void)ucontext_raw;
    if (sig != 30) return;

    uint64_t handler = __atomic_load_n(&g_exception_handlers[sig], __ATOMIC_ACQUIRE);
    if (!handler) return;

    GuestContext *ctx = g_current_ctx;
    if (!ctx || !ctx->mem_base) return;

    // Save full guest state to restore after exception handler returns
    GuestContext saved_ctx = *ctx;

    // Allocate Orbis ucontext structure on the guest stack
    uint64_t ucontext_guest_addr = (ctx->rsp - 0x200ULL) & ~0xfULL;
    if (ucontext_guest_addr + 0x200ULL <= ctx->mem_size) {
        memset(ctx->mem_base + ucontext_guest_addr, 0, 0x200);
        // In FreeBSD/Orbis x86_64 ucontext_t:
        // mc_rdi=0x48, mc_rsi=0x50, mc_rdx=0x58, mc_rcx=0x60, mc_r8=0x68, mc_r9=0x70
        // mc_rax=0x78, mc_rbx=0x80, mc_rbp=0x88, mc_r10=0x90, mc_r11=0x98, mc_r12=0xa0
        // mc_r13=0xa8, mc_r14=0xb0, mc_r15=0xb8, mc_rip=0xe0, mc_rsp=0xf8
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x48ULL) = saved_ctx.rdi;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x50ULL) = saved_ctx.rsi;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x58ULL) = saved_ctx.rdx;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x60ULL) = saved_ctx.rcx;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x68ULL) = saved_ctx.r8;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x70ULL) = saved_ctx.r9;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x78ULL) = saved_ctx.rax;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x80ULL) = saved_ctx.rbx;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x88ULL) = saved_ctx.rbp;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x90ULL) = saved_ctx.r10;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x98ULL) = saved_ctx.r11;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xa0ULL) = saved_ctx.r12;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xa8ULL) = saved_ctx.r13;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xb0ULL) = saved_ctx.r14;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xb8ULL) = saved_ctx.r15;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xe0ULL) = saved_ctx.rip;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xf8ULL) = saved_ctx.rsp;
    }

    // Set arguments for Orbis exception handler:
    // void handler(int signum, void *context)
    ctx->rdi = (uint64_t)sig;
    ctx->rsi = ucontext_guest_addr;

    // Push dummy return address to simulate call frame
    ctx->rsp = ucontext_guest_addr - 8ULL;
    *(uint64_t *)(ctx->mem_base + ctx->rsp) = 0xdeadbeefULL;
    ctx->rip = handler;

    // Execute the guest exception handler (which signals SuspendSemaphore and waits on event flag)
    recomp_dispatch(ctx, handler);

    // Restore guest registers exactly as before signal interruption
    *ctx = saved_ctx;
}

int32_t sceKernelInstallExceptionHandler(int32_t signum, uint64_t handler) {
    if (signum != 1 && signum != 4 && signum != 8 && signum != 10 && signum != 11 && signum != 30) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    if (signum <= 0 || signum >= 64) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    pthread_mutex_lock(&g_exception_mutex);
    __atomic_store_n(&g_exception_handlers[signum], handler, __ATOMIC_RELEASE);

    if (signum == 30 && !g_sigusr1_installed) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = host_sigusr1_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR1, &sa, NULL);
        g_sigusr1_installed = true;
    }
    pthread_mutex_unlock(&g_exception_mutex);

    return 0;
}

int32_t sceKernelRemoveExceptionHandler(int32_t signum) {
    if (signum <= 0 || signum >= 64) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    pthread_mutex_lock(&g_exception_mutex);
    __atomic_store_n(&g_exception_handlers[signum], 0, __ATOMIC_RELEASE);
    if (signum == 30 && g_sigusr1_installed) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigaction(SIGUSR1, &sa, NULL);
        g_sigusr1_installed = false;
    }
    pthread_mutex_unlock(&g_exception_mutex);
    return 0;
}

int32_t sceKernelRaiseException(uint64_t thread, int32_t signum) {
    if (signum != 30) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    RecompThread *t = recomp_find_thread(thread);
    if (!t) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    if (t->finished || !t->host_thread) {
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    if (pthread_equal(t->host_thread, pthread_self())) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    int rc = pthread_kill(t->host_thread, SIGUSR1);
    if (rc != 0) {
        if (rc == ESRCH) return ORBIS_KERNEL_ERROR_ESRCH;
        if (rc == EINVAL) return ORBIS_KERNEL_ERROR_EINVAL;
        return ORBIS_KERNEL_ERROR_ESRCH;
    }
    return 0;
}

void shim_sceKernelInstallExceptionHandler(GuestContext *ctx) {
    int32_t signum = (int32_t)ctx->rdi;
    uint64_t handler = ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceKernelInstallExceptionHandler(signum, handler);
    SHIM_RETURN();
}

void shim_sceKernelRemoveExceptionHandler(GuestContext *ctx) {
    int32_t signum = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceKernelRemoveExceptionHandler(signum);
    SHIM_RETURN();
}

void shim_sceKernelRaiseException(GuestContext *ctx) {
    uint64_t thread = ctx->rdi;
    int32_t signum = (int32_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceKernelRaiseException(thread, signum);
    SHIM_RETURN();
}

void shim_sceKernelDebugRaiseException(GuestContext *ctx) {
    uint64_t error = ctx->rdi;
    uint64_t unk = ctx->rsi;
    fprintf(stderr, "\n[ps4-kernel] Fatal debug exception raised: error=0x%llx (unk=0x%llx, RIP=0x%llx)\n",
            (unsigned long long)error, (unsigned long long)unk, (unsigned long long)ctx->rip);
    recomp_dump_guest_context(ctx);
    fflush(stderr);
    abort();
}

void shim_sceKernelDebugRaiseExceptionOnReleaseMode(GuestContext *ctx) {
    uint64_t error = ctx->rdi;
    uint64_t unk = ctx->rsi;
    fprintf(stderr, "\n[ps4-kernel] Fatal debug exception (release mode) raised: error=0x%llx (unk=0x%llx, RIP=0x%llx)\n",
            (unsigned long long)error, (unsigned long long)unk, (unsigned long long)ctx->rip);
    recomp_dump_guest_context(ctx);
    fflush(stderr);
    abort();
}

void shim_sceKernelPrintBacktraceWithModuleInfo(GuestContext *ctx) {
    fprintf(stderr, "[ps4-kernel] Backtrace with module info requested\n");
    ctx->rax = 0;
    SHIM_RETURN();
}

typedef struct OrbisKernelModuleSegmentInfo {
    uint64_t address;
    uint32_t size;
    int32_t prot;
} OrbisKernelModuleSegmentInfo;

typedef struct OrbisKernelModuleInfoEx {
    size_t size;
    char name[256];
    int32_t handle;
    uint32_t flags;
    uint64_t tls_init_addr;
    uint32_t tls_init_size;
    uint32_t tls_size;
    uint32_t tls_offset;
    uint32_t tls_align;
    uint64_t init_addr;
    uint64_t fini_addr;
    uint64_t eh_frame_hdr_addr;
    uint64_t eh_frame_addr;
    uint32_t eh_frame_hdr_size;
    uint32_t eh_frame_size;
    OrbisKernelModuleSegmentInfo segments[4];
    uint32_t num_segments;
    uint8_t fingerprint[20];
} OrbisKernelModuleInfoEx;

typedef struct OrbisModuleInfoForUnwind {
    uint64_t st_size;
    char name[256];
    uint64_t eh_frame_hdr_addr;
    uint64_t eh_frame_addr;
    uint64_t eh_frame_size;
    uint64_t seg0_addr;
    uint64_t seg0_size;
} OrbisModuleInfoForUnwind;

static int fill_module_info_ex(GuestContext *ctx, uint64_t addr, uint64_t info_addr) {
    if (!info_addr || !ctx->mem_base) return 0x8002000e; // ORBIS_KERNEL_ERROR_EFAULT
    const RecompModuleUnwindInfo *mod = recomp_module_find_by_addr(addr);
    OrbisKernelModuleInfoEx *info = (OrbisKernelModuleInfoEx *)(ctx->mem_base + info_addr);
    memset(info, 0, sizeof(*info));
    info->size = sizeof(OrbisKernelModuleInfoEx);
    if (mod && mod->name) {
        snprintf(info->name, sizeof(info->name), "%s", mod->name);
    } else {
        snprintf(info->name, sizeof(info->name), "eboot.bin");
    }
    info->handle = 1;
    if (mod) {
        info->eh_frame_hdr_addr = mod->eh_frame_hdr_addr;
        info->eh_frame_hdr_size = (uint32_t)mod->eh_frame_hdr_size;
        info->eh_frame_addr = mod->eh_frame_addr;
        info->eh_frame_size = (uint32_t)mod->eh_frame_size;
        info->segments[0].address = mod->seg0_addr ? mod->seg0_addr : mod->start_addr;
        info->segments[0].size = mod->seg0_size ? (uint32_t)mod->seg0_size : (uint32_t)(mod->end_addr - mod->start_addr);
    } else {
        info->segments[0].address = 0x400000ULL;
        info->segments[0].size = 0x10000000ULL;
    }
    info->segments[0].prot = 7;
    info->num_segments = 1;
    return 0;
}

void shim_sceKernelInternalMemoryGetModuleSegmentInfo(GuestContext *ctx) {
    uint64_t seg_out = ctx->rdx;
    if (seg_out && ctx->mem_base) {
        const RecompModuleUnwindInfo *mod = recomp_module_find_by_addr(ctx->rsi);
        OrbisKernelModuleSegmentInfo *seg = (OrbisKernelModuleSegmentInfo *)(ctx->mem_base + seg_out);
        if (mod) {
            seg->address = mod->seg0_addr ? mod->seg0_addr : mod->start_addr;
            seg->size = mod->seg0_size ? (uint32_t)mod->seg0_size : (uint32_t)(mod->end_addr - mod->start_addr);
        } else {
            seg->address = 0x400000ULL;
            seg->size = 0x10000000ULL;
        }
        seg->prot = 7;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelGetModuleInfoForUnwind(GuestContext *ctx) {
    uint64_t addr = ctx->rdi;
    int32_t flags = (int32_t)ctx->rsi;
    uint64_t info_addr = ctx->rdx;

    if (flags >= 3) {
        if (info_addr && ctx->mem_base) {
            memset(ctx->mem_base + info_addr, 0, sizeof(OrbisModuleInfoForUnwind));
        }
        ctx->rax = (uint64_t)(int64_t)0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
        SHIM_RETURN();
    }
    if (!info_addr || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)0x8002000e; // ORBIS_KERNEL_ERROR_EFAULT
        SHIM_RETURN();
    }

    const RecompModuleUnwindInfo *mod = recomp_module_find_by_addr(addr);
    if (!mod) {
        ctx->rax = (uint64_t)(int64_t)0x80020003; // ORBIS_KERNEL_ERROR_ESRCH
        SHIM_RETURN();
    }

    OrbisModuleInfoForUnwind *info = (OrbisModuleInfoForUnwind *)(ctx->mem_base + info_addr);
    memset(info, 0, sizeof(*info));
    info->st_size = sizeof(OrbisModuleInfoForUnwind);
    if (mod->name) {
        snprintf(info->name, sizeof(info->name), "%s", mod->name);
    }
    info->eh_frame_hdr_addr = mod->eh_frame_hdr_addr;
    info->eh_frame_addr = mod->eh_frame_addr;
    info->eh_frame_size = mod->eh_frame_size;
    info->seg0_addr = mod->seg0_addr ? mod->seg0_addr : mod->start_addr;
    info->seg0_size = mod->seg0_size ? mod->seg0_size : (mod->end_addr - mod->start_addr);
    fprintf(stderr, "[ps4-unwind] GetModuleInfoForUnwind: addr=0x%llx -> mod=%s, hdr=0x%llx, frame=0x%llx (size=0x%llx)\n",
            (unsigned long long)addr, mod->name ? mod->name : "(null)",
            (unsigned long long)info->eh_frame_hdr_addr,
            (unsigned long long)info->eh_frame_addr,
            (unsigned long long)info->eh_frame_size);

    ctx->rax = 0; // ORBIS_OK
    SHIM_RETURN();
}

void shim_sceKernelGetModuleInfoFromAddr(GuestContext *ctx) {
    uint64_t addr = ctx->rdi;
    int32_t flags = (int32_t)ctx->rsi;
    uint64_t info_addr = ctx->rdx;

    if (flags >= 3) {
        if (info_addr && ctx->mem_base) {
            memset(ctx->mem_base + info_addr, 0, sizeof(OrbisKernelModuleInfoEx));
        }
        ctx->rax = (uint64_t)(int64_t)0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
        SHIM_RETURN();
    }
    if (!info_addr || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)0x8002000e; // ORBIS_KERNEL_ERROR_EFAULT
        SHIM_RETURN();
    }

    ctx->rax = (uint64_t)(int64_t)fill_module_info_ex(ctx, addr, info_addr);
    SHIM_RETURN();
}

void shim_getargv(GuestContext *ctx) {
    if (ctx && ctx->args_addr && ctx->mem_base) {
        ctx->rax = ctx->args_addr + 8ULL;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim___progname(GuestContext *ctx) {
    if (ctx && ctx->args_addr && ctx->mem_base) {
        uint64_t argv0 = *(uint64_t *)(ctx->mem_base + ctx->args_addr + 8ULL);
        if (argv0) {
            ctx->rax = argv0;
            SHIM_RETURN();
        }
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_getpagesize(GuestContext *ctx) {
    ctx->rax = 4096;
    SHIM_RETURN();
}

static uint64_t g_guest_heap_trace_info_buf = 0;

void shim_sceLibcHeapGetTraceInfo(GuestContext *ctx) {
    uint64_t info_guest = ctx->rdi;
    if (!info_guest || !ctx->mem_base) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    if (!g_guest_heap_trace_info_buf) {
        g_guest_heap_trace_info_buf = recomp_vm_alloc_named(ctx, 4096, 3, 0, "heap_trace_info");
    }
    // HeapInfoInfo structure:
    // +0x00: u64 size
    // +0x08: u32 flag
    // +0x0c: u32 getSegmentInfo = 0
    // +0x10: u64 mspace_atomic_id_mask
    // +0x18: u64 mstate_table
    *(uint32_t *)(ctx->mem_base + info_guest + 0x0c) = 0;
    *(uint64_t *)(ctx->mem_base + info_guest + 0x10) = g_guest_heap_trace_info_buf;
    *(uint64_t *)(ctx->mem_base + info_guest + 0x18) = g_guest_heap_trace_info_buf + 0x10;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_Need_sceLibcInternal(GuestContext *ctx) {
    (void)ctx;
    if (ctx) ctx->rax = 0;
    SHIM_RETURN();
}

