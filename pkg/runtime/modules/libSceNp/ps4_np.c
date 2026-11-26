#include "ps4_np.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>

// Guest pointer arguments are guest addresses; translate them to host pointers.
#define NP_GPTR(ctx, a) ((a) ? (void *)((ctx)->mem_base + (a)) : NULL)

// The game polls several NP stubs every frame, and a printf to a redirected
// stdout can block a job the frame waits on. Each call site prints its first
// call in full; later calls are only counted, with a summary line at every
// 1000th call. PS4_NP_LOG_ALL=1 prints every call.
static int np_log_all(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("PS4_NP_LOG_ALL");
        v = (e && e[0] == '1') ? 1 : 0;
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

#define NP_LOG(fmt, ...) ({ \
    static _Atomic unsigned np_log_calls_; \
    unsigned np_log_n_ = atomic_fetch_add_explicit(&np_log_calls_, 1, memory_order_relaxed) + 1; \
    if (np_log_n_ == 1 || np_log_all()) \
        printf("[libSceNp] " fmt "\n", ##__VA_ARGS__); \
    else if (np_log_n_ % 1000 == 0) \
        printf("[libSceNp] %s:%d: %u calls\n", __func__, __LINE__, np_log_n_); \
})


static int32_t g_score_req_counter = 1;
static int32_t g_tus_req_counter = 1;
static int32_t g_matching_ctx_counter = 1;
static _Atomic int32_t g_signaling_ctx_counter = 1;
static int32_t g_webapi_user_ctx_counter = 1;
static int64_t g_webapi_req_counter = 1;


//=============================================================================
// libSceNpTss / NpTus title-context utilities
//=============================================================================

int32_t sceNpTssCreateNpTitleCtx(OrbisNpServiceLabel serviceLabel, OrbisNpId* npId) {
    return sceNpTusCreateNpTitleCtx(serviceLabel, npId);
}

int32_t sceNpTssGetSmallStorage(int32_t reqId, void *data, uint64_t maxSize, uint64_t *contentLength, void *option) {
    (void)data;
    (void)maxSize;
    (void)contentLength;
    if (reqId <= 0 || option != NULL) {
        return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    }
    return ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
}

int32_t sceNpTusAddAndGetVariableVUserAsync(int32_t reqId, uint64_t targetVirtualUserId, int32_t slotId, int64_t inVariable, void *outVariable, void *option) {
    (void)targetVirtualUserId;
    (void)slotId;
    (void)inVariable;
    (void)outVariable;
    if (reqId <= 0 || option != NULL) {
        return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    }
    return 0; // Async operation started, returns NO_LOGIN on poll/wait
}

int32_t sceNpTusGetMultiSlotVariableVUserAsync(int32_t reqId, uint64_t targetVirtualUserId, const int32_t *slotIdArray, uint32_t slotIdNum, void *outVariableArray, void *option) {
    (void)targetVirtualUserId;
    (void)outVariableArray;
    if (reqId <= 0 || !slotIdArray || slotIdNum == 0 || option != NULL) {
        return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    }
    return 0; // Async operation started, returns NO_LOGIN on poll/wait
}

void shim_sceNpTssGetSmallStorage(GuestContext *ctx) {
    int32_t reqId = (int32_t)ctx->rdi;
    void *data = (void *)NP_GPTR(ctx, ctx->rsi);
    uint64_t maxSize = (uint64_t)ctx->rdx;
    uint64_t *contentLength = (uint64_t *)NP_GPTR(ctx, ctx->rcx);
    void *option = (void *)NP_GPTR(ctx, ctx->r8);
    ctx->rax = (uint64_t)(int64_t)sceNpTssGetSmallStorage(reqId, data, maxSize, contentLength, option);
    SHIM_RETURN();
}

