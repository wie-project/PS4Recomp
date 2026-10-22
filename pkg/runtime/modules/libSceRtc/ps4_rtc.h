#ifndef PS4_RTC_H
#define PS4_RTC_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

typedef struct OrbisRtcTick {
    uint64_t tick;
} OrbisRtcTick;

typedef struct OrbisDateTime {
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint32_t microsecond;
} OrbisDateTime;

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceRtcGetCurrentTick(OrbisRtcTick *tick);
int32_t sceRtcGetTick(const OrbisDateTime *time, OrbisRtcTick *tick);
int32_t sceRtcSetTick(OrbisDateTime *time, const OrbisRtcTick *tick);
int32_t sceRtcGetTickResolution(void);
int32_t sceRtcGetCurrentClockLocalTime(OrbisDateTime *time);
int32_t sceRtcGetDayOfWeek(int32_t year, int32_t month, int32_t day);
int32_t sceRtcIsLeapYear(int32_t year);
int32_t sceRtcGetTime_t(const OrbisDateTime *time, time_t *llTime);
int32_t sceRtcParseDateTime(OrbisRtcTick *pTickUtc, const char *pszDateTime);
int32_t sceRtcFormatRFC3339(char *pszDateTime, const OrbisRtcTick *pTickUtc, int iTimeZoneMinutes);
int32_t sceRtcParseRFC3339(OrbisRtcTick *pTickUtc, const char *pszDateTime);
int32_t sceRtcGetCurrentNetworkTick(OrbisRtcTick *tick);
int32_t sceRtcSetTime_t(OrbisDateTime *out, int64_t time, uint32_t sdk_version);
int32_t sceRtcFormatRFC3339LocalTime(char *out, const OrbisRtcTick *tick_utc);
// Ticks in this module count microseconds since the Unix epoch; real PS4 ticks (since year 1)
// are accepted as well.
int64_t ps4_rtc_tick_to_unix_us(uint64_t tick);
int32_t ps4_rtc_format_rfc3339(char *out, int64_t unix_us, int tz_minutes);

// Guest ABI shims
void shim_sceRtcGetCurrentTick(GuestContext *ctx);
void shim_sceRtcGetTick(GuestContext *ctx);
void shim_sceRtcSetTick(GuestContext *ctx);
void shim_sceRtcGetTickResolution(GuestContext *ctx);
void shim_sceRtcGetCurrentClockLocalTime(GuestContext *ctx);
void shim_sceRtcGetDayOfWeek(GuestContext *ctx);
void shim_sceRtcIsLeapYear(GuestContext *ctx);
void shim_sceRtcGetTime_t(GuestContext *ctx);
void shim_sceRtcParseDateTime(GuestContext *ctx);
void shim_sceRtcFormatRFC3339(GuestContext *ctx);
void shim_sceRtcParseRFC3339(GuestContext *ctx);
void shim_sceRtcGetCurrentNetworkTick(GuestContext *ctx);
void shim_sceRtcSetTime_t(GuestContext *ctx);
void shim_sceRtcFormatRFC3339LocalTime(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_RTC_H
