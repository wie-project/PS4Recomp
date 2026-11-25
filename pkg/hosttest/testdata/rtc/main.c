// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_rtc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                    \
        }                                                                \
    } while (0)

#define SDK_NEW 0x4500000u
#define SDK_OLD 0x2000000u
// 2026-10-16 18:13:44.123456 UTC
#define UNIX_T 1792174424LL
#define TICK_T (ORBIS_RTC_UNIX_EPOCH_TICKS + (uint64_t)UNIX_T * 1000000 + 123456)

static int dt_eq(const OrbisRtcDateTime *t, int y, int mo, int d, int h, int mi, int s, unsigned us) {
    return t->year == y && t->month == mo && t->day == d && t->hour == h && t->minute == mi && t->second == s &&
           t->microsecond == us;
}

static int test_ticks(void) {
    OrbisRtcDateTime t;
    OrbisRtcTick tick = {0};
    // Tick 0 is 0001-01-01 00:00:00, the Unix epoch tick is 1970-01-01.
    CHECK(sceRtcSetTick(&t, &tick) == 0 && dt_eq(&t, 1, 1, 1, 0, 0, 0, 0));
    tick.tick = ORBIS_RTC_UNIX_EPOCH_TICKS;
    CHECK(sceRtcSetTick(&t, &tick) == 0 && dt_eq(&t, 1970, 1, 1, 0, 0, 0, 0));
    tick.tick = TICK_T;
    CHECK(sceRtcSetTick(&t, &tick) == 0 && dt_eq(&t, 2026, 10, 16, 18, 13, 44, 123456));

    const OrbisRtcDateTime in = t;
    OrbisRtcTick back;
    CHECK(sceRtcGetTick(&in, &back) == 0 && back.tick == TICK_T);
    CHECK(memcmp(&in, &t, sizeof(t)) == 0); // the input date is not modified

    const OrbisRtcDateTime leap = {2024, 2, 29, 23, 59, 59, 999999};
    CHECK(sceRtcGetTick(&leap, &back) == 0 && sceRtcSetTick(&t, &back) == 0 && memcmp(&t, &leap, sizeof(t)) == 0);
    const OrbisRtcDateTime bad_day = {2023, 2, 29, 0, 0, 0, 0};
    CHECK(sceRtcGetTick(&bad_day, &back) == ORBIS_RTC_ERROR_INVALID_DAY);
    CHECK(sceRtcGetTick(NULL, &back) == ORBIS_RTC_ERROR_INVALID_POINTER);

    // A day of ticks: 1970-01-02.
    tick.tick = ORBIS_RTC_UNIX_EPOCH_TICKS + 86400000000ULL;
    CHECK(sceRtcSetTick(&t, &tick) == 0 && dt_eq(&t, 1970, 1, 2, 0, 0, 0, 0));

    CHECK(sceRtcGetTickResolution() == 1000000);
    OrbisRtcTick now;
    CHECK(sceRtcGetCurrentTick(NULL) == ORBIS_RTC_ERROR_DATETIME_UNINITIALIZED);
    CHECK(sceRtcGetCurrentTick(&now) == 0);
    const int64_t unix_now = (int64_t)time(NULL);
    const int64_t tick_secs = (int64_t)((now.tick - ORBIS_RTC_UNIX_EPOCH_TICKS) / 1000000);
    CHECK(tick_secs >= unix_now - 2 && tick_secs <= unix_now + 2);
    CHECK(sceRtcGetCurrentNetworkTick(NULL) == ORBIS_RTC_ERROR_INVALID_POINTER);
    CHECK(sceRtcGetCurrentRawNetworkTick(&now) == 0);
    printf("PASS ticks\n");
    return 0;
}

