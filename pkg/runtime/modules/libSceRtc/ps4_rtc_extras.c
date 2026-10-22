// libSceRtc: sceRtcFormatRFC2822, sceRtcTickAddSeconds, sceRtcSetTime_t and
// sceRtcFormatRFC3339LocalTime.
//
// The rest of this module stores ticks as microseconds since the Unix epoch,
// while real PS4 ticks count microseconds since 0001-01-01. Formatting accepts
// both: values at or beyond the 1970 offset in PS4 ticks are converted.
#include "ps4_rtc.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define ORBIS_RTC_ERROR_INVALID_POINTER ((int32_t)0x80B50002)
#define ORBIS_RTC_ERROR_INVALID_VALUE   ((int32_t)0x80B50003)

#define RTC_UNIX_EPOCH_PS4_TICKS 62135596800000000ULL // 0001-01-01 -> 1970-01-01, in us

int64_t ps4_rtc_tick_to_unix_us(uint64_t tick) {
    if (tick >= RTC_UNIX_EPOCH_PS4_TICKS) return (int64_t)(tick - RTC_UNIX_EPOCH_PS4_TICKS);
    return (int64_t)tick;
}

// int sceRtcFormatRFC2822(char *pszDateTime, const SceRtcTick *pTickUtc, int iTimeZoneMinutes)
// Output: "Fri, 16 Oct 2026 18:13:44 +0300" (null pTickUtc = current time).
int32_t sceRtcFormatRFC2822(char *out, const OrbisRtcTick *tick_utc, int tz_minutes) {
    if (!out) return ORBIS_RTC_ERROR_INVALID_POINTER;
    int64_t us;
    if (tick_utc) {
        us = ps4_rtc_tick_to_unix_us(tick_utc->tick);
    } else {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        us = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
    }
    time_t secs = (time_t)(us / 1000000LL) + (time_t)tz_minutes * 60;
    struct tm tm;
    if (!gmtime_r(&secs, &tm)) return ORBIS_RTC_ERROR_INVALID_VALUE;
    static const char *const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int tz = tz_minutes;
    char sign = '+';
    if (tz < 0) {
        sign = '-';
        tz = -tz;
    }
    snprintf(out, 32, "%s, %02d %s %04d %02d:%02d:%02d %c%02d%02d",
             days[tm.tm_wday % 7], tm.tm_mday, months[tm.tm_mon % 12], tm.tm_year + 1900,
             tm.tm_hour, tm.tm_min, tm.tm_sec, sign, (tz / 60) % 100, tz % 60);
    return 0;
}

// int sceRtcTickAddSeconds(SceRtcTick *pTick0, const SceRtcTick *pTick1, int64_t lAdd)
int32_t sceRtcTickAddSeconds(OrbisRtcTick *dst, const OrbisRtcTick *src, int64_t add) {
    if (!dst || !src) return ORBIS_RTC_ERROR_INVALID_POINTER;
    dst->tick = src->tick + (uint64_t)(add * 1000000LL);
    return 0;
}

