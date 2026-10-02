#include "ps4_move.h"
#include <stdio.h>
#include <string.h>

static bool g_move_initialized = false;
static int32_t g_next_move_handle = 0x30b0000;

int32_t sceMoveInit(void) {
    if (g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_ALREADY_INIT;
    }
    g_move_initialized = true;
    return 0;
}

int32_t sceMoveOpen(int32_t userId, int32_t type, int32_t index) {
    (void)userId;
    (void)type;
    (void)index;
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    g_next_move_handle += 0x100;
    return g_next_move_handle;
}

int32_t sceMoveClose(int32_t handle) {
    (void)handle;
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    return 0;
}

int32_t sceMoveTerm(void) {
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    g_move_initialized = false;
    return 0;
}

int32_t sceMoveGetDeviceInfo(int32_t handle, OrbisMoveDeviceInfo *info) {
    (void)handle;
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    if (!info) {
        return (int32_t)ORBIS_MOVE_ERROR_INVALID_ARG;
    }
    return ORBIS_MOVE_ERROR_NO_CONTROLLER_CONNECTED;
}

int32_t sceMoveReadStateLatest(int32_t handle, OrbisMoveData *data) {
    (void)handle;
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    if (!data) {
        return (int32_t)ORBIS_MOVE_ERROR_INVALID_ARG;
    }
    return ORBIS_MOVE_ERROR_NO_CONTROLLER_CONNECTED;
}

int32_t sceMoveReadStateRecent(int32_t handle, int64_t timestamp, OrbisMoveData *data, int32_t *out_count) {
    (void)handle;
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    if (timestamp < 0 || !data || !out_count) {
        return (int32_t)ORBIS_MOVE_ERROR_INVALID_ARG;
    }
    return ORBIS_MOVE_ERROR_NO_CONTROLLER_CONNECTED;
}

int32_t sceMoveGetExtensionPortInfo(int32_t handle, void *data) {
    (void)handle;
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    if (!data) {
        return (int32_t)ORBIS_MOVE_ERROR_INVALID_ARG;
    }
    return ORBIS_MOVE_ERROR_NO_CONTROLLER_CONNECTED;
}

int32_t sceMoveSetVibration(int32_t handle, uint8_t intensity) {
    (void)handle;
    (void)intensity;
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    return ORBIS_MOVE_ERROR_NO_CONTROLLER_CONNECTED;
}

int32_t sceMoveSetLightSphere(int32_t handle, uint8_t red, uint8_t green, uint8_t blue) {
    (void)handle;
    (void)red;
    (void)green;
    (void)blue;
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    return ORBIS_MOVE_ERROR_NO_CONTROLLER_CONNECTED;
}

int32_t sceMoveResetLightSphere(int32_t handle) {
    (void)handle;
    if (!g_move_initialized) {
        return (int32_t)ORBIS_MOVE_ERROR_NOT_INIT;
    }
    return 0;
}

void shim_sceMoveInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceMoveInit();
    SHIM_RETURN();
}

void shim_sceMoveOpen(GuestContext *ctx) {
    int32_t userId = (int32_t)ctx->rdi;
    int32_t type = (int32_t)ctx->rsi;
    int32_t index = (int32_t)ctx->rdx;
    ctx->rax = (uint64_t)(int64_t)sceMoveOpen(userId, type, index);
    SHIM_RETURN();
}

void shim_sceMoveClose(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceMoveClose(handle);
    SHIM_RETURN();
}

void shim_sceMoveTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceMoveTerm();
    SHIM_RETURN();
}

void shim_sceMoveGetDeviceInfo(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t infoGuest = ctx->rsi;
    OrbisMoveDeviceInfo *info = infoGuest ? (OrbisMoveDeviceInfo *)(ctx->mem_base + infoGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceMoveGetDeviceInfo(handle, info);
    SHIM_RETURN();
}

void shim_sceMoveReadStateLatest(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t dataGuest = ctx->rsi;
    OrbisMoveData *data = dataGuest ? (OrbisMoveData *)(ctx->mem_base + dataGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceMoveReadStateLatest(handle, data);
    SHIM_RETURN();
}

void shim_sceMoveReadStateRecent(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    int64_t timestamp = (int64_t)ctx->rsi;
    uint64_t dataGuest = ctx->rdx;
    uint64_t countGuest = ctx->rcx;
    OrbisMoveData *data = dataGuest ? (OrbisMoveData *)(ctx->mem_base + dataGuest) : NULL;
    int32_t *out_count = countGuest ? (int32_t *)(ctx->mem_base + countGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceMoveReadStateRecent(handle, timestamp, data, out_count);
    SHIM_RETURN();
}

void shim_sceMoveGetExtensionPortInfo(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t dataGuest = ctx->rsi;
    void *data = dataGuest ? (void *)(ctx->mem_base + dataGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceMoveGetExtensionPortInfo(handle, data);
    SHIM_RETURN();
}

void shim_sceMoveSetVibration(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint8_t intensity = (uint8_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceMoveSetVibration(handle, intensity);
    SHIM_RETURN();
}

void shim_sceMoveSetLightSphere(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint8_t r = (uint8_t)ctx->rsi;
    uint8_t g = (uint8_t)ctx->rdx;
    uint8_t b = (uint8_t)ctx->rcx;
    ctx->rax = (uint64_t)(int64_t)sceMoveSetLightSphere(handle, r, g, b);
    SHIM_RETURN();
}

void shim_sceMoveResetLightSphere(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceMoveResetLightSphere(handle);
    SHIM_RETURN();
}
