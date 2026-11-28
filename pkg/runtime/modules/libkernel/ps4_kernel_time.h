// SPDX-License-Identifier: GPL-2.0-or-later
//
// libkernel time functions, after shadPS4 (src/core/libraries/kernel/time.{h,cpp}).
#ifndef PS4_KERNEL_TIME_H
#define PS4_KERNEL_TIME_H

#include "recomp_runtime.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Clock ids of posix_clock_gettime / sceKernelClockGettime (FreeBSD + Orbis extensions).
#define ORBIS_CLOCK_REALTIME 0
#define ORBIS_CLOCK_VIRTUAL 1
#define ORBIS_CLOCK_PROF 2
#define ORBIS_CLOCK_MONOTONIC 4
#define ORBIS_CLOCK_UPTIME 5
#define ORBIS_CLOCK_UPTIME_PRECISE 7
#define ORBIS_CLOCK_UPTIME_FAST 8
#define ORBIS_CLOCK_REALTIME_PRECISE 9
#define ORBIS_CLOCK_REALTIME_FAST 10
#define ORBIS_CLOCK_MONOTONIC_PRECISE 11
#define ORBIS_CLOCK_MONOTONIC_FAST 12
#define ORBIS_CLOCK_SECOND 13
#define ORBIS_CLOCK_THREAD_CPUTIME_ID 14
#define ORBIS_CLOCK_PROCTIME 15
#define ORBIS_CLOCK_EXT_NETWORK 16
#define ORBIS_CLOCK_EXT_DEBUG_NETWORK 17
#define ORBIS_CLOCK_EXT_AD_NETWORK 18
#define ORBIS_CLOCK_EXT_RAW_NETWORK 19

// sceKernelGetCpuFrequency / dev.cpu.0.freq: the CPU clock of a PS4 Pro (Neo mode) and a base PS4.
#define PS4_CPU_FREQ_NEO 2133000000ULL
#define PS4_CPU_FREQ_BASE 1594000000ULL

typedef struct OrbisKernelTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
} OrbisKernelTimespec;

typedef struct OrbisKernelTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
} OrbisKernelTimeval;

// The console's time zone (shadPS4 posix_gettimeofday): despite its name, tz_minuteswest holds
// the standard offset EAST of UTC in minutes, and tz_dsttime the minutes daylight saving time
// adds now, so local time = UTC + tz_minuteswest + tz_dsttime. libSceRtc and other system
// libraries add the two to UTC ticks.
typedef struct OrbisKernelTimezone {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
} OrbisKernelTimezone;

typedef struct OrbisTimesec {
    int64_t t;
    uint32_t west_sec;
    uint32_t dst_sec;
} OrbisTimesec;

// Defined in ps4_kernel_sys.c.
int32_t sceKernelIsNeoMode(void);

// Host-side API (host pointers). The int32_t functions return ORBIS_OK or an Orbis kernel error.
uint64_t sceKernelGetTscFrequency(void);
uint64_t sceKernelGetCpuFrequency(void);
uint64_t sceKernelGetProcessTime(void);
uint64_t sceKernelGetProcessTimeCounter(void);
uint64_t sceKernelGetProcessTimeCounterFrequency(void);
uint64_t sceKernelReadTsc(void);
int32_t sceKernelClockGettime(uint32_t clock_id, OrbisKernelTimespec *ts);
int32_t sceKernelClockGetres(uint32_t clock_id, OrbisKernelTimespec *res);
int32_t sceKernelGettimeofday(OrbisKernelTimeval *tp);
int32_t sceKernelGettimezone(OrbisKernelTimezone *tz);
int32_t sceKernelConvertUtcToLocaltime(int64_t time, int64_t *local_time, OrbisTimesec *st,
                                       uint64_t *dst_sec);
int32_t sceKernelConvertLocaltimeToUtc(int64_t local_time, int64_t unused, int64_t *seconds,
                                       OrbisKernelTimezone *tz, int32_t *dst_seconds);

// Guest shims. The posix ones return -1 and set the guest errno; the sceKernel ones return the
// Orbis kernel error (ORBIS_KERNEL_ERROR_UNKNOWN + errno) and set the guest errno as well.
void shim_nanosleep(GuestContext *ctx);
void shim_usleep(GuestContext *ctx);
void shim_sleep(GuestContext *ctx);
void shim_clock_gettime(GuestContext *ctx);
void shim_clock_getres(GuestContext *ctx);
void shim_clock_settime(GuestContext *ctx);
void shim_gettimeofday(GuestContext *ctx);
void shim_settimeofday(GuestContext *ctx);
void shim_sceKernelGetProcessTime(GuestContext *ctx);
void shim_sceKernelGetProcessTimeCounter(GuestContext *ctx);
void shim_sceKernelGetProcessTimeCounterFrequency(GuestContext *ctx);
void shim_sceKernelReadTsc(GuestContext *ctx);
void shim_sceKernelGetTscFrequency(GuestContext *ctx);
void shim_sceKernelGetCpuFrequency(GuestContext *ctx);
void shim_sceKernelNanosleep(GuestContext *ctx);
void shim_sceKernelUsleep(GuestContext *ctx);
void shim_sceKernelSleep(GuestContext *ctx);
void shim_sceKernelClockGettime(GuestContext *ctx);
void shim_sceKernelClockGetres(GuestContext *ctx);
void shim_sceKernelGettimeofday(GuestContext *ctx);
void shim_sceKernelGettimezone(GuestContext *ctx);
void shim_sceKernelSettimeofday(GuestContext *ctx);
void shim_sceKernelConvertLocaltimeToUtc(GuestContext *ctx);
void shim_sceKernelConvertUtcToLocaltime(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
