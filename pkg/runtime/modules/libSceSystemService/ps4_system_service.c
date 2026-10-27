#include "ps4_system_service.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

int ps4_host_choose_language(void); // ps4_settings.m

int32_t sceSystemServiceParamGetInt(int32_t paramId, int32_t *value) {
    if (!value) {
        return (int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    }
    switch (paramId) {
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG:
        *value = ps4_host_choose_language(); // PS4_LANG / saved choice / startup dialog
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_DATE_FORMAT:
        *value = ORBIS_SYSTEM_PARAM_DATE_FORMAT_DDMMYYYY;
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_FORMAT:
        *value = ORBIS_SYSTEM_PARAM_TIME_FORMAT_24HOUR;
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE: {
        time_t t = time(NULL);
        struct tm tm_val;
        localtime_r(&t, &tm_val);
        long dst_sec = (tm_val.tm_isdst > 0) ? 3600 : 0;
        long standard_offset_sec = tm_val.tm_gmtoff - dst_sec;
        *value = (int32_t)(standard_offset_sec / 60);
        break;
    }
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_SUMMERTIME: {
        time_t t = time(NULL);
        struct tm tm_val;
        localtime_r(&t, &tm_val);
        *value = (tm_val.tm_isdst > 0) ? 1 : 0;
        break;
    }
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_GAME_PARENTAL_LEVEL:
        *value = 0; // Off
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_ENTER_BUTTON_ASSIGN:
        *value = ORBIS_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
        break;
    default:
        fprintf(stderr, "[ps4-system] sceSystemServiceParamGetInt: paramId %d unsupported\n", paramId);
        *value = 0;
        break;
    }
    fprintf(stderr, "[ps4-system] sceSystemServiceParamGetInt: paramId=%d -> %d\n", paramId, *value);
    return 0; // ORBIS_OK
}

int32_t sceSystemServiceParamGetString(int32_t paramId, char *buf, size_t bufSize) {
    fprintf(stderr, "[ps4-system] sceSystemServiceParamGetString: paramId=%d, bufSize=%zu\n", paramId, bufSize);
    if (!buf || bufSize == 0) {
        return (int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    }
    if (paramId == ORBIS_SYSTEM_SERVICE_PARAM_ID_SYSTEM_NAME) {
        strncpy(buf, "PS4", bufSize - 1);
    } else {
        strncpy(buf, "PS4User", bufSize - 1);
    }
    buf[bufSize - 1] = '\0';
    return 0;
}

int32_t sceSystemServiceHideSplashScreen(void) {
    return 0;
}

int32_t sceSystemServiceGetStatus(OrbisSystemServiceStatus *status) {
    if (!status) {
        return (int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    }
    memset(status, 0, sizeof(*status));
    status->event_num = 0;
    status->is_system_ui_overlaid = false;
    status->is_in_background_execution = false;
    status->is_cpu_mode7_cpu_normal = true;
    status->is_game_live_streaming_on_air = false;
    status->is_out_of_vr_play_area = false;
    return 0;
}

int32_t sceSystemServiceGetDisplaySafeAreaInfo(OrbisSystemServiceDisplaySafeAreaInfo *info) {
    if (!info) {
        return (int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    }
    memset(info, 0, sizeof(*info));
    info->ratio = 1.0f;
    return 0;
}

int32_t sceSystemServiceReceiveEvent(OrbisSystemServiceEvent *event) {
    if (!event) {
        return (int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    }
    // No queued system events: return ORBIS_SYSTEM_SERVICE_ERROR_NO_EVENT (0x80A10004)
    return (int32_t)ORBIS_SYSTEM_SERVICE_ERROR_NO_EVENT;
}

int32_t sceSystemServicePowerTick(void) {
    return 0;
}

int32_t sceSystemServiceGetHdrToneMapLuminance(void *luminance) {
    if (luminance) {
        memset(luminance, 0, 32);
    }
    return 0;
}

int32_t sceSystemServiceDisableMusicPlayer(void) {
    return 0;
}

int32_t sceSystemServiceReenableMusicPlayer(void) {
    return 0;
}

int32_t sceSystemServiceDisableSuspendConfirmationDialog(void) {
    return 0;
}

int32_t sceSystemServiceEnableSuspendConfirmationDialog(void) {
    return 0;
}

int32_t sceSystemServiceShowControllerSettings(void *param) {
    (void)param;
    return 0;
}

int32_t sceSystemServiceReportAbnormalTermination(int32_t reason, void *data) {
    (void)reason;
    (void)data;
    fprintf(stderr, "[ps4-system] sceSystemServiceReportAbnormalTermination: reason=%d\n", reason);
    return 0;
}

int32_t sceSystemServiceLoadExec(const char *path, const char *argv[]) {
    (void)path;
    (void)argv;
    fprintf(stderr, "[ps4-system] sceSystemServiceLoadExec called for path: %s\n", path ? path : "(null)");
    return 0;
}

int32_t sceSystemServiceDisablePersonalEyeToEyeDistanceSetting(void) {
    return 0;
}

int32_t sceSystemServiceEnablePersonalEyeToEyeDistanceSetting(void) {
    return 0;
}

int32_t sceSystemServiceTurnOffScreenSaver(void) {
    return 0;
}

int32_t sceSystemServiceIsScreenSaverOn(void) {
    return 0;
}

int32_t sceSystemServiceIsAppSuspended(void) {
    return 0;
}

int32_t sceSystemServiceIsBgmPlaying(void) {
    return 0;
}

int32_t sceSystemServiceNavigateToGoHome(void) {
    return 0;
}

int32_t sceSystemServiceNavigateToGoBack(void) {
    return 0;
}

int32_t sceSystemServiceNavigateToGoBackWithValue(void) {
    return 0;
}

int32_t sceSystemServiceNavigateToAnotherApp(void) {
    return 0;
}

int32_t sceSystemServiceLaunchApp(void) {
    return 0;
}

int32_t sceSystemServiceLaunchEventDetails(void) {
    return 0;
}

int32_t sceSystemServiceLaunchTournamentList(void) {
    return 0;
}

int32_t sceSystemServiceLaunchTournamentsTeamProfile(void) {
    return 0;
}

int32_t sceSystemServiceLaunchWebBrowser(void) {
    return 0;
}

int32_t sceSystemServiceSuspendLocalProcess(void) {
    return 0;
}

int32_t sceSystemServiceResumeLocalProcess(void) {
    return 0;
}

int32_t sceSystemServiceKillApp(void) {
    return 0;
}

int32_t sceSystemServiceShowDisplaySafeAreaSettings(void) {
    return 0;
}

int32_t sceSystemServiceShowEyeToEyeDistanceSetting(void) {
    return 0;
}

// Shims
void shim_sceSystemServiceParamGetInt(GuestContext *ctx) {
    int32_t paramId = (int32_t)ctx->rdi;
    uint64_t valGuestPtr = ctx->rsi;
    if (valGuestPtr == 0) {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    } else if (ctx->mem_base && valGuestPtr + sizeof(int32_t) <= ctx->mem_size) {
        int32_t val = 0;
        int32_t rc = sceSystemServiceParamGetInt(paramId, &val);
        if (rc == 0) {
            *(int32_t *)(ctx->mem_base + valGuestPtr) = val;
        }
        ctx->rax = (uint64_t)(int64_t)rc;
    } else {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    }
    SHIM_RETURN();
}

void shim_sceSystemServiceParamGetString(GuestContext *ctx) {
    int32_t paramId = (int32_t)ctx->rdi;
    uint64_t bufGuestPtr = ctx->rsi;
    size_t bufSize = (size_t)ctx->rdx;
    if (bufGuestPtr == 0 || bufSize == 0) {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    } else if (ctx->mem_base && bufGuestPtr + bufSize <= ctx->mem_size) {
        char *hostBuf = (char *)(ctx->mem_base + bufGuestPtr);
        int32_t rc = sceSystemServiceParamGetString(paramId, hostBuf, bufSize);
        ctx->rax = (uint64_t)(int64_t)rc;
    } else {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    }
    SHIM_RETURN();
}

void shim_sceSystemServiceHideSplashScreen(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceHideSplashScreen();
    SHIM_RETURN();
}

void shim_sceSystemServiceGetStatus(GuestContext *ctx) {
    uint64_t p = ctx->rdi;
    if (p == 0 || !ctx->mem_base || p + sizeof(OrbisSystemServiceStatus) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    OrbisSystemServiceStatus *status = (OrbisSystemServiceStatus *)(ctx->mem_base + p);
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceGetStatus(status);
    SHIM_RETURN();
}

void shim_sceSystemServiceGetDisplaySafeAreaInfo(GuestContext *ctx) {
    uint64_t infoGuestPtr = ctx->rdi;
    if (infoGuestPtr == 0) {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    } else if (ctx->mem_base && infoGuestPtr + sizeof(OrbisSystemServiceDisplaySafeAreaInfo) <= ctx->mem_size) {
        OrbisSystemServiceDisplaySafeAreaInfo *infoHost =
            (OrbisSystemServiceDisplaySafeAreaInfo *)(ctx->mem_base + infoGuestPtr);
        ctx->rax = (uint64_t)(int64_t)sceSystemServiceGetDisplaySafeAreaInfo(infoHost);
    } else {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    }
    SHIM_RETURN();
}

void shim_sceSystemServiceReceiveEvent(GuestContext *ctx) {
    uint64_t eventGuestPtr = ctx->rdi;
    if (eventGuestPtr == 0) {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    } else if (ctx->mem_base && eventGuestPtr + sizeof(OrbisSystemServiceEvent) <= ctx->mem_size) {
        OrbisSystemServiceEvent *eventHost = (OrbisSystemServiceEvent *)(ctx->mem_base + eventGuestPtr);
        ctx->rax = (uint64_t)(int64_t)sceSystemServiceReceiveEvent(eventHost);
    } else {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_SYSTEM_SERVICE_ERROR_PARAMETER;
    }
    SHIM_RETURN();
}

void shim_sceSystemServicePowerTick(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServicePowerTick();
    SHIM_RETURN();
}

void shim_sceSystemServiceGetHdrToneMapLuminance(GuestContext *ctx) {
    uint64_t lumGuest = ctx->rdi;
    void *luminance = lumGuest ? (void *)(ctx->mem_base + lumGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceGetHdrToneMapLuminance(luminance);
    SHIM_RETURN();
}

void shim_sceSystemServiceDisableMusicPlayer(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceDisableMusicPlayer();
    SHIM_RETURN();
}

void shim_sceSystemServiceReenableMusicPlayer(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceReenableMusicPlayer();
    SHIM_RETURN();
}

void shim_sceSystemServiceDisableSuspendConfirmationDialog(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceDisableSuspendConfirmationDialog();
    SHIM_RETURN();
}

void shim_sceSystemServiceEnableSuspendConfirmationDialog(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceEnableSuspendConfirmationDialog();
    SHIM_RETURN();
}

void shim_sceSystemServiceShowControllerSettings(GuestContext *ctx) {
    uint64_t paramGuest = ctx->rdi;
    void *param = paramGuest ? (void *)(ctx->mem_base + paramGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceShowControllerSettings(param);
    SHIM_RETURN();
}

void shim_sceSystemServiceReportAbnormalTermination(GuestContext *ctx) {
    int32_t reason = (int32_t)ctx->rdi;
    uint64_t dataGuest = ctx->rsi;
    void *data = dataGuest ? (void *)(ctx->mem_base + dataGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceReportAbnormalTermination(reason, data);
    SHIM_RETURN();
}

void shim_sceSystemServiceLoadExec(GuestContext *ctx) {
    uint64_t pathGuest = ctx->rdi;
    uint64_t argvGuest = ctx->rsi;
    const char *path = pathGuest ? (const char *)(ctx->mem_base + pathGuest) : NULL;
    const char **argv = argvGuest ? (const char **)(ctx->mem_base + argvGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceLoadExec(path, argv);
    SHIM_RETURN();
}

void shim_sceSystemServiceDisablePersonalEyeToEyeDistanceSetting(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceDisablePersonalEyeToEyeDistanceSetting();
    SHIM_RETURN();
}

void shim_sceSystemServiceEnablePersonalEyeToEyeDistanceSetting(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceEnablePersonalEyeToEyeDistanceSetting();
    SHIM_RETURN();
}

void shim_sceSystemServiceTurnOffScreenSaver(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceTurnOffScreenSaver();
    SHIM_RETURN();
}

void shim_sceSystemServiceIsScreenSaverOn(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceIsScreenSaverOn();
    SHIM_RETURN();
}

void shim_sceSystemServiceIsAppSuspended(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceIsAppSuspended();
    SHIM_RETURN();
}

void shim_sceSystemServiceIsBgmPlaying(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceIsBgmPlaying();
    SHIM_RETURN();
}

void shim_sceSystemServiceNavigateToGoHome(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceNavigateToGoHome();
    SHIM_RETURN();
}

void shim_sceSystemServiceNavigateToGoBack(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceNavigateToGoBack();
    SHIM_RETURN();
}

void shim_sceSystemServiceNavigateToGoBackWithValue(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceNavigateToGoBackWithValue();
    SHIM_RETURN();
}

void shim_sceSystemServiceNavigateToAnotherApp(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceNavigateToAnotherApp();
    SHIM_RETURN();
}

void shim_sceSystemServiceLaunchApp(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceLaunchApp();
    SHIM_RETURN();
}

void shim_sceSystemServiceLaunchEventDetails(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceLaunchEventDetails();
    SHIM_RETURN();
}

void shim_sceSystemServiceLaunchTournamentList(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceLaunchTournamentList();
    SHIM_RETURN();
}

void shim_sceSystemServiceLaunchTournamentsTeamProfile(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceLaunchTournamentsTeamProfile();
    SHIM_RETURN();
}

void shim_sceSystemServiceLaunchWebBrowser(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceLaunchWebBrowser();
    SHIM_RETURN();
}

void shim_sceSystemServiceSuspendLocalProcess(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceSuspendLocalProcess();
    SHIM_RETURN();
}

void shim_sceSystemServiceResumeLocalProcess(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceResumeLocalProcess();
    SHIM_RETURN();
}

void shim_sceSystemServiceKillApp(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceKillApp();
    SHIM_RETURN();
}

void shim_sceSystemServiceShowDisplaySafeAreaSettings(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceShowDisplaySafeAreaSettings();
    SHIM_RETURN();
}

void shim_sceSystemServiceShowEyeToEyeDistanceSetting(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceShowEyeToEyeDistanceSetting();
    SHIM_RETURN();
}