void shim_sceNpTusAddAndGetVariableVUserAsync(GuestContext *ctx) {
    int32_t reqId = (int32_t)ctx->rdi;
    uint64_t vuid = (uint64_t)ctx->rsi;
    int32_t slotId = (int32_t)ctx->rdx;
    int64_t inVar = (int64_t)ctx->rcx;
    void *outVar = (void *)NP_GPTR(ctx, ctx->r8);
    void *opt = (void *)NP_GPTR(ctx, ctx->r9);
    ctx->rax = (uint64_t)(int64_t)sceNpTusAddAndGetVariableVUserAsync(reqId, vuid, slotId, inVar, outVar, opt);
    SHIM_RETURN();
}

void shim_sceNpTusGetMultiSlotVariableVUserAsync(GuestContext *ctx) {
    int32_t reqId = (int32_t)ctx->rdi;
    uint64_t vuid = (uint64_t)ctx->rsi;
    const int32_t *slots = (const int32_t *)NP_GPTR(ctx, ctx->rdx);
    uint32_t slotNum = (uint32_t)ctx->rcx;
    void *outVars = (void *)NP_GPTR(ctx, ctx->r8);
    void *opt = (void *)NP_GPTR(ctx, ctx->r9);
    ctx->rax = (uint64_t)(int64_t)sceNpTusGetMultiSlotVariableVUserAsync(reqId, vuid, slots, slotNum, outVars, opt);
    SHIM_RETURN();
}

