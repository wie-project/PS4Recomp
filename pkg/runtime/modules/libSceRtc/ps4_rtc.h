#ifndef PS4_RTC_H
#define PS4_RTC_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "recomp_runtime.h"

#define ORBIS_RTC_ERROR_DATETIME_UNINITIALIZED ((int32_t)0x7FFEF9FE)
#define ORBIS_RTC_ERROR_INVALID_PARAMETER ((int32_t)0x80010602)
#define ORBIS_RTC_ERROR_INVALID_TICK_PARAMETER ((int32_t)0x80010603)
#define ORBIS_RTC_ERROR_INVALID_DATE_PARAMETER ((int32_t)0x80010604)
#define ORBIS_RTC_ERROR_NOT_IMPLEMENTED ((int32_t)0x80010605)
#define ORBIS_RTC_ERROR_INVALID_TIMEZONE_FORMAT ((int32_t)0x80010607)
#define ORBIS_RTC_ERROR_NOT_INITIALIZED ((int32_t)0x80B50001)
#define ORBIS_RTC_ERROR_INVALID_POINTER ((int32_t)0x80B50002)
#define ORBIS_RTC_ERROR_INVALID_VALUE ((int32_t)0x80B50003)
#define ORBIS_RTC_ERROR_INVALID_ARG ((int32_t)0x80B50004)
#define ORBIS_RTC_ERROR_NOT_SUPPORTED ((int32_t)0x80B50005)
#define ORBIS_RTC_ERROR_NO_CLOCK ((int32_t)0x80B50006)
#define ORBIS_RTC_ERROR_BAD_PARSE ((int32_t)0x80B50007)
#define ORBIS_RTC_ERROR_INVALID_YEAR ((int32_t)0x80B50008)
#define ORBIS_RTC_ERROR_INVALID_MONTH ((int32_t)0x80B50009)
#define ORBIS_RTC_ERROR_INVALID_DAY ((int32_t)0x80B5000A)
#define ORBIS_RTC_ERROR_INVALID_HOUR ((int32_t)0x80B5000B)
#define ORBIS_RTC_ERROR_INVALID_MINUTE ((int32_t)0x80B5000C)
#define ORBIS_RTC_ERROR_INVALID_SECOND ((int32_t)0x80B5000D)
#define ORBIS_RTC_ERROR_INVALID_MICROSECOND ((int32_t)0x80B5000E)

// Ticks count microseconds since 0001-01-01 00:00:00 UTC.
#define ORBIS_RTC_UNIX_EPOCH_TICKS 0xdcbffeff2bc000ULL
#define ORBIS_RTC_WIN32_FILETIME_EPOCH_TICKS 0xb36168b6a58000ULL

// First SDK version whose sceRtcSetTime_t and sceRtcGetDayOfWeek apply the newer range checks.
#define ORBIS_RTC_SDK_RANGE_CHECKS 0x3000000u

typedef struct OrbisRtcTick {
    uint64_t tick;
} OrbisRtcTick;

typedef struct OrbisRtcDateTime {
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint32_t microsecond;
} OrbisRtcDateTime;
_Static_assert(sizeof(OrbisRtcDateTime) == 16, "OrbisRtcDateTime layout");

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceRtcCheckValid(const OrbisRtcDateTime *pTime);
int32_t sceRtcCompareTick(const OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2);
int32_t sceRtcConvertLocalTimeToUtc(const OrbisRtcTick *pTickLocal, OrbisRtcTick *pTickUtc);
int32_t sceRtcConvertUtcToLocalTime(const OrbisRtcTick *pTickUtc, OrbisRtcTick *pTickLocal);
int32_t sceRtcEnd(void);
int32_t sceRtcFormatRFC2822(char *pszDateTime, const OrbisRtcTick *pTickUtc, int32_t iTimeZoneMinutes);
int32_t sceRtcFormatRFC2822LocalTime(char *pszDateTime, const OrbisRtcTick *pTickUtc);
int32_t sceRtcFormatRFC3339(char *pszDateTime, const OrbisRtcTick *pTickUtc, int32_t iTimeZoneMinutes);
int32_t sceRtcFormatRFC3339LocalTime(char *pszDateTime, const OrbisRtcTick *pTickUtc);
int32_t sceRtcFormatRFC3339Precise(char *pszDateTime, const OrbisRtcTick *pTickUtc, int32_t iTimeZoneMinutes);
int32_t sceRtcFormatRFC3339PreciseLocalTime(char *pszDateTime, const OrbisRtcTick *pTickUtc);
int32_t sceRtcGetCurrentAdNetworkTick(OrbisRtcTick *pTick);
int32_t sceRtcGetCurrentClock(OrbisRtcDateTime *pTime, int32_t timeZone);
int32_t sceRtcGetCurrentClockLocalTime(OrbisRtcDateTime *pTime);
int32_t sceRtcGetCurrentDebugNetworkTick(OrbisRtcTick *pTick);
int32_t sceRtcGetCurrentNetworkTick(OrbisRtcTick *pTick);
int32_t sceRtcGetCurrentRawNetworkTick(OrbisRtcTick *pTick);
int32_t sceRtcGetCurrentTick(OrbisRtcTick *pTick);
// sdk_version: the title's compiled SDK version (0xffffffff when unknown).
int32_t sceRtcGetDayOfWeek(int32_t year, int32_t month, int32_t day, uint32_t sdk_version);
int32_t sceRtcGetDaysInMonth(int32_t year, int32_t month);
int32_t sceRtcGetDosTime(const OrbisRtcDateTime *pTime, uint32_t *dosTime);
int32_t sceRtcGetTick(const OrbisRtcDateTime *pTime, OrbisRtcTick *pTick);
uint32_t sceRtcGetTickResolution(void);
int32_t sceRtcGetTime_t(const OrbisRtcDateTime *pTime, int64_t *llTime);
int32_t sceRtcGetWin32FileTime(const OrbisRtcDateTime *pTime, uint64_t *ulWin32Time);
int32_t sceRtcInit(void);
int32_t sceRtcIsLeapYear(int32_t year);
int32_t sceRtcParseDateTime(OrbisRtcTick *pTickUtc, const char *pszDateTime);
int32_t sceRtcParseRFC3339(OrbisRtcTick *pTickUtc, const char *pszDateTime);
int32_t sceRtcSetCurrentAdNetworkTick(const OrbisRtcTick *pTick);
int32_t sceRtcSetCurrentDebugNetworkTick(const OrbisRtcTick *pTick);
int32_t sceRtcSetCurrentNetworkTick(const OrbisRtcTick *pTick);
int32_t sceRtcSetCurrentTick(const OrbisRtcTick *pTick);
int32_t sceRtcSetDosTime(OrbisRtcDateTime *pTime, uint32_t dosTime);
int32_t sceRtcSetTick(OrbisRtcDateTime *pTime, const OrbisRtcTick *pTick);
int32_t sceRtcSetTime_t(OrbisRtcDateTime *pTime, int64_t llTime, uint32_t sdk_version);
int32_t sceRtcSetWin32FileTime(OrbisRtcDateTime *pTime, int64_t ulWin32Time);
int32_t sceRtcTickAddDays(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd);
int32_t sceRtcTickAddHours(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd);
int32_t sceRtcTickAddMicroseconds(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int64_t lAdd);
int32_t sceRtcTickAddMinutes(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int64_t lAdd);
int32_t sceRtcTickAddMonths(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd);
int32_t sceRtcTickAddSeconds(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int64_t lAdd);
int32_t sceRtcTickAddTicks(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int64_t lAdd);
int32_t sceRtcTickAddWeeks(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd);
int32_t sceRtcTickAddYears(OrbisRtcTick *pTick1, const OrbisRtcTick *pTick2, int32_t lAdd);

