// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpUtility, following shadPS4 (src/core/libraries/np/np_utility).
//
// NpLookup and NpWordFilter keep shadPS4's title contexts and requests: id bases, limits,
// argument checks in its order, one operation per request, abort / delete semantics,
// asynchronous completion through PollAsync / WaitAsync, and the timeouts SetTimeout records.
//
// sceNpLookupNpId resolves an online id through the NP handler (libSceNp/ps4_np_handler.c,
// shadPS4 NpHandler::ResolveOnlineId): a LookupOnlineId request on the searching user's shadNet
// session, which completes the request from the client's reader thread. Without a session it
// completes with ORBIS_NP_ERROR_SIGNED_OUT. A title context created from an NP id belongs to the
// signed-in user with that online id, or to user -1 (NpHandler::GetUserIdByOnlineId).
//
// Word filtering is local in shadPS4 (no shadNet command yet): CensorComment accepts the
// comment and SanitizeComment copies it unchanged; both complete with ORBIS_OK.
//
// Every other function of the library is a stub in shadPS4 and is a stub here.
#include "ps4_np_utility.h"
#include "ps4_hle_stub.h"
#include "ps4_np_handler.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>

// sceNpLookupSetTimeout / sceNpWordFilterSetTimeout values. No network request reads them on a
// signed-out host, as in shadPS4, but they are kept per title context and copied into the
// requests created from it.
typedef struct {
    int32_t resolve_retry;
    uint32_t resolve_timeout;
    uint32_t conn_timeout;
    uint32_t send_timeout;
    uint32_t recv_timeout;
} NpUtilityTimeouts;

typedef struct {
    int32_t id; // 0 = free slot
    int32_t user_id;
    bool has_timeouts;
    NpUtilityTimeouts timeouts;
} NpUtilityTitleCtx;

// A request. shadPS4 holds requests by shared_ptr, so a thread blocked in WaitAsync keeps its
// request alive after it was deleted. Here the slot stays reserved (deleted, waiters > 0) until
// the last waiter leaves; a deleted request cannot be found by id.
typedef struct {
    int32_t id; // 0 = free slot
    int32_t title_ctx_id;
    bool is_async;
    bool used;
    bool aborted;
    bool deleted;
    bool has_result;
    int32_t result;
    uint32_t waiters;
    bool has_timeouts;
    NpUtilityTimeouts timeouts;
    OrbisNpId *np_id; // the NP id sceNpLookupNpId fills
} NpUtilityRequest;

// The live-request limit is MAX_REQUEST_NUM; the extra slots hold deleted requests that still
// have waiters.
#define NP_UTILITY_REQUEST_SLOTS 64

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t done; // broadcast whenever a request gets its result
    NpUtilityTitleCtx ctx[NP_LOOKUP_MAX_CTX_NUM];
    NpUtilityRequest req[NP_UTILITY_REQUEST_SLOTS];
    int32_t live_requests;
    int32_t next_ctx_id;
    int32_t next_req_id;
    int32_t max_ctx;
    int32_t max_req;
} NpUtilityPool;

_Static_assert(NP_LOOKUP_MAX_CTX_NUM == NP_WORD_FILTER_MAX_CTX_NUM, "one title context table size");
_Static_assert(NP_LOOKUP_MAX_REQUEST_NUM <= NP_UTILITY_REQUEST_SLOTS, "request slots");
_Static_assert(NP_WORD_FILTER_MAX_REQUEST_NUM <= NP_UTILITY_REQUEST_SLOTS, "request slots");

static NpUtilityPool g_lookup = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .done = PTHREAD_COND_INITIALIZER,
    .next_ctx_id = 1,
    .next_req_id = 1,
    .max_ctx = NP_LOOKUP_MAX_CTX_NUM,
    .max_req = NP_LOOKUP_MAX_REQUEST_NUM,
};

static NpUtilityPool g_word_filter = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .done = PTHREAD_COND_INITIALIZER,
    .next_ctx_id = NP_WORD_FILTER_TITLE_CTX_ID_OFFSET + 1,
    .next_req_id = NP_WORD_FILTER_REQUEST_ID_OFFSET + 1,
    .max_ctx = NP_WORD_FILTER_MAX_CTX_NUM,
    .max_req = NP_WORD_FILTER_MAX_REQUEST_NUM,
};

// NpHandler::GetUserIdByOnlineId: the signed-in user whose NP id carries the online id, or -1.
static int32_t np_user_id_by_online_id(const void *online_id) {
    ps4_np_handler_start();
    return ps4_np_handler_user_by_online_id(online_id);
}

