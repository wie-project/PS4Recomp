// SPDX-License-Identifier: GPL-2.0-or-later
//
// libkernel time functions, after shadPS4 (src/core/libraries/kernel/time.cpp, the POSIX host
// branch as it builds on macOS):
//  * posix_nanosleep / usleep / sleep can be interrupted by a signal (EINTR, the time left in
//    rmtp); sceKernelNanosleep / Usleep / Sleep resume the sleep instead.
//  * posix_clock_gettime maps the Orbis clock ids onto the host clocks (there are no coarse
//    clocks on macOS, so the _FAST and SECOND clocks read the precise ones, as in shadPS4);
//    PROCTIME is sceKernelGetProcessTime, the EXT_*NETWORK clocks run from the wall clock at
//    process start on the monotonic clock, VIRTUAL / PROF are the user / system CPU time.
//  * the sceKernel variants return ORBIS_KERNEL_ERROR_UNKNOWN + errno (ErrnoToSceKernelError)
//    and leave the errno set, as shadPS4's wrappers do.
//  * posix_clock_settime / posix_settimeofday are stubs in shadPS4 too: the guest cannot set
//    the host clock.
//
// Deliberate differences:
//  * The TSC runs at RECOMP_TSC_FREQ: the lifted RDTSC instruction scales the host counter by
//    that compile-time constant (pkg/lifter guestTscFreq), so sceKernelGetTscFrequency and
//    sceKernelReadTsc report the same fixed rate in base and Neo mode. shadPS4 patches RDTSC
//    at run time and picks 2.133 / 1.594 GHz by mode.
//  * shadPS4 asserts on clock ids posix_clock_getres does not map (UNREACHABLE); here they fail
//    with EINVAL, what FreeBSD's clock_getres returns for an unknown clock.
//  * sceKernelSleep sleeps seconds * 1e6 microseconds in 64 bits; shadPS4 multiplies in 32 bits.
#include "ps4_kernel_time.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

#define NSEC_PER_SEC 1000000000LL
#define ORBIS_KERNEL_ERROR_UNKNOWN 0x80020000u

static uint64_t g_initial_ptc;
static struct timespec g_network_boottime; // CLOCK_REALTIME at process start
static struct timespec g_initial_uptime;   // CLOCK_MONOTONIC at process start

// shadPS4 RegisterTime: the process clocks start when libkernel is loaded.
__attribute__((constructor)) static void ps4_kernel_time_init(void) {
    g_initial_ptc = recomp_rdtsc();
    clock_gettime(CLOCK_REALTIME, &g_network_boottime);
    clock_gettime(CLOCK_MONOTONIC, &g_initial_uptime);
}

static inline int32_t kernel_error(int err) {
    return (int32_t)(ORBIS_KERNEL_ERROR_UNKNOWN + (uint32_t)err);
}

