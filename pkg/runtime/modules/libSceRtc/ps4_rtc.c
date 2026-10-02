#include "ps4_rtc.h"
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <errno.h>

int32_t sceRtcGetTickResolution(void) {
    return 1000000; // 1 MHz = 1 microsecond per tick
}

int32_t sceRtcGetCurrentTick(OrbisRtcTick *tick) {
    if (!tick) {
        return -EINVAL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tick->tick = ((uint64_t)tv.tv_sec * 1000000ULL) + (uint64_t)tv.tv_usec;
    return 0;
}

int32_t sceRtcGetCurrentClockLocalTime(OrbisDateTime *timeOut) {
    if (!timeOut) {
        return -EINVAL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t sec = tv.tv_sec;
    struct tm tm;
    localtime_r(&sec, &tm);

    timeOut->year = (uint16_t)(tm.tm_year + 1900);
    timeOut->month = (uint16_t)(tm.tm_mon + 1);
    timeOut->day = (uint16_t)tm.tm_mday;
    timeOut->hour = (uint16_t)tm.tm_hour;
    timeOut->minute = (uint16_t)tm.tm_min;
    timeOut->second = (uint16_t)tm.tm_sec;
    timeOut->microsecond = (uint32_t)tv.tv_usec;
    return 0;
}

int32_t sceRtcGetTick(const OrbisDateTime *timeIn, OrbisRtcTick *tick) {
    if (!timeIn || !tick) {
        return -EINVAL;
    }
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = (int)timeIn->year - 1900;
    tm.tm_mon = (int)timeIn->month - 1;
    tm.tm_mday = (int)timeIn->day;
    tm.tm_hour = (int)timeIn->hour;
    tm.tm_min = (int)timeIn->minute;
    tm.tm_sec = (int)timeIn->second;
    tm.tm_isdst = -1;

    time_t sec = mktime(&tm);
    if (sec == (time_t)-1) {
        return -EINVAL;
    }
    tick->tick = ((uint64_t)sec * 1000000ULL) + (uint64_t)timeIn->microsecond;
    return 0;
}

int32_t sceRtcSetTick(OrbisDateTime *timeOut, const OrbisRtcTick *tick) {
    if (!timeOut || !tick) {
        return -EINVAL;
    }
    time_t sec = (time_t)(tick->tick / 1000000ULL);
    uint32_t usec = (uint32_t)(tick->tick % 1000000ULL);

    struct tm tm;
    localtime_r(&sec, &tm);

    timeOut->year = (uint16_t)(tm.tm_year + 1900);
    timeOut->month = (uint16_t)(tm.tm_mon + 1);
    timeOut->day = (uint16_t)tm.tm_mday;
    timeOut->hour = (uint16_t)tm.tm_hour;
    timeOut->minute = (uint16_t)tm.tm_min;
    timeOut->second = (uint16_t)tm.tm_sec;
    timeOut->microsecond = usec;
    return 0;
}

int32_t sceRtcGetDayOfWeek(int32_t year, int32_t month, int32_t day) {
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return -EINVAL;
    }
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

int32_t sceRtcIsLeapYear(int32_t year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
}

// Guest ABI shims
void shim_sceRtcGetTickResolution(GuestContext *ctx) {
    int32_t ret = sceRtcGetTickResolution();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceRtcGetCurrentTick(GuestContext *ctx) {
    uint64_t tickGuest = ctx->rdi;
    OrbisRtcTick *tick = tickGuest ? (OrbisRtcTick *)(ctx->mem_base + tickGuest) : NULL;
    int32_t ret = sceRtcGetCurrentTick(tick);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceRtcGetCurrentClockLocalTime(GuestContext *ctx) {
    uint64_t timeGuest = ctx->rdi;
    OrbisDateTime *timeOut = timeGuest ? (OrbisDateTime *)(ctx->mem_base + timeGuest) : NULL;
    int32_t ret = sceRtcGetCurrentClockLocalTime(timeOut);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceRtcGetTick(GuestContext *ctx) {
    uint64_t timeGuest = ctx->rdi;
    uint64_t tickGuest = ctx->rsi;
    const OrbisDateTime *timeIn = timeGuest ? (const OrbisDateTime *)(ctx->mem_base + timeGuest) : NULL;
    OrbisRtcTick *tick = tickGuest ? (OrbisRtcTick *)(ctx->mem_base + tickGuest) : NULL;

    int32_t ret = sceRtcGetTick(timeIn, tick);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceRtcSetTick(GuestContext *ctx) {
    uint64_t timeGuest = ctx->rdi;
    uint64_t tickGuest = ctx->rsi;
    OrbisDateTime *timeOut = timeGuest ? (OrbisDateTime *)(ctx->mem_base + timeGuest) : NULL;
    const OrbisRtcTick *tick = tickGuest ? (const OrbisRtcTick *)(ctx->mem_base + tickGuest) : NULL;

    int32_t ret = sceRtcSetTick(timeOut, tick);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceRtcGetDayOfWeek(GuestContext *ctx) {
    int32_t year = (int32_t)ctx->rdi;
    int32_t month = (int32_t)ctx->rsi;
    int32_t day = (int32_t)ctx->rdx;

    int32_t ret = sceRtcGetDayOfWeek(year, month, day);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceRtcIsLeapYear(GuestContext *ctx) {
    int32_t year = (int32_t)ctx->rdi;
    int32_t ret = sceRtcIsLeapYear(year);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

int32_t sceRtcGetTime_t(const OrbisDateTime *pTime, time_t *llTime) {
    if (!pTime || !llTime) return -EINVAL;
    OrbisRtcTick tick;
    int32_t rc = sceRtcGetTick(pTime, &tick);
    if (rc != 0) return rc;
    *llTime = (time_t)(tick.tick / 1000000ULL);
    return 0;
}

int32_t sceRtcParseDateTime(OrbisRtcTick *pTickUtc, const char *pszDateTime) {
    if (!pTickUtc || !pszDateTime) return -EINVAL;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    char *res = strptime(pszDateTime, "%Y-%m-%dT%H:%M:%S", &tm);
    if (!res) {
        res = strptime(pszDateTime, "%Y-%m-%d %H:%M:%S", &tm);
    }
    if (!res) {
        res = strptime(pszDateTime, "%Y/%m/%d %H:%M:%S", &tm);
    }
    if (!res) {
        return -EINVAL;
    }
    time_t t = timegm(&tm);
    if (t == (time_t)-1) {
        t = mktime(&tm);
    }
    pTickUtc->tick = (uint64_t)t * 1000000ULL;
    return 0;
}

int32_t sceRtcGetCurrentNetworkTick(OrbisRtcTick *tick) {
    return sceRtcGetCurrentTick(tick);
}

void shim_sceRtcGetTime_t(GuestContext *ctx) {
    uint64_t timeIn = ctx->rdi;
    uint64_t timeOut = ctx->rsi;
    if (!timeIn || !timeOut) {
        ctx->rax = (uint64_t)-EINVAL;
        SHIM_RETURN();
    }
    OrbisDateTime dt;
    memcpy(&dt, ctx->mem_base + timeIn, sizeof(dt));
    time_t t = 0;
    int32_t rc = sceRtcGetTime_t(&dt, &t);
    if (rc == 0) {
        *(int64_t *)(ctx->mem_base + timeOut) = (int64_t)t;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceRtcParseDateTime(GuestContext *ctx) {
    uint64_t tickOut = ctx->rdi;
    uint64_t strIn = ctx->rsi;
    if (!tickOut || !strIn) {
        ctx->rax = (uint64_t)-EINVAL;
        SHIM_RETURN();
    }
    const char *str = (const char *)(ctx->mem_base + strIn);
    OrbisRtcTick tick;
    int32_t rc = sceRtcParseDateTime(&tick, str);
    if (rc == 0) {
        memcpy(ctx->mem_base + tickOut, &tick, sizeof(tick));
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceRtcGetCurrentNetworkTick(GuestContext *ctx) {
    shim_sceRtcGetCurrentTick(ctx);
}