static int test_validation(void) {
    OrbisRtcDateTime t = {2026, 10, 16, 18, 13, 44, 0};
    CHECK(sceRtcCheckValid(&t) == 0);
    t.year = 0;
    CHECK(sceRtcCheckValid(&t) == ORBIS_RTC_ERROR_INVALID_YEAR);
    t.year = 2026, t.month = 13;
    CHECK(sceRtcCheckValid(&t) == ORBIS_RTC_ERROR_INVALID_MONTH);
    t.month = 4, t.day = 31;
    CHECK(sceRtcCheckValid(&t) == ORBIS_RTC_ERROR_INVALID_DAY);
    t.day = 30, t.hour = 24;
    CHECK(sceRtcCheckValid(&t) == ORBIS_RTC_ERROR_INVALID_HOUR);
    t.hour = 0, t.minute = 60;
    CHECK(sceRtcCheckValid(&t) == ORBIS_RTC_ERROR_INVALID_MINUTE);
    t.minute = 0, t.second = 60;
    CHECK(sceRtcCheckValid(&t) == ORBIS_RTC_ERROR_INVALID_SECOND);
    t.second = 0, t.microsecond = 1000000;
    CHECK(sceRtcCheckValid(&t) == ORBIS_RTC_ERROR_INVALID_MICROSECOND);
    CHECK(sceRtcCheckValid(NULL) == ORBIS_RTC_ERROR_INVALID_POINTER);

    CHECK(sceRtcIsLeapYear(2000) == 1 && sceRtcIsLeapYear(1900) == 0 && sceRtcIsLeapYear(2024) == 1);
    CHECK(sceRtcIsLeapYear(0) == ORBIS_RTC_ERROR_INVALID_YEAR);
    CHECK(sceRtcGetDaysInMonth(2024, 2) == 29 && sceRtcGetDaysInMonth(2023, 2) == 28 && sceRtcGetDaysInMonth(2026, 4) == 30);
    CHECK(sceRtcGetDaysInMonth(0, 1) == ORBIS_RTC_ERROR_INVALID_YEAR);
    CHECK(sceRtcGetDaysInMonth(2026, 0) == ORBIS_RTC_ERROR_INVALID_MONTH);

    CHECK(sceRtcGetDayOfWeek(2026, 10, 16, SDK_NEW) == 5); // Friday
    CHECK(sceRtcGetDayOfWeek(1970, 1, 1, SDK_NEW) == 4);   // Thursday
    CHECK(sceRtcGetDayOfWeek(1, 1, 1, SDK_NEW) == 1);      // Monday (proleptic Gregorian)
    CHECK(sceRtcGetDayOfWeek(2026, 2, 29, SDK_NEW) == ORBIS_RTC_ERROR_INVALID_DAY);
    CHECK(sceRtcGetDayOfWeek(10000, 1, 1, SDK_NEW) == ORBIS_RTC_ERROR_INVALID_YEAR);
    CHECK(sceRtcGetDayOfWeek(10000, 1, 1, SDK_OLD) >= 0);
    CHECK(sceRtcGetDayOfWeek(2026, 13, 1, SDK_OLD) == ORBIS_RTC_ERROR_INVALID_MONTH);

    OrbisRtcTick a = {5}, b = {7};
    CHECK(sceRtcCompareTick(&a, &b) == -1 && sceRtcCompareTick(&b, &a) == 1 && sceRtcCompareTick(&a, &a) == 0);
    CHECK(sceRtcCompareTick(NULL, &a) == ORBIS_RTC_ERROR_INVALID_POINTER);
    printf("PASS validation\n");
    return 0;
}

