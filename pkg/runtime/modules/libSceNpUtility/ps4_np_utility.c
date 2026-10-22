// libSceNpUtility (NpLookup) for an offline host. Title contexts and requests
// are handed out so setup code succeeds; the actual lookup fails with the NP
// community "not logged in" error.
#include "recomp_runtime.h"
#include <pthread.h>
#include <stdio.h>

#define NP_COMMUNITY_ERROR_INVALID_ARGUMENT ((int32_t)0x80550704)
#define NP_COMMUNITY_ERROR_NO_LOGIN         ((int32_t)0x80550705)

static pthread_mutex_t g_lookup_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_lookup_ctx_counter = 1;
static int32_t g_lookup_req_counter = 1;

static int32_t lookup_next(int32_t *c) {
    pthread_mutex_lock(&g_lookup_mutex);
    int32_t v = (*c)++;
    if (*c <= 0) *c = 1;
    pthread_mutex_unlock(&g_lookup_mutex);
    return v;
}

// int sceNpLookupCreateTitleCtx(const SceNpTitleId *titleId, const SceNpId *selfNpId) -> titleCtxId
void shim_sceNpLookupCreateTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)lookup_next(&g_lookup_ctx_counter);
    SHIM_RETURN();
}

// int sceNpLookupDeleteTitleCtx(int titleCtxId)
void shim_sceNpLookupDeleteTitleCtx(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpLookupCreateRequest(int titleCtxId) -> reqId
void shim_sceNpLookupCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)lookup_next(&g_lookup_req_counter);
    SHIM_RETURN();
}

// int sceNpLookupDeleteRequest(int reqId)
void shim_sceNpLookupDeleteRequest(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpLookupNpId(int reqId, const SceNpOnlineId *onlineId, SceNpId *npId, void *option)
void shim_sceNpLookupNpId(GuestContext *ctx) {
    static int warned;
    if (!warned) {
        warned = 1;
        fprintf(stderr, "[libSceNpUtility] sceNpLookupNpId -> NO_LOGIN (offline)\n");
    }
    if (!ctx->rsi || !ctx->rdx) {
        ctx->rax = (uint64_t)(int64_t)NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
        SHIM_RETURN();
    }
    ctx->rax = (uint64_t)(int64_t)NP_COMMUNITY_ERROR_NO_LOGIN;
    SHIM_RETURN();
}
