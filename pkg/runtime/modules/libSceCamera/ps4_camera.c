#include "ps4_camera.h"
#include <stdio.h>
#include <string.h>

void shim_sceCameraIsAttached(GuestContext *ctx) {
    // 0 = disconnected, 1 = connected
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCameraOpen(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_CAMERA_ERROR_NOT_CONNECTED;
    SHIM_RETURN();
}

void shim_sceCameraClose(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCameraStart(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_CAMERA_ERROR_NOT_CONNECTED;
    SHIM_RETURN();
}

void shim_sceCameraStop(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCameraIsValidFrameData(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCameraSetConfig(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCameraGetFrameData(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_CAMERA_ERROR_NOT_CONNECTED;
    SHIM_RETURN();
}

void shim_sceCameraSetVideoSync(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCameraGetDeviceCount(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}
