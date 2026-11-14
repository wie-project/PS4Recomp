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

// NpWebApi error codes (0x805529xx, shadPS4 np_error.h).
#define NPX_WEBAPI_ERROR_OUT_OF_MEMORY             ((int32_t)0x80552901)
#define NPX_WEBAPI_ERROR_INVALID_ARGUMENT          ((int32_t)0x80552902)
#define NPX_WEBAPI_ERROR_HANDLE_NOT_FOUND          ((int32_t)0x8055290D)
#define NPX_WEBAPI_ERROR_SERVICE_PUSH_EVENT_CALLBACK_NOT_FOUND ((int32_t)0x8055290F)

// NpInGameMessage error codes (0x80552Bxx, shadPS4 np_error.h).
#define NPX_IGM_ERROR_OUT_OF_MEMORY                ((int32_t)0x80552B01)
#define NPX_IGM_ERROR_HANDLE_NOT_FOUND             ((int32_t)0x80552B05)

static pthread_mutex_t g_npx_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_npx_webapi_filter = 1;
static int32_t g_npx_webapi_callback = 1;

static int32_t npx_next(int32_t *counter) {
    pthread_mutex_lock(&g_npx_mutex);
    int32_t v = (*counter)++;
    if (*counter <= 0 || *counter > 0x7fff) *counter = 1;
    pthread_mutex_unlock(&g_npx_mutex);
    return v;
}

// Live objects of one kind, each owned by the context id it was created under (handles by
// their library context, push-event callbacks by their user context), so deleting checks
// both, as shadPS4's per-context maps do (np_web_api_internal.cpp). Fixed capacity: a full
// table is the library's OUT_OF_MEMORY.
#define NPX_MAX_OBJECTS 256
typedef struct {
    int32_t owner;
    int32_t id; // 0 = free slot
} NpxObject;

typedef struct {
    NpxObject slot[NPX_MAX_OBJECTS];
    int32_t next; // next id candidate, 1..0x7fff
} NpxObjectTable;

static NpxObjectTable g_npx_igm_handles = {.next = 1};
static NpxObjectTable g_npx_webapi_handles = {.next = 1};
static NpxObjectTable g_npx_webapi_service_callbacks = {.next = 1};

// Lock held.
static bool npx_id_live(const NpxObjectTable *t, int32_t id) {
    for (int i = 0; i < NPX_MAX_OBJECTS; i++) {
        if (t->slot[i].id == id) return true;
    }
    return false;
}

// Registers a new object under owner; returns its id, or 0 when the table is full.
static int32_t npx_object_add(NpxObjectTable *t, int32_t owner) {
    pthread_mutex_lock(&g_npx_mutex);
    int32_t id = 0;
    for (int i = 0; i < NPX_MAX_OBJECTS; i++) {
        if (t->slot[i].id != 0) continue;
        do { // ids are positive and unique among live objects
            id = t->next++;
            if (t->next > 0x7fff) t->next = 1;
        } while (npx_id_live(t, id));
        t->slot[i].owner = owner;
        t->slot[i].id = id;
        break;
    }
    pthread_mutex_unlock(&g_npx_mutex);
    return id;
}

