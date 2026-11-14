// libSceNpUtility for an offline host: NpLookup, NpWordFilter and NpBandwidthTest.
// Title contexts and requests are local objects and are tracked as such; every operation
// that needs the NP servers fails the way a signed-out console does.
#include "ps4_np_utility.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

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

// int sceNpLookupCreateAsyncRequest(int titleCtxId, const void *param) -> reqId
void shim_sceNpLookupCreateAsyncRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)lookup_next(&g_lookup_req_counter);
    SHIM_RETURN();
}

// int sceNpLookupPollAsync(int reqId, int *result)
void shim_sceNpLookupPollAsync(GuestContext *ctx) {
    int32_t *result = (int32_t *)((ctx->rsi) ? (ctx->mem_base + ctx->rsi) : NULL);
    if (result) {
        *result = NP_COMMUNITY_ERROR_NO_LOGIN;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpLookupAbortRequest(int reqId)
void shim_sceNpLookupAbortRequest(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

//=============================================================================
// NpWordFilter. Title contexts and requests follow shadPS4's np_utility.cpp lifecycle (id
// bases, limits, argument checks, abort/delete semantics, one operation per request).
// Censoring and sanitizing are NP community server requests: on this signed-out host they
// complete with NP_ERROR_SIGNED_OUT, returned directly by a synchronous request and through
// sceNpWordFilterPollAsync by an asynchronous one. The comment is never passed through as
// accepted.
//=============================================================================

typedef struct {
    int32_t id; // 0 = free slot
} WordFilterTitleCtx;

typedef struct {
    int32_t id; // 0 = free slot
    int32_t title_ctx_id;
    bool is_async;
    bool used;
    bool aborted;
    bool has_result;
    int32_t result;
} WordFilterRequest;

static pthread_mutex_t g_wf_mutex = PTHREAD_MUTEX_INITIALIZER;
static WordFilterTitleCtx g_wf_ctx[NP_WORD_FILTER_MAX_CTX_NUM];
static WordFilterRequest g_wf_req[NP_WORD_FILTER_MAX_REQUEST_NUM];
static int32_t g_wf_next_ctx = NP_WORD_FILTER_TITLE_CTX_ID_OFFSET + 1;
static int32_t g_wf_next_req = NP_WORD_FILTER_REQUEST_ID_OFFSET + 1;

// Lock held.
static WordFilterTitleCtx *wf_ctx_find(int32_t id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < NP_WORD_FILTER_MAX_CTX_NUM; i++) {
        if (g_wf_ctx[i].id == id) return &g_wf_ctx[i];
    }
    return NULL;
}

// Lock held.
static WordFilterRequest *wf_req_find(int32_t id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < NP_WORD_FILTER_MAX_REQUEST_NUM; i++) {
        if (g_wf_req[i].id == id) return &g_wf_req[i];
    }
    return NULL;
}

// Lock held. The first result a request gets is final (abort after completion keeps it).
static void wf_req_complete(WordFilterRequest *r, int32_t result) {
    if (r->has_result) return;
    r->has_result = true;
    r->result = result;
}

int32_t ps4_np_word_filter_create_title_ctx(const void *self_np_id) {
    if (!self_np_id) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    pthread_mutex_lock(&g_wf_mutex);
    WordFilterTitleCtx *slot = NULL;
    for (int i = 0; i < NP_WORD_FILTER_MAX_CTX_NUM && !slot; i++) {
        if (g_wf_ctx[i].id == 0) slot = &g_wf_ctx[i];
    }
    int32_t r = NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS;
    if (slot) {
        slot->id = g_wf_next_ctx++;
        r = slot->id;
    }
    pthread_mutex_unlock(&g_wf_mutex);
    return r;
}

int32_t ps4_np_word_filter_delete_title_ctx(int32_t title_ctx_id) {
    pthread_mutex_lock(&g_wf_mutex);
    WordFilterTitleCtx *c = wf_ctx_find(title_ctx_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (c) {
        // Deleting a title context deletes (aborts) its requests.
        for (int i = 0; i < NP_WORD_FILTER_MAX_REQUEST_NUM; i++) {
            if (g_wf_req[i].id && g_wf_req[i].title_ctx_id == title_ctx_id) {
                memset(&g_wf_req[i], 0, sizeof(g_wf_req[i]));
            }
        }
        c->id = 0;
        r = 0;
    }
    pthread_mutex_unlock(&g_wf_mutex);
    return r;
}

static int32_t wf_create_request(int32_t title_ctx_id, bool is_async) {
    pthread_mutex_lock(&g_wf_mutex);
    int32_t r;
    if (!wf_ctx_find(title_ctx_id)) {
        r = NP_COMMUNITY_ERROR_INVALID_ID;
    } else {
        WordFilterRequest *slot = NULL;
        for (int i = 0; i < NP_WORD_FILTER_MAX_REQUEST_NUM && !slot; i++) {
            if (g_wf_req[i].id == 0) slot = &g_wf_req[i];
        }
        if (!slot) {
            r = NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS;
        } else {
            memset(slot, 0, sizeof(*slot));
            slot->id = g_wf_next_req++;
            slot->title_ctx_id = title_ctx_id;
            slot->is_async = is_async;
            r = slot->id;
        }
    }
    pthread_mutex_unlock(&g_wf_mutex);
    return r;
}

int32_t ps4_np_word_filter_create_request(int32_t title_ctx_id) {
    return wf_create_request(title_ctx_id, false);
}

int32_t ps4_np_word_filter_create_async_request(int32_t title_ctx_id, const void *param) {
    uint64_t size = 0;
    if (param) memcpy(&size, param, sizeof(size));
    if (!param || size != NP_WORD_FILTER_ASYNC_PARAM_SIZE) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    return wf_create_request(title_ctx_id, true);
}

int32_t ps4_np_word_filter_delete_request(int32_t req_id) {
    pthread_mutex_lock(&g_wf_mutex);
    WordFilterRequest *q = wf_req_find(req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q) {
        memset(q, 0, sizeof(*q));
        r = 0;
    }
    pthread_mutex_unlock(&g_wf_mutex);
    return r;
}

int32_t ps4_np_word_filter_abort_request(int32_t req_id) {
    pthread_mutex_lock(&g_wf_mutex);
    WordFilterRequest *q = wf_req_find(req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q) {
        q->aborted = true;
        wf_req_complete(q, NP_COMMUNITY_ERROR_ABORTED);
        r = 0;
    }
    pthread_mutex_unlock(&g_wf_mutex);
    return r;
}

// Shared by CensorComment and SanitizeComment once the pointers are checked.
static int32_t wf_run_server_request(int32_t req_id, const char *comment, size_t comment_room,
                                     const void *option) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    size_t limit = NP_WORD_FILTER_COMMENT_MAX_LENGTH + 1;
    if (comment_room < limit) limit = comment_room;
    // Longer than the maximum, or not terminated inside guest memory.
    if (strnlen(comment, limit) == limit) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_wf_mutex);
    WordFilterRequest *q = wf_req_find(req_id);
    int32_t r;
    if (!q) {
        r = NP_COMMUNITY_ERROR_INVALID_ID;
    } else if (q->aborted) {
        r = NP_COMMUNITY_ERROR_ABORTED;
    } else if (q->used) {
        r = NP_COMMUNITY_ERROR_INVALID_TYPE; // a request carries one operation
    } else {
        q->used = true;
        wf_req_complete(q, NP_ERROR_SIGNED_OUT);
        r = q->is_async ? 0 : NP_ERROR_SIGNED_OUT;
    }
    pthread_mutex_unlock(&g_wf_mutex);
    return r;
}

