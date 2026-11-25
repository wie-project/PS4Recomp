// libSceRtc, ported from shadPS4 (src/core/libraries/rtc/rtc.cpp).
//
// Ticks are microseconds since 0001-01-01 UTC, as on the console. Where shadPS4 is plainly wrong
// this port is not (each place says so):
//  * sceRtcGetTick does not modify the caller's date; sceRtcTickAddMonths/Years read the source
//    tick instead of the destination;
//  * RFC 3339 / RFC 2822 parsing subtracts the zone offset ("12:00+03:00" is 09:00 UTC), reads
//    the fraction as a fraction of a second, and rejects malformed text with
//    ORBIS_RTC_ERROR_BAD_PARSE instead of throwing;
//  * zone minutes are always printed with two digits; sceRtcGetDosTime stores the value instead
//    of OR-ing it into the output; sceRtcSetDosTime decodes years past 2043;
//  * sceRtcCompareTick returns -1, 0 or 1 (the PSP/PS4 Rtc contract) instead of 1 for "<=";
//  * the local zone is the host's offset at the instant converted (DST included); shadPS4 adds
//    sceKernelGettimezone's tz_minuteswest + tz_dsttime, the offset now.

#include "ps4_rtc.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define US_PER_SEC 1000000ULL
#define US_PER_MIN 60000000ULL
#define US_PER_HOUR 3600000000ULL
#define US_PER_DAY 86400000000ULL
#define US_PER_WEEK 604800000000ULL

static const char *const k_weekdays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const k_months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static bool is_leap(int32_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int32_t days_in_month(int32_t year, int32_t month) {
    static const int8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && is_leap(year) ? 29 : days[month - 1];
}

static uint64_t now_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * US_PER_SEC + (uint64_t)ts.tv_nsec / 1000 + ORBIS_RTC_UNIX_EPOCH_TICKS;
}

// Seconds since the Unix epoch of a tick (negative before 1970).
static int64_t tick_unix_seconds(uint64_t tick) {
    return (int64_t)(tick - ORBIS_RTC_UNIX_EPOCH_TICKS) / (int64_t)US_PER_SEC;
}

// The host zone's offset east of UTC, in minutes, at a Unix time.
static int32_t local_offset_minutes(int64_t unix_seconds) {
    const time_t t = (time_t)unix_seconds;
    struct tm tm;
    if (!localtime_r(&t, &tm)) {
        return 0;
    }
    return (int32_t)(tm.tm_gmtoff / 60);
}

