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
int32_t sceSystemServicePowerTick(void);
int32_t sceSystemServiceGetHdrToneMapLuminance(void *luminance);
int32_t sceSystemServiceDisableMusicPlayer(void);
int32_t sceSystemServiceReenableMusicPlayer(void);
int32_t sceSystemServiceDisableSuspendConfirmationDialog(void);
int32_t sceSystemServiceEnableSuspendConfirmationDialog(void);
int32_t sceSystemServiceShowControllerSettings(void *param);
int32_t sceSystemServiceReportAbnormalTermination(int32_t reason, void *data);
int32_t sceSystemServiceLoadExec(const char *path, const char *argv[]);
int32_t sceSystemServiceDisablePersonalEyeToEyeDistanceSetting(void);
int32_t sceSystemServiceEnablePersonalEyeToEyeDistanceSetting(void);

// Shims
void shim_sceSystemServiceParamGetInt(GuestContext *ctx);
void shim_sceSystemServiceParamGetString(GuestContext *ctx);
void shim_sceSystemServiceHideSplashScreen(GuestContext *ctx);
void shim_sceSystemServiceGetStatus(GuestContext *ctx);
void shim_sceSystemServiceGetDisplaySafeAreaInfo(GuestContext *ctx);
void shim_sceSystemServiceReceiveEvent(GuestContext *ctx);
void shim_sceSystemServicePowerTick(GuestContext *ctx);
void shim_sceSystemServiceGetHdrToneMapLuminance(GuestContext *ctx);
void shim_sceSystemServiceDisableMusicPlayer(GuestContext *ctx);
void shim_sceSystemServiceReenableMusicPlayer(GuestContext *ctx);
void shim_sceSystemServiceDisableSuspendConfirmationDialog(GuestContext *ctx);
void shim_sceSystemServiceEnableSuspendConfirmationDialog(GuestContext *ctx);
void shim_sceSystemServiceShowControllerSettings(GuestContext *ctx);
void shim_sceSystemServiceReportAbnormalTermination(GuestContext *ctx);
void shim_sceSystemServiceLoadExec(GuestContext *ctx);
void shim_sceSystemServiceDisablePersonalEyeToEyeDistanceSetting(GuestContext *ctx);
void shim_sceSystemServiceEnablePersonalEyeToEyeDistanceSetting(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SYSTEM_SERVICE_H