int32_t ps4_np_word_filter_censor(int32_t req_id, const char *comment, size_t comment_room,
                                  const void *option) {
    if (!comment) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    return wf_run_server_request(req_id, comment, comment_room, option);
}

int32_t ps4_np_word_filter_sanitize(int32_t req_id, const char *comment, size_t comment_room,
                                    char *sanitized, const void *option) {
    if (!comment || !sanitized) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    return wf_run_server_request(req_id, comment, comment_room, option);
}

int32_t ps4_np_word_filter_poll_async(int32_t req_id, int32_t *result) {
    if (!result) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_wf_mutex);
    WordFilterRequest *q = wf_req_find(req_id);
    int32_t r;
    if (!q || !q->is_async) {
        r = NP_COMMUNITY_ERROR_INVALID_ID;
    } else if (!q->has_result) {
        r = NP_WORD_FILTER_POLL_ASYNC_RET_RUNNING;
    } else {
        *result = q->result;
        r = NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED;
    }
    pthread_mutex_unlock(&g_wf_mutex);
    return r;
}

static void *np_utility_gptr(GuestContext *ctx, uint64_t addr) {
    return (addr && addr < ctx->mem_size) ? (void *)(ctx->mem_base + addr) : NULL;
}

static size_t np_utility_room(GuestContext *ctx, uint64_t addr) {
    return (addr && addr < ctx->mem_size) ? (size_t)(ctx->mem_size - addr) : 0;
}