void shim_sceRtcFormatRFC2822(GuestContext *ctx) {
    char *out = ctx->rdi ? (char *)(ctx->mem_base + ctx->rdi) : NULL;
    const OrbisRtcTick *tick = ctx->rsi ? (const OrbisRtcTick *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceRtcFormatRFC2822(out, tick, (int)(int32_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceRtcTickAddSeconds(GuestContext *ctx) {
    OrbisRtcTick *dst = ctx->rdi ? (OrbisRtcTick *)(ctx->mem_base + ctx->rdi) : NULL;
    const OrbisRtcTick *src = ctx->rsi ? (const OrbisRtcTick *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceRtcTickAddSeconds(dst, src, (int64_t)ctx->rdx);
    SHIM_RETURN();
}

// ---------------------------------------------------------------------------------
// sceRtcSetTime_t
// ---------------------------------------------------------------------------------

#define RTC_SDK_NEW_TIME_T 0x3000000u // first SDK with the sign-checked time_t conversion

// Splits seconds since the Unix epoch into a UTC calendar date (proleptic Gregorian,
// Howard Hinnant's civil_from_days). Returns false outside years 1..9999.
static bool rtc_civil_from_unix(int64_t secs, OrbisDateTime *out) {
    int64_t days = secs / 86400;
    int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y += 1;
    if (y < 1 || y > 9999) return false;
    out->year = (uint16_t)y;
    out->month = (uint16_t)m;
    out->day = (uint16_t)d;
    out->hour = (uint16_t)(rem / 3600);
    out->minute = (uint16_t)(rem % 3600 / 60);
    out->second = (uint16_t)(rem % 60);
    out->microsecond = 0;
    return true;
}

// int sceRtcSetTime_t(SceRtcDateTime *pTime, time_t llTime)
//
// Converts seconds since 1970-01-01 UTC to a date and time. Behaviour as in shadPS4
// (libraries/rtc/rtc.cpp sceRtcSetTime_t, which mirrors libSceRtc): NULL output is
// INVALID_POINTER; titles built with an SDK older than 3.0 get the time as an unsigned 32-bit
// value, newer ones get INVALID_VALUE for a negative time. A time outside years 1..9999 is
// INVALID_VALUE as well. `sdk_version` is the compiled SDK version from the process param.
int32_t sceRtcSetTime_t(OrbisDateTime *out, int64_t time, uint32_t sdk_version) {
    if (!out) return ORBIS_RTC_ERROR_INVALID_POINTER;
    if (sdk_version < RTC_SDK_NEW_TIME_T) {
        time = (int64_t)((uint64_t)time & 0xffffffffULL);
    } else if (time < 0) {
        return ORBIS_RTC_ERROR_INVALID_VALUE;
    }
    OrbisDateTime dt;
    if (!rtc_civil_from_unix(time, &dt)) return ORBIS_RTC_ERROR_INVALID_VALUE;
    *out = dt;
    return 0;
}

// SceKernelProcParam: size u64, magic "ORBI" u32, entry count u32, SDK version u32 at +0x10.
static uint32_t rtc_compiled_sdk_version(const GuestContext *ctx) {
    const GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    if (!proc->proc_param_addr || !proc->mem_base || proc->proc_param_addr + 0x14 > proc->mem_size)
        return 0xffffffffu; // unknown: the current behaviour
    return *(const uint32_t *)(proc->mem_base + proc->proc_param_addr + 0x10);
}

void shim_sceRtcSetTime_t(GuestContext *ctx) {
    OrbisDateTime *out = ctx->rdi ? (OrbisDateTime *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceRtcSetTime_t(out, (int64_t)ctx->rsi, rtc_compiled_sdk_version(ctx));
    SHIM_RETURN();
}

// ---------------------------------------------------------------------------------
// sceRtcFormatRFC3339LocalTime
// ---------------------------------------------------------------------------------

// Formats `unix_us` shifted by `tz_minutes` (east of UTC) as the console does: the fraction is
// always present, in hundredths ("2026-10-19T12:34:56.78+03:00"), the zone is "Z" for 0.
// Format as in shadPS4 (rtc.cpp sceRtcFormatRFC3339Precise); the buffer needs 29 bytes.
int32_t ps4_rtc_format_rfc3339(char *out, int64_t unix_us, int tz_minutes) {
    int64_t us = unix_us % 1000000;
    int64_t secs = unix_us / 1000000;
    if (us < 0) {
        us += 1000000;
        secs -= 1;
    }
    OrbisDateTime dt;
    if (!rtc_civil_from_unix(secs + (int64_t)tz_minutes * 60, &dt)) return ORBIS_RTC_ERROR_INVALID_VALUE;
    int n = snprintf(out, 29, "%04u-%02u-%02uT%02u:%02u:%02u.%02u", dt.year, dt.month, dt.day, dt.hour,
                     dt.minute, dt.second, (unsigned)(us / 10000));
    if (tz_minutes == 0) {
        strcpy(out + n, "Z");
    } else {
        int tz = tz_minutes < 0 ? -tz_minutes : tz_minutes;
        snprintf(out + n, 7, "%c%02d:%02d", tz_minutes < 0 ? '-' : '+', tz / 60 % 100, tz % 60);
    }
    return 0;
}

// int sceRtcFormatRFC3339LocalTime(char *pszDateTime, const SceRtcTick *pTickUtc)
//
// RFC 3339 text of a UTC tick in the console's local time zone (NULL tick = now). The console
// takes the zone from sceKernelGettimezone (minutes west + DST minutes, shadPS4
// rtc.cpp sceRtcFormatRFC3339LocalTime); the host's zone at that instant is the equivalent
// here, DST included. A NULL output buffer is INVALID_POINTER.
int32_t sceRtcFormatRFC3339LocalTime(char *out, const OrbisRtcTick *tick_utc) {
    if (!out) return ORBIS_RTC_ERROR_INVALID_POINTER;
    int64_t us;
    if (tick_utc) {
        us = ps4_rtc_tick_to_unix_us(tick_utc->tick);
    } else {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        us = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
    }
    time_t secs = (time_t)(us >= 0 ? us / 1000000 : -((-us + 999999) / 1000000));
    struct tm tm;
    int tz_minutes = 0;
    if (localtime_r(&secs, &tm)) tz_minutes = (int)(tm.tm_gmtoff / 60);
    return ps4_rtc_format_rfc3339(out, us, tz_minutes);
}

void shim_sceRtcFormatRFC3339LocalTime(GuestContext *ctx) {
    char *out = ctx->rdi ? (char *)(ctx->mem_base + ctx->rdi) : NULL;
    const OrbisRtcTick *tick = ctx->rsi ? (const OrbisRtcTick *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceRtcFormatRFC3339LocalTime(out, tick);
    SHIM_RETURN();
}