// Lock held.
static NpUtilityTitleCtx *pool_ctx_find(NpUtilityPool *p, int32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < p->max_ctx; i++) {
        if (p->ctx[i].id == id) return &p->ctx[i];
    }
    return NULL;
}

// Lock held. Deleted requests are not found.
static NpUtilityRequest *pool_req_find(NpUtilityPool *p, int32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < NP_UTILITY_REQUEST_SLOTS; i++) {
        if (p->req[i].id == id && !p->req[i].deleted) return &p->req[i];
    }
    return NULL;
}

// Lock held. LookupRequestCtx::SetResult: the first result is final.
static void pool_req_set_result(NpUtilityPool *p, NpUtilityRequest *r, int32_t result) {
    if (r->has_result) return;
    r->has_result = true;
    r->result = result;
    pthread_cond_broadcast(&p->done);
}

// Lock held. A request by id, deleted or not: a completion still reaches a deleted request
// that has waiters (ids are never reused).
static NpUtilityRequest *pool_req_find_any(NpUtilityPool *p, int32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < NP_UTILITY_REQUEST_SLOTS; i++) {
        if (p->req[i].id == id) return &p->req[i];
    }
    return NULL;
}

// Lock held. Removes a request from the id space; its slot is reused once no thread waits on it.
static void pool_req_erase(NpUtilityPool *p, NpUtilityRequest *r) {
    p->live_requests--;
    if (r->waiters == 0) {
        memset(r, 0, sizeof(*r));
    } else {
        r->deleted = true;
    }
}

