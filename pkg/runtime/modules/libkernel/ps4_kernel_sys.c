#include "ps4_kernel_sys.h"
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

void shim_sceKernelInstallExceptionHandler(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelRaiseException(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelDebugRaiseException(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelDebugRaiseExceptionOnReleaseMode(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
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

static void fill_module_info_ex(GuestContext *ctx, uint64_t info_addr) {
    if (!info_addr || !ctx->mem_base) return;
    OrbisKernelModuleInfoEx *info = (OrbisKernelModuleInfoEx *)(ctx->mem_base + info_addr);
    memset(info, 0, sizeof(*info));
    info->size = sizeof(OrbisKernelModuleInfoEx);
    snprintf(info->name, sizeof(info->name), "eboot.bin");
    info->handle = 1;
    info->segments[0].address = 0x400000ULL;
    info->segments[0].size = 0x10000000ULL;
    info->segments[0].prot = 7;
    info->num_segments = 1;
}

void shim_sceKernelInternalMemoryGetModuleSegmentInfo(GuestContext *ctx) {
    uint64_t seg_out = ctx->rdx;
    if (seg_out && ctx->mem_base) {
        OrbisKernelModuleSegmentInfo *seg = (OrbisKernelModuleSegmentInfo *)(ctx->mem_base + seg_out);
        seg->address = 0x400000ULL;
        seg->size = 0x10000000ULL;
        seg->prot = 7;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelGetModuleInfoForUnwind(GuestContext *ctx) {
    uint64_t info_addr = ctx->rsi;
    fill_module_info_ex(ctx, info_addr);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceKernelGetModuleInfoFromAddr(GuestContext *ctx) {
    uint64_t info_addr = ctx->rdx;
    fill_module_info_ex(ctx, info_addr);
    ctx->rax = 0;
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

