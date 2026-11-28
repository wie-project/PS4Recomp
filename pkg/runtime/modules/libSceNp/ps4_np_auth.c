// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpAuth after shadPS4 src/core/libraries/np/np_auth/np_auth.cpp, with the
// authorization-code request of np_handler/np_handler_auth.cpp.
//
// Requests are tracked as in shadPS4 (at most 16, ids from 0x10000001, the lowest free one
// first). Starting one gives it a context (shadPS4 AuthRequestCtx, shared between the request,
// the pending server request and the threads waiting on it, here by reference count) that
// receives a single result: a synchronous request blocks for it, an asynchronous one reports it
// through sceNpAuthPollAsync / sceNpAuthWaitAsync.
//
// With shadNet off (libSceNp/ps4_np_handler.h) a started request ends at once with
// ORBIS_NP_ERROR_SIGNED_OUT, and the calls that name the player by online id fail with
// ORBIS_NP_ERROR_USER_NOT_FOUND. With shadNet on, an authorization code is a
// GetAuthorizationCode request on the user's shadNet session; the ID token calls fill in the
// fixed token shadPS4 returns (it has no shadNet request for them yet).

#include "ps4_np_auth.h"
#include "ps4_np_handler.h"
#include "ps4_user_service.h"
#include "shadnet_client.h"
#include "shadnet_pb.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// shadPS4 AuthRequestCtx. g_mutex guards every field.
typedef struct AuthCtx {
    uint32_t refs;
    bool has_result;
    int32_t result;
    OrbisNpAuthorizationCode *auth_code; // filled by the code reply
    int32_t *issuer_id;
} AuthCtx;

typedef struct NpAuthRequest {
    bool used;
    bool async;
    bool aborted;
    AuthCtx *ctx; // shadPS4: the request was started
} NpAuthRequest;

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_result_cond = PTHREAD_COND_INITIALIZER; // a context got its result
static NpAuthRequest g_requests[ORBIS_NP_AUTH_REQUEST_LIMIT];

// shadPS4 g_shadnet_enabled, read when the library registers.
static bool shadnet_enabled(void) {
    ps4_np_handler_start();
    return ps4_np_handler_config()->enabled;
}

// g_mutex held.
static void ctx_release(AuthCtx *c) {
    if (--c->refs == 0) free(c);
}

// AuthRequestCtx::SetResult; g_mutex held. The first result stays.
static void set_result(AuthCtx *c, int32_t result) {
    if (c->has_result) return;
    c->has_result = true;
    c->result = result;
    pthread_cond_broadcast(&g_result_cond);
}

// g_mutex held. Blocks until c has a result.
static int32_t ctx_wait(AuthCtx *c) {
    while (!c->has_result) pthread_cond_wait(&g_result_cond, &g_mutex);
    return c->result;
}

// The slot of req_id, whether in use or not.
static NpAuthRequest *slot_of(int32_t req_id) {
    const int64_t i = (int64_t)req_id - ORBIS_NP_AUTH_REQUEST_ID_OFFSET - 1;
    return i >= 0 && i < ORBIS_NP_AUTH_REQUEST_LIMIT ? &g_requests[i] : NULL;
}

