#include "ps4_hmd.h"
#include <stdio.h>
#include <string.h>

static int g_hmd_initialized = 0;

void shim_sceHmdInitialize(GuestContext *ctx) {
    g_hmd_initialized = 1;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdInitialize315(GuestContext *ctx) {
    g_hmd_initialized = 1;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdTerminate(GuestContext *ctx) {
    g_hmd_initialized = 0;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdOpen(GuestContext *ctx) {
    // When no PSVR headset is connected, return ORBIS_HMD_ERROR_DEVICE_DISCONNECTED (0x81110004)
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_HMD_ERROR_DEVICE_DISCONNECTED;
    SHIM_RETURN();
}

void shim_sceHmdClose(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdGetDeviceInformation(GuestContext *ctx) {
    if (ctx->rdi && ctx->mem_base) {
        OrbisHmdDeviceInformation *info = (OrbisHmdDeviceInformation *)(ctx->mem_base + ctx->rdi);
        memset(info, 0, sizeof(*info));
        info->status = ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED;
        info->user_id = -1;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdGetDeviceInformationByHandle(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_HMD_ERROR_HANDLE_INVALID;
    SHIM_RETURN();
}

void shim_sceHmdGetFieldOfView(GuestContext *ctx) {
    if (ctx->rsi && ctx->mem_base) {
        OrbisHmdFieldOfView *fov = (OrbisHmdFieldOfView *)(ctx->mem_base + ctx->rsi);
        fov->left = -0.85f;
        fov->right = 0.85f;
        fov->top = 0.85f;
        fov->bottom = -0.85f;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdGet2DEyeOffset(GuestContext *ctx) {
    if (ctx->rsi && ctx->rdx && ctx->mem_base) {
        OrbisHmdEyeOffset *left = (OrbisHmdEyeOffset *)(ctx->mem_base + ctx->rsi);
        OrbisHmdEyeOffset *right = (OrbisHmdEyeOffset *)(ctx->mem_base + ctx->rdx);
        left->offset_x = -0.0315f; left->offset_y = 0.0f; left->offset_z = 0.0f;
        right->offset_x = 0.0315f; right->offset_y = 0.0f; right->offset_z = 0.0f;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdGetAssyError(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_HMD_ERROR_DEVICE_DISCONNECTED;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionInitialize(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionFinalize(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionQueryGarlicBuffSize(GuestContext *ctx) {
    if (ctx->rdi && ctx->mem_base) {
        *(size_t *)(ctx->mem_base + ctx->rdi) = 0x200000;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionQueryGarlicBuffAlign(GuestContext *ctx) {
    if (ctx->rdi && ctx->mem_base) {
        *(size_t *)(ctx->mem_base + ctx->rdi) = 0x10000;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionQueryOnionBuffSize(GuestContext *ctx) {
    if (ctx->rdi && ctx->mem_base) {
        *(size_t *)(ctx->mem_base + ctx->rdi) = 0x200000;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionQueryOnionBuffAlign(GuestContext *ctx) {
    if (ctx->rdi && ctx->mem_base) {
        *(size_t *)(ctx->mem_base + ctx->rdi) = 0x10000;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionSetDisplayBuffers(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionUnsetDisplayBuffers(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionStart2dVr(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionStartMultilayer(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionStop(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionSetUserEventStart(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionSetUserEventEnd(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionClearUserEventStart(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdReprojectionSetOutputMinColor(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// libSceHmdSetupDialog
void shim_sceHmdSetupDialogInitialize(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdSetupDialogOpen(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdSetupDialogClose(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdSetupDialogGetResult(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdSetupDialogUpdateStatus(GuestContext *ctx) {
    // 2 = Finished/Closed dialog
    ctx->rax = 2;
    SHIM_RETURN();
}

void shim_sceHmdSetupDialogTerminate(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}