int32_t sceRtcCheckValid(const OrbisRtcDateTime *pTime) {
    if (!pTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    if (pTime->year == 0 || pTime->year > 9999) return ORBIS_RTC_ERROR_INVALID_YEAR;
    if (pTime->month == 0 || pTime->month > 12) return ORBIS_RTC_ERROR_INVALID_MONTH;
    if (pTime->day == 0 || pTime->day > days_in_month(pTime->year, pTime->month)) return ORBIS_RTC_ERROR_INVALID_DAY;
    if (pTime->hour >= 24) return ORBIS_RTC_ERROR_INVALID_HOUR;
    if (pTime->minute >= 60) return ORBIS_RTC_ERROR_INVALID_MINUTE;
    if (pTime->second >= 60) return ORBIS_RTC_ERROR_INVALID_SECOND;
    if (pTime->microsecond >= 1000000) return ORBIS_RTC_ERROR_INVALID_MICROSECOND;
    return 0;
}

int32_t sceRtcCompareTick(const OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2) {
    if (!pTick1 || !pTick2) return ORBIS_RTC_ERROR_INVALID_POINTER;
    if (pTick1->tick < pTick2->tick) return -1;
    return pTick1->tick > pTick2->tick ? 1 : 0;
}

int32_t sceRtcTickAddTicks(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int64_t lAdd) {
    if (!pTick1 || !pTick2) return ORBIS_RTC_ERROR_INVALID_POINTER;
    pTick1->tick = pTick2->tick + (uint64_t)lAdd;
    return 0;
}

int32_t sceRtcTickAddMicroseconds(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int64_t lAdd) {
    return sceRtcTickAddTicks(pTick1, pTick2, lAdd);
}

int32_t sceRtcTickAddSeconds(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int64_t lAdd) {
    return sceRtcTickAddTicks(pTick1, pTick2, (int64_t)((uint64_t)lAdd * US_PER_SEC));
}

int32_t sceRtcTickAddMinutes(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int64_t lAdd) {
    return sceRtcTickAddTicks(pTick1, pTick2, (int64_t)((uint64_t)lAdd * US_PER_MIN));
}

int32_t sceRtcTickAddHours(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd) {
    return sceRtcTickAddTicks(pTick1, pTick2, (int64_t)lAdd * (int64_t)US_PER_HOUR);
}

int32_t sceRtcTickAddDays(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd) {
    return sceRtcTickAddTicks(pTick1, pTick2, (int64_t)lAdd * (int64_t)US_PER_DAY);
}

int32_t sceRtcTickAddWeeks(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd) {
    return sceRtcTickAddTicks(pTick1, pTick2, (int64_t)lAdd * (int64_t)US_PER_WEEK);
}

// Date of a tick; the algorithm is shadPS4's (and the console's) days-since-March arithmetic.
int32_t sceRtcSetTick(OrbisRtcDateTime *pTime, const OrbisRtcTick *pTick) {
    if (!pTime || !pTick) return ORBIS_RTC_ERROR_INVALID_POINTER;

    uint64_t days = pTick->tick / US_PER_DAY + 307;
    uint64_t usec = pTick->tick % US_PER_DAY;

    uint32_t j = (uint32_t)((days << 2) - 1);
    uint32_t ly = j / 146097;
    j -= 146097 * ly;
    uint32_t ld = j >> 2;
    j = ((ld << 2) + 3) / 1461;
    ld = (((ld << 2) + 7) - 1461 * j) >> 2;
    uint32_t lm = (5 * ld - 3) / 153;
    ld = (5 * ld + 2 - 153 * lm) / 5;
    ly = 100 * ly + j;
    if (lm < 10) {
        lm += 3;
    } else {
        lm -= 9;
        ly++;
    }

    pTime->year = (uint16_t)ly;
    pTime->month = (uint16_t)lm;
    pTime->day = (uint16_t)ld;
    pTime->hour = (uint16_t)(usec / US_PER_HOUR);
    usec %= US_PER_HOUR;
    pTime->minute = (uint16_t)(usec / US_PER_MIN);
    usec %= US_PER_MIN;
    pTime->second = (uint16_t)(usec / US_PER_SEC);
    pTime->microsecond = (uint32_t)(usec % US_PER_SEC);
    return 0;
}

int32_t sceRtcGetTick(const OrbisRtcDateTime *pTime, OrbisRtcTick *pTick) {
    if (!pTime || !pTick) return ORBIS_RTC_ERROR_INVALID_POINTER;
    const int32_t valid = sceRtcCheckValid(pTime);
    if (valid != 0) return valid;

    // Years start in March so that the leap day ends the year.
    uint64_t month = pTime->month;
    uint64_t year = pTime->year;
    if (month > 2) {
        month -= 3;
    } else {
        month += 9;
        year -= 1;
    }
    const uint64_t c = year / 100;
    const uint64_t ya = year - 100 * c;
    uint64_t days = ((146097 * c) >> 2) + ((1461 * ya) >> 2) + (153 * month + 2) / 5 + pTime->day;
    days -= 307;
    pTick->tick = days * US_PER_DAY + pTime->hour * US_PER_HOUR + pTime->minute * US_PER_MIN +
                  pTime->second * US_PER_SEC + pTime->microsecond;
    return 0;
}

int32_t sceRtcTickAddMonths(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd) {
    if (!pTick1 || !pTick2) return ORBIS_RTC_ERROR_INVALID_POINTER;
    if (lAdd == 0) {
        pTick1->tick = pTick2->tick;
        return 0;
    }
    OrbisRtcDateTime time;
    sceRtcSetTick(&time, pTick2);

    const int64_t s = lAdd >= 0 ? 1 : -1;
    int64_t year = (int64_t)time.year + lAdd / 12;
    int64_t month0 = (int64_t)time.month + lAdd % 12 - 1;
    if (month0 > 11 || month0 < 0) {
        month0 -= s * 12;
        year += s;
    }
    if (year < 1 || year > 9999) return ORBIS_RTC_ERROR_INVALID_YEAR;
    time.year = (uint16_t)year;
    time.month = (uint16_t)(month0 + 1);
    const int32_t last = days_in_month(time.year, time.month);
    if (time.day > last) time.day = (uint16_t)last;

    const int32_t valid = sceRtcCheckValid(&time);
    if (valid != 0) return valid;
    return sceRtcGetTick(&time, pTick1);
}

int32_t sceRtcTickAddYears(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd) {
    if (!pTick1 || !pTick2) return ORBIS_RTC_ERROR_INVALID_POINTER;
    if (lAdd == 0) {
        pTick1->tick = pTick2->tick;
        return 0;
    }
    OrbisRtcDateTime time;
    sceRtcSetTick(&time, pTick2);
    const int64_t year = (int64_t)time.year + lAdd;
    if (year < 1 || year > 9999) return ORBIS_RTC_ERROR_INVALID_YEAR;
    time.year = (uint16_t)year;
    // Feb 29 in a non-leap target year fails, as in shadPS4.
    const int32_t valid = sceRtcCheckValid(&time);
    if (valid != 0) return valid;
    return sceRtcGetTick(&time, pTick1);
}

int32_t sceRtcConvertUtcToLocalTime(const OrbisRtcTick *pTickUtc, OrbisRtcTick *pTickLocal) {
    if (!pTickUtc || !pTickLocal) return ORBIS_RTC_ERROR_INVALID_POINTER;
    const int32_t offset = local_offset_minutes(tick_unix_seconds(pTickUtc->tick));
    return sceRtcTickAddMinutes(pTickLocal, pTickUtc, offset);
}

int32_t sceRtcConvertLocalTimeToUtc(const OrbisRtcTick *pTickLocal, OrbisRtcTick *pTickUtc) {
    if (!pTickLocal || !pTickUtc) return ORBIS_RTC_ERROR_INVALID_POINTER;
    // The offset of the UTC instant that shows this local time: estimate with the offset at the
    // local value read as UTC, then take the offset at the estimate (settles across DST changes).
    const int64_t local = tick_unix_seconds(pTickLocal->tick);
    const int32_t guess = local_offset_minutes(local);
    const int32_t offset = local_offset_minutes(local - (int64_t)guess * 60);
    return sceRtcTickAddMinutes(pTickUtc, pTickLocal, -(int64_t)offset);
}

int32_t sceRtcInit(void) {
    return 0;
}

int32_t sceRtcEnd(void) {
    return 0;
}

// The zone suffix: "Z" or "+HH:MM" (RFC 3339), "+0000" or "+HHMM" (RFC 2822).
static void format_zone(char *out, size_t size, int32_t minutes, bool rfc3339) {
    if (minutes == 0) {
        snprintf(out, size, "%s", rfc3339 ? "Z" : "+0000");
        return;
    }
    const char sign = minutes < 0 ? '-' : '+';
    const int32_t m = minutes < 0 ? -minutes : minutes;
    snprintf(out, size, rfc3339 ? "%c%02d:%02d" : "%c%02d%02d", sign, m / 60 % 100, m % 60);
}

// "Fri, 16 Oct 2026 18:13:44 +0300". Writes nothing for a tick outside years 1..9999.
int32_t sceRtcFormatRFC2822(char *pszDateTime, const OrbisRtcTick *pTickUtc, int32_t iTimeZoneMinutes) {
    if (!pszDateTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    OrbisRtcTick tick = {pTickUtc ? pTickUtc->tick : now_tick()};
    sceRtcTickAddMinutes(&tick, &tick, iTimeZoneMinutes);
    OrbisRtcDateTime t;
    sceRtcSetTick(&t, &tick);
    if (sceRtcCheckValid(&t) != 0) return 0;

    char zone[8];
    format_zone(zone, sizeof(zone), iTimeZoneMinutes, false);
    const int32_t wday = sceRtcGetDayOfWeek(t.year, t.month, t.day, 0xffffffffu);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s, %02u %s %04u %02u:%02u:%02u %s", k_weekdays[wday], t.day,
             k_months[t.month - 1], t.year, t.hour, t.minute, t.second, zone);
    strcpy(pszDateTime, buf);
    return 0;
}

int32_t sceRtcFormatRFC2822LocalTime(char *pszDateTime, const OrbisRtcTick *pTickUtc) {
    const uint64_t tick = pTickUtc ? pTickUtc->tick : now_tick();
    return sceRtcFormatRFC2822(pszDateTime, pTickUtc, local_offset_minutes(tick_unix_seconds(tick)));
}

// "2026-10-19T12:34:56.78+03:00": the fraction is two digits (hundredths), as in shadPS4.
int32_t sceRtcFormatRFC3339Precise(char *pszDateTime, const OrbisRtcTick *pTickUtc, int32_t iTimeZoneMinutes) {
    if (!pszDateTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    OrbisRtcTick tick = {pTickUtc ? pTickUtc->tick : now_tick()};
    sceRtcTickAddMinutes(&tick, &tick, iTimeZoneMinutes);
    OrbisRtcDateTime t;
    sceRtcSetTick(&t, &tick);

    char zone[8];
    format_zone(zone, sizeof(zone), iTimeZoneMinutes, true);
    char buf[40];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%02u%s", t.year, t.month, t.day, t.hour,
             t.minute, t.second, t.microsecond / 10000, zone);
    strcpy(pszDateTime, buf);
    return 0;
}

int32_t sceRtcFormatRFC3339(char *pszDateTime, const OrbisRtcTick *pTickUtc, int32_t iTimeZoneMinutes) {
    return sceRtcFormatRFC3339Precise(pszDateTime, pTickUtc, iTimeZoneMinutes);
}

int32_t sceRtcFormatRFC3339PreciseLocalTime(char *pszDateTime, const OrbisRtcTick *pTickUtc) {
    const uint64_t tick = pTickUtc ? pTickUtc->tick : now_tick();
    return sceRtcFormatRFC3339Precise(pszDateTime, pTickUtc, local_offset_minutes(tick_unix_seconds(tick)));
}

int32_t sceRtcFormatRFC3339LocalTime(char *pszDateTime, const OrbisRtcTick *pTickUtc) {
    return sceRtcFormatRFC3339PreciseLocalTime(pszDateTime, pTickUtc);
}

int32_t sceRtcGetCurrentTick(OrbisRtcTick *pTick) {
    if (!pTick) return ORBIS_RTC_ERROR_DATETIME_UNINITIALIZED;
    pTick->tick = now_tick();
    return 0;
}

// The network clocks are the host's real-time clock (as in shadPS4).
int32_t sceRtcGetCurrentNetworkTick(OrbisRtcTick *pTick) {
    if (!pTick) return ORBIS_RTC_ERROR_INVALID_POINTER;
    pTick->tick = now_tick();
    return 0;
}

int32_t sceRtcGetCurrentAdNetworkTick(OrbisRtcTick *pTick) {
    return sceRtcGetCurrentNetworkTick(pTick);
}

int32_t sceRtcGetCurrentDebugNetworkTick(OrbisRtcTick *pTick) {
    return sceRtcGetCurrentNetworkTick(pTick);
}

int32_t sceRtcGetCurrentRawNetworkTick(OrbisRtcTick *pTick) {
    return sceRtcGetCurrentNetworkTick(pTick);
}

int32_t sceRtcGetCurrentClock(OrbisRtcDateTime *pTime, int32_t timeZone) {
    if (!pTime) return ORBIS_RTC_ERROR_DATETIME_UNINITIALIZED;
    OrbisRtcTick tick = {now_tick()};
    sceRtcTickAddMinutes(&tick, &tick, timeZone);
    return sceRtcSetTick(pTime, &tick);
}

int32_t sceRtcGetCurrentClockLocalTime(OrbisRtcDateTime *pTime) {
    if (!pTime) return ORBIS_RTC_ERROR_DATETIME_UNINITIALIZED;
    OrbisRtcTick tick = {now_tick()};
    sceRtcTickAddMinutes(&tick, &tick, local_offset_minutes(tick_unix_seconds(tick.tick)));
    return sceRtcSetTick(pTime, &tick);
}

// Titles built before SDK 3.0 get no upper year bound, as on the console.
int32_t sceRtcGetDayOfWeek(int32_t year, int32_t month, int32_t day, uint32_t sdk_version) {
    if (year < 1 || (sdk_version >= ORBIS_RTC_SDK_RANGE_CHECKS && year > 9999)) return ORBIS_RTC_ERROR_INVALID_YEAR;
    if (month < 1 || month > 12) return ORBIS_RTC_ERROR_INVALID_MONTH;
    if (day < 1 || day > days_in_month(year, month)) return ORBIS_RTC_ERROR_INVALID_DAY;
    // Zeller-style count for the proleptic Gregorian calendar; 0 is Sunday.
    static const int8_t t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    const int64_t y = month < 3 ? (int64_t)year - 1 : year;
    return (int32_t)((y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7);
}

int32_t sceRtcGetDaysInMonth(int32_t year, int32_t month) {
    if (year <= 0) return ORBIS_RTC_ERROR_INVALID_YEAR;
    if (month <= 0 || month > 12) return ORBIS_RTC_ERROR_INVALID_MONTH;
    return days_in_month(year, month);
}

int32_t sceRtcIsLeapYear(int32_t year) {
    if (year < 1) return ORBIS_RTC_ERROR_INVALID_YEAR;
    return is_leap(year) ? 1 : 0;
}

uint32_t sceRtcGetTickResolution(void) {
    return 1000000;
}

int32_t sceRtcGetDosTime(const OrbisRtcDateTime *pTime, uint32_t *dosTime) {
    if (!pTime || !dosTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    const int32_t valid = sceRtcCheckValid(pTime);
    if (valid != 0) return valid;
    *dosTime = ((uint32_t)(pTime->second / 2) & 0x1F) | (((uint32_t)pTime->minute & 0x3F) << 5) |
               (((uint32_t)pTime->hour & 0x1F) << 11) | (((uint32_t)pTime->day & 0x1F) << 16) |
               (((uint32_t)pTime->month & 0x0F) << 21) | (((uint32_t)(pTime->year - 1980) & 0x7F) << 25);
    return 0;
}

int32_t sceRtcSetDosTime(OrbisRtcDateTime *pTime, uint32_t dosTime) {
    if (!pTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    const uint16_t date = (uint16_t)(dosTime >> 16);
    pTime->microsecond = 0;
    pTime->second = (uint16_t)((dosTime << 1) & 0x3E);
    pTime->minute = (uint16_t)((dosTime >> 5) & 0x3F);
    pTime->hour = (uint16_t)((dosTime >> 11) & 0x1F);
    pTime->day = date & 0x1F;
    pTime->month = (date >> 5) & 0x0F;
    pTime->year = (uint16_t)((date >> 9) + 1980);
    return 0;
}

int32_t sceRtcGetTime_t(const OrbisRtcDateTime *pTime, int64_t *llTime) {
    if (!pTime || !llTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    OrbisRtcTick tick;
    const int32_t rc = sceRtcGetTick(pTime, &tick);
    if (rc != 0) return rc;
    *llTime = tick.tick < ORBIS_RTC_UNIX_EPOCH_TICKS ? 0 : (int64_t)((tick.tick - ORBIS_RTC_UNIX_EPOCH_TICKS) / US_PER_SEC);
    return 0;
}

// Titles built before SDK 3.0 pass time_t as an unsigned 32-bit value; newer ones get
// INVALID_VALUE for a negative time.
int32_t sceRtcSetTime_t(OrbisRtcDateTime *pTime, int64_t llTime, uint32_t sdk_version) {
    if (!pTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    uint64_t secs;
    if (sdk_version < ORBIS_RTC_SDK_RANGE_CHECKS) {
        secs = (uint64_t)llTime & 0xffffffffULL;
    } else {
        if (llTime < 0) return ORBIS_RTC_ERROR_INVALID_VALUE;
        secs = (uint64_t)llTime;
    }
    const OrbisRtcTick tick = {secs * US_PER_SEC + ORBIS_RTC_UNIX_EPOCH_TICKS};
    return sceRtcSetTick(pTime, &tick);
}

// Windows FILETIME counts 100 ns units since 1601-01-01.
int32_t sceRtcGetWin32FileTime(const OrbisRtcDateTime *pTime, uint64_t *ulWin32Time) {
    if (!pTime || !ulWin32Time) return ORBIS_RTC_ERROR_INVALID_POINTER;
    OrbisRtcTick tick;
    const int32_t rc = sceRtcGetTick(pTime, &tick);
    if (rc != 0) return rc;
    *ulWin32Time = tick.tick < ORBIS_RTC_WIN32_FILETIME_EPOCH_TICKS
                       ? 0
                       : (tick.tick - ORBIS_RTC_WIN32_FILETIME_EPOCH_TICKS) * 10;
    return 0;
}

int32_t sceRtcSetWin32FileTime(OrbisRtcDateTime *pTime, int64_t ulWin32Time) {
    if (!pTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    const OrbisRtcTick tick = {(uint64_t)(ulWin32Time / 10) + ORBIS_RTC_WIN32_FILETIME_EPOCH_TICKS};
    return sceRtcSetTick(pTime, &tick);
}

// Setting the console clocks does not change the host clock; the value is checked as in
// shadPS4, whose clock_settime/settimeofday are no-ops as well.
static int32_t check_settable_tick(const OrbisRtcTick *pTick) {
    if (!pTick) return ORBIS_RTC_ERROR_INVALID_POINTER;
    if (pTick->tick <= ORBIS_RTC_UNIX_EPOCH_TICKS) return ORBIS_RTC_ERROR_INVALID_VALUE;
    return 0;
}

int32_t sceRtcSetCurrentTick(const OrbisRtcTick *pTick) {
    return check_settable_tick(pTick);
}

int32_t sceRtcSetCurrentNetworkTick(const OrbisRtcTick *pTick) {
    return check_settable_tick(pTick);
}

int32_t sceRtcSetCurrentAdNetworkTick(const OrbisRtcTick *pTick) {
    return check_settable_tick(pTick);
}

int32_t sceRtcSetCurrentDebugNetworkTick(const OrbisRtcTick *pTick) {
    return check_settable_tick(pTick);
}

// ---------------------------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------------------------

// Reads `n` decimal digits at s[pos]; a leading space counts as 0 (asctime pads the day).
static bool read_digits(const char *s, size_t len, size_t pos, size_t n, int32_t *out) {
    if (pos + n > len) return false;
    int32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        const char c = s[pos + i];
        if (c == ' ' && i == 0 && n > 1) continue;
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    *out = v;
    return true;
}

static int32_t month_from_name(const char *s, size_t len, size_t pos) {
    if (pos + 3 > len) return 0;
    for (int32_t i = 0; i < 12; i++) {
        if (memcmp(s + pos, k_months[i], 3) == 0) return i + 1;
    }
    return 0;
}

static int32_t tick_from_fields(OrbisRtcTick *out, int32_t year, int32_t month, int32_t day, int32_t hour,
                                int32_t minute, int32_t second, uint32_t usec) {
    if (year < 0 || year > 0xFFFF || month < 0 || month > 0xFFFF || day < 0 || day > 0xFFFF) {
        return ORBIS_RTC_ERROR_BAD_PARSE;
    }
    const OrbisRtcDateTime t = {(uint16_t)year, (uint16_t)month, (uint16_t)day, (uint16_t)hour,
                                (uint16_t)minute, (uint16_t)second, usec};
    return sceRtcGetTick(&t, out);
}

// "YYYY-MM-DDTHH:MM:SS[.fraction](Z|+HH:MM|-HH:MM)"
int32_t sceRtcParseRFC3339(OrbisRtcTick *pTickUtc, const char *pszDateTime) {
    if (!pTickUtc || !pszDateTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    const char *s = pszDateTime;
    const size_t len = strnlen(s, 64);
    int32_t year, month, day, hour, minute, second;
    if (!read_digits(s, len, 0, 4, &year) || s[4] != '-' || !read_digits(s, len, 5, 2, &month) ||
        s[7] != '-' || !read_digits(s, len, 8, 2, &day) || (s[10] != 'T' && s[10] != 't' && s[10] != ' ') ||
        !read_digits(s, len, 11, 2, &hour) || s[13] != ':' || !read_digits(s, len, 14, 2, &minute) ||
        s[16] != ':' || !read_digits(s, len, 17, 2, &second)) {
        return ORBIS_RTC_ERROR_BAD_PARSE;
    }
    size_t pos = 19;
    uint32_t usec = 0;
    if (pos < len && s[pos] == '.') {
        pos++;
        uint32_t scale = 100000;
        const size_t start = pos;
        while (pos < len && s[pos] >= '0' && s[pos] <= '9') {
            usec += (uint32_t)(s[pos] - '0') * scale;
            scale /= 10;
            pos++;
        }
        if (pos == start) return ORBIS_RTC_ERROR_BAD_PARSE;
    }
    int64_t offset = 0;
    if (pos < len && (s[pos] == 'Z' || s[pos] == 'z')) {
        offset = 0;
    } else if (pos < len && (s[pos] == '+' || s[pos] == '-')) {
        int32_t oh, om;
        if (!read_digits(s, len, pos + 1, 2, &oh) || s[pos + 3] != ':' || !read_digits(s, len, pos + 4, 2, &om)) {
            return ORBIS_RTC_ERROR_BAD_PARSE;
        }
        offset = (int64_t)oh * 60 + om;
        if (s[pos] == '-') offset = -offset;
    } else {
        return ORBIS_RTC_ERROR_BAD_PARSE;
    }
    const int32_t rc = tick_from_fields(pTickUtc, year, month, day, hour, minute, second, usec);
    if (rc != 0) return rc;
    return sceRtcTickAddMinutes(pTickUtc, pTickUtc, -offset);
}

// RFC 3339, RFC 2822 ("Fri, 16 Oct 2026 18:13:44 +0300") or asctime ("Fri Oct 16 18:13:44 2026"),
// told apart by the character at index 19 as in shadPS4.
int32_t sceRtcParseDateTime(OrbisRtcTick *pTickUtc, const char *pszDateTime) {
    if (!pTickUtc || !pszDateTime) return ORBIS_RTC_ERROR_INVALID_POINTER;
    const char *s = pszDateTime;
    const size_t len = strnlen(s, 64);
    if (len < 20) return ORBIS_RTC_ERROR_BAD_PARSE;

    const char key = s[19];
    if (key == 'Z' || key == '-' || key == '+' || key == '.') {
        return sceRtcParseRFC3339(pTickUtc, s);
    }
    int32_t year, day, hour, minute, second;
    if (key == ':') {
        const int32_t month = month_from_name(s, len, 8);
        if (!month || !read_digits(s, len, 5, 2, &day) || !read_digits(s, len, 12, 4, &year) ||
            !read_digits(s, len, 17, 2, &hour) || !read_digits(s, len, 20, 2, &minute) ||
            !read_digits(s, len, 23, 2, &second)) {
            return ORBIS_RTC_ERROR_BAD_PARSE;
        }
        const int32_t rc = tick_from_fields(pTickUtc, year, month, day, hour, minute, second, 0);
        if (rc != 0) return rc;
        if (len > 26 && (s[26] == '+' || s[26] == '-')) {
            int32_t oh, om;
            if (!read_digits(s, len, 27, 2, &oh) || !read_digits(s, len, 29, 2, &om)) return ORBIS_RTC_ERROR_BAD_PARSE;
            const int64_t offset = (int64_t)oh * 60 + om;
            return sceRtcTickAddMinutes(pTickUtc, pTickUtc, s[26] == '+' ? -offset : offset);
        }
        return 0;
    }
    const int32_t month = month_from_name(s, len, 4);
    if (!month || !read_digits(s, len, 8, 2, &day) || !read_digits(s, len, 11, 2, &hour) ||
        !read_digits(s, len, 14, 2, &minute) || !read_digits(s, len, 17, 2, &second) ||
        !read_digits(s, len, 20, 4, &year)) {
        return ORBIS_RTC_ERROR_BAD_PARSE;
    }
    return tick_from_fields(pTickUtc, year, month, day, hour, minute, second, 0);
}

// ---------------------------------------------------------------------------------------------
// Guest ABI shims
// ---------------------------------------------------------------------------------------------

#define GPTR(type, reg) ((reg) ? (type)(ctx->mem_base + (reg)) : NULL)
#define RET(expr)                                \
    do {                                         \
        ctx->rax = (uint64_t)(int64_t)(expr);    \
        SHIM_RETURN();                           \
    } while (0)

// SceKernelProcParam: size u64, magic "ORBI" u32, entry count u32, SDK version u32 at +0x10.
static uint32_t compiled_sdk_version(const GuestContext *ctx) {
    const GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    if (!proc->proc_param_addr || !proc->mem_base || proc->proc_param_addr + 0x14 > proc->mem_size) {
        return 0xffffffffu; // unknown: the current behaviour
    }
    return *(const uint32_t *)(proc->mem_base + proc->proc_param_addr + 0x10);
}

void shim_sceRtcCheckValid(GuestContext *ctx) {
    RET(sceRtcCheckValid(GPTR(const OrbisRtcDateTime *, ctx->rdi)));
}

void shim_sceRtcCompareTick(GuestContext *ctx) {
    RET(sceRtcCompareTick(GPTR(const OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi)));
}

void shim_sceRtcConvertLocalTimeToUtc(GuestContext *ctx) {
    RET(sceRtcConvertLocalTimeToUtc(GPTR(const OrbisRtcTick *, ctx->rdi), GPTR(OrbisRtcTick *, ctx->rsi)));
}

void shim_sceRtcConvertUtcToLocalTime(GuestContext *ctx) {
    RET(sceRtcConvertUtcToLocalTime(GPTR(const OrbisRtcTick *, ctx->rdi), GPTR(OrbisRtcTick *, ctx->rsi)));
}

void shim_sceRtcEnd(GuestContext *ctx) {
    RET(sceRtcEnd());
}

void shim_sceRtcFormatRFC2822(GuestContext *ctx) {
    RET(sceRtcFormatRFC2822(GPTR(char *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int32_t)ctx->rdx));
}

void shim_sceRtcFormatRFC2822LocalTime(GuestContext *ctx) {
    RET(sceRtcFormatRFC2822LocalTime(GPTR(char *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi)));
}

void shim_sceRtcFormatRFC3339(GuestContext *ctx) {
    RET(sceRtcFormatRFC3339(GPTR(char *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int32_t)ctx->rdx));
}

void shim_sceRtcFormatRFC3339LocalTime(GuestContext *ctx) {
    RET(sceRtcFormatRFC3339LocalTime(GPTR(char *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi)));
}

void shim_sceRtcFormatRFC3339Precise(GuestContext *ctx) {
    RET(sceRtcFormatRFC3339Precise(GPTR(char *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int32_t)ctx->rdx));
}

void shim_sceRtcFormatRFC3339PreciseLocalTime(GuestContext *ctx) {
    RET(sceRtcFormatRFC3339PreciseLocalTime(GPTR(char *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi)));
}

void shim_sceRtcGetCurrentAdNetworkTick(GuestContext *ctx) {
    RET(sceRtcGetCurrentAdNetworkTick(GPTR(OrbisRtcTick *, ctx->rdi)));
}

void shim_sceRtcGetCurrentClock(GuestContext *ctx) {
    RET(sceRtcGetCurrentClock(GPTR(OrbisRtcDateTime *, ctx->rdi), (int32_t)ctx->rsi));
}

void shim_sceRtcGetCurrentClockLocalTime(GuestContext *ctx) {
    RET(sceRtcGetCurrentClockLocalTime(GPTR(OrbisRtcDateTime *, ctx->rdi)));
}

void shim_sceRtcGetCurrentDebugNetworkTick(GuestContext *ctx) {
    RET(sceRtcGetCurrentDebugNetworkTick(GPTR(OrbisRtcTick *, ctx->rdi)));
}

void shim_sceRtcGetCurrentNetworkTick(GuestContext *ctx) {
    RET(sceRtcGetCurrentNetworkTick(GPTR(OrbisRtcTick *, ctx->rdi)));
}

void shim_sceRtcGetCurrentRawNetworkTick(GuestContext *ctx) {
    RET(sceRtcGetCurrentRawNetworkTick(GPTR(OrbisRtcTick *, ctx->rdi)));
}

void shim_sceRtcGetCurrentTick(GuestContext *ctx) {
    RET(sceRtcGetCurrentTick(GPTR(OrbisRtcTick *, ctx->rdi)));
}

void shim_sceRtcGetDayOfWeek(GuestContext *ctx) {
    RET(sceRtcGetDayOfWeek((int32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx, compiled_sdk_version(ctx)));
}

void shim_sceRtcGetDaysInMonth(GuestContext *ctx) {
    RET(sceRtcGetDaysInMonth((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_sceRtcGetDosTime(GuestContext *ctx) {
    RET(sceRtcGetDosTime(GPTR(const OrbisRtcDateTime *, ctx->rdi), GPTR(uint32_t *, ctx->rsi)));
}

void shim_sceRtcGetTick(GuestContext *ctx) {
    RET(sceRtcGetTick(GPTR(const OrbisRtcDateTime *, ctx->rdi), GPTR(OrbisRtcTick *, ctx->rsi)));
}

void shim_sceRtcGetTickResolution(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceRtcGetTickResolution();
    SHIM_RETURN();
}

void shim_sceRtcGetTime_t(GuestContext *ctx) {
    RET(sceRtcGetTime_t(GPTR(const OrbisRtcDateTime *, ctx->rdi), GPTR(int64_t *, ctx->rsi)));
}

void shim_sceRtcGetWin32FileTime(GuestContext *ctx) {
    RET(sceRtcGetWin32FileTime(GPTR(const OrbisRtcDateTime *, ctx->rdi), GPTR(uint64_t *, ctx->rsi)));
}

void shim_sceRtcInit(GuestContext *ctx) {
    RET(sceRtcInit());
}

void shim_sceRtcIsLeapYear(GuestContext *ctx) {
    RET(sceRtcIsLeapYear((int32_t)ctx->rdi));
}

void shim_sceRtcParseDateTime(GuestContext *ctx) {
    RET(sceRtcParseDateTime(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const char *, ctx->rsi)));
}

void shim_sceRtcParseRFC3339(GuestContext *ctx) {
    RET(sceRtcParseRFC3339(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const char *, ctx->rsi)));
}

// void sceRtcSetConf(void *, void *, int32_t minuteswest, int32_t dsttime): sets the console
// time zone. The host zone stays in effect, as in shadPS4 (its settimeofday is a no-op).
void shim_sceRtcSetConf(GuestContext *ctx) {
    SHIM_RETURN();
}

void shim_sceRtcSetCurrentAdNetworkTick(GuestContext *ctx) {
    RET(sceRtcSetCurrentAdNetworkTick(GPTR(const OrbisRtcTick *, ctx->rdi)));
}

void shim_sceRtcSetCurrentDebugNetworkTick(GuestContext *ctx) {
    RET(sceRtcSetCurrentDebugNetworkTick(GPTR(const OrbisRtcTick *, ctx->rdi)));
}

void shim_sceRtcSetCurrentNetworkTick(GuestContext *ctx) {
    RET(sceRtcSetCurrentNetworkTick(GPTR(const OrbisRtcTick *, ctx->rdi)));
}

void shim_sceRtcSetCurrentTick(GuestContext *ctx) {
    RET(sceRtcSetCurrentTick(GPTR(const OrbisRtcTick *, ctx->rdi)));
}

void shim_sceRtcSetDosTime(GuestContext *ctx) {
    RET(sceRtcSetDosTime(GPTR(OrbisRtcDateTime *, ctx->rdi), (uint32_t)ctx->rsi));
}

void shim_sceRtcSetTick(GuestContext *ctx) {
    RET(sceRtcSetTick(GPTR(OrbisRtcDateTime *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi)));
}

void shim_sceRtcSetTime_t(GuestContext *ctx) {
    RET(sceRtcSetTime_t(GPTR(OrbisRtcDateTime *, ctx->rdi), (int64_t)ctx->rsi, compiled_sdk_version(ctx)));
}

void shim_sceRtcSetWin32FileTime(GuestContext *ctx) {
    RET(sceRtcSetWin32FileTime(GPTR(OrbisRtcDateTime *, ctx->rdi), (int64_t)ctx->rsi));
}

void shim_sceRtcTickAddDays(GuestContext *ctx) {
    RET(sceRtcTickAddDays(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int32_t)ctx->rdx));
}

void shim_sceRtcTickAddHours(GuestContext *ctx) {
    RET(sceRtcTickAddHours(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int32_t)ctx->rdx));
}

void shim_sceRtcTickAddMicroseconds(GuestContext *ctx) {
    RET(sceRtcTickAddMicroseconds(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int64_t)ctx->rdx));
}

void shim_sceRtcTickAddMinutes(GuestContext *ctx) {
    RET(sceRtcTickAddMinutes(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int64_t)ctx->rdx));
}

void shim_sceRtcTickAddMonths(GuestContext *ctx) {
    RET(sceRtcTickAddMonths(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int32_t)ctx->rdx));
}

void shim_sceRtcTickAddSeconds(GuestContext *ctx) {
    RET(sceRtcTickAddSeconds(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int64_t)ctx->rdx));
}

void shim_sceRtcTickAddTicks(GuestContext *ctx) {
    RET(sceRtcTickAddTicks(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int64_t)ctx->rdx));
}

void shim_sceRtcTickAddWeeks(GuestContext *ctx) {
    RET(sceRtcTickAddWeeks(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int32_t)ctx->rdx));
}

void shim_sceRtcTickAddYears(GuestContext *ctx) {
    RET(sceRtcTickAddYears(GPTR(OrbisRtcTick *, ctx->rdi), GPTR(const OrbisRtcTick *, ctx->rsi), (int32_t)ctx->rdx));
}