// Removes the object id owned by owner; false when there is none.
static bool npx_object_remove(NpxObjectTable *t, int32_t owner, int32_t id) {
    if (id <= 0) return false;
    bool found = false;
    pthread_mutex_lock(&g_npx_mutex);
    for (int i = 0; i < NPX_MAX_OBJECTS; i++) {
        if (t->slot[i].id == id && t->slot[i].owner == owner) {
            t->slot[i].id = 0;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_npx_mutex);
    return found;
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
    int32_t id = npx_object_add(&g_npx_igm_handles, (int32_t)ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)(id ? id : NPX_IGM_ERROR_OUT_OF_MEMORY);
    SHIM_RETURN();
}

// int sceNpInGameMessageDeleteHandle(int libCtxId, int handleId)
void shim_sceNpInGameMessageDeleteHandle(GuestContext *ctx) {
    bool ok = npx_object_remove(&g_npx_igm_handles, (int32_t)ctx->rdi, (int32_t)ctx->rsi);
    ctx->rax = ok ? 0 : (uint64_t)(int64_t)NPX_IGM_ERROR_HANDLE_NOT_FOUND;
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
    int32_t id = npx_object_add(&g_npx_webapi_handles, (int32_t)ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)(id ? id : NPX_WEBAPI_ERROR_OUT_OF_MEMORY);
    SHIM_RETURN();
}

// int sceNpWebApiDeleteHandle(int libCtxId, int handleId)
void shim_sceNpWebApiDeleteHandle(GuestContext *ctx) {
    bool ok = npx_object_remove(&g_npx_webapi_handles, (int32_t)ctx->rdi, (int32_t)ctx->rsi);
    ctx->rax = ok ? 0 : (uint64_t)(int64_t)NPX_WEBAPI_ERROR_HANDLE_NOT_FOUND;
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
// A NULL callback is INVALID_ARGUMENT (shadPS4 np_web_api.cpp).
void shim_sceNpWebApiRegisterServicePushEventCallback(GuestContext *ctx) {
    if (!ctx->rdx) {
        ctx->rax = (uint64_t)(int64_t)NPX_WEBAPI_ERROR_INVALID_ARGUMENT;
        SHIM_RETURN();
    }
    int32_t id = npx_object_add(&g_npx_webapi_service_callbacks, (int32_t)ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)(id ? id : NPX_WEBAPI_ERROR_OUT_OF_MEMORY);
    SHIM_RETURN();
}

// int sceNpWebApiUnregisterServicePushEventCallback(int titleUserCtxId, int callbackId)
void shim_sceNpWebApiUnregisterServicePushEventCallback(GuestContext *ctx) {
    bool ok = npx_object_remove(&g_npx_webapi_service_callbacks, (int32_t)ctx->rdi, (int32_t)ctx->rsi);
    ctx->rax = ok ? 0 : (uint64_t)(int64_t)NPX_WEBAPI_ERROR_SERVICE_PUSH_EVENT_CALLBACK_NOT_FOUND;
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

//=============================================================================
// Additional libSceNpSignaling
//=============================================================================

void shim_sceNpSignalingDeactivateConnection(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

//=============================================================================
// Additional libSceNpScore
//=============================================================================

void shim_sceNpScoreAbortRequest(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpScoreCensorComment(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpScoreSanitizeComment(GuestContext *ctx) {
    const char *comment = (const char *)NPX_GPTR(ctx, ctx->rsi);
    char *sanitized = (char *)NPX_GPTR(ctx, ctx->rdx);
    if (sanitized && comment) {
        strncpy(sanitized, comment, 63);
        sanitized[63] = '\0';
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpScoreSetPlayerCharacterId(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpScoreGetRankingByNpIdPcId(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetRankingByRange(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetBoardInfo(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreRecordGameData(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetGameData(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreRecordScore(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

//=============================================================================
// Additional libSceNpWebApi
//=============================================================================

void shim_sceNpWebApiRegisterPushEventCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)npx_next(&g_npx_webapi_callback);
    SHIM_RETURN();
}

void shim_sceNpWebApiUnregisterPushEventCallback(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpWebApiCreatePushEventFilter(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)npx_next(&g_npx_webapi_filter);
    SHIM_RETURN();
}

void shim_sceNpWebApiDeletePushEventFilter(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpWebApiGetHttpStatusCode(GuestContext *ctx) {
    int32_t *code = (int32_t *)NPX_GPTR(ctx, ctx->rsi);
    if (code) *code = 401;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_WEBAPI_ERROR_NOT_SIGNED_IN;
    SHIM_RETURN();
}

void shim_sceNpWebApiGetHttpResponseHeaderValue(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_WEBAPI_ERROR_NOT_SIGNED_IN;
    SHIM_RETURN();
}

void shim_sceNpWebApiGetHttpResponseHeaderValueLength(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_WEBAPI_ERROR_NOT_SIGNED_IN;
    SHIM_RETURN();
}

//=============================================================================
// Additional libSceNpMatching2
//=============================================================================

#define ORBIS_NET_ERROR_RESOLVER_ETIMEDOUT ((int32_t)0x804101e2)

void shim_sceNpMatching2SetDefaultRequestOptParam(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2ContextStart(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ORBIS_NET_ERROR_RESOLVER_ETIMEDOUT;
    SHIM_RETURN();
}

void shim_sceNpMatching2ContextStop(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2DestroyContext(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2RegisterContextCallback(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2RegisterLobbyEventCallback(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2RegisterRoomEventCallback(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2RegisterSignalingCallback(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2GetServerId(GuestContext *ctx) {
    uint32_t *serverId = (uint32_t *)NPX_GPTR(ctx, ctx->rsi);
    if (serverId) *serverId = 1;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2CreateJoinRoom(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2JoinRoom(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2LeaveRoom(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2SearchRoom(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2SetRoomDataInternal(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2SetRoomDataExternal(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2SetRoomMemberDataInternal(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2GrantRoomOwner(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2KickoutRoomMember(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2JoinLobby(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2LeaveLobby(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2GetLobbyInfoList(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2GetWorldInfoList(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2SignalingGetConnectionStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_CONN_NOT_FOUND;
    SHIM_RETURN();
}

void shim_sceNpMatching2SignalingGetPingInfo(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_CONN_NOT_FOUND;
    SHIM_RETURN();
}

void shim_sceNpMatching2SignalingGetConnectionInfo(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_CONN_NOT_FOUND;
    SHIM_RETURN();
}

void shim_sceNpWebApiGetErrorCode(GuestContext *ctx) {
    ctx->rax = 0; // ORBIS_OK: no pending WebAPI error
    SHIM_RETURN();
}

//=============================================================================
// Catherine / Offline NpScore, NpMatching2, NpTus additions
//=============================================================================

void shim_sceNpScoreRecordGameDataAsync(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreRecordScoreAsync(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetRankingByAccountId(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetGameDataByAccountId(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetRankingByRangeA(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetGameDataByAccountIdAsync(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetRankingByAccountIdAsync(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetFriendsRankingA(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpScoreGetRankingByRangeAAsync(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpMatching2RegisterLobbyMessageCallback(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2RegisterRoomMessageCallback(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2AbortContextStart(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpMatching2SignalingGetConnectionInfoA(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)NPX_SIGNALING_ERROR_CONN_NOT_FOUND;
    SHIM_RETURN();
}

void shim_sceNpMatching2GetRoomDataExternalList(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2GetRoomMemberDataInternal(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2GetUserInfoListA(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2SendRoomMessage(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2CreateJoinRoomA(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2JoinRoomA(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpTusSetDataAAsync(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}

void shim_sceNpTusGetDataAAsync(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}




// Matching2 server requests the eboot imports beyond the ones above. Like the other room and
// lobby requests here they fail with NOT_NP_SIGN_IN; the request and request-id pointers are
// checked first, in shadPS4's order (np_matching2.cpp).
// int f(SceNpMatching2ContextId ctxId, const void *request, const SceNpMatching2RequestOptParam *opt,
//       SceNpMatching2RequestId *requestId)
static void npx_matching2_request(GuestContext *ctx) {
    if (!ctx->rsi || !ctx->rcx) {
        ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_INVALID_ARGUMENT;
        SHIM_RETURN();
    }
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}

void shim_sceNpMatching2SetUserInfo(GuestContext *ctx) { npx_matching2_request(ctx); }
void shim_sceNpMatching2GetUserInfoList(GuestContext *ctx) { npx_matching2_request(ctx); }
void shim_sceNpMatching2GetRoomDataInternal(GuestContext *ctx) { npx_matching2_request(ctx); }

// int sceNpMatching2SendRoomChatMessage(SceNpMatching2ContextId ctxId, const void *request,
//                                       const SceNpMatching2RequestOptParam *opt,
//                                       SceNpMatching2RequestId *requestId)
// A room chat message needs a joined room, which cannot exist signed out.
void shim_sceNpMatching2SendRoomChatMessage(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN;
    SHIM_RETURN();
}