// int sceNpWordFilterCreateTitleCtx(const SceNpId *selfNpId)
void shim_sceNpWordFilterCreateTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_np_word_filter_create_title_ctx(np_utility_gptr(ctx, ctx->rdi));
    SHIM_RETURN();
}

// int sceNpWordFilterDeleteTitleCtx(int titleCtxId)
void shim_sceNpWordFilterDeleteTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_np_word_filter_delete_title_ctx((int32_t)ctx->rdi);
    SHIM_RETURN();
}

// int sceNpWordFilterCreateRequest(int titleCtxId)
void shim_sceNpWordFilterCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_np_word_filter_create_request((int32_t)ctx->rdi);
    SHIM_RETURN();
}

// int sceNpWordFilterCreateAsyncRequest(int titleCtxId,
//                                       const SceNpWordFilterCreateAsyncRequestParameter *param)
void shim_sceNpWordFilterCreateAsyncRequest(GuestContext *ctx) {
    const void *param = ctx->rsi + NP_WORD_FILTER_ASYNC_PARAM_SIZE <= ctx->mem_size
                            ? np_utility_gptr(ctx, ctx->rsi)
                            : NULL;
    ctx->rax = (uint64_t)(int64_t)ps4_np_word_filter_create_async_request((int32_t)ctx->rdi, param);
    SHIM_RETURN();
}

// int sceNpWordFilterDeleteRequest(int reqId)
void shim_sceNpWordFilterDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_np_word_filter_delete_request((int32_t)ctx->rdi);
    SHIM_RETURN();
}

// int sceNpWordFilterAbortRequest(int reqId)
void shim_sceNpWordFilterAbortRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_np_word_filter_abort_request((int32_t)ctx->rdi);
    SHIM_RETURN();
}

// int sceNpWordFilterCensorComment(int reqId, const char *comment, void *option)
void shim_sceNpWordFilterCensorComment(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_np_word_filter_censor(
        (int32_t)ctx->rdi, (const char *)np_utility_gptr(ctx, ctx->rsi), np_utility_room(ctx, ctx->rsi),
        np_utility_gptr(ctx, ctx->rdx));
    SHIM_RETURN();
}

// int sceNpWordFilterSanitizeComment(int reqId, const char *comment, char *sanitizedComment,
//                                    void *option)
void shim_sceNpWordFilterSanitizeComment(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_np_word_filter_sanitize(
        (int32_t)ctx->rdi, (const char *)np_utility_gptr(ctx, ctx->rsi), np_utility_room(ctx, ctx->rsi),
        (char *)np_utility_gptr(ctx, ctx->rdx), np_utility_gptr(ctx, ctx->rcx));
    SHIM_RETURN();
}

// int sceNpWordFilterPollAsync(int reqId, int *result)
void shim_sceNpWordFilterPollAsync(GuestContext *ctx) {
    int32_t *result = ctx->rsi + sizeof(int32_t) <= ctx->mem_size
                          ? (int32_t *)np_utility_gptr(ctx, ctx->rsi)
                          : NULL;
    ctx->rax = (uint64_t)(int64_t)ps4_np_word_filter_poll_async((int32_t)ctx->rdi, result);
    SHIM_RETURN();
}

//=============================================================================
// NpBandwidthTest measures the connection against a PSN server, so it cannot start on a
// signed-out host. InitStart fails with NP_ERROR_SIGNED_OUT; no test context can therefore
// exist, and the calls that take one report CONTEXT_NOT_AVAILABLE (shadPS4 np_error.h)
// without reading their other arguments.
//=============================================================================

// int sceNpBandwidthTestInitStart(const SceNpBandwidthTestInitParam *param) -> ctxId
void shim_sceNpBandwidthTestInitStart(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NP_ERROR_SIGNED_OUT;
    SHIM_RETURN();
}

// int sceNpBandwidthTestGetStatus(int ctxId, int *status)
void shim_sceNpBandwidthTestGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NP_BANDWIDTH_TEST_ERROR_CONTEXT_NOT_AVAILABLE;
    SHIM_RETURN();
}

// int sceNpBandwidthTestShutdown(int ctxId, SceNpBandwidthTestResult *result)
void shim_sceNpBandwidthTestShutdown(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)NP_BANDWIDTH_TEST_ERROR_CONTEXT_NOT_AVAILABLE;
    SHIM_RETURN();
}
