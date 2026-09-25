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

// Guest ABI shims
void shim_sceRtcGetCurrentTick(GuestContext *ctx);
void shim_sceRtcGetTick(GuestContext *ctx);
void shim_sceRtcSetTick(GuestContext *ctx);
void shim_sceRtcGetTickResolution(GuestContext *ctx);
void shim_sceRtcGetCurrentClockLocalTime(GuestContext *ctx);
void shim_sceRtcGetDayOfWeek(GuestContext *ctx);
void shim_sceRtcIsLeapYear(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_RTC_H