static int32_t pool_create_title_ctx(NpUtilityPool *p, int32_t user_id) {
    pthread_mutex_lock(&p->mutex);
    NpUtilityTitleCtx *slot = NULL;
    for (int i = 0; i < p->max_ctx && !slot; i++) {
        if (p->ctx[i].id == 0) slot = &p->ctx[i];
    }
    int32_t r = NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS;
    if (slot) {
        memset(slot, 0, sizeof(*slot));
        slot->id = p->next_ctx_id++;
        slot->user_id = user_id;
        r = slot->id;
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

// Deletes a title context with its requests, which complete as aborted. set_aborted: the
// word filter also marks them aborted (sceNpLookupDeleteTitleCtx only sets the result).
static int32_t pool_delete_title_ctx(NpUtilityPool *p, int32_t title_ctx_id, bool set_aborted) {
    pthread_mutex_lock(&p->mutex);
    NpUtilityTitleCtx *c = pool_ctx_find(p, title_ctx_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (c) {
        for (int i = 0; i < NP_UTILITY_REQUEST_SLOTS; i++) {
            NpUtilityRequest *q = &p->req[i];
            if (q->id == 0 || q->deleted || q->title_ctx_id != title_ctx_id) continue;
            if (set_aborted) q->aborted = true;
            pool_req_set_result(p, q, NP_COMMUNITY_ERROR_ABORTED);
            pool_req_erase(p, q);
        }
        memset(c, 0, sizeof(*c));
        r = 0;
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

static int32_t pool_create_request(NpUtilityPool *p, int32_t title_ctx_id, bool is_async) {
    pthread_mutex_lock(&p->mutex);
    int32_t r;
    NpUtilityTitleCtx *c = pool_ctx_find(p, title_ctx_id);
    NpUtilityRequest *slot = NULL;
    if (!c) {
        r = NP_COMMUNITY_ERROR_INVALID_ID;
    } else {
        if (p->live_requests < p->max_req) {
            for (int i = 0; i < NP_UTILITY_REQUEST_SLOTS && !slot; i++) {
                if (p->req[i].id == 0) slot = &p->req[i];
            }
        }
        if (!slot) {
            r = NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS;
        } else {
            memset(slot, 0, sizeof(*slot));
            slot->id = p->next_req_id++;
            slot->title_ctx_id = title_ctx_id;
            slot->is_async = is_async;
            slot->has_timeouts = c->has_timeouts;
            slot->timeouts = c->timeouts;
            p->live_requests++;
            r = slot->id;
        }
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

// sceNp*DeleteRequest: marks the request aborted, completes it as aborted and removes its id.
static int32_t pool_delete_request(NpUtilityPool *p, int32_t req_id) {
    pthread_mutex_lock(&p->mutex);
    NpUtilityRequest *q = pool_req_find(p, req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q) {
        q->aborted = true;
        pool_req_set_result(p, q, NP_COMMUNITY_ERROR_ABORTED);
        pool_req_erase(p, q);
        r = 0;
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

static int32_t pool_abort_request(NpUtilityPool *p, int32_t req_id) {
    pthread_mutex_lock(&p->mutex);
    NpUtilityRequest *q = pool_req_find(p, req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q) {
        q->aborted = true;
        pool_req_set_result(p, q, NP_COMMUNITY_ERROR_ABORTED);
        r = 0;
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

// Lock held. Starts the single operation of a request (StartWordFilterRequest and the checks in
// sceNpLookupNpId).
static int32_t pool_req_start(NpUtilityRequest *q) {
    if (q->aborted) return NP_COMMUNITY_ERROR_ABORTED;
    if (q->used) return NP_COMMUNITY_ERROR_INVALID_TYPE;
    q->used = true;
    return 0;
}

// Lock held. Blocks until q has a result (LookupRequestCtx::Wait); q stays valid meanwhile even
// if another thread deletes it.
static int32_t pool_req_wait(NpUtilityPool *p, NpUtilityRequest *q) {
    q->waiters++;
    while (!q->has_result) {
        pthread_cond_wait(&p->done, &p->mutex);
    }
    const int32_t result = q->result;
    q->waiters--;
    if (q->deleted && q->waiters == 0) {
        memset(q, 0, sizeof(*q));
    }
    return result;
}

// Records timeouts on a title context or, failing that, a request with that id.
static int32_t pool_set_timeout(NpUtilityPool *p, int32_t id, const NpUtilityTimeouts *t) {
    pthread_mutex_lock(&p->mutex);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    NpUtilityTitleCtx *c = pool_ctx_find(p, id);
    if (c) {
        c->has_timeouts = true;
        c->timeouts = *t;
        r = 0;
    } else {
        NpUtilityRequest *q = pool_req_find(p, id);
        if (q) {
            q->has_timeouts = true;
            q->timeouts = *t;
            r = 0;
        }
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

// The argument checks of both SetTimeout functions: timeouts in microseconds, 0 = no effect.
static bool timeouts_valid(const NpUtilityTimeouts *t) {
    if (t->resolve_retry < 0) return false;
    if (t->resolve_timeout != NP_LOOKUP_TIMEOUT_NO_EFFECT && t->resolve_timeout < 1000000u) return false;
    if (t->conn_timeout != NP_LOOKUP_TIMEOUT_NO_EFFECT && t->conn_timeout < 10000000u) return false;
    if (t->send_timeout != NP_LOOKUP_TIMEOUT_NO_EFFECT && t->send_timeout < 10000000u) return false;
    if (t->recv_timeout != NP_LOOKUP_TIMEOUT_NO_EFFECT && t->recv_timeout < 10000000u) return false;
    return !(t->resolve_retry == 0 && t->resolve_timeout == NP_LOOKUP_TIMEOUT_NO_EFFECT &&
             t->conn_timeout == NP_LOOKUP_TIMEOUT_NO_EFFECT && t->send_timeout == NP_LOOKUP_TIMEOUT_NO_EFFECT &&
             t->recv_timeout == NP_LOOKUP_TIMEOUT_NO_EFFECT);
}

//=============================================================================
// NpLookup
//=============================================================================

int32_t ps4_np_lookup_create_title_ctx(const void *self_np_id) {
    if (!self_np_id) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    // OrbisNpId starts with its OrbisNpOnlineId handle.
    return pool_create_title_ctx(&g_lookup, np_user_id_by_online_id(self_np_id));
}

int32_t ps4_np_lookup_create_title_ctx_a(int32_t user_id) {
    return pool_create_title_ctx(&g_lookup, user_id);
}

int32_t ps4_np_lookup_delete_title_ctx(int32_t title_ctx_id) {
    return pool_delete_title_ctx(&g_lookup, title_ctx_id, false);
}

int32_t ps4_np_lookup_create_request(int32_t title_ctx_id, bool is_async) {
    return pool_create_request(&g_lookup, title_ctx_id, is_async);
}

int32_t ps4_np_lookup_delete_request(int32_t req_id) {
    return pool_delete_request(&g_lookup, req_id);
}

int32_t ps4_np_lookup_abort_request(int32_t req_id) {
    return pool_abort_request(&g_lookup, req_id);
}

// The ResolveOnlineId result for request (intptr_t)user: fills its NP id with the canonical
// online id on success (shadPS4 SetNpId) and completes it. shadPS4 keeps the request alive
// through its shared_ptr and writes the NP id even after sceNpLookupDeleteRequest; here a
// request that was deleted with no thread waiting on it is gone, and nothing is written.
static void lookup_np_id_done(void *user, int32_t result, uint64_t account_id, const char *canonical_online_id) {
    (void)account_id;
    NpUtilityPool *p = &g_lookup;
    pthread_mutex_lock(&p->mutex);
    NpUtilityRequest *q = pool_req_find_any(p, (int32_t)(intptr_t)user);
    if (q) {
        if (result == 0) {
            memset(q->np_id, 0, sizeof(*q->np_id));
            memcpy(q->np_id->handle.data, canonical_online_id,
                   strnlen(canonical_online_id, ORBIS_NP_ONLINEID_MAX_LENGTH));
        }
        pool_req_set_result(p, q, result);
    }
    pthread_mutex_unlock(&p->mutex);
}

int32_t ps4_np_lookup_np_id(int32_t req_id, const void *online_id, void *np_id, const void *option) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!online_id || !np_id) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    NpUtilityPool *p = &g_lookup;
    pthread_mutex_lock(&p->mutex);
    NpUtilityRequest *q = pool_req_find(p, req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    int32_t user_id = -1;
    bool is_async = false;
    if (q && (r = pool_req_start(q)) == 0) {
        const NpUtilityTitleCtx *c = pool_ctx_find(p, q->title_ctx_id);
        if (c) user_id = c->user_id;
        is_async = q->is_async;
        q->np_id = np_id;
    }
    pthread_mutex_unlock(&p->mutex);
    if (r != 0) return r;

    char target[ORBIS_NP_ONLINEID_MAX_LENGTH + 1];
    const OrbisNpOnlineId *id = online_id;
    const size_t n = strnlen(id->data, ORBIS_NP_ONLINEID_MAX_LENGTH);
    memcpy(target, id->data, n);
    target[n] = '\0';
    // The result may arrive on this thread (no session) or on the client's reader thread.
    ps4_np_handler_start();
    ps4_np_handler_resolve_online_id(user_id, target, lookup_np_id_done, (void *)(intptr_t)req_id);
    if (is_async) return 0;

    pthread_mutex_lock(&p->mutex);
    q = pool_req_find_any(p, req_id);
    r = q ? pool_req_wait(p, q) : NP_COMMUNITY_ERROR_ABORTED;
    pthread_mutex_unlock(&p->mutex);
    return r;
}

int32_t ps4_np_lookup_poll_async(int32_t req_id, int32_t *result) {
    NpUtilityPool *p = &g_lookup;
    pthread_mutex_lock(&p->mutex);
    NpUtilityRequest *q = pool_req_find(p, req_id);
    int32_t r;
    if (!q) {
        r = NP_COMMUNITY_ERROR_INVALID_ID;
    } else if (!q->has_result) {
        r = NP_LOOKUP_POLL_ASYNC_RET_RUNNING;
    } else {
        if (result) *result = q->result;
        r = 0;
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

int32_t ps4_np_lookup_wait_async(int32_t req_id, int32_t *result) {
    NpUtilityPool *p = &g_lookup;
    pthread_mutex_lock(&p->mutex);
    NpUtilityRequest *q = pool_req_find(p, req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q) {
        const int32_t done = pool_req_wait(p, q);
        if (result) *result = done;
        r = 0;
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

int32_t ps4_np_lookup_set_timeout(int32_t id, int32_t resolve_retry, uint32_t resolve_timeout,
                                  uint32_t conn_timeout, uint32_t send_timeout, uint32_t recv_timeout) {
    const NpUtilityTimeouts t = {resolve_retry, resolve_timeout, conn_timeout, send_timeout, recv_timeout};
    if (!timeouts_valid(&t)) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    return pool_set_timeout(&g_lookup, id, &t);
}

//=============================================================================
// NpWordFilter
//=============================================================================

int32_t ps4_np_word_filter_create_title_ctx(const void *self_np_id) {
    if (!self_np_id) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    return pool_create_title_ctx(&g_word_filter, np_user_id_by_online_id(self_np_id));
}

int32_t ps4_np_word_filter_create_title_ctx_a(int32_t user_id) {
    return pool_create_title_ctx(&g_word_filter, user_id);
}

int32_t ps4_np_word_filter_delete_title_ctx(int32_t title_ctx_id) {
    return pool_delete_title_ctx(&g_word_filter, title_ctx_id, true);
}

int32_t ps4_np_word_filter_create_request(int32_t title_ctx_id) {
    return pool_create_request(&g_word_filter, title_ctx_id, false);
}

int32_t ps4_np_word_filter_create_async_request(int32_t title_ctx_id, const void *param) {
    uint64_t size = 0;
    if (param) memcpy(&size, param, sizeof(size));
    if (!param || size != NP_WORD_FILTER_ASYNC_PARAM_SIZE) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    return pool_create_request(&g_word_filter, title_ctx_id, true);
}

int32_t ps4_np_word_filter_delete_request(int32_t req_id) {
    return pool_delete_request(&g_word_filter, req_id);
}

int32_t ps4_np_word_filter_abort_request(int32_t req_id) {
    return pool_abort_request(&g_word_filter, req_id);
}

// The length of a comment that fits the word filter limit and is terminated inside guest
// memory, or -1.
static int64_t word_filter_comment_length(const char *comment, size_t comment_room) {
    size_t limit = NP_WORD_FILTER_COMMENT_MAX_LENGTH + 1;
    if (comment_room < limit) limit = comment_room;
    const size_t len = strnlen(comment, limit);
    return (len == limit) ? -1 : (int64_t)len;
}

// Starts the request's operation and completes it with ORBIS_OK (CompleteWordFilterRequest):
// a synchronous request returns the result, an asynchronous one returns 0. sanitized, when
// set, receives the comment and its terminator before completion.
static int32_t word_filter_run(int32_t req_id, const char *comment, size_t length, char *sanitized) {
    NpUtilityPool *p = &g_word_filter;
    pthread_mutex_lock(&p->mutex);
    NpUtilityRequest *q = pool_req_find(p, req_id);
    int32_t r;
    if (!q) {
        r = NP_COMMUNITY_ERROR_INVALID_ID;
    } else if ((r = pool_req_start(q)) == 0) {
        if (sanitized) memcpy(sanitized, comment, length + 1);
        pool_req_set_result(p, q, 0);
        r = 0;
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

int32_t ps4_np_word_filter_censor(int32_t req_id, const char *comment, size_t comment_room,
                                  const void *option) {
    if (!comment) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    const int64_t length = word_filter_comment_length(comment, comment_room);
    if (length < 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    return word_filter_run(req_id, comment, (size_t)length, NULL);
}

int32_t ps4_np_word_filter_sanitize(int32_t req_id, const char *comment, size_t comment_room,
                                    char *sanitized, const void *option) {
    if (!comment || !sanitized) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    const int64_t length = word_filter_comment_length(comment, comment_room);
    if (length < 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    return word_filter_run(req_id, comment, (size_t)length, sanitized);
}

int32_t ps4_np_word_filter_poll_async(int32_t req_id, int32_t *result) {
    if (!result) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    NpUtilityPool *p = &g_word_filter;
    pthread_mutex_lock(&p->mutex);
    NpUtilityRequest *q = pool_req_find(p, req_id);
    int32_t r;
    if (!q || !q->is_async) {
        r = NP_COMMUNITY_ERROR_INVALID_ID;
    } else if (!q->has_result) {
        r = NP_WORD_FILTER_POLL_ASYNC_RET_RUNNING;
    } else {
        *result = q->result;
        r = NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED;
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

int32_t ps4_np_word_filter_wait_async(int32_t req_id, int32_t *result) {
    if (!result) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    NpUtilityPool *p = &g_word_filter;
    pthread_mutex_lock(&p->mutex);
    NpUtilityRequest *q = pool_req_find(p, req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q && q->is_async) {
        *result = pool_req_wait(p, q);
        r = 0;
    }
    pthread_mutex_unlock(&p->mutex);
    return r;
}

int32_t ps4_np_word_filter_set_timeout(int32_t id, int32_t resolve_retry, uint32_t resolve_timeout,
                                       uint32_t conn_timeout, uint32_t send_timeout,
                                       uint32_t recv_timeout) {
    const NpUtilityTimeouts t = {resolve_retry, resolve_timeout, conn_timeout, send_timeout, recv_timeout};
    if (!timeouts_valid(&t)) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    return pool_set_timeout(&g_word_filter, id, &t);
}

//=============================================================================
// Shims
//=============================================================================

// The host object at a guest address that has at least size readable bytes, or NULL.
static void *np_utility_gptr(GuestContext *ctx, uint64_t addr, size_t size) {
    return (addr && addr < ctx->mem_size && size <= ctx->mem_size - addr) ? (void *)(ctx->mem_base + addr) : NULL;
}

static size_t np_utility_room(GuestContext *ctx, uint64_t addr) {
    return (addr && addr < ctx->mem_size) ? (size_t)(ctx->mem_size - addr) : 0;
}

#define NP_ID_SIZE 36        // OrbisNpId: handle (20), opt (8), reserved (8)
#define NP_ONLINE_ID_SIZE 20 // OrbisNpOnlineId: data (16), term, dummy (3)
#define RET(v)                                  \
    do {                                        \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v); \
        SHIM_RETURN();                          \
    } while (0)

// int sceNpLookupCreateTitleCtx(const OrbisNpId *selfNpId)
void shim_sceNpLookupCreateTitleCtx(GuestContext *ctx) {
    RET(ps4_np_lookup_create_title_ctx(np_utility_gptr(ctx, ctx->rdi, NP_ID_SIZE)));
}

// int sceNpLookupCreateTitleCtxA(OrbisUserServiceUserId userId)
void shim_sceNpLookupCreateTitleCtxA(GuestContext *ctx) {
    RET(ps4_np_lookup_create_title_ctx_a((int32_t)ctx->rdi));
}

// int sceNpLookupDeleteTitleCtx(int titleCtxId)
void shim_sceNpLookupDeleteTitleCtx(GuestContext *ctx) {
    RET(ps4_np_lookup_delete_title_ctx((int32_t)ctx->rdi));
}

// int sceNpLookupCreateRequest(int titleCtxId)
void shim_sceNpLookupCreateRequest(GuestContext *ctx) {
    RET(ps4_np_lookup_create_request((int32_t)ctx->rdi, false));
}

// int sceNpLookupCreateAsyncRequest(int titleCtxId, const OrbisNpLookupCreateAsyncRequestParameter *)
// shadPS4 does not read the parameter.
void shim_sceNpLookupCreateAsyncRequest(GuestContext *ctx) {
    RET(ps4_np_lookup_create_request((int32_t)ctx->rdi, true));
}

// int sceNpLookupDeleteRequest(int reqId)
void shim_sceNpLookupDeleteRequest(GuestContext *ctx) {
    RET(ps4_np_lookup_delete_request((int32_t)ctx->rdi));
}

// int sceNpLookupAbortRequest(int reqId)
void shim_sceNpLookupAbortRequest(GuestContext *ctx) {
    RET(ps4_np_lookup_abort_request((int32_t)ctx->rdi));
}

// int sceNpLookupNpId(int reqId, const OrbisNpOnlineId *onlineId, OrbisNpId *npId, void *option)
void shim_sceNpLookupNpId(GuestContext *ctx) {
    RET(ps4_np_lookup_np_id((int32_t)ctx->rdi, np_utility_gptr(ctx, ctx->rsi, NP_ONLINE_ID_SIZE),
                            np_utility_gptr(ctx, ctx->rdx, NP_ID_SIZE), np_utility_gptr(ctx, ctx->rcx, 1)));
}

// int sceNpLookupPollAsync(int reqId, int *result)
void shim_sceNpLookupPollAsync(GuestContext *ctx) {
    RET(ps4_np_lookup_poll_async((int32_t)ctx->rdi, np_utility_gptr(ctx, ctx->rsi, sizeof(int32_t))));
}

// int sceNpLookupWaitAsync(int reqId, int *result)
void shim_sceNpLookupWaitAsync(GuestContext *ctx) {
    RET(ps4_np_lookup_wait_async((int32_t)ctx->rdi, np_utility_gptr(ctx, ctx->rsi, sizeof(int32_t))));
}

// int sceNpLookupSetTimeout(int id, int resolveRetry, uint resolveTimeout, uint connTimeout,
//                           uint sendTimeout, uint recvTimeout)
void shim_sceNpLookupSetTimeout(GuestContext *ctx) {
    RET(ps4_np_lookup_set_timeout((int32_t)ctx->rdi, (int32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx,
                                  (uint32_t)ctx->r8, (uint32_t)ctx->r9));
}

// int sceNpWordFilterCreateTitleCtx(const OrbisNpId *selfNpId)
void shim_sceNpWordFilterCreateTitleCtx(GuestContext *ctx) {
    RET(ps4_np_word_filter_create_title_ctx(np_utility_gptr(ctx, ctx->rdi, NP_ID_SIZE)));
}

// int sceNpWordFilterCreateTitleCtxA(OrbisUserServiceUserId userId)
void shim_sceNpWordFilterCreateTitleCtxA(GuestContext *ctx) {
    RET(ps4_np_word_filter_create_title_ctx_a((int32_t)ctx->rdi));
}

// int sceNpWordFilterDeleteTitleCtx(int titleCtxId)
void shim_sceNpWordFilterDeleteTitleCtx(GuestContext *ctx) {
    RET(ps4_np_word_filter_delete_title_ctx((int32_t)ctx->rdi));
}

// int sceNpWordFilterCreateRequest(int titleCtxId)
void shim_sceNpWordFilterCreateRequest(GuestContext *ctx) {
    RET(ps4_np_word_filter_create_request((int32_t)ctx->rdi));
}

// int sceNpWordFilterCreateAsyncRequest(int titleCtxId,
//                                       const OrbisNpWordFilterCreateAsyncRequestParameter *param)
void shim_sceNpWordFilterCreateAsyncRequest(GuestContext *ctx) {
    RET(ps4_np_word_filter_create_async_request((int32_t)ctx->rdi,
                                                np_utility_gptr(ctx, ctx->rsi, NP_WORD_FILTER_ASYNC_PARAM_SIZE)));
}

// int sceNpWordFilterDeleteRequest(int reqId)
void shim_sceNpWordFilterDeleteRequest(GuestContext *ctx) {
    RET(ps4_np_word_filter_delete_request((int32_t)ctx->rdi));
}

// int sceNpWordFilterAbortRequest(int reqId)
void shim_sceNpWordFilterAbortRequest(GuestContext *ctx) {
    RET(ps4_np_word_filter_abort_request((int32_t)ctx->rdi));
}

// int sceNpWordFilterCensorComment(int reqId, const char *comment, void *option)
void shim_sceNpWordFilterCensorComment(GuestContext *ctx) {
    RET(ps4_np_word_filter_censor((int32_t)ctx->rdi, np_utility_gptr(ctx, ctx->rsi, 1), np_utility_room(ctx, ctx->rsi),
                                  np_utility_gptr(ctx, ctx->rdx, 1)));
}

// int sceNpWordFilterSanitizeComment(int reqId, const char *comment, char *sanitizedComment,
//                                    void *option)
// sanitizedComment receives up to COMMENT_MAX_LENGTH + 1 bytes; it must have that room.
void shim_sceNpWordFilterSanitizeComment(GuestContext *ctx) {
    RET(ps4_np_word_filter_sanitize((int32_t)ctx->rdi, np_utility_gptr(ctx, ctx->rsi, 1), np_utility_room(ctx, ctx->rsi),
                                    np_utility_gptr(ctx, ctx->rdx, NP_WORD_FILTER_COMMENT_MAX_LENGTH + 1),
                                    np_utility_gptr(ctx, ctx->rcx, 1)));
}

// int sceNpWordFilterPollAsync(int reqId, int *result)
void shim_sceNpWordFilterPollAsync(GuestContext *ctx) {
    RET(ps4_np_word_filter_poll_async((int32_t)ctx->rdi, np_utility_gptr(ctx, ctx->rsi, sizeof(int32_t))));
}

// int sceNpWordFilterWaitAsync(int reqId, int *result)
void shim_sceNpWordFilterWaitAsync(GuestContext *ctx) {
    RET(ps4_np_word_filter_wait_async((int32_t)ctx->rdi, np_utility_gptr(ctx, ctx->rsi, sizeof(int32_t))));
}

// int sceNpWordFilterSetTimeout(int id, int resolveRetry, uint resolveTimeout, uint connTimeout,
//                               uint sendTimeout, uint recvTimeout)
void shim_sceNpWordFilterSetTimeout(GuestContext *ctx) {
    RET(ps4_np_word_filter_set_timeout((int32_t)ctx->rdi, (int32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx,
                                       (uint32_t)ctx->r8, (uint32_t)ctx->r9));
}

// Stubs in shadPS4's np_utility.cpp, in its source order.
PS4_HLE_STUB(sceNpAppInfoIntAbortRequest)
PS4_HLE_STUB(sceNpAppInfoIntCheckAvailability)
PS4_HLE_STUB(sceNpAppInfoIntCheckAvailabilityA)
PS4_HLE_STUB(sceNpAppInfoIntCheckAvailabilityAll)
PS4_HLE_STUB(sceNpAppInfoIntCheckAvailabilityAllA)
PS4_HLE_STUB(sceNpAppInfoIntCheckServiceAvailability)
PS4_HLE_STUB(sceNpAppInfoIntCheckServiceAvailabilityA)
PS4_HLE_STUB(sceNpAppInfoIntCheckServiceAvailabilityAll)
PS4_HLE_STUB(sceNpAppInfoIntCheckServiceAvailabilityAllA)
PS4_HLE_STUB(sceNpAppInfoIntCreateRequest)
PS4_HLE_STUB(sceNpAppInfoIntDestroyRequest)
PS4_HLE_STUB(sceNpAppInfoIntFinalize)
PS4_HLE_STUB(sceNpAppInfoIntInitialize)
PS4_HLE_STUB(sceNpAppLaunchLink2IntAbortRequest)
PS4_HLE_STUB(sceNpAppLaunchLink2IntCreateRequest)
PS4_HLE_STUB(sceNpAppLaunchLink2IntDestroyRequest)
PS4_HLE_STUB(sceNpAppLaunchLink2IntFinalize)
PS4_HLE_STUB(sceNpAppLaunchLink2IntGetCompatibleTitleIdList)
PS4_HLE_STUB(sceNpAppLaunchLink2IntGetCompatibleTitleIdNum)
PS4_HLE_STUB(sceNpAppLaunchLink2IntInitialize)
PS4_HLE_STUB(sceNpAppLaunchLinkIntAbortRequest)
PS4_HLE_STUB(sceNpAppLaunchLinkIntCreateRequest)
PS4_HLE_STUB(sceNpAppLaunchLinkIntDestroyRequest)
PS4_HLE_STUB(sceNpAppLaunchLinkIntFinalize)
PS4_HLE_STUB(sceNpAppLaunchLinkIntGetCompatibleTitleIdList)
PS4_HLE_STUB(sceNpAppLaunchLinkIntGetCompatibleTitleIdNum)
PS4_HLE_STUB(sceNpAppLaunchLinkIntInitialize)
PS4_HLE_STUB(sceNpBandwidthTestAbort)
PS4_HLE_STUB(sceNpBandwidthTestDownloadOnlyInitStart)
PS4_HLE_STUB(sceNpBandwidthTestGetStatus)
PS4_HLE_STUB(sceNpBandwidthTestInitStart)
PS4_HLE_STUB(sceNpBandwidthTestInitStartDownload)
PS4_HLE_STUB(sceNpBandwidthTestInitStartUpload)
PS4_HLE_STUB(sceNpBandwidthTestShutdown)
PS4_HLE_STUB(sceNpBandwidthTestShutdownWithDetailedInfo)
PS4_HLE_STUB(sceNpBandwidthTestUploadOnlyInitStart)
PS4_HLE_STUB(sceNpLookupNetAbortRequest)
PS4_HLE_STUB(sceNpLookupNetCensorComment)
PS4_HLE_STUB(sceNpLookupNetConvertJidToNpId)
PS4_HLE_STUB(sceNpLookupNetConvertNpIdToJid)
PS4_HLE_STUB(sceNpLookupNetCreateRequest)
PS4_HLE_STUB(sceNpLookupNetCreateTitleCtx)
PS4_HLE_STUB(sceNpLookupNetDeleteRequest)
PS4_HLE_STUB(sceNpLookupNetDeleteTitleCtx)
PS4_HLE_STUB(sceNpLookupNetInit)
PS4_HLE_STUB(sceNpLookupNetInitWithFunctionPointer)
PS4_HLE_STUB(sceNpLookupNetInitWithMemoryPool)
PS4_HLE_STUB(sceNpLookupNetIsInit)
PS4_HLE_STUB(sceNpLookupNetNpId)
PS4_HLE_STUB(sceNpLookupNetSanitizeComment)
PS4_HLE_STUB(sceNpLookupNetSetTimeout)
PS4_HLE_STUB(sceNpLookupNetTerm)
PS4_HLE_STUB(sceNpServiceChecker2IntAbortRequest)
PS4_HLE_STUB(sceNpServiceChecker2IntCreateRequest)
PS4_HLE_STUB(sceNpServiceChecker2IntDestroyRequest)
PS4_HLE_STUB(sceNpServiceChecker2IntFinalize)
PS4_HLE_STUB(sceNpServiceChecker2IntGetServiceAvailability)
PS4_HLE_STUB(sceNpServiceChecker2IntGetServiceAvailabilityA)
PS4_HLE_STUB(sceNpServiceChecker2IntInitialize)
PS4_HLE_STUB(sceNpServiceChecker2IntIsSetServiceType)
PS4_HLE_STUB(sceNpServiceCheckerIntAbortRequest)
PS4_HLE_STUB(sceNpServiceCheckerIntCreateRequest)
PS4_HLE_STUB(sceNpServiceCheckerIntDestroyRequest)
PS4_HLE_STUB(sceNpServiceCheckerIntFinalize)
PS4_HLE_STUB(sceNpServiceCheckerIntGetAvailability)
PS4_HLE_STUB(sceNpServiceCheckerIntGetAvailabilityList)
PS4_HLE_STUB(sceNpServiceCheckerIntInitialize)
PS4_HLE_STUB(sceNpServiceCheckerIntIsCached)
PS4_HLE_STUB(sceNpTitleMetadataIntAbortRequest)
PS4_HLE_STUB(sceNpTitleMetadataIntCreateRequest)
PS4_HLE_STUB(sceNpTitleMetadataIntDeleteRequest)
PS4_HLE_STUB(sceNpTitleMetadataIntGetInfo)
PS4_HLE_STUB(sceNpTitleMetadataIntGetNpTitleId)
PS4_HLE_STUB(sceNpUtilityInit)
PS4_HLE_STUB(sceNpUtilityTerm)
