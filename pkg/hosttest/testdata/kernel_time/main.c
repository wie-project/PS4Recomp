// SPDX-License-Identifier: GPL-2.0-or-later

// libkernel time functions against shadPS4's kernel/time.cpp: the Orbis clock ids and their
// errors, clock_getres, interruptible posix sleeps against resumed sceKernel sleeps, the time
// zone conversions, and the process time counters. Guest pointers point into a test memory
// block; the guest errno lives in its TCB.
#include "ps4_kernel_time.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

static int g_neo;
int32_t sceKernelIsNeoMode(void) { return g_neo; }

#define MEM_SIZE 0x100000ull
#define STACK_TOP 0x80000ull
#define TCB 0x90000ull
#define TS 0x1000ull
#define REM 0x1010ull
#define TV 0x1020ull
#define TZ 0x1030ull
#define LOCAL 0x1040ull
#define TIMESEC 0x1050ull
#define DST 0x1060ull

#define E_EFAULT ((int32_t)0x8002000E)
#define E_EINVAL ((int32_t)0x80020016)

static uint8_t *mem;
static GuestContext ctx;

static int64_t *i64_at(uint64_t addr) { return (int64_t *)(mem + addr); }
static int guest_errno(void) { return *(int *)(mem + TCB + RECOMP_TCB_ERRNO_SLOT); }

static int64_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    *(int *)(mem + TCB + RECOMP_TCB_ERRNO_SLOT) = 0;
    ctx.rsp = STACK_TOP - 8;
    ctx.rdi = a0, ctx.rsi = a1, ctx.rdx = a2, ctx.rcx = a3, ctx.r8 = a4;
    shim(&ctx);
    CHECK(ctx.rsp == STACK_TOP);
    return (int64_t)ctx.rax;
}

static double mono_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void setup(void) {
    mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(mem != MAP_FAILED);
    ctx.mem_base = mem;
    ctx.mem_size = MEM_SIZE;
    ctx.fs_base = TCB;
}

static void test_clocks(void) {
    // REALTIME follows the host wall clock.
    CHECK(call(shim_clock_gettime, ORBIS_CLOCK_REALTIME, TS, 0, 0, 0) == 0);
    CHECK(llabs(i64_at(TS)[0] - (int64_t)time(NULL)) <= 1);
    CHECK(i64_at(TS)[1] >= 0 && i64_at(TS)[1] < 1000000000);
    // Every mapped id answers through both entry points.
    const uint32_t ids[] = {0, 1, 2, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        CHECK(call(shim_clock_gettime, ids[i], TS, 0, 0, 0) == 0);
        CHECK((int32_t)call(shim_sceKernelClockGettime, ids[i], TS, 0, 0, 0) == 0);
    }
    // The network clocks run with the wall clock.
    CHECK(call(shim_clock_gettime, ORBIS_CLOCK_EXT_NETWORK, TS, 0, 0, 0) == 0);
    CHECK(llabs(i64_at(TS)[0] - (int64_t)time(NULL)) <= 1);
    // MONOTONIC never goes back.
    CHECK(call(shim_clock_gettime, ORBIS_CLOCK_MONOTONIC, TS, 0, 0, 0) == 0);
    const int64_t m0 = i64_at(TS)[0] * 1000000000 + i64_at(TS)[1];
    CHECK(call(shim_clock_gettime, ORBIS_CLOCK_MONOTONIC_FAST, TS, 0, 0, 0) == 0);
    CHECK(i64_at(TS)[0] * 1000000000 + i64_at(TS)[1] >= m0);
    // PROCTIME is sceKernelGetProcessTime: seconds since the process started.
    CHECK(call(shim_clock_gettime, ORBIS_CLOCK_PROCTIME, TS, 0, 0, 0) == 0);
    CHECK(i64_at(TS)[0] < 60);

    // Unknown id and NULL timespec: EFAULT, the errno stays set behind the kernel error.
    CHECK(call(shim_clock_gettime, 3, TS, 0, 0, 0) == -1 && guest_errno() == EFAULT);
    CHECK(call(shim_clock_gettime, ORBIS_CLOCK_REALTIME, 0, 0, 0, 0) == -1 && guest_errno() == EFAULT);
    CHECK((int32_t)call(shim_sceKernelClockGettime, 99, TS, 0, 0, 0) == E_EFAULT && guest_errno() == EFAULT);
    CHECK((int32_t)call(shim_sceKernelClockGettime, 0, 0, 0, 0, 0) == E_EFAULT);

    // clock_getres: host resolution, 1 ns for the network clocks, EINVAL where shadPS4 asserts.
    CHECK(call(shim_clock_getres, ORBIS_CLOCK_MONOTONIC, TS, 0, 0, 0) == 0);
    CHECK(i64_at(TS)[0] == 0 && i64_at(TS)[1] > 0);
    CHECK((int32_t)call(shim_sceKernelClockGetres, ORBIS_CLOCK_EXT_RAW_NETWORK, TS, 0, 0, 0) == 0);
    CHECK(i64_at(TS)[0] == 0 && i64_at(TS)[1] == 1);
    CHECK(call(shim_clock_getres, ORBIS_CLOCK_PROCTIME, TS, 0, 0, 0) == -1 && guest_errno() == EINVAL);
    CHECK((int32_t)call(shim_sceKernelClockGetres, ORBIS_CLOCK_REALTIME, 0, 0, 0, 0) == E_EFAULT);

    // The guest cannot set the clock; shadPS4 reports success.
    CHECK(call(shim_clock_settime, ORBIS_CLOCK_REALTIME, TS, 0, 0, 0) == 0);
    CHECK(call(shim_settimeofday, TV, 0, 0, 0, 0) == 0);
    CHECK(call(shim_sceKernelSettimeofday, TV, 0, 0, 0, 0) == 0);
    printf("PASS clocks\n");
}