static inline void *guest_ptr(GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

// A posix shim's failure: -1 with the guest errno.
static void posix_fail(GuestContext *ctx, int err) {
    set_guest_errno(ctx, err);
    ctx->rax = (uint64_t)-1;
}

// A sceKernel shim's result from the posix core's errno (0 = success).
static void kernel_result(GuestContext *ctx, int err) {
    if (err != 0) {
        set_guest_errno(ctx, err);
        ctx->rax = (uint64_t)(int64_t)kernel_error(err);
    } else {
        ctx->rax = 0;
    }
}

uint64_t sceKernelGetTscFrequency(void) {
    return RECOMP_TSC_FREQ;
}

// shadPS4 reads dev.cpu.0.freq, which reports the clock of the console mode.
uint64_t sceKernelGetCpuFrequency(void) {
    return sceKernelIsNeoMode() ? PS4_CPU_FREQ_NEO : PS4_CPU_FREQ_BASE;
}

uint64_t sceKernelReadTsc(void) {
    // Same counter as the lifted RDTSC instruction.
    return recomp_rdtsc();
}

uint64_t sceKernelGetProcessTimeCounter(void) {
    return sceKernelReadTsc() - g_initial_ptc;
}

uint64_t sceKernelGetProcessTimeCounterFrequency(void) {
    return sceKernelGetTscFrequency();
}

// Microseconds since the process started.
uint64_t sceKernelGetProcessTime(void) {
    return (uint64_t)(((unsigned __int128)sceKernelGetProcessTimeCounter() * 1000000u) /
                      sceKernelGetTscFrequency());
}

// ---------------------------------------------------------------------------------------------
// Sleeping (shadPS4 posix_nanosleep_impl over Common::AccurateSleep)

// Sleeps req; an interruptible sleep stops at a signal, any other resumes with the time left.
// Returns 0, or EINVAL / EINTR; rem (optional) receives the time left (0 after a full sleep).
static int sleep_impl(const OrbisKernelTimespec *req, OrbisKernelTimespec *rem, bool interruptible) {
    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= NSEC_PER_SEC) {
        return EINVAL;
    }
    struct timespec request = {.tv_sec = (time_t)req->tv_sec, .tv_nsec = (long)req->tv_nsec};
    struct timespec remain = {0, 0};
    int ret;
    while ((ret = nanosleep(&request, &remain)) < 0 && errno == EINTR) {
        if (interruptible) {
            break;
        }
        request = remain;
    }
    const bool interrupted = ret < 0 && errno == EINTR;
    if (rem) {
        rem->tv_sec = interrupted ? remain.tv_sec : 0;
        rem->tv_nsec = interrupted ? remain.tv_nsec : 0;
    }
    return interrupted ? EINTR : 0;
}