static int32_t create_request(bool async) {
    pthread_mutex_lock(&g_mutex);
    for (int32_t i = 0; i < ORBIS_NP_AUTH_REQUEST_LIMIT; i++) {
        if (!g_requests[i].used) {
            g_requests[i] = (NpAuthRequest){.used = true, .async = async};
            pthread_mutex_unlock(&g_mutex);
            return ORBIS_NP_AUTH_REQUEST_ID_OFFSET + 1 + i;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    fprintf(stderr, "[libSceNpAuth] request limit reached, cannot create new request\n");
    return ORBIS_NP_AUTH_ERROR_REQUEST_MAX;
}

// shadPS4 FindRequest; g_mutex held.
static int32_t find_request(int32_t req_id, NpAuthRequest **request) {
    if (req_id <= 0) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    NpAuthRequest *r = slot_of(req_id);
    if (!r || !r->used) {
        return ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND;
    }
    *request = r;
    return 0;
}

// shadPS4 StartRequest: gives the request its context. *ctx holds a reference for the caller.
static int32_t start_request(int32_t req_id, bool *async, AuthCtx **ctx) {
    pthread_mutex_lock(&g_mutex);
    NpAuthRequest *r = NULL;
    int32_t ret = find_request(req_id, &r);
    if (ret == 0) {
        if (r->ctx) {
            ret = ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT; // already started
        } else if (r->aborted) {
            ret = ORBIS_NP_AUTH_ERROR_ABORTED;
        } else if (!(r->ctx = calloc(1, sizeof(*r->ctx)))) {
            ret = ORBIS_NP_AUTH_ERROR_OUT_OF_MEMORY;
        } else {
            r->ctx->refs = 2; // the request's and the caller's
            *async = r->async;
            *ctx = r->ctx;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

// shadPS4 FinishStartedRequest: an asynchronous request returns 0 and keeps the result for the
// poll, a synchronous one blocks for the result and returns it. Drops the caller's reference.
static int32_t finish_started_request(bool async, AuthCtx *c) {
    pthread_mutex_lock(&g_mutex);
    const int32_t ret = async ? 0 : ctx_wait(c);
    ctx_release(c);
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

static void complete(AuthCtx *c, int32_t result) {
    pthread_mutex_lock(&g_mutex);
    set_result(c, result);
    pthread_mutex_unlock(&g_mutex);
}

//=============================================================================
// The authorization-code request (shadPS4 np_handler_auth.cpp)
//=============================================================================

// shadPS4 AuthErrorFromReply.
static int32_t auth_error_from_reply(uint8_t error) {
    switch (error) {
    case SHADNET_ERR_INVALID_INPUT: return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    case SHADNET_ERR_UNAUTHORIZED: return (int32_t)ORBIS_NP_ERROR_SIGNED_OUT;
    case SHADNET_ERR_TOO_SOON: return ORBIS_NP_AUTH_ERROR_SERVICE_BUSY;
    default: return ORBIS_NP_AUTH_ERROR_SERVICE_DOWN;
    }
}

// NpHandler::OnAuthReply. The code fills the request's buffer only while the request has no
// result (AuthRequestCtx::SetAuthorizationCode), so an aborted or deleted request is left alone.
static void on_auth_reply(void *user, int32_t user_id, uint8_t error, const uint8_t *body, size_t len) {
    (void)user_id;
    AuthCtx *c = user;
    int32_t result = 0;
    ShadPbArena arena = {0};
    ShadPb_GetAuthorizationCodeReply reply;
    if (error != SHADNET_ERR_NO_ERROR) {
        result = auth_error_from_reply(error);
    } else if (len < 4) {
        result = ORBIS_NP_AUTH_ERROR_NO_TOKEN_RECEIVED;
    } else {
        const uint32_t n = (uint32_t)body[0] | ((uint32_t)body[1] << 8) | ((uint32_t)body[2] << 16) |
                           ((uint32_t)body[3] << 24);
        if (len - 4 < n || !shadpb_decode(&shadpb_GetAuthorizationCodeReply_desc, body + 4, n, &reply, &arena) ||
            reply.code.len == 0 || reply.code.len >= sizeof(c->auth_code->code)) {
            result = ORBIS_NP_AUTH_ERROR_NO_TOKEN_RECEIVED;
        }
    }
    pthread_mutex_lock(&g_mutex);
    if (result == 0 && !c->has_result) {
        memset(c->auth_code, 0, sizeof(*c->auth_code));
        memcpy(c->auth_code->code, reply.code.data, reply.code.len);
        if (c->issuer_id) *c->issuer_id = ORBIS_NP_AUTH_ISSUER_ID;
    }
    set_result(c, result);
    ctx_release(c);
    pthread_mutex_unlock(&g_mutex);
    shadpb_arena_free(&arena);
}

static void on_auth_fail(void *user, int32_t error) {
    AuthCtx *c = user;
    pthread_mutex_lock(&g_mutex);
    set_result(c, error);
    ctx_release(c);
    pthread_mutex_unlock(&g_mutex);
}

static const Ps4NpReplyHandler g_auth_reply = {on_auth_reply, on_auth_fail};

// NpHandler::RequestAuthorizationCode: sends the request with a reference to c for the reply. A
// session that is not authenticated counts as signed out here.
static int32_t request_authorization_code(int32_t user_id, const char *client_id, const char *scope, AuthCtx *c) {
    const ShadPb_GetAuthorizationCodeRequest req = {.client_id = shadpb_str(client_id), .scope = shadpb_str(scope)};
    pthread_mutex_lock(&g_mutex);
    c->refs++;
    pthread_mutex_unlock(&g_mutex);
    const int32_t r = ps4_np_handler_send_request(user_id, SHADNET_CMD_GET_AUTHORIZATION_CODE, NULL, 0,
                                                  &shadpb_GetAuthorizationCodeRequest_desc, &req, &g_auth_reply, c);
    if (r < 0) {
        // Drops the reference meant for the reply; the caller's keeps c alive.
        pthread_mutex_lock(&g_mutex);
        c->refs--;
        pthread_mutex_unlock(&g_mutex);
    }
    return r == (int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN ? (int32_t)ORBIS_NP_ERROR_SIGNED_OUT : r;
}

//=============================================================================
// Library functions. mem is the guest memory base: the parameter structures hold guest
// addresses.
//=============================================================================

static void *guest_ptr(const uint8_t *mem, uint64_t addr) {
    return addr ? (void *)(mem + addr) : NULL;
}

static int32_t create_async_request(const OrbisNpAuthCreateAsyncRequestParameter *param) {
    if (!param) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthCreateAsyncRequestParameter)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    return create_request(true);
}

// shadPS4 GetAuthorizationCode (sceNpAuthGetAuthorizationCodeA and V3, whose flag it ignores).
static int32_t get_authorization_code(const uint8_t *mem, int32_t req_id,
                                      const OrbisNpAuthGetAuthorizationCodeParameterA *param,
                                      OrbisNpAuthorizationCode *auth_code, int32_t *issuer_id) {
    if (!param || !auth_code) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthGetAuthorizationCodeParameterA)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    if (!param->client_id || !param->scope) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->user_id != ORBIS_USER_SERVICE_INITIAL_USER_ID) { // the only logged-in user
        return ORBIS_NP_ERROR_USER_NOT_FOUND;
    }
    bool async = false;
    AuthCtx *c = NULL;
    const int32_t ret = start_request(req_id, &async, &c);
    if (ret != 0) {
        return ret;
    }
    if (!shadnet_enabled()) {
        complete(c, (int32_t)ORBIS_NP_ERROR_SIGNED_OUT);
        return finish_started_request(async, c);
    }
    const OrbisNpClientId *id = guest_ptr(mem, param->client_id);
    char client_id[sizeof(id->id) + 1];
    const size_t n = strnlen(id->id, sizeof(id->id));
    memcpy(client_id, id->id, n);
    client_id[n] = '\0';
    pthread_mutex_lock(&g_mutex);
    c->auth_code = auth_code;
    c->issuer_id = issuer_id;
    pthread_mutex_unlock(&g_mutex);
    const int32_t submit = request_authorization_code(param->user_id, client_id, guest_ptr(mem, param->scope), c);
    if (submit < 0) {
        complete(c, submit);
    }
    return finish_started_request(async, c);
}

static int32_t get_authorization_code_by_online_id(const uint8_t *mem, int32_t req_id,
                                                   const OrbisNpAuthGetAuthorizationCodeParameter *param,
                                                   OrbisNpAuthorizationCode *auth_code, int32_t *issuer_id) {
    if (!param || !auth_code) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthGetAuthorizationCodeParameter)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    if (!param->online_id || !param->client_id || !param->scope) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (!shadnet_enabled()) {
        return ORBIS_NP_ERROR_USER_NOT_FOUND; // no user has an online id while signed out
    }
    const int32_t user_id = ps4_np_handler_user_by_online_id(guest_ptr(mem, param->online_id));
    if (user_id < 0) {
        return ORBIS_NP_ERROR_USER_NOT_FOUND;
    }
    const OrbisNpAuthGetAuthorizationCodeParameterA internal = {
        .size = sizeof(internal),
        .user_id = user_id,
        .client_id = param->client_id,
        .scope = param->scope,
    };
    return get_authorization_code(mem, req_id, &internal, auth_code, issuer_id);
}

// shadPS4 GetIdToken (sceNpAuthGetIdTokenA and V3).
static int32_t get_id_token(int32_t req_id, const OrbisNpAuthGetIdTokenParameterA *param, OrbisNpIdToken *token) {
    if (!param || !token) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthGetIdTokenParameterA)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    if (param->user_id == ORBIS_USER_SERVICE_USER_ID_INVALID || !param->client_id || !param->client_secret ||
        !param->scope) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    bool async = false;
    AuthCtx *c = NULL;
    const int32_t ret = start_request(req_id, &async, &c);
    if (ret != 0) {
        return ret;
    }
    if (!shadnet_enabled()) {
        complete(c, (int32_t)ORBIS_NP_ERROR_SIGNED_OUT);
        return finish_started_request(async, c);
    }
    // shadPS4 has no token request on shadNet yet and answers with a fixed token.
    memset(token, 0, sizeof(*token));
    memcpy(token->token, "TOKEN", 6);
    complete(c, 0);
    return finish_started_request(async, c);
}

static int32_t get_id_token_by_online_id(int32_t req_id, const OrbisNpAuthGetIdTokenParameter *param,
                                         OrbisNpIdToken *token) {
    if (!param || !token) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthGetIdTokenParameter)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    if (!param->online_id || !param->client_id || !param->client_secret || !param->scope) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (!shadnet_enabled()) {
        return ORBIS_NP_ERROR_USER_NOT_FOUND; // no user has an online id while signed out
    }
    // shadPS4 takes the initial user for the online id.
    const OrbisNpAuthGetIdTokenParameterA internal = {
        .size = sizeof(internal),
        .user_id = ORBIS_USER_SERVICE_INITIAL_USER_ID,
        .client_id = param->client_id,
        .client_secret = param->client_secret,
        .scope = param->scope,
    };
    return get_id_token(req_id, &internal, token);
}

static int32_t set_timeout(int32_t req_id, int32_t resolve_retry, uint32_t resolve_timeout, uint32_t conn_timeout,
                           uint32_t send_timeout, uint32_t recv_timeout) {
    const uint32_t min_resolve_us = 1000000, min_transfer_us = 10000000;
#define NP_AUTH_TIMEOUT_VALID(v, min) ((v) == ORBIS_NP_AUTH_TIMEOUT_NO_EFFECT || (v) >= (min))
    if (resolve_retry < 0 || !NP_AUTH_TIMEOUT_VALID(resolve_timeout, min_resolve_us) ||
        !NP_AUTH_TIMEOUT_VALID(conn_timeout, min_transfer_us) || !NP_AUTH_TIMEOUT_VALID(send_timeout, min_transfer_us) ||
        !NP_AUTH_TIMEOUT_VALID(recv_timeout, min_transfer_us) ||
        (resolve_retry == 0 && resolve_timeout == 0 && conn_timeout == 0 && send_timeout == 0 && recv_timeout == 0)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
#undef NP_AUTH_TIMEOUT_VALID
    pthread_mutex_lock(&g_mutex);
    NpAuthRequest *r = NULL;
    const int32_t ret = find_request(req_id, &r);
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

static int32_t abort_request(int32_t req_id) {
    pthread_mutex_lock(&g_mutex);
    NpAuthRequest *r = NULL;
    const int32_t ret = find_request(req_id, &r);
    if (ret == 0) {
        if (r->ctx) {
            set_result(r->ctx, ORBIS_NP_AUTH_ERROR_ABORTED);
        } else {
            r->aborted = true;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

// shadPS4 FindAsyncRequest; g_mutex held.
static int32_t find_async_request(int32_t req_id, AuthCtx **ctx) {
    NpAuthRequest *r = NULL;
    const int32_t ret = find_request(req_id, &r);
    if (ret != 0) {
        return ret;
    }
    if (!r->async || !r->ctx) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ID;
    }
    *ctx = r->ctx;
    return 0;
}

static int32_t wait_async(int32_t req_id, int32_t *result) {
    if (!result) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&g_mutex);
    AuthCtx *c = NULL;
    const int32_t ret = find_async_request(req_id, &c);
    if (ret == 0) {
        c->refs++; // the context outlives a deletion while this thread waits
        *result = ctx_wait(c);
        ctx_release(c);
    }
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

static int32_t poll_async(int32_t req_id, int32_t *result) {
    if (!result) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&g_mutex);
    AuthCtx *c = NULL;
    int32_t ret = find_async_request(req_id, &c);
    if (ret == 0) {
        if (!c->has_result) {
            ret = ORBIS_NP_AUTH_POLL_ASYNC_RET_RUNNING;
        } else {
            *result = c->result;
            ret = ORBIS_NP_AUTH_POLL_ASYNC_RET_FINISHED;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

static int32_t delete_request(int32_t req_id) {
    pthread_mutex_lock(&g_mutex);
    NpAuthRequest *r = NULL;
    const int32_t ret = find_request(req_id, &r);
    if (ret == 0) {
        if (r->ctx) {
            set_result(r->ctx, ORBIS_NP_AUTH_ERROR_ABORTED);
            ctx_release(r->ctx);
        }
        *r = (NpAuthRequest){0};
    }
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

//=============================================================================
// Guest ABI shims
//=============================================================================

#define NP_AUTH_RET(v)                              \
    do {                                            \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v); \
        SHIM_RETURN();                              \
    } while (0)

void shim_sceNpAuthCreateRequest(GuestContext *ctx) {
    NP_AUTH_RET(create_request(false));
}

void shim_sceNpAuthCreateAsyncRequest(GuestContext *ctx) {
    NP_AUTH_RET(create_async_request(guest_ptr(ctx->mem_base, ctx->rdi)));
}

void shim_sceNpAuthGetAuthorizationCode(GuestContext *ctx) {
    NP_AUTH_RET(get_authorization_code_by_online_id(ctx->mem_base, (int32_t)ctx->rdi, guest_ptr(ctx->mem_base, ctx->rsi),
                                                    guest_ptr(ctx->mem_base, ctx->rdx),
                                                    guest_ptr(ctx->mem_base, ctx->rcx)));
}

void shim_sceNpAuthGetAuthorizationCodeA(GuestContext *ctx) {
    NP_AUTH_RET(get_authorization_code(ctx->mem_base, (int32_t)ctx->rdi, guest_ptr(ctx->mem_base, ctx->rsi),
                                       guest_ptr(ctx->mem_base, ctx->rdx), guest_ptr(ctx->mem_base, ctx->rcx)));
}

// V3 differs from A only in a flag shadPS4 does not use.
void shim_sceNpAuthGetAuthorizationCodeV3(GuestContext *ctx) {
    shim_sceNpAuthGetAuthorizationCodeA(ctx);
}

void shim_sceNpAuthGetIdToken(GuestContext *ctx) {
    NP_AUTH_RET(get_id_token_by_online_id((int32_t)ctx->rdi, guest_ptr(ctx->mem_base, ctx->rsi),
                                          guest_ptr(ctx->mem_base, ctx->rdx)));
}

void shim_sceNpAuthGetIdTokenA(GuestContext *ctx) {
    NP_AUTH_RET(get_id_token((int32_t)ctx->rdi, guest_ptr(ctx->mem_base, ctx->rsi), guest_ptr(ctx->mem_base, ctx->rdx)));
}

void shim_sceNpAuthGetIdTokenV3(GuestContext *ctx) {
    shim_sceNpAuthGetIdTokenA(ctx);
}

void shim_sceNpAuthSetTimeout(GuestContext *ctx) {
    NP_AUTH_RET(set_timeout((int32_t)ctx->rdi, (int32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx,
                            (uint32_t)ctx->r8, (uint32_t)ctx->r9));
}

void shim_sceNpAuthAbortRequest(GuestContext *ctx) {
    NP_AUTH_RET(abort_request((int32_t)ctx->rdi));
}

void shim_sceNpAuthWaitAsync(GuestContext *ctx) {
    NP_AUTH_RET(wait_async((int32_t)ctx->rdi, guest_ptr(ctx->mem_base, ctx->rsi)));
}

void shim_sceNpAuthPollAsync(GuestContext *ctx) {
    NP_AUTH_RET(poll_async((int32_t)ctx->rdi, guest_ptr(ctx->mem_base, ctx->rsi)));
}

void shim_sceNpAuthDeleteRequest(GuestContext *ctx) {
    NP_AUTH_RET(delete_request((int32_t)ctx->rdi));
}