static void test_sleep(void) {
    // tv_nsec out of range or a negative time is EINVAL.
    i64_at(TS)[0] = 0, i64_at(TS)[1] = 1000000000;
    CHECK(call(shim_nanosleep, TS, REM, 0, 0, 0) == -1 && guest_errno() == EINVAL);
    CHECK((int32_t)call(shim_sceKernelNanosleep, TS, 0, 0, 0, 0) == E_EINVAL && guest_errno() == EINVAL);
    i64_at(TS)[0] = -1, i64_at(TS)[1] = 0;
    CHECK(call(shim_nanosleep, TS, 0, 0, 0, 0) == -1 && guest_errno() == EINVAL);
    CHECK(call(shim_nanosleep, 0, 0, 0, 0, 0) == -1 && guest_errno() == EINVAL);

    // A full sleep leaves no time in rmtp.
    i64_at(TS)[0] = 0, i64_at(TS)[1] = 2000000;
    i64_at(REM)[0] = 7, i64_at(REM)[1] = 7;
    double t0 = mono_s();
    CHECK(call(shim_nanosleep, TS, REM, 0, 0, 0) == 0);
    CHECK(mono_s() - t0 >= 0.002);
    CHECK(i64_at(REM)[0] == 0 && i64_at(REM)[1] == 0);

    t0 = mono_s();
    CHECK((int32_t)call(shim_sceKernelUsleep, 3000, 0, 0, 0, 0) == 0);
    CHECK(call(shim_usleep, 3000, 0, 0, 0, 0) == 0);
    CHECK(mono_s() - t0 >= 0.006);
    CHECK(call(shim_sleep, 0, 0, 0, 0, 0) == 0);
    CHECK((int32_t)call(shim_sceKernelSleep, 0, 0, 0, 0, 0) == 0);
    printf("PASS sleep\n");
}

static void on_alarm(int sig) { (void)sig; }

static void arm_alarm_ms(int ms) {
    struct itimerval it = {.it_value = {.tv_sec = 0, .tv_usec = ms * 1000}};
    CHECK(setitimer(ITIMER_REAL, &it, NULL) == 0);
}

static void test_interrupt(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm; // no SA_RESTART: the signal ends the sleep with EINTR
    CHECK(sigaction(SIGALRM, &sa, NULL) == 0);

    // posix nanosleep stops at the signal and reports the time left.
    i64_at(TS)[0] = 1, i64_at(TS)[1] = 0;
    arm_alarm_ms(20);
    double t0 = mono_s();
    CHECK(call(shim_nanosleep, TS, REM, 0, 0, 0) == -1 && guest_errno() == EINTR);
    CHECK(mono_s() - t0 < 0.5);
    CHECK(i64_at(REM)[0] == 0 && i64_at(REM)[1] > 500000000);

    // sleep returns the seconds left, rounded up.
    arm_alarm_ms(20);
    CHECK(call(shim_sleep, 2, 0, 0, 0, 0) == 2 && guest_errno() == EINTR);

    // sceKernelNanosleep resumes after the signal and sleeps the full time.
    i64_at(TS)[0] = 0, i64_at(TS)[1] = 100000000;
    arm_alarm_ms(20);
    t0 = mono_s();
    CHECK((int32_t)call(shim_sceKernelNanosleep, TS, 0, 0, 0, 0) == 0);
    CHECK(mono_s() - t0 >= 0.1);
    arm_alarm_ms(20);
    t0 = mono_s();
    CHECK((int32_t)call(shim_sceKernelUsleep, 100000, 0, 0, 0, 0) == 0);
    CHECK(mono_s() - t0 >= 0.1);
    printf("PASS interrupt\n");
}

