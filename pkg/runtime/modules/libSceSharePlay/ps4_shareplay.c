#include "ps4_shareplay.h"
#include "ps4_hle_stub.h"
#include <stdbool.h>
#include <string.h>

static bool g_shareplay_initialized = false;
static int32_t g_shareplay_prohibited = 0;

int32_t sceSharePlayInitialize(void) {
    g_shareplay_initialized = true;
    return 0;
}

int32_t sceSharePlayTerminate(void) {
    g_shareplay_initialized = false;
    return 0;
}

int32_t sceSharePlaySetProhibition(int32_t prohibited) {
    g_shareplay_prohibited = prohibited;
    return 0;
}

int32_t sceSharePlayGetCurrentConnectionInfo(OrbisSharePlayConnectionInfo *pInfo) {
    if (!pInfo) {
        return ORBIS_SHARE_PLAY_ERROR_INVALID_ARGUMENT;
    }
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->status = ORBIS_SHARE_PLAY_CONNECTION_STATUS_DORMANT;
    return 0;
}

// Share Play sessions are never hosted: the connection is always dormant.
int32_t sceSharePlayGetCurrentConnectionInfoA(OrbisSharePlayConnectionInfoA *pInfo) {
    if (!pInfo) {
        return ORBIS_SHARE_PLAY_ERROR_INVALID_ARGUMENT;
    }
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->status = ORBIS_SHARE_PLAY_CONNECTION_STATUS_DORMANT;
    return 0;
}

// Shims
void shim_sceSharePlayInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSharePlayInitialize();
    SHIM_RETURN();
}

void shim_sceSharePlayTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSharePlayTerminate();
    SHIM_RETURN();
}

void shim_sceSharePlaySetProhibition(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSharePlaySetProhibition((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceSharePlayGetCurrentConnectionInfo(GuestContext *ctx) {
    // rdi is a guest address: translate it, a raw cast faults on the host
    OrbisSharePlayConnectionInfo *info = ctx->rdi ? (OrbisSharePlayConnectionInfo *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSharePlayGetCurrentConnectionInfo(info);
    SHIM_RETURN();
}

void shim_sceSharePlayGetCurrentConnectionInfoA(GuestContext *ctx) {
    OrbisSharePlayConnectionInfoA *info = ctx->rdi ? (OrbisSharePlayConnectionInfoA *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSharePlayGetCurrentConnectionInfoA(info);
    SHIM_RETURN();
}

// Functions that shadPS4 stubs as well (src/core/libraries, libSceSharePlay).
PS4_HLE_STUB(sceSharePlayCrashDaemon)
PS4_HLE_STUB(sceSharePlayGetCurrentInfo)
PS4_HLE_STUB(sceSharePlayGetEvent)
PS4_HLE_STUB(sceSharePlayNotifyDialogOpen)
PS4_HLE_STUB(sceSharePlayNotifyForceCloseForCdlg)
PS4_HLE_STUB(sceSharePlayNotifyOpenQuickMenu)
PS4_HLE_STUB(sceSharePlayResumeScreenForCdlg)
PS4_HLE_STUB(sceSharePlayServerLock)
PS4_HLE_STUB(sceSharePlayServerUnLock)
PS4_HLE_STUB(sceSharePlaySetMode)
PS4_HLE_STUB(sceSharePlaySetProhibitionModeWithAppId)
PS4_HLE_STUB(sceSharePlayStartStandby)
PS4_HLE_STUB(sceSharePlayStartStreaming)
PS4_HLE_STUB(sceSharePlayStopStandby)
PS4_HLE_STUB(sceSharePlayStopStreaming)
PS4_HLE_STUB(Func_2E93C0EA6A6B67C4)
PS4_HLE_STUB(Func_C1C236728D88E177)
PS4_HLE_STUB(Func_E9E80C474781F115)
PS4_HLE_STUB(Func_F3DD6199DA15ED44)
