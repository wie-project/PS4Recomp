#include "ps4_remoteplay.h"
#include <string.h>
#include <errno.h>

int32_t sceRemoteplayInitialize(void) {
    return 0;
}

int32_t sceRemoteplayApprove(void) {
    return 0;
}

int32_t sceRemoteplayGetConnectionStatus(int32_t userId, int32_t *status) {
    (void)userId;
    if (!status) {
        return -EINVAL;
    }
    *status = ORBIS_REMOTEPLAY_CONNECTION_STATUS_DISCONNECT;
    return 0;
}

int32_t sceRemoteplayProhibit(int32_t mode) {
    (void)mode;
    return 0;
}

int32_t sceRemoteplayProhibitStreaming(int32_t mode) {
    (void)mode;
    return 0;
}

void shim_sceRemoteplayInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceRemoteplayInitialize();
    SHIM_RETURN();
}

void shim_sceRemoteplayApprove(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceRemoteplayApprove();
    SHIM_RETURN();
}

void shim_sceRemoteplayGetConnectionStatus(GuestContext *ctx) {
    int32_t userId = (int32_t)ctx->rdi;
    uint64_t statusGuest = ctx->rsi;
    int32_t *status = statusGuest ? (int32_t *)(ctx->mem_base + statusGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceRemoteplayGetConnectionStatus(userId, status);
    SHIM_RETURN();
}

void shim_sceRemoteplayProhibit(GuestContext *ctx) {
    int32_t mode = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceRemoteplayProhibit(mode);
    SHIM_RETURN();
}

void shim_sceRemoteplayProhibitStreaming(GuestContext *ctx) {
    int32_t mode = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceRemoteplayProhibitStreaming(mode);
    SHIM_RETURN();
}
