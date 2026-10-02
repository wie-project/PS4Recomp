#include "ps4_system_service.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

int32_t sceSystemServiceParamGetInt(int32_t paramId, int32_t *value) {
    if (!value) {
        return -EINVAL;
    }
    switch (paramId) {
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG:
        *value = ORBIS_SYSTEM_PARAM_LANG_ENGLISH_US; // 1 = English (US)
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_DATE_FORMAT:
        *value = 1; // DDMMYYYY
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_FORMAT:
        *value = 1; // 24Hour
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE:
        *value = 180; // UTC+3
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_SUMMERTIME:
        *value = 0;
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_GAME_PARENTAL_LEVEL:
        *value = 0; // Off
        break;
    case ORBIS_SYSTEM_SERVICE_PARAM_ID_ENTER_BUTTON_ASSIGN:
        *value = ORBIS_SYSTEM_PARAM_ENTER_BUTTON_CROSS; // 1 = Cross
        break;
    default:
        *value = 0;
        break;
    }
    return 0; // ORBIS_OK
}

int32_t sceSystemServiceParamGetString(int32_t paramId, char *buf, size_t bufSize) {
    (void)paramId;
    if (!buf || bufSize == 0) return -EINVAL;
    strncpy(buf, "PS4User", bufSize - 1);
    buf[bufSize - 1] = '\0';
    return 0;
}

int32_t sceSystemServiceHideSplashScreen(void) {
    return 0;
}

int32_t sceSystemServiceGetStatus(void) {
    return 0;
}

int32_t sceSystemServiceGetDisplaySafeAreaInfo(void *info) {
    if (!info) return -EINVAL;
    memset(info, 0, 32);
    return 0;
}

int32_t sceSystemServiceReceiveEvent(void *event) {
    (void)event;
    return 0;
}

// Shims
void shim_sceSystemServiceParamGetInt(GuestContext *ctx) {
    // SysV AMD64: RDI: paramId, RSI: valueGuestPtr
    int32_t paramId = (int32_t)ctx->rdi;
    uint64_t valGuestPtr = ctx->rsi;
    if (valGuestPtr == 0) {
        ctx->rax = (uint64_t)(int64_t)-EINVAL;
    } else if (ctx->mem_base && valGuestPtr + sizeof(int32_t) <= ctx->mem_size) {
        int32_t val = 0;
        int32_t rc = sceSystemServiceParamGetInt(paramId, &val);
        if (rc == 0) {
            *(int32_t *)(ctx->mem_base + valGuestPtr) = val;
        }
        ctx->rax = (uint64_t)(int64_t)rc;
    } else {
        ctx->rax = (uint64_t)(int64_t)-EFAULT;
    }
    SHIM_RETURN();
}

void shim_sceSystemServiceParamGetString(GuestContext *ctx) {
    // SysV AMD64: RDI: paramId, RSI: bufGuestPtr, RDX: bufSize
    int32_t paramId = (int32_t)ctx->rdi;
    uint64_t bufGuestPtr = ctx->rsi;
    size_t bufSize = (size_t)ctx->rdx;
    if (bufGuestPtr == 0 || bufSize == 0) {
        ctx->rax = (uint64_t)(int64_t)-EINVAL;
    } else if (ctx->mem_base && bufGuestPtr + bufSize <= ctx->mem_size) {
        char *hostBuf = (char *)(ctx->mem_base + bufGuestPtr);
        int32_t rc = sceSystemServiceParamGetString(paramId, hostBuf, bufSize);
        ctx->rax = (uint64_t)(int64_t)rc;
    } else {
        ctx->rax = (uint64_t)(int64_t)-EFAULT;
    }
    SHIM_RETURN();
}

void shim_sceSystemServiceHideSplashScreen(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceHideSplashScreen();
    SHIM_RETURN();
}

void shim_sceSystemServiceGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSystemServiceGetStatus();
    SHIM_RETURN();
}

void shim_sceSystemServiceGetDisplaySafeAreaInfo(GuestContext *ctx) {
    uint64_t infoGuestPtr = ctx->rdi;
    if (infoGuestPtr == 0) {
        ctx->rax = (uint64_t)(int64_t)-EINVAL;
    } else if (ctx->mem_base && infoGuestPtr + 32 <= ctx->mem_size) {
        void *infoHost = (void *)(ctx->mem_base + infoGuestPtr);
        ctx->rax = (uint64_t)(int64_t)sceSystemServiceGetDisplaySafeAreaInfo(infoHost);
    } else {
        ctx->rax = (uint64_t)(int64_t)-EFAULT;
    }
    SHIM_RETURN();
}

void shim_sceSystemServiceReceiveEvent(GuestContext *ctx) {
    uint64_t eventGuestPtr = ctx->rdi;
    if (eventGuestPtr == 0) {
        ctx->rax = (uint64_t)(int64_t)-EINVAL;
    } else if (ctx->mem_base && eventGuestPtr + 16 <= ctx->mem_size) {
        void *eventHost = (void *)(ctx->mem_base + eventGuestPtr);
        ctx->rax = (uint64_t)(int64_t)sceSystemServiceReceiveEvent(eventHost);
    } else {
        ctx->rax = (uint64_t)(int64_t)-EFAULT;
    }
    SHIM_RETURN();
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
    fprintf(stderr, "[ps4-recomp] sceSystemServiceReportAbnormalTermination: reason=%d\n", reason);
    return 0;
}

int32_t sceSystemServiceLoadExec(const char *path, const char *argv[]) {
    (void)path;
    (void)argv;
    fprintf(stderr, "[ps4-recomp] sceSystemServiceLoadExec called for path: %s\n", path ? path : "(null)");
    return 0;
}

int32_t sceSystemServiceDisablePersonalEyeToEyeDistanceSetting(void) {
    return 0;
}

int32_t sceSystemServiceEnablePersonalEyeToEyeDistanceSetting(void) {
    return 0;
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