// Guest ABI shims
void shim_sceRtcCheckValid(GuestContext *ctx);
void shim_sceRtcCompareTick(GuestContext *ctx);
void shim_sceRtcConvertLocalTimeToUtc(GuestContext *ctx);
void shim_sceRtcConvertUtcToLocalTime(GuestContext *ctx);
void shim_sceRtcEnd(GuestContext *ctx);
void shim_sceRtcFormatRFC2822(GuestContext *ctx);
void shim_sceRtcFormatRFC2822LocalTime(GuestContext *ctx);
void shim_sceRtcFormatRFC3339(GuestContext *ctx);
void shim_sceRtcFormatRFC3339LocalTime(GuestContext *ctx);
void shim_sceRtcFormatRFC3339Precise(GuestContext *ctx);
void shim_sceRtcFormatRFC3339PreciseLocalTime(GuestContext *ctx);
void shim_sceRtcGetCurrentAdNetworkTick(GuestContext *ctx);
void shim_sceRtcGetCurrentClock(GuestContext *ctx);
void shim_sceRtcGetCurrentClockLocalTime(GuestContext *ctx);
void shim_sceRtcGetCurrentDebugNetworkTick(GuestContext *ctx);
void shim_sceRtcGetCurrentNetworkTick(GuestContext *ctx);
void shim_sceRtcGetCurrentRawNetworkTick(GuestContext *ctx);
void shim_sceRtcGetCurrentTick(GuestContext *ctx);
void shim_sceRtcGetDayOfWeek(GuestContext *ctx);
void shim_sceRtcGetDaysInMonth(GuestContext *ctx);
void shim_sceRtcGetDosTime(GuestContext *ctx);
void shim_sceRtcGetTick(GuestContext *ctx);
void shim_sceRtcGetTickResolution(GuestContext *ctx);
void shim_sceRtcGetTime_t(GuestContext *ctx);
void shim_sceRtcGetWin32FileTime(GuestContext *ctx);
void shim_sceRtcInit(GuestContext *ctx);
void shim_sceRtcIsLeapYear(GuestContext *ctx);
void shim_sceRtcParseDateTime(GuestContext *ctx);
void shim_sceRtcParseRFC3339(GuestContext *ctx);
void shim_sceRtcSetConf(GuestContext *ctx);
void shim_sceRtcSetCurrentAdNetworkTick(GuestContext *ctx);
void shim_sceRtcSetCurrentDebugNetworkTick(GuestContext *ctx);
void shim_sceRtcSetCurrentNetworkTick(GuestContext *ctx);
void shim_sceRtcSetCurrentTick(GuestContext *ctx);
void shim_sceRtcSetDosTime(GuestContext *ctx);
void shim_sceRtcSetTick(GuestContext *ctx);
void shim_sceRtcSetTime_t(GuestContext *ctx);
void shim_sceRtcSetWin32FileTime(GuestContext *ctx);
void shim_sceRtcTickAddDays(GuestContext *ctx);
void shim_sceRtcTickAddHours(GuestContext *ctx);
void shim_sceRtcTickAddMicroseconds(GuestContext *ctx);
void shim_sceRtcTickAddMinutes(GuestContext *ctx);
void shim_sceRtcTickAddMonths(GuestContext *ctx);
void shim_sceRtcTickAddSeconds(GuestContext *ctx);
void shim_sceRtcTickAddTicks(GuestContext *ctx);
void shim_sceRtcTickAddWeeks(GuestContext *ctx);
void shim_sceRtcTickAddYears(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_RTC_H
