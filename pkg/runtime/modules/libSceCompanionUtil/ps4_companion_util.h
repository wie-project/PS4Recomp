// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_COMPANION_UTIL_H
#define PS4_COMPANION_UTIL_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_COMPANION_UTIL_INVALID_ARGUMENT ((int32_t)0x80AD0004)
#define ORBIS_COMPANION_UTIL_INVALID_POINTER ((int32_t)0x80AD0006)
#define ORBIS_COMPANION_UTIL_NO_EVENT ((int32_t)0x80AD0008)

// No second-screen app connects, so there is never an event (shadPS4 likewise).
int32_t sceCompanionUtilGetEvent(void *event);

void shim_sceCompanionUtilGetEvent(GuestContext *ctx);
void shim_sceCompanionUtilGetRemoteOskEvent(GuestContext *ctx);
void shim_sceCompanionUtilInitialize(GuestContext *ctx);
void shim_sceCompanionUtilOptParamInitialize(GuestContext *ctx);
void shim_sceCompanionUtilTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_COMPANION_UTIL_H
