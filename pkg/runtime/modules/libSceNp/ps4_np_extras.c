// Extra NP entry points (libSceNpManager in-game messaging / parental control,
// libSceNpSignaling connection queries, libSceNpWebApi handles, push-event
// filters/callbacks and utilities). The host is always "offline / signed out":
// registration-style calls succeed with positive ids so the game's setup paths
// complete, while any call that would talk to PSN fails with a proper error.
#include "ps4_np.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define NPX_GPTR(ctx, a) ((a) ? (void *)((ctx)->mem_base + (a)) : NULL)

#define NPX_LOG_ONCE(fmt, ...)                                                 \
    do {                                                                       \
        static int warned_;                                                    \
        if (!warned_) {                                                        \
            warned_ = 1;                                                       \
            fprintf(stderr, "[libSceNp] " fmt "\n", ##__VA_ARGS__);            \
        }                                                                      \
    } while (0)

// NpSignaling error codes (0x805527xx, same ordering as the Vita library).
#define NPX_SIGNALING_ERROR_REQ_NOT_FOUND          ((int32_t)0x80552707)
#define NPX_SIGNALING_ERROR_NETINFO_NOT_AVAILABLE  ((int32_t)0x8055270B)
#define NPX_SIGNALING_ERROR_CONN_NOT_FOUND         ((int32_t)0x8055270E)
#define NPX_SIGNALING_ERROR_INVALID_ARGUMENT       ((int32_t)0x80552715)
#define NPX_SIGNALING_CONN_STATUS_INACTIVE         0

// NpWebApi error codes (0x805529xx).
#define NPX_WEBAPI_ERROR_INVALID_ARGUMENT          ((int32_t)0x80552902)

static pthread_mutex_t g_npx_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_npx_igm_handle = 1;
static int32_t g_npx_webapi_handle = 1;
static int32_t g_npx_webapi_filter = 1;
static int32_t g_npx_webapi_callback = 1;

static int32_t npx_next(int32_t *counter) {
    pthread_mutex_lock(&g_npx_mutex);
    int32_t v = (*counter)++;
    if (*counter <= 0 || *counter > 0x7fff) *counter = 1;
    pthread_mutex_unlock(&g_npx_mutex);
    return v;
}

//=============================================================================
// libSceNpManager
//=============================================================================

// int sceNpGetParentalControlInfo(int reqId, const SceNpOnlineId *onlineId,
//                                 int8_t *age, SceNpParentalControlInfo *info)
void shim_sceNpGetParentalControlInfo(GuestContext *ctx) {
    NPX_LOG_ONCE("sceNpGetParentalControlInfo -> SIGNED_OUT");
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_ERROR_SIGNED_OUT;
    SHIM_RETURN();
}

// int sceNpInGameMessageInitialize(size_t poolSize, void *option) -> libCtxId
void shim_sceNpInGameMessageInitialize(GuestContext *ctx) {
    NPX_LOG_ONCE("sceNpInGameMessageInitialize -> libCtxId 1 (offline)");
    ctx->rax = 1;
    SHIM_RETURN();
}

// int sceNpInGameMessageTerminate(int libCtxId)
void shim_sceNpInGameMessageTerminate(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpInGameMessageCreateHandle(int libCtxId) -> handleId
void shim_sceNpInGameMessageCreateHandle(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)npx_next(&g_npx_igm_handle);
    SHIM_RETURN();
}

// int sceNpInGameMessageAbortHandle(int libCtxId, int handleId)
void shim_sceNpInGameMessageAbortHandle(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpInGameMessagePrepare(int libCtxId, int handleId, const void *reserved,
//                               SceNpInGameMessageEventCallback cb, void *arg)
// Registration only; the callback simply never fires while offline.
void shim_sceNpInGameMessagePrepare(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpInGameMessageSendData(int libCtxId, int handleId, const SceNpPeerAddress *to,
//                                const void *data, size_t size)
void shim_sceNpInGameMessageSendData(GuestContext *ctx) {
    NPX_LOG_ONCE("sceNpInGameMessageSendData -> SIGNED_OUT");
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_ERROR_SIGNED_OUT;
    SHIM_RETURN();
}

//=============================================================================
// libSceNpSignaling (no connections can exist while offline)
//=============================================================================

// int sceNpSignalingActivateConnection(uint32_t ctxId, const SceNpId *npId, uint32_t *connId)
void shim_sceNpSignalingActivateConnection(GuestContext *ctx) {
    NPX_LOG_ONCE("sceNpSignalingActivateConnection -> NETINFO_NOT_AVAILABLE (offline)");
    if (!ctx->rsi || !ctx->rdx) {
        ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_INVALID_ARGUMENT;
        SHIM_RETURN();
    }
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_NETINFO_NOT_AVAILABLE;
    SHIM_RETURN();
}

// int sceNpSignalingDeleteContext(uint32_t ctxId)
void shim_sceNpSignalingDeleteContext(GuestContext *ctx) {
    ps4_np_signaling_ctx_forget((uint32_t)ctx->rdi);
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpSignalingGetConnectionFromNpId(uint32_t ctxId, const SceNpId *npId, uint32_t *connId)
void shim_sceNpSignalingGetConnectionFromNpId(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_CONN_NOT_FOUND;
    SHIM_RETURN();
}

// int sceNpSignalingGetConnectionInfo(uint32_t ctxId, uint32_t connId, int code, SceNpSignalingConnectionInfo *info)
void shim_sceNpSignalingGetConnectionInfo(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_CONN_NOT_FOUND;
    SHIM_RETURN();
}

// int sceNpSignalingGetConnectionStatus(uint32_t ctxId, uint32_t connId, int *connStatus,
//                                       SceNetInAddr *peerAddr, SceNetInPort_t *peerPort)
void shim_sceNpSignalingGetConnectionStatus(GuestContext *ctx) {
    int32_t *status = (int32_t *)NPX_GPTR(ctx, ctx->rdx);
    if (status) *status = NPX_SIGNALING_CONN_STATUS_INACTIVE;
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_CONN_NOT_FOUND;
    SHIM_RETURN();
}

// int sceNpSignalingGetPeerNetInfoResult(uint32_t ctxId, uint32_t reqId, SceNpSignalingPeerNetInfoResult *result)
void shim_sceNpSignalingGetPeerNetInfoResult(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_REQ_NOT_FOUND;
    SHIM_RETURN();
}

// int sceNpSignalingTerminateConnection(uint32_t ctxId, uint32_t connId)
// Cleanup path: report success so teardown code never stalls.
void shim_sceNpSignalingTerminateConnection(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

//=============================================================================
// libSceNpWebApi
//=============================================================================

// int sceNpWebApiCreateHandle(int libCtxId) -> handleId
void shim_sceNpWebApiCreateHandle(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)npx_next(&g_npx_webapi_handle);
    SHIM_RETURN();
}

// int sceNpWebApiReadData(int64_t requestId, void *pData, size_t size) -> bytes read.
// Requests are never sent (SendRequest fails NOT_SIGNED_IN), so there is no body.
void shim_sceNpWebApiReadData(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpWebApiCreateExtdPushEventFilter(int libCtxId, int handleId, const char *pNpServiceName,
//         SceNpServiceLabel label, const SceNpWebApiExtdPushEventFilterParameter *p, size_t n) -> filterId
void shim_sceNpWebApiCreateExtdPushEventFilter(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)npx_next(&g_npx_webapi_filter);
    SHIM_RETURN();
}

// int sceNpWebApiCreateServicePushEventFilter(int libCtxId, int handleId, const char *pNpServiceName,
//         SceNpServiceLabel label, const SceNpWebApiServicePushEventFilterParameter *p, size_t n) -> filterId
void shim_sceNpWebApiCreateServicePushEventFilter(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)npx_next(&g_npx_webapi_filter);
    SHIM_RETURN();
}

// int sceNpWebApiDeleteExtdPushEventFilter(int libCtxId, int filterId)
void shim_sceNpWebApiDeleteExtdPushEventFilter(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpWebApiDeleteServicePushEventFilter(int libCtxId, int filterId)
void shim_sceNpWebApiDeleteServicePushEventFilter(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpWebApiRegisterExtdPushEventCallback(int titleUserCtxId, int filterId, cb, void *arg) -> callbackId
// The callback never fires (no push events while offline).
void shim_sceNpWebApiRegisterExtdPushEventCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)npx_next(&g_npx_webapi_callback);
    SHIM_RETURN();
}

// int sceNpWebApiRegisterServicePushEventCallback(int titleUserCtxId, int filterId, cb, void *arg) -> callbackId
void shim_sceNpWebApiRegisterServicePushEventCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)npx_next(&g_npx_webapi_callback);
    SHIM_RETURN();
}

// int sceNpWebApiUnregisterExtdPushEventCallback(int titleUserCtxId, int callbackId)
void shim_sceNpWebApiUnregisterExtdPushEventCallback(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpWebApiUtilityParseNpId(const char *pJsonNpId, SceNpId *pNpId)
// Parses the WebAPI JSON form {"handle":{"data":"<onlineId>",...},...} or a bare
// online id string into an SceNpId.
void shim_sceNpWebApiUtilityParseNpId(GuestContext *ctx) {
    const char *json = (const char *)NPX_GPTR(ctx, ctx->rdi);
    OrbisNpId *npid = (OrbisNpId *)NPX_GPTR(ctx, ctx->rsi);
    if (!json || !npid) {
        ctx->rax = (uint64_t)(int64_t)NPX_WEBAPI_ERROR_INVALID_ARGUMENT;
        SHIM_RETURN();
    }
    memset(npid, 0, sizeof(*npid));
    const char *start = NULL;
    const char *key = strstr(json, "\"data\"");
    if (!key) key = strstr(json, "\"onlineId\"");
    if (key) {
        const char *colon = strchr(key + 1, ':');
        if (colon) start = strchr(colon, '"');
        if (start) start++;
    } else {
        // Bare online id (no JSON punctuation).
        start = json;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '{' || *start == '[') start = NULL;
    }
    if (!start) {
        ctx->rax = (uint64_t)(int64_t)NPX_WEBAPI_ERROR_INVALID_ARGUMENT;
        SHIM_RETURN();
    }
    size_t n = 0;
    while (start[n] && start[n] != '"' && n < ORBIS_NP_ONLINEID_MAX_LENGTH) {
        npid->handle.data[n] = start[n];
        n++;
    }
    if (n == 0) {
        ctx->rax = (uint64_t)(int64_t)NPX_WEBAPI_ERROR_INVALID_ARGUMENT;
        SHIM_RETURN();
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

//=============================================================================
// libSceNpSessionSignaling
//=============================================================================

void shim_sceNpSessionSignalingInitialize(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingTerminate(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingCreateContext2(GuestContext *ctx) {
    uint32_t *ctxId = (uint32_t *)NPX_GPTR(ctx, ctx->rdx);
    if (ctxId) *ctxId = 1;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingDestroyContext(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingActivateSession(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingDeactivate(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingActivateUser(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingGetConnectionInfo(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_CONN_NOT_FOUND;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingGetConnectionStatus(GuestContext *ctx) {
    int32_t *status = (int32_t *)NPX_GPTR(ctx, ctx->rdx);
    if (status) *status = NPX_SIGNALING_CONN_STATUS_INACTIVE;
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_CONN_NOT_FOUND;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingGetLocalNetInfo(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_NETINFO_NOT_AVAILABLE;
    SHIM_RETURN();
}

void shim_sceNpSessionSignalingRequestPrepare(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpGetParentalControlInfoA(GuestContext *ctx) {
    NPX_LOG_ONCE("sceNpGetParentalControlInfoA -> SIGNED_OUT");
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_ERROR_SIGNED_OUT;
    SHIM_RETURN();
}

void shim_sceNpGetUserIdByAccountId(GuestContext *ctx) {
    int32_t *userId = (int32_t *)NPX_GPTR(ctx, ctx->rsi);
    if (userId) *userId = 0x10000000;
    ctx->rax = 0;
    SHIM_RETURN();
}

