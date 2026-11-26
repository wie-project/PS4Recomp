// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpAuth after shadPS4 src/core/libraries/np/np_auth/np_auth.cpp, on its path without a
// shadNet sign-in: the runtime is never signed in to an NP server (README). Requests are
// tracked as in shadPS4 (at most 16, ids from 0x10000001, the lowest free one first). A started
// request ends at once with ORBIS_NP_ERROR_SIGNED_OUT: a synchronous request returns it, an
// asynchronous one reports it through sceNpAuthPollAsync / sceNpAuthWaitAsync. The calls that
// name the player by online id fail with ORBIS_NP_ERROR_USER_NOT_FOUND, since a signed-out
// player has no online id.

#include "ps4_np_auth.h"
#include "ps4_user_service.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct NpAuthRequest {
    bool used;
    bool async;
    bool aborted;
    bool started;     // shadPS4: the request has a context
    bool has_result;  // shadPS4: the context has a result
    int32_t result;
} NpAuthRequest;

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static NpAuthRequest g_requests[ORBIS_NP_AUTH_REQUEST_LIMIT];

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

// shadPS4 SetResult: the first result stays.
static void set_result(NpAuthRequest *r, int32_t result) {
    if (!r->has_result) {
        r->has_result = true;
        r->result = result;
    }
}

// shadPS4 StartRequest, then the signed-out outcome and FinishStartedRequest: an asynchronous
// request returns 0 and keeps the result for the poll, a synchronous one returns the result.
static int32_t run_signed_out(int32_t req_id) {
    pthread_mutex_lock(&g_mutex);
    NpAuthRequest *r = NULL;
    int32_t ret = find_request(req_id, &r);
    if (ret == 0) {
        if (r->started) {
            ret = ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT; // already started
        } else if (r->aborted) {
            ret = ORBIS_NP_AUTH_ERROR_ABORTED;
        } else {
            r->started = true;
            set_result(r, ORBIS_NP_ERROR_SIGNED_OUT);
            ret = r->async ? 0 : r->result;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

int32_t sceNpAuthCreateRequest(void) {
    return create_request(false);
}

int32_t sceNpAuthCreateAsyncRequest(const OrbisNpAuthCreateAsyncRequestParameter *param) {
    if (!param) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthCreateAsyncRequestParameter)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    return create_request(true);
}

// shadPS4 GetAuthorizationCode (sceNpAuthGetAuthorizationCodeA and V3).
static int32_t get_authorization_code(int32_t req_id, const OrbisNpAuthGetAuthorizationCodeParameterA *param,
                                      const OrbisNpAuthorizationCode *auth_code) {
    if (!param || !auth_code) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthGetAuthorizationCodeParameterA)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    if (!param->client_id || !param->scope) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->user_id != ORBIS_USER_SERVICE_INITIAL_USER_ID) {
        return ORBIS_NP_ERROR_USER_NOT_FOUND;
    }
    return run_signed_out(req_id);
}

int32_t sceNpAuthGetAuthorizationCode(int32_t req_id, const OrbisNpAuthGetAuthorizationCodeParameter *param,
                                      OrbisNpAuthorizationCode *auth_code, int32_t *issuer_id) {
    (void)req_id;
    (void)issuer_id;
    if (!param || !auth_code) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthGetAuthorizationCodeParameter)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    if (!param->online_id || !param->client_id || !param->scope) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    return ORBIS_NP_ERROR_USER_NOT_FOUND; // no user has an online id while signed out
}

int32_t sceNpAuthGetAuthorizationCodeA(int32_t req_id, const OrbisNpAuthGetAuthorizationCodeParameterA *param,
                                       OrbisNpAuthorizationCode *auth_code, int32_t *issuer_id) {
    (void)issuer_id;
    return get_authorization_code(req_id, param, auth_code);
}

// shadPS4 GetIdToken (sceNpAuthGetIdTokenA and V3).
static int32_t get_id_token(int32_t req_id, const OrbisNpAuthGetIdTokenParameterA *param, const OrbisNpIdToken *token) {
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
    return run_signed_out(req_id);
}

