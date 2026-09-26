#ifndef PS4_SYSTEM_SERVICE_H
#define PS4_SYSTEM_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// System service parameter IDs
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG 1
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_DATE_FORMAT 2
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_FORMAT 3
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE 4
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_SUMMERTIME 5
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_SYSTEM_NAME 6
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_GAME_PARENTAL_LEVEL 7
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_ENTER_BUTTON_ASSIGN 1000

// Parameter values
#define ORBIS_SYSTEM_PARAM_LANG_ENGLISH_US 1
#define ORBIS_SYSTEM_PARAM_ENTER_BUTTON_CROSS 1

int32_t sceSystemServiceParamGetInt(int32_t paramId, int32_t *value);
int32_t sceSystemServiceParamGetString(int32_t paramId, char *buf, size_t bufSize);
int32_t sceSystemServiceHideSplashScreen(void);
int32_t sceSystemServiceGetStatus(void);
int32_t sceSystemServiceGetDisplaySafeAreaInfo(void *info);
int32_t sceSystemServiceReceiveEvent(void *event);

// Shims
void shim_sceSystemServiceParamGetInt(GuestContext *ctx);
void shim_sceSystemServiceParamGetString(GuestContext *ctx);
void shim_sceSystemServiceHideSplashScreen(GuestContext *ctx);
void shim_sceSystemServiceGetStatus(GuestContext *ctx);
void shim_sceSystemServiceGetDisplaySafeAreaInfo(GuestContext *ctx);
void shim_sceSystemServiceReceiveEvent(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SYSTEM_SERVICE_H
