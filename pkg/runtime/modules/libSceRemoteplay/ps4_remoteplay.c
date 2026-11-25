#include "ps4_remoteplay.h"
#include "ps4_hle_stub.h"
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

// Functions that shadPS4 stubs as well (src/core/libraries, libSceRemoteplay).
PS4_HLE_STUB(sceRemoteplayChangeEnterKey)
PS4_HLE_STUB(sceRemoteplayClearAllRegistData)
PS4_HLE_STUB(sceRemoteplayClearConnectHistory)
PS4_HLE_STUB(sceRemoteplayConfirmDeviceRegist)
PS4_HLE_STUB(sceRemoteplayDisconnect)
PS4_HLE_STUB(sceRemoteplayGeneratePinCode)
PS4_HLE_STUB(sceRemoteplayGetApMode)
PS4_HLE_STUB(sceRemoteplayGetConnectHistory)
PS4_HLE_STUB(sceRemoteplayGetConnectUserId)
PS4_HLE_STUB(sceRemoteplayGetMbusDeviceInfo)
PS4_HLE_STUB(sceRemoteplayGetOperationStatus)
PS4_HLE_STUB(sceRemoteplayGetRemoteplayStatus)
PS4_HLE_STUB(sceRemoteplayGetRpMode)
PS4_HLE_STUB(sceRemoteplayImeClose)
PS4_HLE_STUB(sceRemoteplayImeFilterResult)
PS4_HLE_STUB(sceRemoteplayImeGetEvent)
PS4_HLE_STUB(sceRemoteplayImeNotify)
PS4_HLE_STUB(sceRemoteplayImeNotifyEventResult)
PS4_HLE_STUB(sceRemoteplayImeOpen)
PS4_HLE_STUB(sceRemoteplayImeSetCaret)
PS4_HLE_STUB(sceRemoteplayImeSetText)
PS4_HLE_STUB(sceRemoteplayIsRemoteOskReady)
PS4_HLE_STUB(sceRemoteplayIsRemotePlaying)
PS4_HLE_STUB(sceRemoteplayNotifyMbusDeviceRegistComplete)
PS4_HLE_STUB(sceRemoteplayNotifyNpPushWakeup)
PS4_HLE_STUB(sceRemoteplayNotifyPinCodeError)
PS4_HLE_STUB(sceRemoteplayNotifyUserDelete)
PS4_HLE_STUB(sceRemoteplayPrintAllRegistData)
PS4_HLE_STUB(sceRemoteplayServerLock)
PS4_HLE_STUB(sceRemoteplayServerUnLock)
PS4_HLE_STUB(sceRemoteplaySetApMode)
PS4_HLE_STUB(sceRemoteplaySetLogLevel)
PS4_HLE_STUB(sceRemoteplaySetProhibition)
PS4_HLE_STUB(sceRemoteplaySetProhibitionForVsh)
PS4_HLE_STUB(sceRemoteplaySetRpMode)
PS4_HLE_STUB(Func_1D5EE365ED5FADB3)