static int test_tick_add(void) {
    const OrbisRtcTick src = {TICK_T};
    OrbisRtcTick dst;
    OrbisRtcDateTime t;
    CHECK(sceRtcTickAddTicks(&dst, &src, -123456) == 0 && dst.tick == TICK_T - 123456);
    CHECK(sceRtcTickAddMicroseconds(&dst, &src, 1) == 0 && dst.tick == TICK_T + 1);
    CHECK(sceRtcTickAddSeconds(&dst, &src, -44) == 0 && sceRtcSetTick(&t, &dst) == 0 && t.second == 0 && t.minute == 13);
    CHECK(sceRtcTickAddMinutes(&dst, &src, 47) == 0 && sceRtcSetTick(&t, &dst) == 0 && t.hour == 19 && t.minute == 0);
    CHECK(sceRtcTickAddHours(&dst, &src, 6) == 0 && sceRtcSetTick(&t, &dst) == 0 && dt_eq(&t, 2026, 10, 17, 0, 13, 44, 123456));
    CHECK(sceRtcTickAddDays(&dst, &src, -16) == 0 && sceRtcSetTick(&t, &dst) == 0 && dt_eq(&t, 2026, 9, 30, 18, 13, 44, 123456));
    CHECK(sceRtcTickAddWeeks(&dst, &src, 11) == 0 && sceRtcSetTick(&t, &dst) == 0 && dt_eq(&t, 2027, 1, 1, 18, 13, 44, 123456));
    CHECK(sceRtcTickAddDays(NULL, &src, 1) == ORBIS_RTC_ERROR_INVALID_POINTER);

    // Months clamp the day to the target month; the source tick is the one read.
    const OrbisRtcDateTime jan31 = {2024, 1, 31, 12, 0, 0, 0};
    OrbisRtcTick j;
    CHECK(sceRtcGetTick(&jan31, &j) == 0);
    dst.tick = 0;
    CHECK(sceRtcTickAddMonths(&dst, &j, 1) == 0 && sceRtcSetTick(&t, &dst) == 0 && dt_eq(&t, 2024, 2, 29, 12, 0, 0, 0));
    CHECK(sceRtcTickAddMonths(&dst, &j, 13) == 0 && sceRtcSetTick(&t, &dst) == 0 && dt_eq(&t, 2025, 2, 28, 12, 0, 0, 0));
    CHECK(sceRtcTickAddMonths(&dst, &j, -1) == 0 && sceRtcSetTick(&t, &dst) == 0 && dt_eq(&t, 2023, 12, 31, 12, 0, 0, 0));
    CHECK(sceRtcTickAddMonths(&dst, &j, -25) == 0 && sceRtcSetTick(&t, &dst) == 0 && dt_eq(&t, 2021, 12, 31, 12, 0, 0, 0));
    CHECK(sceRtcTickAddMonths(&dst, &j, 0) == 0 && dst.tick == j.tick);

    CHECK(sceRtcTickAddYears(&dst, &j, 3) == 0 && sceRtcSetTick(&t, &dst) == 0 && dt_eq(&t, 2027, 1, 31, 12, 0, 0, 0));
    const OrbisRtcDateTime feb29 = {2024, 2, 29, 0, 0, 0, 0};
    CHECK(sceRtcGetTick(&feb29, &j) == 0);
    CHECK(sceRtcTickAddYears(&dst, &j, 1) == ORBIS_RTC_ERROR_INVALID_DAY);
    CHECK(sceRtcTickAddYears(&dst, &j, 4) == 0 && sceRtcSetTick(&t, &dst) == 0 && dt_eq(&t, 2028, 2, 29, 0, 0, 0, 0));
    CHECK(sceRtcTickAddYears(&dst, &j, 8000) == ORBIS_RTC_ERROR_INVALID_YEAR);
    printf("PASS tick-add\n");
    return 0;
}

