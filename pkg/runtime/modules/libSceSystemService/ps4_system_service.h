#ifndef PS4_SYSTEM_SERVICE_H
#define PS4_SYSTEM_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// SystemService error codes (matches PS4 SDK / shadps4)
#define ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER 0x80A10003
#define ORBIS_SYSTEM_SERVICE_ERROR_NO_EVENT   0x80A10004

// System service parameter IDs
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG 1
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_DATE_FORMAT 2
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_FORMAT 3
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE 4
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_SUMMERTIME 5
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_SYSTEM_NAME 6
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_GAME_PARENTAL_LEVEL 7
#define ORBIS_SYSTEM_SERVICE_PARAM_ID_ENTER_BUTTON_ASSIGN 1000

// Date formats
#define ORBIS_SYSTEM_PARAM_DATE_FORMAT_YYYYMMDD 0
#define ORBIS_SYSTEM_PARAM_DATE_FORMAT_DDMMYYYY 1
#define ORBIS_SYSTEM_PARAM_DATE_FORMAT_MMDDYYYY 2

// Time formats
#define ORBIS_SYSTEM_PARAM_TIME_FORMAT_12HOUR 0
#define ORBIS_SYSTEM_PARAM_TIME_FORMAT_24HOUR 1

// Enter button assignments
#define ORBIS_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE 0
#define ORBIS_SYSTEM_PARAM_ENTER_BUTTON_CROSS 1

// Structures
typedef struct OrbisSystemServiceStatus {
    int32_t event_num;
    bool is_system_ui_overlaid;
    bool is_in_background_execution;
    bool is_cpu_mode7_cpu_normal;
    bool is_game_live_streaming_on_air;
    bool is_out_of_vr_play_area;
    uint8_t reserved[10];
} OrbisSystemServiceStatus;

typedef struct OrbisSystemServiceDisplaySafeAreaInfo {
    float ratio;
    uint8_t reserved[128];
} OrbisSystemServiceDisplaySafeAreaInfo;

typedef struct OrbisSystemServiceEvent {
    int32_t event_type;
    uint8_t data[8192];
} OrbisSystemServiceEvent;

int32_t sceSystemServiceParamGetInt(int32_t paramId, int32_t *value);
int32_t sceSystemServiceParamGetString(int32_t paramId, char *buf, size_t bufSize);
int32_t sceSystemServiceHideSplashScreen(void);
int32_t sceSystemServiceGetStatus(OrbisSystemServiceStatus *status);
int32_t sceSystemServiceGetDisplaySafeAreaInfo(OrbisSystemServiceDisplaySafeAreaInfo *info);
int32_t sceSystemServiceReceiveEvent(OrbisSystemServiceEvent *event);
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
int32_t sceSystemServiceTurnOffScreenSaver(void);
int32_t sceSystemServiceIsScreenSaverOn(void);
int32_t sceSystemServiceIsAppSuspended(void);
int32_t sceSystemServiceIsBgmPlaying(void);
int32_t sceSystemServiceNavigateToGoHome(void);
int32_t sceSystemServiceNavigateToGoBack(void);
int32_t sceSystemServiceNavigateToGoBackWithValue(void);
int32_t sceSystemServiceNavigateToAnotherApp(void);
int32_t sceSystemServiceLaunchApp(void);
int32_t sceSystemServiceLaunchEventDetails(void);
int32_t sceSystemServiceLaunchTournamentList(void);
int32_t sceSystemServiceLaunchTournamentsTeamProfile(void);
int32_t sceSystemServiceLaunchWebBrowser(void);
int32_t sceSystemServiceSuspendLocalProcess(void);
int32_t sceSystemServiceResumeLocalProcess(void);
int32_t sceSystemServiceKillApp(void);
int32_t sceSystemServiceShowDisplaySafeAreaSettings(void);
int32_t sceSystemServiceShowEyeToEyeDistanceSetting(void);

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
void shim_sceSystemServiceTurnOffScreenSaver(GuestContext *ctx);
void shim_sceSystemServiceIsScreenSaverOn(GuestContext *ctx);
void shim_sceSystemServiceIsAppSuspended(GuestContext *ctx);
void shim_sceSystemServiceIsBgmPlaying(GuestContext *ctx);
void shim_sceSystemServiceNavigateToGoHome(GuestContext *ctx);
void shim_sceSystemServiceNavigateToGoBack(GuestContext *ctx);
void shim_sceSystemServiceNavigateToGoBackWithValue(GuestContext *ctx);
void shim_sceSystemServiceNavigateToAnotherApp(GuestContext *ctx);
void shim_sceSystemServiceLaunchApp(GuestContext *ctx);
void shim_sceSystemServiceLaunchEventDetails(GuestContext *ctx);
void shim_sceSystemServiceLaunchTournamentList(GuestContext *ctx);
void shim_sceSystemServiceLaunchTournamentsTeamProfile(GuestContext *ctx);
void shim_sceSystemServiceLaunchWebBrowser(GuestContext *ctx);
void shim_sceSystemServiceSuspendLocalProcess(GuestContext *ctx);
void shim_sceSystemServiceResumeLocalProcess(GuestContext *ctx);
void shim_sceSystemServiceKillApp(GuestContext *ctx);
void shim_sceSystemServiceShowDisplaySafeAreaSettings(GuestContext *ctx);
void shim_sceSystemServiceShowEyeToEyeDistanceSetting(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SYSTEM_SERVICE_H