void shim_sceNpTssCreateNpTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceNpTssCreateNpTitleCtx((OrbisNpServiceLabel)ctx->rdi, (OrbisNpId*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}


//=============================================================================
// libSceNpMatching2
//=============================================================================

int32_t sceNpMatching2Initialize(void* param) {
    (void)param;
    NP_LOG("sceNpMatching2Initialize");
    return 0;
}

int32_t sceNpMatching2Terminate(void) {
    NP_LOG("sceNpMatching2Terminate");
    return 0;
}

int32_t sceNpMatching2CreateContext(const void* param, uint16_t* ctxId) {
    (void)param;
    if (!ctxId) return ORBIS_NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    *ctxId = (uint16_t)(g_matching_ctx_counter++);
    NP_LOG("sceNpMatching2CreateContext -> ctxId=%u", *ctxId);
    return 0;
}

int32_t sceNpMatching2CreateContextA(const void* param, uint16_t* ctxId) {
    (void)param;
    if (!ctxId) return ORBIS_NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    *ctxId = (uint16_t)(g_matching_ctx_counter++);
    NP_LOG("sceNpMatching2CreateContextA -> ctxId=%u", *ctxId);
    return 0;
}

void shim_sceNpMatching2Initialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpMatching2Initialize((void*)NP_GPTR(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceNpMatching2Terminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpMatching2Terminate();
    SHIM_RETURN();
}

void shim_sceNpMatching2CreateContext(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpMatching2CreateContext((const void*)NP_GPTR(ctx, ctx->rdi), (uint16_t*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceNpMatching2CreateContextA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpMatching2CreateContextA((const void*)NP_GPTR(ctx, ctx->rdi), (uint16_t*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}

//=============================================================================
// libSceNpSignaling
//=============================================================================

// Library state: initialised flag and the live contexts with their option flags.
#define SIGNALING_MAX_CTX 64
static pthread_mutex_t g_signaling_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_signaling_initialized;
static struct {
    uint32_t id; // 0 = free slot
    uint32_t flags;
} g_signaling_ctx[SIGNALING_MAX_CTX];

// Registers a new context; false when all slots are used (the console's context limit).
static bool signaling_ctx_add(uint32_t id) {
    bool ok = false;
    pthread_mutex_lock(&g_signaling_mutex);
    for (int i = 0; i < SIGNALING_MAX_CTX; i++) {
        if (g_signaling_ctx[i].id == 0) {
            g_signaling_ctx[i].id = id;
            g_signaling_ctx[i].flags = 0;
            ok = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_signaling_mutex);
    return ok;
}

void ps4_np_signaling_ctx_forget(uint32_t id) {
    pthread_mutex_lock(&g_signaling_mutex);
    for (int i = 0; i < SIGNALING_MAX_CTX; i++) {
        if (g_signaling_ctx[i].id == id) g_signaling_ctx[i].id = 0;
    }
    pthread_mutex_unlock(&g_signaling_mutex);
}

// Option flags of a live context, or -1 when the id is not a live context.
int64_t ps4_np_signaling_ctx_flags(uint32_t id) {
    int64_t r = -1;
    pthread_mutex_lock(&g_signaling_mutex);
    for (int i = 0; i < SIGNALING_MAX_CTX; i++) {
        if (g_signaling_ctx[i].id == id && id != 0) r = g_signaling_ctx[i].flags;
    }
    pthread_mutex_unlock(&g_signaling_mutex);
    return r;
}

int32_t sceNpSignalingInitialize(int64_t memorySize, int32_t threadPriority, int32_t cpuAffinityMask, int64_t threadStackSize) {
    (void)memorySize;
    (void)threadPriority;
    (void)cpuAffinityMask;
    (void)threadStackSize;
    NP_LOG("sceNpSignalingInitialize");
    pthread_mutex_lock(&g_signaling_mutex);
    g_signaling_initialized = true;
    pthread_mutex_unlock(&g_signaling_mutex);
    return 0;
}

int32_t sceNpSignalingTerminate(void) {
    NP_LOG("sceNpSignalingTerminate");
    pthread_mutex_lock(&g_signaling_mutex);
    g_signaling_initialized = false;
    memset(g_signaling_ctx, 0, sizeof(g_signaling_ctx));
    pthread_mutex_unlock(&g_signaling_mutex);
    return 0;
}

// int sceNpSignalingSetContextOption(uint32_t ctxId, int optionId, int optionValue)
//
// The only option is CONTEXT_OPTION_FLAG (1); its value 0 clears and 1 sets bit 0x2 of the
// context's flags. Checks in the order of shadPS4 (libraries/np/np_signaling/np_signaling.cpp):
// library not initialised, unknown context, unknown option id, option value other than 0/1.
int32_t sceNpSignalingSetContextOption(uint32_t ctxId, int32_t optionId, int32_t optionValue) {
    int32_t rc = 0;
    pthread_mutex_lock(&g_signaling_mutex);
    int idx = -1;
    for (int i = 0; i < SIGNALING_MAX_CTX && ctxId != 0; i++) {
        if (g_signaling_ctx[i].id == ctxId) idx = i;
    }
    if (!g_signaling_initialized) {
        rc = (int32_t)ORBIS_NP_SIGNALING_ERROR_NOT_INITIALIZED;
    } else if (idx < 0) {
        rc = (int32_t)0x80552705; // CTX_NOT_FOUND
    } else if (optionId != 1) { // ORBIS_NP_SIGNALING_CONTEXT_OPTION_FLAG
        rc = (int32_t)ORBIS_NP_SIGNALING_ERROR_INVALID_ARGUMENT;
    } else if (optionValue == 0) {
        g_signaling_ctx[idx].flags &= ~0x2u;
    } else if (optionValue == 1) {
        g_signaling_ctx[idx].flags |= 0x2u;
    } else {
        rc = (int32_t)ORBIS_NP_SIGNALING_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_unlock(&g_signaling_mutex);
    return rc;
}

int32_t sceNpSignalingCreateContext(const void* npId, void* callback, void* callbackArg, uint32_t* outContextId) {
    (void)npId;
    (void)callback;
    (void)callbackArg;
    if (!outContextId) return ORBIS_NP_SIGNALING_ERROR_INVALID_ARGUMENT;
    *outContextId = (uint32_t)atomic_fetch_add(&g_signaling_ctx_counter, 1);
    if (!signaling_ctx_add(*outContextId)) {
        *outContextId = 0;
        return (int32_t)0x80552712; // ORBIS_NP_SIGNALING_ERROR_CTX_MAX
    }
    NP_LOG("sceNpSignalingCreateContext -> outContextId=%u", *outContextId);
    return 0;
}

int32_t sceNpSignalingCreateContextA(int32_t userId, void* callback, void* callbackArg, uint32_t* outContextId) {
    (void)userId;
    (void)callback;
    (void)callbackArg;
    if (!outContextId) return ORBIS_NP_SIGNALING_ERROR_INVALID_ARGUMENT;
    *outContextId = (uint32_t)atomic_fetch_add(&g_signaling_ctx_counter, 1);
    if (!signaling_ctx_add(*outContextId)) {
        *outContextId = 0;
        return (int32_t)0x80552712; // ORBIS_NP_SIGNALING_ERROR_CTX_MAX
    }
    NP_LOG("sceNpSignalingCreateContextA -> outContextId=%u", *outContextId);
    return 0;
}

void shim_sceNpSignalingInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpSignalingInitialize((int64_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx, (int64_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceNpSignalingTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpSignalingTerminate();
    SHIM_RETURN();
}

void shim_sceNpSignalingCreateContext(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpSignalingCreateContext((const void*)NP_GPTR(ctx, ctx->rdi), (void*)NP_GPTR(ctx, ctx->rsi), (void*)NP_GPTR(ctx, ctx->rdx), (uint32_t*)NP_GPTR(ctx, ctx->rcx));
    SHIM_RETURN();
}

void shim_sceNpSignalingSetContextOption(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceNpSignalingSetContextOption((uint32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceNpSignalingCreateContextA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpSignalingCreateContextA((int32_t)ctx->rdi, (void*)NP_GPTR(ctx, ctx->rsi), (void*)NP_GPTR(ctx, ctx->rdx), (uint32_t*)NP_GPTR(ctx, ctx->rcx));
    SHIM_RETURN();
}

//=============================================================================
// libSceNpWebApi & libSceNpWebApi2
//=============================================================================

int32_t sceNpWebApiInitialize(int32_t libHttpCtxId, uint64_t poolSize) {
    NP_LOG("sceNpWebApiInitialize(libHttpCtxId=%d, poolSize=%llu)", libHttpCtxId, (unsigned long long)poolSize);
    return 0;
}

int32_t sceNpWebApiTerminate(int32_t libCtxId) {
    NP_LOG("sceNpWebApiTerminate(libCtxId=%d)", libCtxId);
    return 0;
}

int32_t sceNpWebApiCreateContext(int32_t libCtxId, OrbisNpOnlineId* onlineId) {
    (void)onlineId;
    int32_t id = (libCtxId << 16) | (g_webapi_user_ctx_counter++);
    NP_LOG("sceNpWebApiCreateContext -> userCtxId=%d", id);
    return id;
}

int32_t sceNpWebApiCreateContextA(int32_t libCtxId, int32_t userId) {
    (void)userId;
    int32_t id = (libCtxId << 16) | (g_webapi_user_ctx_counter++);
    NP_LOG("sceNpWebApiCreateContextA -> userCtxId=%d", id);
    return id;
}

int32_t sceNpWebApiDeleteContext(int32_t titleUserCtxId) {
    NP_LOG("sceNpWebApiDeleteContext(titleUserCtxId=%d)", titleUserCtxId);
    return 0;
}

int32_t sceNpWebApiCreateRequest(int32_t titleUserCtxId, const char* pApiGroup, const char* pPath, int32_t method, const void* pContentParameter, int64_t* pRequestId) {
    (void)pApiGroup;
    (void)method;
    (void)pContentParameter;
    if (!pRequestId) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *pRequestId = ((int64_t)titleUserCtxId << 32) | (g_webapi_req_counter++);
    NP_LOG("sceNpWebApiCreateRequest(path=%s) -> reqId=%lld", pPath ? pPath : "null", (long long)*pRequestId);
    return 0;
}

int32_t sceNpWebApiSendRequest(int64_t requestId, const void* pData, uint64_t dataSize) {
    (void)pData;
    (void)dataSize;
    NP_LOG("sceNpWebApiSendRequest(reqId=%lld) -> NOT_SIGNED_IN", (long long)requestId);
    return ORBIS_NP_WEBAPI_ERROR_NOT_SIGNED_IN;
}

int32_t sceNpWebApiSendRequest2(int64_t requestId, const void* pData, uint64_t dataSize, void* pRespInfoOption) {
    (void)pData;
    (void)dataSize;
    (void)pRespInfoOption;
    NP_LOG("sceNpWebApiSendRequest2(reqId=%lld) -> NOT_SIGNED_IN", (long long)requestId);
    return ORBIS_NP_WEBAPI_ERROR_NOT_SIGNED_IN;
}

int32_t sceNpWebApiDeleteRequest(int64_t requestId) {
    NP_LOG("sceNpWebApiDeleteRequest(reqId=%lld)", (long long)requestId);
    return 0;
}

void shim_sceNpWebApiInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiInitialize((int32_t)ctx->rdi, (uint64_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceNpWebApiTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiTerminate((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNpWebApiCreateContext(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiCreateContext((int32_t)ctx->rdi, (OrbisNpOnlineId*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceNpWebApiCreateContextA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiCreateContextA((int32_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceNpWebApiDeleteContext(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiDeleteContext((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNpWebApiCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiCreateRequest(
        (int32_t)ctx->rdi, (const char*)NP_GPTR(ctx, ctx->rsi), (const char*)NP_GPTR(ctx, ctx->rdx), (int32_t)ctx->rcx, (const void*)NP_GPTR(ctx, ctx->r8), (int64_t*)NP_GPTR(ctx, ctx->r9));
    SHIM_RETURN();
}

void shim_sceNpWebApiSendRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiSendRequest((int64_t)ctx->rdi, (const void*)NP_GPTR(ctx, ctx->rsi), (uint64_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceNpWebApiSendRequest2(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiSendRequest2((int64_t)ctx->rdi, (const void*)NP_GPTR(ctx, ctx->rsi), (uint64_t)ctx->rdx, (void*)NP_GPTR(ctx, ctx->rcx));
    SHIM_RETURN();
}

void shim_sceNpWebApiDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiDeleteRequest((int64_t)ctx->rdi);
    SHIM_RETURN();
}

//=============================================================================
// libSceNpScore
//=============================================================================

int32_t sceNpScoreCreateNpTitleCtx(OrbisNpServiceLabel serviceLabel, const OrbisNpId* selfNpId) {
    (void)serviceLabel;
    (void)selfNpId;
    NP_LOG("sceNpScoreCreateNpTitleCtx -> SIGNED_OUT");
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpScoreCreateNpTitleCtxA(OrbisNpServiceLabel npServiceLabel, int32_t selfId) {
    (void)npServiceLabel;
    (void)selfId;
    NP_LOG("sceNpScoreCreateNpTitleCtxA(selfId=%d) -> SIGNED_OUT", selfId);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpScoreDeleteNpTitleCtx(int32_t titleCtxId) {
    NP_LOG("sceNpScoreDeleteNpTitleCtx(titleCtxId=%d)", titleCtxId);
    return 0;
}

int32_t sceNpScoreCreateRequest(int32_t titleCtxId) {
    (void)titleCtxId;
    int32_t id = g_score_req_counter++;
    NP_LOG("sceNpScoreCreateRequest(titleCtxId=%d) -> reqId=%d", titleCtxId, id);
    return id;
}

int32_t sceNpScoreDeleteRequest(int32_t reqId) {
    NP_LOG("sceNpScoreDeleteRequest(reqId=%d)", reqId);
    return 0;
}

int32_t sceNpScorePollAsync(int32_t reqId, int32_t* result) {
    (void)reqId;
    if (!result) return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    return 0;
}

int32_t sceNpScoreWaitAsync(int32_t reqId, int32_t* result) {
    (void)reqId;
    if (!result) return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    return 0;
}

void shim_sceNpScoreCreateNpTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreCreateNpTitleCtx((OrbisNpServiceLabel)ctx->rdi, (const OrbisNpId*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceNpScoreCreateNpTitleCtxA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreCreateNpTitleCtxA((OrbisNpServiceLabel)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceNpScoreDeleteNpTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreDeleteNpTitleCtx((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNpScoreCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreCreateRequest((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNpScoreDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreDeleteRequest((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNpScorePollAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScorePollAsync((int32_t)ctx->rdi, (int32_t*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceNpScoreWaitAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreWaitAsync((int32_t)ctx->rdi, (int32_t*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceNpScoreCensorCommentAsync(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpScoreGetFriendsRanking(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpScoreGetRankingByNpId(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

//=============================================================================
// libSceNpTus
//=============================================================================

int32_t sceNpTusCreateNpTitleCtx(OrbisNpServiceLabel serviceLabel, OrbisNpId* npId) {
    (void)serviceLabel;
    (void)npId;
    NP_LOG("sceNpTusCreateNpTitleCtx -> SIGNED_OUT");
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpTusCreateNpTitleCtxA(OrbisNpServiceLabel serviceLabel, int32_t userId) {
    (void)serviceLabel;
    (void)userId;
    NP_LOG("sceNpTusCreateNpTitleCtxA(userId=%d) -> SIGNED_OUT", userId);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpTssCreateNpTitleCtxA(OrbisNpServiceLabel serviceLabel, int32_t userId) {
    return sceNpTusCreateNpTitleCtxA(serviceLabel, userId);
}

int32_t sceNpTusSetThreadParam(void *param) {
    (void)param;
    return 0;
}

int32_t sceNpTusDeleteNpTitleCtx(int32_t ctxId) {
    NP_LOG("sceNpTusDeleteNpTitleCtx(ctxId=%d)", ctxId);
    return 0;
}

int32_t sceNpTusCreateRequest(int32_t libCtxId) {
    (void)libCtxId;
    int32_t id = g_tus_req_counter++;
    NP_LOG("sceNpTusCreateRequest(libCtxId=%d) -> reqId=%d", libCtxId, id);
    return id;
}

int32_t sceNpTusDeleteRequest(int32_t requestId) {
    NP_LOG("sceNpTusDeleteRequest(requestId=%d)", requestId);
    return 0;
}

int32_t sceNpTusPollAsync(int32_t reqId, int32_t* result) {
    (void)reqId;
    if (!result) return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    return 0;
}

int32_t sceNpTusWaitAsync(int32_t reqId, int32_t* result) {
    (void)reqId;
    if (!result) return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    return 0;
}

void shim_sceNpTusCreateNpTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusCreateNpTitleCtx((OrbisNpServiceLabel)ctx->rdi, (OrbisNpId*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceNpTusCreateNpTitleCtxA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusCreateNpTitleCtxA((OrbisNpServiceLabel)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceNpTssCreateNpTitleCtxA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTssCreateNpTitleCtxA((OrbisNpServiceLabel)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceNpTusSetThreadParam(GuestContext *ctx) {
    void *param = ctx->rdi ? (void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)sceNpTusSetThreadParam(param);
    SHIM_RETURN();
}

void shim_sceNpTusDeleteNpTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusDeleteNpTitleCtx((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNpTusCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusCreateRequest((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNpTusDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusDeleteRequest((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNpTusPollAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusPollAsync((int32_t)ctx->rdi, (int32_t*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceNpTusWaitAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusWaitAsync((int32_t)ctx->rdi, (int32_t*)NP_GPTR(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceNpTusAbortRequest(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpTusSetMultiSlotVariableAsync(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpTusSetDataAsync(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpTusAddAndGetVariableAsync(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpTusGetMultiSlotVariableAsync(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpTusGetDataAsync(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// Synchronous TUS/TSS data and variable transfers and asynchronous score ranking queries are NP
// server requests. Without a signed-in user they fail with SCE_NP_ERROR_SIGNED_OUT: shadPS4's
// NpHandler (np_handler_tus.cpp, np_handler_score.cpp) returns it for every such request when no
// client is signed in, and the libSceNpTus/libSceNpScore entry points pass it to the caller.
static void np_server_request_signed_out(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)ORBIS_NP_ERROR_SIGNED_OUT;
    SHIM_RETURN();
}

void shim_sceNpTusGetData(GuestContext *ctx) {
    NP_LOG("sceNpTusGetData -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusGetDataVUser(GuestContext *ctx) {
    NP_LOG("sceNpTusGetDataVUser -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusSetData(GuestContext *ctx) {
    NP_LOG("sceNpTusSetData -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusSetDataVUser(GuestContext *ctx) {
    NP_LOG("sceNpTusSetDataVUser -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusGetMultiSlotVariable(GuestContext *ctx) {
    NP_LOG("sceNpTusGetMultiSlotVariable -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusGetMultiSlotVariableVUser(GuestContext *ctx) {
    NP_LOG("sceNpTusGetMultiSlotVariableVUser -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusSetMultiSlotVariable(GuestContext *ctx) {
    NP_LOG("sceNpTusSetMultiSlotVariable -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusSetMultiSlotVariableVUser(GuestContext *ctx) {
    NP_LOG("sceNpTusSetMultiSlotVariableVUser -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusDeleteMultiSlotData(GuestContext *ctx) {
    NP_LOG("sceNpTusDeleteMultiSlotData -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusDeleteMultiSlotDataVUser(GuestContext *ctx) {
    NP_LOG("sceNpTusDeleteMultiSlotDataVUser -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusAddAndGetVariable(GuestContext *ctx) {
    NP_LOG("sceNpTusAddAndGetVariable -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTusAddAndGetVariableVUser(GuestContext *ctx) {
    NP_LOG("sceNpTusAddAndGetVariableVUser -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpScoreGetGameDataAsync(GuestContext *ctx) {
    NP_LOG("sceNpScoreGetGameDataAsync -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpTssGetData(GuestContext *ctx) {
    NP_LOG("sceNpTssGetData -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpScoreGetRankingByNpIdAsync(GuestContext *ctx) {
    NP_LOG("sceNpScoreGetRankingByNpIdAsync -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpScoreGetRankingByRangeAsync(GuestContext *ctx) {
    NP_LOG("sceNpScoreGetRankingByRangeAsync -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}

void shim_sceNpScoreGetFriendsRankingAsync(GuestContext *ctx) {
    NP_LOG("sceNpScoreGetFriendsRankingAsync -> SIGNED_OUT");
    np_server_request_signed_out(ctx);
}


//=============================================================================
// libSceNpUtility & libSceNpGameIntent
//=============================================================================

int32_t sceNpGameIntentInitialize(void) {
    NP_LOG("sceNpGameIntentInitialize");
    return 0;
}

int32_t sceNpGameIntentTerminate(void) {
    NP_LOG("sceNpGameIntentTerminate");
    return 0;
}

void shim_sceNpGameIntentInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGameIntentInitialize();
    SHIM_RETURN();
}

void shim_sceNpGameIntentTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGameIntentTerminate();
    SHIM_RETURN();
}

void shim_sceNpWebApiCheckTimeout(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpWebApiSetRequestTimeout(GuestContext *ctx) {
    (void)ctx->rdi;
    (void)ctx->rsi;
    ctx->rax = 0;
    SHIM_RETURN();
}
