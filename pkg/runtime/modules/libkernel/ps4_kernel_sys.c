#include "recomp_runtime.h"
#include "ps4_kernel_sys.h"
#include "ps4_sysmodule.h"
#include "ps4_fiber.h"
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
    return RECOMP_TSC_FREQ; // 2.13 GHz (PS4 Pro / Neo clock frequency)
}

uint64_t sceKernelReadTsc(void) {
    // Same counter as the lifted RDTSC instruction.
    return recomp_rdtsc();
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

// PS4 Pro (Neo) mode: 4K checkerboard targets and Neo-only shaders. Comes from the startup
// settings (on by default; PS4_PRO=0|1 overrides; the older PS4_NEO_MODE=0|1 still works).
static int ps4_neo_mode(void) {
    static int mode = -1;
    if (mode < 0) {
        const char *e = getenv("PS4_NEO_MODE");
        mode = (e && *e) ? (e[0] == '1') : (ps4_host_setting_pro_mode() != 0);
    }
    return mode;
}

int32_t sceKernelIsNeoMode(void) {
    return ps4_neo_mode();
}

int32_t sceKernelHasNeoMode(void) {
    return ps4_neo_mode();
}

int32_t sceKernelIsAuthenticNeo(void) {
    return ps4_neo_mode();
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

// The guest general-purpose registers and rip, laid out as at the start of GuestContext.
typedef struct GuestGprs {
    uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, rip;
} GuestGprs;
#define GUEST_GPRS_FIELD(f) \
    _Static_assert(offsetof(GuestGprs, f) == offsetof(GuestContext, f), "GuestGprs." #f " matches GuestContext")
GUEST_GPRS_FIELD(rax);
GUEST_GPRS_FIELD(rcx);
GUEST_GPRS_FIELD(rdx);
GUEST_GPRS_FIELD(rbx);
GUEST_GPRS_FIELD(rsp);
GUEST_GPRS_FIELD(rbp);
GUEST_GPRS_FIELD(rsi);
GUEST_GPRS_FIELD(rdi);
GUEST_GPRS_FIELD(r8);
GUEST_GPRS_FIELD(r9);
GUEST_GPRS_FIELD(r10);
GUEST_GPRS_FIELD(r11);
GUEST_GPRS_FIELD(r12);
GUEST_GPRS_FIELD(r13);
GUEST_GPRS_FIELD(r14);
GUEST_GPRS_FIELD(r15);
GUEST_GPRS_FIELD(rip);
#undef GUEST_GPRS_FIELD
_Static_assert(offsetof(GuestContext, fs_base) == sizeof(GuestGprs), "GuestGprs is the start of GuestContext");

// What running guest code changes in its GuestContext: the GPRs and rip, the flags, the vector,
// x87, MXCSR and MMX state (cf up to mmx). fs_base and gs_base in between and everything after
// mmx (memory, heap, TLS keys and destructors, unwind frames, the VM mutex) are thread state,
// which a guest exception handler may change for good.
#define GUEST_CPU_REST_BEGIN offsetof(GuestContext, cf)
#define GUEST_CPU_REST_END offsetof(GuestContext, mem_base)
_Static_assert(offsetof(GuestContext, cf) == offsetof(GuestContext, gs_base) + 8 &&
                   offsetof(GuestContext, mem_base) == offsetof(GuestContext, mmx) + sizeof(((GuestContext *)0)->mmx),
               "the guest CPU state is rax..rip and cf..mmx of GuestContext");
typedef struct GuestCpuState {
    GuestGprs gprs;
    uint8_t rest[GUEST_CPU_REST_END - GUEST_CPU_REST_BEGIN];
} GuestCpuState;

static void guest_cpu_state_save(GuestCpuState *s, const GuestContext *ctx) {
    memcpy(&s->gprs, ctx, sizeof s->gprs);
    memcpy(s->rest, (const uint8_t *)ctx + GUEST_CPU_REST_BEGIN, sizeof s->rest);
}

static void guest_cpu_state_restore(GuestContext *ctx, const GuestCpuState *s) {
    memcpy(ctx, &s->gprs, sizeof s->gprs);
    memcpy((uint8_t *)ctx + GUEST_CPU_REST_BEGIN, s->rest, sizeof s->rest);
}

static uint64_t g_exception_handlers[64] = {0};
static pthread_mutex_t g_exception_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_sigusr1_installed = false;

// The handler table is indexed by the Orbis signal number; the host SIGUSR1 handler looks its
// entry up by the host number, which is the same (30 on FreeBSD and macOS).
_Static_assert(SIGUSR1 == 30, "host SIGUSR1 is Orbis signal 30");

static void host_sigusr1_handler(int sig, siginfo_t *si, void *ucontext_raw) {
    (void)si;
    if (sig != SIGUSR1) return;

    uint64_t handler = __atomic_load_n(&g_exception_handlers[sig], __ATOMIC_ACQUIRE);
    if (!handler) return;

    GuestContext *ctx = g_current_ctx;
    if (!ctx || !ctx->mem_base || !ctx->fs_base) return;

    // Guest code finds its GuestContext through the TCB slot (contract at recomp_enter_guest).
    // When the slot names another endpoint of this guest thread, that endpoint is what the
    // guest thread executes: in fiber thread mode it runs on its own host thread, so pass the
    // exception on; in coroutine mode this host thread is between endpoints (a hand-over in
    // progress) and the handler runs on ctx with the slot pointed at it.
    uint64_t *slot = (uint64_t *)(ctx->mem_base + ctx->fs_base + RECOMP_TCB_CTX_SLOT);
    const uint64_t saved_slot = *slot;
    if (saved_slot != (uint64_t)(uintptr_t)ctx && ps4_fiber_threads_mode()) {
        pthread_t target;
        if (ps4_fiber_endpoint_host_thread((GuestContext *)(uintptr_t)saved_slot, &target) &&
            !pthread_equal(target, pthread_self()) && pthread_kill(target, SIGUSR1) == 0)
            return;
        // No other host thread runs the endpoint (it is changing hands): handle it here.
    }

    // The guest CPU state in ctx when the signal arrived: the handler runs guest code on ctx, and
    // this comes back afterwards (only the CPU state: what the handler did to the thread's TLS,
    // heap or unwind state stays). `interrupted` holds the registers the handler gets in its
    // ucontext: in guest code the live registers, else the ones in ctx.
    GuestCpuState orig_cpu;
    guest_cpu_state_save(&orig_cpu, ctx);
    GuestGprs interrupted = orig_cpu.gprs;

#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
    // Interrupted in recompiled guest code: the guest registers are the pinned host registers.
    // Anywhere else (a shim, the bridge, a helper) the host registers hold host values and ctx
    // holds the guest registers the bridge stored, which the ucontext must show.
    if (ucontext_raw) {
        arm_thread_state64_t *state = (arm_thread_state64_t *)&(((ucontext_t *)ucontext_raw)->uc_mcontext->__ss);
        uint64_t host_x26 = state->__x[26];
        if (recomp_pc_in_guest_code((uint64_t)state->__pc) &&
            state->__x[28] == (uint64_t)ctx->mem_base &&
            host_x26 >= 0x10000ULL && host_x26 < ctx->mem_size) {
            interrupted.rax = state->__x[19];
            interrupted.rbx = state->__x[20];
            interrupted.rcx = state->__x[21];
            interrupted.rdx = state->__x[22];
            interrupted.rsi = state->__x[23];
            interrupted.rdi = state->__x[24];
            interrupted.rbp = state->__x[25];
            interrupted.rsp = host_x26;
            interrupted.r8  = state->__x[8];
            interrupted.r9  = state->__x[9];
            interrupted.r10 = state->__x[10];
            interrupted.r11 = state->__x[11];
            interrupted.r12 = state->__x[12];
            interrupted.r13 = state->__x[13];
            interrupted.r14 = state->__x[14];
            interrupted.r15 = state->__x[15];
        }
    }
#endif

    if (interrupted.rsp < 0x10000ULL || interrupted.rsp > ctx->mem_size) {
        return;
    }

    // Allocate Orbis ucontext structure on the guest stack, below the 128-byte red zone
    uint64_t ucontext_guest_addr = (interrupted.rsp - 128ULL - 0x200ULL) & ~0xfULL;
    if (ucontext_guest_addr + 0x200ULL <= ctx->mem_size) {
        memset(ctx->mem_base + ucontext_guest_addr, 0, 0x200);
        // In FreeBSD/Orbis x86_64 ucontext_t:
        // mc_rdi=0x48, mc_rsi=0x50, mc_rdx=0x58, mc_rcx=0x60, mc_r8=0x68, mc_r9=0x70
        // mc_rax=0x78, mc_rbx=0x80, mc_rbp=0x88, mc_r10=0x90, mc_r11=0x98, mc_r12=0xa0
        // mc_r13=0xa8, mc_r14=0xb0, mc_r15=0xb8, mc_rip=0xe0, mc_rsp=0xf8
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x48ULL) = interrupted.rdi;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x50ULL) = interrupted.rsi;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x58ULL) = interrupted.rdx;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x60ULL) = interrupted.rcx;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x68ULL) = interrupted.r8;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x70ULL) = interrupted.r9;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x78ULL) = interrupted.rax;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x80ULL) = interrupted.rbx;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x88ULL) = interrupted.rbp;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x90ULL) = interrupted.r10;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0x98ULL) = interrupted.r11;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xa0ULL) = interrupted.r12;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xa8ULL) = interrupted.r13;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xb0ULL) = interrupted.r14;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xb8ULL) = interrupted.r15;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xe0ULL) = interrupted.rip;
        *(uint64_t *)(ctx->mem_base + ucontext_guest_addr + 0xf8ULL) = interrupted.rsp;
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
    *slot = (uint64_t)(uintptr_t)ctx;
    recomp_dispatch(ctx, handler);
    *slot = saved_slot;

    // The CPU state in ctx as before the signal. Not `interrupted`: in guest code its GPRs are the
    // live registers, which may not hold what the code wrote to ctx just before (a helper's
    // rcx result the lifted code has not loaded yet); the live registers come back by
    // sigreturn anyway.
    guest_cpu_state_restore(ctx, &orig_cpu);
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
    // The host thread that executes the guest thread now (a fiber's own host thread in fiber
    // thread mode).
    pthread_t target = t->host_thread;
    pthread_t fiber_host;
    if (t->ctx && ps4_fiber_endpoint_host_thread(t->ctx, &fiber_host)) target = fiber_host;
    if (pthread_equal(target, pthread_self())) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    int rc = pthread_kill(target, SIGUSR1);
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

