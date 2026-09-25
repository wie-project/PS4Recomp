#include "ps4_kernel_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

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