// int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
void shim_nanosleep(GuestContext *ctx) {
    OrbisKernelTimespec *rmtp = guest_ptr(ctx, ctx->rsi);
    const int err = sleep_impl(guest_ptr(ctx, ctx->rdi), rmtp, true);
    if (err != 0) {
        posix_fail(ctx, err);
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

// int sceKernelNanosleep(const SceKernelTimespec *rqtp, SceKernelTimespec *rmtp)
void shim_sceKernelNanosleep(GuestContext *ctx) {
    kernel_result(ctx, sleep_impl(guest_ptr(ctx, ctx->rdi), guest_ptr(ctx, ctx->rsi), false));
    SHIM_RETURN();
}

static OrbisKernelTimespec usec_timespec(uint64_t microseconds) {
    OrbisKernelTimespec ts = {
        .tv_sec = (int64_t)(microseconds / 1000000u),
        .tv_nsec = (int64_t)(microseconds % 1000000u) * 1000,
    };
    return ts;
}

// int usleep(useconds_t microseconds)
void shim_usleep(GuestContext *ctx) {
    const OrbisKernelTimespec ts = usec_timespec((uint32_t)ctx->rdi);
    const int err = sleep_impl(&ts, NULL, true);
    if (err != 0) {
        posix_fail(ctx, err);
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

// int sceKernelUsleep(SceKernelUseconds microseconds)
void shim_sceKernelUsleep(GuestContext *ctx) {
    const OrbisKernelTimespec ts = usec_timespec((uint32_t)ctx->rdi);
    kernel_result(ctx, sleep_impl(&ts, NULL, false));
    SHIM_RETURN();
}

// unsigned int sleep(unsigned int seconds): the seconds left (rounded up) when a signal ends
// the sleep, else 0.
void shim_sleep(GuestContext *ctx) {
    const uint32_t seconds = (uint32_t)ctx->rdi;
    const OrbisKernelTimespec ts = {.tv_sec = seconds, .tv_nsec = 0};
    OrbisKernelTimespec rm;
    const int err = sleep_impl(&ts, &rm, true);
    if (err != 0) {
        set_guest_errno(ctx, err);
        ctx->rax = err == EINTR ? (uint64_t)(uint32_t)(rm.tv_sec + (rm.tv_nsec == 0 ? 0 : 1)) : seconds;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

// int sceKernelSleep(unsigned int seconds)
void shim_sceKernelSleep(GuestContext *ctx) {
    const OrbisKernelTimespec ts = usec_timespec((uint64_t)(uint32_t)ctx->rdi * 1000000u);
    kernel_result(ctx, sleep_impl(&ts, NULL, false));
    SHIM_RETURN();
}

// ---------------------------------------------------------------------------------------------
// Clocks

static int network_time(OrbisKernelTimespec *ts) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return errno;
    }
    const int64_t ns = (int64_t)g_network_boottime.tv_sec * NSEC_PER_SEC + g_network_boottime.tv_nsec +
                       ((int64_t)now.tv_sec - g_initial_uptime.tv_sec) * NSEC_PER_SEC +
                       ((int64_t)now.tv_nsec - g_initial_uptime.tv_nsec);
    ts->tv_sec = ns / NSEC_PER_SEC;
    ts->tv_nsec = ns % NSEC_PER_SEC;
    return 0;
}

static bool is_network_clock(uint32_t clock_id) {
    return clock_id == ORBIS_CLOCK_EXT_NETWORK || clock_id == ORBIS_CLOCK_EXT_DEBUG_NETWORK ||
           clock_id == ORBIS_CLOCK_EXT_AD_NETWORK || clock_id == ORBIS_CLOCK_EXT_RAW_NETWORK;
}

// The host clock of an Orbis clock id shared by gettime and getres; false for the others.
static bool host_clock(uint32_t clock_id, clockid_t *out) {
    switch (clock_id) {
    case ORBIS_CLOCK_REALTIME:
    case ORBIS_CLOCK_REALTIME_PRECISE:
    case ORBIS_CLOCK_SECOND:
    case ORBIS_CLOCK_REALTIME_FAST:
        *out = CLOCK_REALTIME;
        return true;
    case ORBIS_CLOCK_UPTIME:
    case ORBIS_CLOCK_UPTIME_PRECISE:
    case ORBIS_CLOCK_MONOTONIC:
    case ORBIS_CLOCK_MONOTONIC_PRECISE:
    case ORBIS_CLOCK_UPTIME_FAST:
    case ORBIS_CLOCK_MONOTONIC_FAST:
        *out = CLOCK_MONOTONIC;
        return true;
    default:
        return false;
    }
}

static void from_timeval(OrbisKernelTimespec *ts, struct timeval tv) {
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = (int64_t)tv.tv_usec * 1000;
}

// posix_clock_gettime: 0 or the errno.
static int clock_gettime_impl(uint32_t clock_id, OrbisKernelTimespec *ts) {
    if (!ts) {
        return EFAULT;
    }
    if (clock_id == ORBIS_CLOCK_PROCTIME) {
        const uint64_t us = sceKernelGetProcessTime();
        ts->tv_sec = (int64_t)(us / 1000000u);
        ts->tv_nsec = (int64_t)(us % 1000000u) * 1000;
        return 0;
    }
    if (is_network_clock(clock_id)) {
        return network_time(ts);
    }
    clockid_t host;
    if (clock_id == ORBIS_CLOCK_THREAD_CPUTIME_ID) {
        host = CLOCK_THREAD_CPUTIME_ID;
    } else if (clock_id == ORBIS_CLOCK_VIRTUAL || clock_id == ORBIS_CLOCK_PROF) {
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru) < 0) {
            return EFAULT;
        }
        from_timeval(ts, clock_id == ORBIS_CLOCK_VIRTUAL ? ru.ru_utime : ru.ru_stime);
        return 0;
    } else if (!host_clock(clock_id, &host)) {
        return EFAULT;
    }
    struct timespec t = {0, 0};
    const int result = clock_gettime(host, &t);
    ts->tv_sec = t.tv_sec;
    ts->tv_nsec = t.tv_nsec;
    return result < 0 ? errno : 0;
}

// posix_clock_getres: 0 or the errno.
static int clock_getres_impl(uint32_t clock_id, OrbisKernelTimespec *res) {
    if (!res) {
        return EFAULT;
    }
    if (is_network_clock(clock_id)) {
        res->tv_sec = 0;
        res->tv_nsec = 1;
        return 0;
    }
    clockid_t host;
    if (!host_clock(clock_id, &host)) {
        fprintf(stderr, "[libkernel] clock_getres: unsupported clock id %u\n", clock_id);
        return EINVAL;
    }
    struct timespec t = {0, 0};
    const int result = clock_getres(host, &t);
    res->tv_sec = t.tv_sec;
    res->tv_nsec = t.tv_nsec;
    return result < 0 ? errno : 0;
}

int32_t sceKernelClockGettime(uint32_t clock_id, OrbisKernelTimespec *ts) {
    const int err = clock_gettime_impl(clock_id, ts);
    return err ? kernel_error(err) : 0;
}

int32_t sceKernelClockGetres(uint32_t clock_id, OrbisKernelTimespec *res) {
    const int err = clock_getres_impl(clock_id, res);
    return err ? kernel_error(err) : 0;
}

// int clock_gettime(clockid_t clock_id, struct timespec *tp)
void shim_clock_gettime(GuestContext *ctx) {
    const int err = clock_gettime_impl((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi));
    if (err != 0) {
        posix_fail(ctx, err);
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

// int sceKernelClockGettime(SceKernelClockid clock_id, SceKernelTimespec *tp)
void shim_sceKernelClockGettime(GuestContext *ctx) {
    kernel_result(ctx, clock_gettime_impl((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
    SHIM_RETURN();
}

// int clock_getres(clockid_t clock_id, struct timespec *res)
void shim_clock_getres(GuestContext *ctx) {
    const int err = clock_getres_impl((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi));
    if (err != 0) {
        posix_fail(ctx, err);
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

// int sceKernelClockGetres(SceKernelClockid clock_id, SceKernelTimespec *res)
void shim_sceKernelClockGetres(GuestContext *ctx) {
    kernel_result(ctx, clock_getres_impl((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
    SHIM_RETURN();
}

// shadPS4 stub: the guest cannot set the host clock.
// int clock_settime(clockid_t clock_id, const struct timespec *tp)
void shim_clock_settime(GuestContext *ctx) {
    fprintf(stderr, "[libkernel] clock_settime(%d) (STUBBED)\n", (int)ctx->rdi);
    ctx->rax = 0;
    SHIM_RETURN();
}

// ---------------------------------------------------------------------------------------------
// Wall clock and time zone

// posix_gettimeofday: both outputs optional; 0 or the errno.
static int gettimeofday_impl(OrbisKernelTimeval *tp, OrbisKernelTimezone *tz) {
    struct timeval tv = {0, 0};
    const int ret = gettimeofday(&tv, NULL);
    const int err = ret < 0 ? errno : 0;
    if (tp) {
        tp->tv_sec = tv.tv_sec;
        tp->tv_usec = tv.tv_usec;
    }
    if (tz) {
        const time_t t = tv.tv_sec;
        struct tm tm_val;
        localtime_r(&t, &tm_val);
        const int32_t dst_sec = tm_val.tm_isdst > 0 ? 3600 : 0;
        const int32_t standard_offset_sec = (int32_t)tm_val.tm_gmtoff - dst_sec;
        tz->tz_minuteswest = standard_offset_sec / 60;
        tz->tz_dsttime = dst_sec / 60;
    }
    return err;
}

int32_t sceKernelGettimeofday(OrbisKernelTimeval *tp) {
    const int err = gettimeofday_impl(tp, NULL);
    return err ? kernel_error(err) : 0;
}

int32_t sceKernelGettimezone(OrbisKernelTimezone *tz) {
    const int err = gettimeofday_impl(NULL, tz);
    return err ? kernel_error(err) : 0;
}

// int gettimeofday(struct timeval *tp, struct timezone *tz)
void shim_gettimeofday(GuestContext *ctx) {
    const int err = gettimeofday_impl(guest_ptr(ctx, ctx->rdi), guest_ptr(ctx, ctx->rsi));
    if (err != 0) {
        posix_fail(ctx, err);
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

// int sceKernelGettimeofday(SceKernelTimeval *tp)
void shim_sceKernelGettimeofday(GuestContext *ctx) {
    kernel_result(ctx, gettimeofday_impl(guest_ptr(ctx, ctx->rdi), NULL));
    SHIM_RETURN();
}

// int sceKernelGettimezone(SceKernelTimezone *tz)
void shim_sceKernelGettimezone(GuestContext *ctx) {
    kernel_result(ctx, gettimeofday_impl(NULL, guest_ptr(ctx, ctx->rdi)));
    SHIM_RETURN();
}

// shadPS4 stub: the guest cannot set the host clock.
// int settimeofday(const struct timeval *tv, const struct timezone *tz)
void shim_settimeofday(GuestContext *ctx) {
    fprintf(stderr, "[libkernel] settimeofday (STUBBED)\n");
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceKernelSettimeofday(const SceKernelTimeval *tv, const SceKernelTimezone *tz):
// posix_settimeofday, which always succeeds.
void shim_sceKernelSettimeofday(GuestContext *ctx) {
    fprintf(stderr, "[libkernel] settimeofday (STUBBED)\n");
    ctx->rax = 0;
    SHIM_RETURN();
}

// Takes the zone of now (sceKernelGettimezone), not the zone of local_time; unused is the
// second argument shadPS4 ignores.
int32_t sceKernelConvertLocaltimeToUtc(int64_t local_time, int64_t unused, int64_t *seconds,
                                       OrbisKernelTimezone *tz, int32_t *dst_seconds) {
    (void)unused;
    if (!tz) {
        return kernel_error(EINVAL);
    }
    sceKernelGettimezone(tz);
    local_time -= (int64_t)(tz->tz_minuteswest + tz->tz_dsttime) * 60;
    if (seconds) {
        *seconds = local_time;
    }
    if (dst_seconds) {
        *dst_seconds = tz->tz_dsttime * 60;
    }
    return 0;
}

int32_t sceKernelConvertUtcToLocaltime(int64_t time, int64_t *local_time, OrbisTimesec *st,
                                       uint64_t *dst_sec) {
    const time_t t = (time_t)time;
    struct tm tm_val;
    localtime_r(&t, &tm_val);
    const int32_t dst_sec_val = tm_val.tm_isdst > 0 ? 3600 : 0;
    const int32_t west_sec_val = (int32_t)tm_val.tm_gmtoff - dst_sec_val;
    if (local_time) {
        *local_time = time + tm_val.tm_gmtoff;
    }
    if (st) {
        st->t = time;
        st->west_sec = (uint32_t)west_sec_val;
        st->dst_sec = (uint32_t)dst_sec_val;
    }
    if (dst_sec) {
        *dst_sec = (uint64_t)(int64_t)dst_sec_val;
    }
    return 0;
}

// int sceKernelConvertLocaltimeToUtc(time_t local, int64_t, time_t *utc, SceKernelTimezone *tz,
//                                    int *dst_seconds)
void shim_sceKernelConvertLocaltimeToUtc(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelConvertLocaltimeToUtc(
        (int64_t)ctx->rdi, (int64_t)ctx->rsi, guest_ptr(ctx, ctx->rdx), guest_ptr(ctx, ctx->rcx),
        guest_ptr(ctx, ctx->r8));
    SHIM_RETURN();
}

// int sceKernelConvertUtcToLocaltime(time_t utc, time_t *local, SceKernelTimesec *st,
//                                    unsigned long *dst_sec)
void shim_sceKernelConvertUtcToLocaltime(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKernelConvertUtcToLocaltime(
        (int64_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx), guest_ptr(ctx, ctx->rcx));
    SHIM_RETURN();
}

// ---------------------------------------------------------------------------------------------
// Counters

void shim_sceKernelGetProcessTime(GuestContext *ctx) {
    ctx->rax = sceKernelGetProcessTime();
    SHIM_RETURN();
}

void shim_sceKernelGetProcessTimeCounter(GuestContext *ctx) {
    ctx->rax = sceKernelGetProcessTimeCounter();
    SHIM_RETURN();
}

void shim_sceKernelGetProcessTimeCounterFrequency(GuestContext *ctx) {
    ctx->rax = sceKernelGetProcessTimeCounterFrequency();
    SHIM_RETURN();
}

void shim_sceKernelReadTsc(GuestContext *ctx) {
    ctx->rax = sceKernelReadTsc();
    SHIM_RETURN();
}

void shim_sceKernelGetTscFrequency(GuestContext *ctx) {
    ctx->rax = sceKernelGetTscFrequency();
    SHIM_RETURN();
}

void shim_sceKernelGetCpuFrequency(GuestContext *ctx) {
    ctx->rax = sceKernelGetCpuFrequency();
    SHIM_RETURN();
}