static void test_timezone(void) {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    OrbisTimesec *st = (OrbisTimesec *)(mem + TIMESEC);

    // Summer (CEST): UTC+2 of which 1 h is daylight saving time.
    const int64_t summer = 1720000000; // 2024-07-03
    CHECK(call(shim_sceKernelConvertUtcToLocaltime, (uint64_t)summer, LOCAL, TIMESEC, DST, 0) == 0);
    CHECK(*i64_at(LOCAL) == summer + 7200);
    CHECK(st->t == summer && st->west_sec == 3600 && st->dst_sec == 3600);
    CHECK(*(uint64_t *)(mem + DST) == 3600);
    // Winter (CET): UTC+1, no daylight saving time.
    const int64_t winter = 1700000000; // 2023-11-14
    CHECK(call(shim_sceKernelConvertUtcToLocaltime, (uint64_t)winter, LOCAL, TIMESEC, 0, 0) == 0);
    CHECK(*i64_at(LOCAL) == winter + 3600);
    CHECK(st->west_sec == 3600 && st->dst_sec == 0);
    // Every output is optional.
    CHECK(call(shim_sceKernelConvertUtcToLocaltime, (uint64_t)winter, 0, 0, 0, 0) == 0);

    // Gettimezone: east offset in minutes and the daylight saving minutes of now.
    CHECK(call(shim_sceKernelGettimezone, TZ, 0, 0, 0, 0) == 0);
    const OrbisKernelTimezone *tz = (const OrbisKernelTimezone *)(mem + TZ);
    CHECK(tz->tz_minuteswest == 60);
    CHECK(tz->tz_dsttime == 0 || tz->tz_dsttime == 60);
    const int32_t zone_min = tz->tz_minuteswest + tz->tz_dsttime;
    // NULL outputs are allowed (shadPS4 posix_gettimeofday).
    CHECK(call(shim_sceKernelGettimezone, 0, 0, 0, 0, 0) == 0);
    CHECK(call(shim_sceKernelGettimeofday, 0, 0, 0, 0, 0) == 0);
    CHECK(call(shim_sceKernelGettimeofday, TV, 0, 0, 0, 0) == 0);
    CHECK(llabs(i64_at(TV)[0] - (int64_t)time(NULL)) <= 1);
    memset(mem + TZ, 0xff, 8);
    CHECK(call(shim_gettimeofday, TV, TZ, 0, 0, 0) == 0);
    CHECK(tz->tz_minuteswest == 60);

    // LocaltimeToUtc(local, unused, *utc, *tz, *dst_seconds): the zone of now; tz is required.
    CHECK((int32_t)call(shim_sceKernelConvertLocaltimeToUtc, 100000, 0, LOCAL, 0, DST) == E_EINVAL);
    CHECK(call(shim_sceKernelConvertLocaltimeToUtc, 100000, 0, LOCAL, TZ, DST) == 0);
    CHECK(*i64_at(LOCAL) == 100000 - (int64_t)zone_min * 60);
    CHECK(*(int32_t *)(mem + DST) == tz->tz_dsttime * 60);
    printf("PASS timezone\n");
}

static void test_counters(void) {
    CHECK((uint64_t)call(shim_sceKernelGetTscFrequency, 0, 0, 0, 0, 0) == RECOMP_TSC_FREQ);
    CHECK((uint64_t)call(shim_sceKernelGetProcessTimeCounterFrequency, 0, 0, 0, 0, 0) == RECOMP_TSC_FREQ);
    g_neo = 1;
    CHECK((uint64_t)call(shim_sceKernelGetCpuFrequency, 0, 0, 0, 0, 0) == 2133000000ull);
    g_neo = 0;
    CHECK((uint64_t)call(shim_sceKernelGetCpuFrequency, 0, 0, 0, 0, 0) == 1594000000ull);

    const uint64_t c0 = (uint64_t)call(shim_sceKernelGetProcessTimeCounter, 0, 0, 0, 0, 0);
    const uint64_t p0 = (uint64_t)call(shim_sceKernelGetProcessTime, 0, 0, 0, 0, 0);
    const uint64_t r0 = (uint64_t)call(shim_sceKernelReadTsc, 0, 0, 0, 0, 0);
    struct timespec ms10 = {0, 10000000};
    nanosleep(&ms10, NULL);
    const uint64_t c1 = (uint64_t)call(shim_sceKernelGetProcessTimeCounter, 0, 0, 0, 0, 0);
    const uint64_t p1 = (uint64_t)call(shim_sceKernelGetProcessTime, 0, 0, 0, 0, 0);
    CHECK((uint64_t)call(shim_sceKernelReadTsc, 0, 0, 0, 0, 0) > r0);
    // The counter starts at process start and runs at the TSC frequency.
    CHECK(c0 < 60 * RECOMP_TSC_FREQ);
    CHECK(c1 - c0 >= RECOMP_TSC_FREQ / 100);
    CHECK(p1 - p0 >= 10000 && p1 - p0 < 1000000);
    printf("PASS counters\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    setup();
    if (strcmp(argv[1], "clocks") == 0) {
        test_clocks();
    } else if (strcmp(argv[1], "sleep") == 0) {
        test_sleep();
    } else if (strcmp(argv[1], "interrupt") == 0) {
        test_interrupt();
    } else if (strcmp(argv[1], "timezone") == 0) {
        test_timezone();
    } else if (strcmp(argv[1], "counters") == 0) {
        test_counters();
    } else {
        fprintf(stderr, "unknown case %s\n", argv[1]);
        return 2;
    }
    return 0;
}