static int test_conversions(void) {
    OrbisRtcDateTime t;
    int64_t tt;
    CHECK(sceRtcSetTime_t(&t, UNIX_T, SDK_NEW) == 0 && dt_eq(&t, 2026, 10, 16, 18, 13, 44, 0));
    CHECK(sceRtcGetTime_t(&t, &tt) == 0 && tt == UNIX_T);
    CHECK(sceRtcSetTime_t(&t, -1, SDK_NEW) == ORBIS_RTC_ERROR_INVALID_VALUE);
    CHECK(sceRtcSetTime_t(&t, -1, SDK_OLD) == 0 && dt_eq(&t, 2106, 2, 7, 6, 28, 15, 0)); // 0xffffffff
    CHECK(sceRtcSetTime_t(NULL, 0, SDK_NEW) == ORBIS_RTC_ERROR_INVALID_POINTER);
    const OrbisRtcDateTime old = {1969, 12, 31, 0, 0, 0, 0};
    CHECK(sceRtcGetTime_t(&old, &tt) == 0 && tt == 0);

    // FILETIME of the Unix epoch.
    const OrbisRtcDateTime epoch = {1970, 1, 1, 0, 0, 0, 0};
    uint64_t ft;
    CHECK(sceRtcGetWin32FileTime(&epoch, &ft) == 0 && ft == 116444736000000000ULL);
    CHECK(sceRtcSetWin32FileTime(&t, 116444736000000000LL) == 0 && dt_eq(&t, 1970, 1, 1, 0, 0, 0, 0));

    // DOS time: 2026-10-16 18:13:44.
    const OrbisRtcDateTime dos_in = {2026, 10, 16, 18, 13, 44, 0};
    uint32_t dos = 0xFFFFFFFFu;
    CHECK(sceRtcGetDosTime(&dos_in, &dos) == 0);
    CHECK(dos == ((46u << 25) | (10u << 21) | (16u << 16) | (18u << 11) | (13u << 5) | 22u));
    CHECK(sceRtcSetDosTime(&t, dos) == 0 && dt_eq(&t, 2026, 10, 16, 18, 13, 44, 0));
    // Years from 2044 set the top bit of the date word.
    const OrbisRtcDateTime late = {2100, 1, 2, 3, 4, 6, 0};
    CHECK(sceRtcGetDosTime(&late, &dos) == 0 && sceRtcSetDosTime(&t, dos) == 0 && dt_eq(&t, 2100, 1, 2, 3, 4, 6, 0));

    // Local time conversions follow the host zone: use a fixed zone without DST.
    setenv("TZ", "UTC-3", 1);
    tzset();
    const OrbisRtcTick utc = {TICK_T};
    OrbisRtcTick local, back;
    CHECK(sceRtcConvertUtcToLocalTime(&utc, &local) == 0 && local.tick == TICK_T + 180ULL * 60000000ULL);
    CHECK(sceRtcConvertLocalTimeToUtc(&local, &back) == 0 && back.tick == TICK_T);
    CHECK(sceRtcConvertUtcToLocalTime(&utc, NULL) == ORBIS_RTC_ERROR_INVALID_POINTER);
    printf("PASS conversions\n");
    return 0;
}

