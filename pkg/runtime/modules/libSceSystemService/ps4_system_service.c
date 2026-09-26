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