typedef struct OrbisKernelModuleInfo {
    uint64_t st_size;
    char name[256];
    OrbisKernelModuleSegmentInfo segments[4];
    uint32_t num_segments;
    uint8_t fingerprint[20];
} OrbisKernelModuleInfo;

typedef struct OrbisKernelModuleInfoEx {
    uint64_t st_size;
    char name[256];
    int32_t id;
    uint32_t tls_index;
    uint64_t tls_init_addr;
    uint32_t tls_init_size;
    uint32_t tls_size;
    uint32_t tls_offset;
    uint32_t tls_align;
    uint64_t init_proc_addr;
    uint64_t fini_proc_addr;
    uint64_t reserved1;
    uint64_t reserved2;
    uint64_t eh_frame_hdr_addr;
    uint64_t eh_frame_addr;
    uint32_t eh_frame_hdr_size;
    uint32_t eh_frame_size;
    OrbisKernelModuleSegmentInfo segments[4];
    uint32_t segment_count;
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

// Copies the segments of m (recomp_module_snapshot_segments: the ones the emitter registered)
// into the console's segment-info layout; returns their number.
static uint32_t snapshot_segments_out(const RecompModuleSnapshot *m, OrbisKernelModuleSegmentInfo segs[4]) {
    RecompModuleSegment s[4];
    const uint32_t n = recomp_module_snapshot_segments(m, s);
    for (uint32_t i = 0; i < n; i++) {
        segs[i].address = s[i].addr;
        segs[i].size = (uint32_t)s[i].size;
        segs[i].prot = s[i].prot;
    }
    return n;
}

static int fill_module_info_ex(GuestContext *ctx, uint64_t addr, uint64_t info_addr) {
    if (!info_addr || !ctx->mem_base) return ORBIS_KERNEL_ERROR_EFAULT;
    RecompModuleSnapshot snap;
    if (recomp_module_info_by_addr(addr, &snap) != 0) return ORBIS_KERNEL_ERROR_ESRCH;
    OrbisKernelModuleInfoEx *info = (OrbisKernelModuleInfoEx *)(ctx->mem_base + info_addr);
    memset(info, 0, sizeof(*info));
    info->st_size = sizeof(OrbisKernelModuleInfoEx);
    snprintf(info->name, sizeof(info->name), "%s", snap.name);
    info->id = snap.handle;
    info->eh_frame_hdr_addr = snap.eh_frame_hdr_addr;
    info->eh_frame_hdr_size = (uint32_t)snap.eh_frame_hdr_size;
    info->eh_frame_addr = snap.eh_frame_addr;
    info->eh_frame_size = (uint32_t)snap.eh_frame_size;
    info->segment_count = snapshot_segments_out(&snap, info->segments);
    // Logged on the first call only (unwinding can call this for every frame).
    static int s_logged;
    if (!__atomic_exchange_n(&s_logged, 1, __ATOMIC_RELAXED)) {
        fprintf(stderr, "[ps4-kernel] GetModuleInfoFromAddr: addr=0x%llx -> mod=%s (handle %d), num_segments=%u (first call)\n",
                (unsigned long long)addr, info->name, info->id, info->segment_count);
        for (uint32_t i = 0; i < info->segment_count; i++) {
            fprintf(stderr, "    seg[%u]: addr=0x%llx, size=0x%x, prot=%d\n",
                    i, (unsigned long long)info->segments[i].address, info->segments[i].size, info->segments[i].prot);
        }
    }
    return 0;
}

// The first segment of the module containing rsi, to rdx; ESRCH if no module contains it.
void shim_sceKernelInternalMemoryGetModuleSegmentInfo(GuestContext *ctx) {
    uint64_t seg_out = ctx->rdx;
    if (!seg_out || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }
    RecompModuleSnapshot snap;
    OrbisKernelModuleSegmentInfo segs[4];
    if (recomp_module_info_by_addr(ctx->rsi, &snap) != 0 || snapshot_segments_out(&snap, segs) == 0) {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_KERNEL_ERROR_ESRCH;
        SHIM_RETURN();
    }
    *(OrbisKernelModuleSegmentInfo *)(ctx->mem_base + seg_out) = segs[0];
    ctx->rax = 0;
    SHIM_RETURN();
}

// shim_sceKernelGetModuleInfo lives in ps4_kernel_modules.c (it shares the module handles of
// sceKernelGetModuleList); GetModuleInfo2 is the same call.
void shim_sceKernelGetModuleInfo2(GuestContext *ctx) {
    shim_sceKernelGetModuleInfo(ctx);
}

void shim_sceKernelGetModuleInfoInternal(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t info_addr = ctx->rsi;
    if (!info_addr || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)0x8002000e; // ORBIS_KERNEL_ERROR_EFAULT
        SHIM_RETURN();
    }
    RecompModuleSnapshot snap;
    if (recomp_module_info_by_handle(handle, &snap) != 0) {
        ctx->rax = (uint64_t)(int64_t)0x80020003; // ORBIS_KERNEL_ERROR_ESRCH
        SHIM_RETURN();
    }
    OrbisKernelModuleInfoEx *info = (OrbisKernelModuleInfoEx *)(ctx->mem_base + info_addr);
    memset(info, 0, sizeof(*info));
    info->st_size = sizeof(OrbisKernelModuleInfoEx);
    snprintf(info->name, sizeof(info->name), "%s", snap.name);
    info->id = handle;
    info->eh_frame_hdr_addr = snap.eh_frame_hdr_addr;
    info->eh_frame_hdr_size = (uint32_t)snap.eh_frame_hdr_size;
    info->eh_frame_addr = snap.eh_frame_addr;
    info->eh_frame_size = (uint32_t)snap.eh_frame_size;
    info->segment_count = snapshot_segments_out(&snap, info->segments);
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


// int sceKernelSetFsstParam(uint32_t a, uint32_t b)
//
// Class N (documented no-op returning 0). The function has no public documentation and no
// shadPS4 implementation (only an aerolib name stub); its name and the values Uncharted 4
// passes at boot (0x100, 0x40, call at 0x265f2f) point to a file-system tuning knob of the
// console's own file system. The host serves guest files from the host file system through
// the VFS layer, which has nothing to tune. The only use of the result is a test against zero
// right after the call (TEST EAX, EAX / JNE at 0x265f34 to the error path), so returning 0
// is the entire observable behaviour the caller depends on.
void shim_sceKernelSetFsstParam(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}