static int test_format_parse(void) {
    char buf[64];
    const OrbisRtcTick tick = {TICK_T};
    CHECK(sceRtcFormatRFC3339(buf, &tick, 0) == 0 && strcmp(buf, "2026-10-16T18:13:44.12Z") == 0);
    CHECK(sceRtcFormatRFC3339Precise(buf, &tick, 330) == 0 && strcmp(buf, "2026-10-16T23:43:44.12+05:30") == 0);
    CHECK(sceRtcFormatRFC3339(buf, &tick, -65) == 0 && strcmp(buf, "2026-10-16T17:08:44.12-01:05") == 0);
    CHECK(sceRtcFormatRFC2822(buf, &tick, 0) == 0 && strcmp(buf, "Fri, 16 Oct 2026 18:13:44 +0000") == 0);
    CHECK(sceRtcFormatRFC2822(buf, &tick, 180) == 0 && strcmp(buf, "Fri, 16 Oct 2026 21:13:44 +0300") == 0);
    CHECK(sceRtcFormatRFC2822(NULL, &tick, 0) == ORBIS_RTC_ERROR_INVALID_POINTER);

    setenv("TZ", "UTC-3", 1);
    tzset();
    CHECK(sceRtcFormatRFC3339LocalTime(buf, &tick) == 0 && strcmp(buf, "2026-10-16T21:13:44.12+03:00") == 0);
    CHECK(sceRtcFormatRFC2822LocalTime(buf, &tick) == 0 && strcmp(buf, "Fri, 16 Oct 2026 21:13:44 +0300") == 0);

    OrbisRtcTick p;
    CHECK(sceRtcParseRFC3339(&p, "2026-10-16T18:13:44.123456Z") == 0 && p.tick == TICK_T);
    CHECK(sceRtcParseRFC3339(&p, "2026-10-16T21:13:44.123456+03:00") == 0 && p.tick == TICK_T);
    CHECK(sceRtcParseRFC3339(&p, "2026-10-16T17:08:44.123456-01:05") == 0 && p.tick == TICK_T);
    CHECK(sceRtcParseRFC3339(&p, "2026-10-16T18:13:44Z") == 0 && p.tick == TICK_T - 123456);
    CHECK(sceRtcParseRFC3339(&p, "2026-10-16T18:13:44.5Z") == 0 && p.tick == TICK_T - 123456 + 500000);
    CHECK(sceRtcParseRFC3339(&p, "2026-10-16T18:13") == ORBIS_RTC_ERROR_BAD_PARSE);
    CHECK(sceRtcParseRFC3339(&p, "2026-13-16T18:13:44Z") == ORBIS_RTC_ERROR_INVALID_MONTH);
    CHECK(sceRtcParseRFC3339(&p, "2026-10-16T18:13:44") == ORBIS_RTC_ERROR_BAD_PARSE);
    CHECK(sceRtcParseRFC3339(NULL, "x") == ORBIS_RTC_ERROR_INVALID_POINTER);

    // Format then parse is the identity at hundredth precision.
    CHECK(sceRtcFormatRFC3339(buf, &tick, 330) == 0 && sceRtcParseRFC3339(&p, buf) == 0 && p.tick == TICK_T - 3456);

    CHECK(sceRtcParseDateTime(&p, "2026-10-16T18:13:44.123456Z") == 0 && p.tick == TICK_T);
    CHECK(sceRtcParseDateTime(&p, "Fri, 16 Oct 2026 21:13:44 +0300") == 0 && p.tick == TICK_T - 123456);
    CHECK(sceRtcParseDateTime(&p, "Fri, 16 Oct 2026 18:13:44 +0000") == 0 && p.tick == TICK_T - 123456);
    CHECK(sceRtcParseDateTime(&p, "Fri Oct 16 18:13:44 2026") == 0 && p.tick == TICK_T - 123456);
    CHECK(sceRtcParseDateTime(&p, "Thu Jan  1 00:00:00 1970") == 0 && p.tick == ORBIS_RTC_UNIX_EPOCH_TICKS);
    CHECK(sceRtcParseDateTime(&p, "Fri, 16 Foo 2026 21:13:44 +0300") == ORBIS_RTC_ERROR_BAD_PARSE);
    CHECK(sceRtcParseDateTime(&p, "short") == ORBIS_RTC_ERROR_BAD_PARSE);
    printf("PASS format-parse\n");
    return 0;
}

static int test_set_clock(void) {
    OrbisRtcTick t = {ORBIS_RTC_UNIX_EPOCH_TICKS};
    CHECK(sceRtcSetCurrentTick(NULL) == ORBIS_RTC_ERROR_INVALID_POINTER);
    CHECK(sceRtcSetCurrentTick(&t) == ORBIS_RTC_ERROR_INVALID_VALUE);
    CHECK(sceRtcSetCurrentNetworkTick(&t) == ORBIS_RTC_ERROR_INVALID_VALUE);
    t.tick = TICK_T;
    CHECK(sceRtcSetCurrentTick(&t) == 0 && sceRtcSetCurrentAdNetworkTick(&t) == 0 &&
          sceRtcSetCurrentDebugNetworkTick(&t) == 0 && sceRtcSetCurrentNetworkTick(&t) == 0);
    CHECK(sceRtcInit() == 0 && sceRtcEnd() == 0);

    OrbisRtcDateTime now;
    CHECK(sceRtcGetCurrentClock(NULL, 0) == ORBIS_RTC_ERROR_DATETIME_UNINITIALIZED);
    CHECK(sceRtcGetCurrentClock(&now, 0) == 0 && sceRtcCheckValid(&now) == 0 && now.year >= 2026);
    CHECK(sceRtcGetCurrentClockLocalTime(&now) == 0 && sceRtcCheckValid(&now) == 0);
    printf("PASS set-clock\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *c = argv[1];
    if (strcmp(c, "ticks") == 0) return test_ticks();
    if (strcmp(c, "validation") == 0) return test_validation();
    if (strcmp(c, "tick-add") == 0) return test_tick_add();
    if (strcmp(c, "conversions") == 0) return test_conversions();
    if (strcmp(c, "format-parse") == 0) return test_format_parse();
    if (strcmp(c, "set-clock") == 0) return test_set_clock();
    return 2;
}