int32_t sceNpAuthGetIdToken(int32_t req_id, const OrbisNpAuthGetIdTokenParameter *param, OrbisNpIdToken *token) {
    (void)req_id;
    if (!param || !token) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthGetIdTokenParameter)) {
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    if (!param->online_id || !param->client_id || !param->client_secret || !param->scope) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    return ORBIS_NP_ERROR_USER_NOT_FOUND; // no user has an online id while signed out
}

int32_t sceNpAuthGetIdTokenA(int32_t req_id, const OrbisNpAuthGetIdTokenParameterA *param, OrbisNpIdToken *token) {
    return get_id_token(req_id, param, token);
}

int32_t sceNpAuthSetTimeout(int32_t req_id, int32_t resolve_retry, uint32_t resolve_timeout, uint32_t conn_timeout,
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

int32_t sceNpAuthAbortRequest(int32_t req_id) {
    pthread_mutex_lock(&g_mutex);
    NpAuthRequest *r = NULL;
    const int32_t ret = find_request(req_id, &r);
    if (ret == 0) {
        if (r->started) {
            set_result(r, ORBIS_NP_AUTH_ERROR_ABORTED);
        } else {
            r->aborted = true;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

// shadPS4 FindAsyncRequest and the result of its context, which a started request here always
// has: sceNpAuthWaitAsync never blocks and sceNpAuthPollAsync never reports RUNNING.
static int32_t async_result(int32_t req_id, int32_t *result) {
    if (!result) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&g_mutex);
    NpAuthRequest *r = NULL;
    int32_t ret = find_request(req_id, &r);
    if (ret == 0) {
        if (!r->async || !r->started) {
            ret = ORBIS_NP_AUTH_ERROR_INVALID_ID;
        } else {
            *result = r->result;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

int32_t sceNpAuthWaitAsync(int32_t req_id, int32_t *result) {
    return async_result(req_id, result);
}

int32_t sceNpAuthPollAsync(int32_t req_id, int32_t *result) {
    const int32_t ret = async_result(req_id, result);
    return ret == 0 ? ORBIS_NP_AUTH_POLL_ASYNC_RET_FINISHED : ret;
}

int32_t sceNpAuthDeleteRequest(int32_t req_id) {
    pthread_mutex_lock(&g_mutex);
    NpAuthRequest *r = NULL;
    const int32_t ret = find_request(req_id, &r);
    if (ret == 0) {
        *r = (NpAuthRequest){0};
    }
    pthread_mutex_unlock(&g_mutex);
    return ret;
}

// Guest ABI shims

static void *guest_ptr(const GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

#define NP_AUTH_RET(v)                              \
    do {                                            \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v); \
        SHIM_RETURN();                              \
    } while (0)

void shim_sceNpAuthCreateRequest(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthCreateRequest());
}

void shim_sceNpAuthCreateAsyncRequest(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthCreateAsyncRequest(guest_ptr(ctx, ctx->rdi)));
}

void shim_sceNpAuthGetAuthorizationCode(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthGetAuthorizationCode((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx),
                                              guest_ptr(ctx, ctx->rcx)));
}

void shim_sceNpAuthGetAuthorizationCodeA(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthGetAuthorizationCodeA((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx),
                                               guest_ptr(ctx, ctx->rcx)));
}

// V3 differs from A only in a flag shadPS4 hands to the server request.
void shim_sceNpAuthGetAuthorizationCodeV3(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthGetAuthorizationCodeA((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx),
                                               guest_ptr(ctx, ctx->rcx)));
}

void shim_sceNpAuthGetIdToken(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthGetIdToken((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx)));
}

void shim_sceNpAuthGetIdTokenA(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthGetIdTokenA((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx)));
}

void shim_sceNpAuthGetIdTokenV3(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthGetIdTokenA((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx)));
}

void shim_sceNpAuthSetTimeout(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthSetTimeout((int32_t)ctx->rdi, (int32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx,
                                    (uint32_t)ctx->r8, (uint32_t)ctx->r9));
}

void shim_sceNpAuthAbortRequest(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthAbortRequest((int32_t)ctx->rdi));
}

void shim_sceNpAuthWaitAsync(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthWaitAsync((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
}

void shim_sceNpAuthPollAsync(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthPollAsync((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
}

void shim_sceNpAuthDeleteRequest(GuestContext *ctx) {
    NP_AUTH_RET(sceNpAuthDeleteRequest((int32_t)ctx->rdi));
}
