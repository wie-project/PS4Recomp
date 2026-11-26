// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpManager after shadPS4 src/core/libraries/np/np_manager.cpp, on its path without a
// shadNet sign-in: the runtime is never signed in to an NP server (README "User").
//
// Requests are tracked as in shadPS4 (at most 32 active, ids from 0x20000001, a free slot is
// reused first). A request that needs the server completes at once with
// ORBIS_NP_ERROR_SIGNED_OUT: a synchronous call returns it, an asynchronous one returns 0 and
// reports it through sceNpPollAsync / sceNpWaitAsync. A user is only known by user id: no user
// has an online id or account id while signed out, so the calls that name one fail with
// ORBIS_NP_ERROR_USER_NOT_FOUND or ORBIS_NP_ERROR_SIGNED_OUT as in shadPS4.
//
// NP state: each login or logout event the game takes from libSceUserService queues a SignedOut
// state for that user; sceNpCheckCallback / sceNpCheckCallbackForLib deliver the queued states to
// the state callbacks on the calling thread, then the reachability callback (Unavailable) when a
// user's reachability changed. Game presence and PS Plus callbacks are stored and never called:
// nothing produces those events (as in shadPS4).

#include "ps4_np_manager.h"
#include "ps4_kernel_sys.h"
#include "ps4_user_service.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum NpRequestState {
    REQ_NONE = 0,
    REQ_READY = 1,
    REQ_ABORTED = 2,
    REQ_COMPLETE = 3,
} NpRequestState;

typedef struct NpRequest {
    NpRequestState state;
    bool async;
    int32_t result;
} NpRequest;

typedef struct NpCallback {
    uint64_t func; // guest address; 0 when unused
    uint64_t userdata;
} NpCallback;

typedef struct NpStateEvent {
    int32_t user_id;
    uint32_t state;
} NpStateEvent;

typedef struct GamePresenceCallbackA {
    int32_t handle;
    NpCallback cb;
} GamePresenceCallbackA;

#define REACHABILITY_USERS 16

static pthread_mutex_t g_req_mutex = PTHREAD_MUTEX_INITIALIZER;
static NpRequest g_requests[ORBIS_NP_MANAGER_REQUEST_LIMIT];
static int32_t g_requests_size; // slots ever used (shadPS4's vector size)
static int32_t g_active_requests;

static pthread_mutex_t g_cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static NpCallback g_legacy_cb, g_toolkit_cb, g_reachability_cb;
static NpCallback g_state_cbs_a[ORBIS_NP_STATE_CALLBACK_MAX];
static NpStateEvent *g_events;
static size_t g_events_len, g_events_cap;
// Last reachability delivered to the reachability callback, per user.
static struct {
    int32_t user_id;
    uint32_t state;
} g_reach_last[REACHABILITY_USERS];
static int g_reach_last_len;

static pthread_mutex_t g_presence_mutex = PTHREAD_MUTEX_INITIALIZER;
static NpCallback g_presence_cb, g_plus_cb;
static GamePresenceCallbackA g_presence_cbs_a[ORBIS_NP_STATE_CALLBACK_MAX];
static int32_t g_presence_next_handle;

static void *guest_ptr(const GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

// The only user: the initial user of libSceUserService (shadPS4 UserManagement.GetUserByID).
static bool user_exists(int32_t user_id) {
    return user_id == ORBIS_USER_SERVICE_INITIAL_USER_ID;
}

// ---------------------------------------------------------------------------------------------
// Requests

static int32_t create_request(bool async) {
    pthread_mutex_lock(&g_req_mutex);
    if (g_active_requests == ORBIS_NP_MANAGER_REQUEST_LIMIT) {
        pthread_mutex_unlock(&g_req_mutex);
        return (int32_t)ORBIS_NP_ERROR_REQUEST_MAX;
    }
    int32_t i = 0;
    while (i < g_requests_size && g_requests[i].state != REQ_NONE) {
        i++;
    }
    if (i == g_requests_size) {
        g_requests_size++;
    }
    g_requests[i] = (NpRequest){.state = REQ_READY, .async = async, .result = 0};
    g_active_requests++;
    pthread_mutex_unlock(&g_req_mutex);
    return i + ORBIS_NP_MANAGER_REQUEST_ID_OFFSET + 1;
}

// The live request of req_id, or NULL; g_req_mutex held.
static NpRequest *lookup(int32_t req_id) {
    const int64_t i = (int64_t)req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || i < 0 || i >= g_requests_size || g_requests[i].state == REQ_NONE) {
        return NULL;
    }
    return &g_requests[i];
}

// shadPS4 GetRequest: a request that can still run; g_req_mutex held.
static NpRequest *get_request(int32_t req_id, int32_t *err) {
    if (req_id == 0) {
        *err = (int32_t)ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return NULL;
    }
    NpRequest *r = lookup(req_id);
    if (!r) {
        *err = (int32_t)ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
        return NULL;
    }
    if (r->state == REQ_COMPLETE) {
        *err = r->result = (int32_t)ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return NULL;
    }
    if (r->state == REQ_ABORTED) {
        *err = r->result = (int32_t)ORBIS_NP_ERROR_ABORTED;
        return NULL;
    }
    return r;
}

// Runs a server request of req_id: it completes as signed out (shadPS4 CompleteRequest).
static int32_t complete_signed_out(int32_t req_id) {
    pthread_mutex_lock(&g_req_mutex);
    int32_t ret;
    NpRequest *r = get_request(req_id, &ret);
    if (r) {
        r->state = REQ_COMPLETE;
        r->result = (int32_t)ORBIS_NP_ERROR_SIGNED_OUT;
        ret = r->async ? 0 : r->result;
    }
    pthread_mutex_unlock(&g_req_mutex);
    return ret;
}

static int32_t find_live(int32_t req_id) {
    pthread_mutex_lock(&g_req_mutex);
    const int32_t ret = lookup(req_id) ? 0 : (int32_t)ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    pthread_mutex_unlock(&g_req_mutex);
    return ret;
}

static int32_t async_result(int32_t req_id, int32_t *result) {
    if (!result) {
        return (int32_t)ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&g_req_mutex);
    int32_t ret = 0;
    NpRequest *r = lookup(req_id);
    if (!r) {
        ret = (int32_t)ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    } else if (!r->async || r->state == REQ_READY) {
        ret = (int32_t)ORBIS_NP_ERROR_INVALID_ID;
    } else {
        *result = r->result;
    }
    pthread_mutex_unlock(&g_req_mutex);
    return ret;
}

#define RET(v)                                      \
    do {                                            \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v); \
        SHIM_RETURN();                              \
    } while (0)

#define INVALID_USER(u) ((int32_t)(u) == ORBIS_USER_SERVICE_USER_ID_INVALID)
#define E_ARG ((int32_t)ORBIS_NP_ERROR_INVALID_ARGUMENT)
#define E_SIZE ((int32_t)ORBIS_NP_ERROR_INVALID_SIZE)
#define E_SIGNED_OUT ((int32_t)ORBIS_NP_ERROR_SIGNED_OUT)
#define E_USER_NOT_FOUND ((int32_t)ORBIS_NP_ERROR_USER_NOT_FOUND)
#define E_NOT_REGISTERED ((int32_t)ORBIS_NP_ERROR_CALLBACK_NOT_REGISTERED)
#define E_ALREADY_REGISTERED ((int32_t)ORBIS_NP_ERROR_CALLBACK_ALREADY_REGISTERED)

void shim_sceNpCreateRequest(GuestContext *ctx) {
    RET(create_request(false));
}

void shim_sceNpCreateAsyncRequest(GuestContext *ctx) {
    const OrbisNpCreateAsyncRequestParameter *param = guest_ptr(ctx, ctx->rdi);
    if (!param) RET(E_ARG);
    if (param->size != sizeof(OrbisNpCreateAsyncRequestParameter)) RET(E_SIZE);
    RET(create_request(true));
}

// (req_id, online_id): no user has an online id while signed out.
void shim_sceNpCheckNpAvailability(GuestContext *ctx) {
    RET(ctx->rsi ? E_USER_NOT_FOUND : E_ARG);
}

// (req_id, user_id)
void shim_sceNpCheckNpAvailabilityA(GuestContext *ctx) {
    if (INVALID_USER(ctx->rsi)) RET(E_ARG);
    RET(complete_signed_out((int32_t)ctx->rdi));
}

// (req_id, user_id)
void shim_sceNpCheckNpReachability(GuestContext *ctx) {
    if (INVALID_USER(ctx->rsi)) RET(E_ARG);
    RET(complete_signed_out((int32_t)ctx->rdi));
}

// (req_id, const OrbisNpCheckPlusParameter *, OrbisNpCheckPlusResult *)
void shim_sceNpCheckPlus(GuestContext *ctx) {
    const OrbisNpCheckPlusParameter *param = guest_ptr(ctx, ctx->rsi);
    if ((int32_t)ctx->rdi == 0 || !param || !ctx->rdx) RET(E_ARG);
    if (param->size != sizeof(OrbisNpCheckPlusParameter)) RET(E_SIZE);
    if (param->user_id == ORBIS_USER_SERVICE_USER_ID_INVALID) RET(E_ARG);
    if (param->features < 1 || param->features > 3) RET(E_ARG);
    for (size_t i = 0; i < sizeof(param->reserved); i++) {
        if (param->reserved[i] != 0) RET(E_ARG);
    }
    RET(complete_signed_out((int32_t)ctx->rdi));
}

// (req_id, online_id, OrbisNpLanguageCode *)
void shim_sceNpGetAccountLanguage(GuestContext *ctx) {
    RET(ctx->rsi && ctx->rdx ? E_USER_NOT_FOUND : E_ARG);
}

// (req_id, user_id, OrbisNpLanguageCode *)
void shim_sceNpGetAccountLanguageA(GuestContext *ctx) {
    if (!ctx->rdx || INVALID_USER(ctx->rsi)) RET(E_ARG);
    RET(complete_signed_out((int32_t)ctx->rdi));
}

// (req_id, online_id, int8_t *age, OrbisNpParentalControlInfo *)
void shim_sceNpGetParentalControlInfo(GuestContext *ctx) {
    RET(ctx->rsi && ctx->rdx && ctx->rcx ? E_USER_NOT_FOUND : E_ARG);
}

// (req_id, user_id, int8_t *age, OrbisNpParentalControlInfo *)
void shim_sceNpGetParentalControlInfoA(GuestContext *ctx) {
    if (!ctx->rdx || !ctx->rcx || INVALID_USER(ctx->rsi)) RET(E_ARG);
    RET(complete_signed_out((int32_t)ctx->rdi));
}

void shim_sceNpAbortRequest(GuestContext *ctx) {
    pthread_mutex_lock(&g_req_mutex);
    int32_t ret = 0;
    NpRequest *r = lookup((int32_t)ctx->rdi);
    if (!r) {
        ret = (int32_t)ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    } else if (r->state != REQ_COMPLETE) { // aborting a completed request is ignored
        r->state = REQ_ABORTED;
    }
    pthread_mutex_unlock(&g_req_mutex);
    RET(ret);
}

// (req_id, resolve_retry, resolve_timeout, conn_timeout, send_timeout, recv_timeout); the
// timeouts are checked and kept nowhere: no request reaches a server.
void shim_sceNpSetTimeout(GuestContext *ctx) {
    const int32_t req_id = (int32_t)ctx->rdi, retry = (int32_t)ctx->rsi;
    const uint32_t resolve = (uint32_t)ctx->rdx, conn = (uint32_t)ctx->rcx, send = (uint32_t)ctx->r8,
                   recv = (uint32_t)ctx->r9;
    if (req_id <= 0) RET(E_ARG);
#define UNSET_OR_AT_LEAST(v, min) ((v) == 0 || (v) >= (min))
    const bool all_zero = retry == 0 && resolve == 0 && conn == 0 && send == 0 && recv == 0;
    if (all_zero || retry < 0 || !UNSET_OR_AT_LEAST(resolve, 1000000u) || !UNSET_OR_AT_LEAST(conn, 10000000u) ||
        !UNSET_OR_AT_LEAST(send, 10000000u) || !UNSET_OR_AT_LEAST(recv, 10000000u)) {
        RET(E_ARG);
    }
#undef UNSET_OR_AT_LEAST
    RET(find_live(req_id));
}

void shim_sceNpWaitAsync(GuestContext *ctx) {
    RET(async_result((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
}

void shim_sceNpPollAsync(GuestContext *ctx) {
    RET(async_result((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
}

void shim_sceNpDeleteRequest(GuestContext *ctx) {
    pthread_mutex_lock(&g_req_mutex);
    int32_t ret = 0;
    NpRequest *r = lookup((int32_t)ctx->rdi);
    if (!r) {
        ret = (int32_t)ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    } else {
        r->state = REQ_NONE;
        g_active_requests--;
    }
    pthread_mutex_unlock(&g_req_mutex);
    RET(ret);
}

// ---------------------------------------------------------------------------------------------
// Account queries

// (online_id, OrbisNpCountryCode *)
void shim_sceNpGetAccountCountry(GuestContext *ctx) {
    RET(ctx->rdi && ctx->rsi ? E_USER_NOT_FOUND : E_ARG);
}

// (user_id, OrbisNpCountryCode *)
void shim_sceNpGetAccountCountryA(GuestContext *ctx) {
    if (!ctx->rsi || INVALID_USER(ctx->rdi)) RET(E_ARG);
    RET(user_exists((int32_t)ctx->rdi) ? E_SIGNED_OUT : E_USER_NOT_FOUND);
}

// (online_id, OrbisNpDate *)
void shim_sceNpGetAccountDateOfBirth(GuestContext *ctx) {
    RET(ctx->rdi && ctx->rsi ? E_USER_NOT_FOUND : E_ARG);
}

// (user_id, OrbisNpDate *)
void shim_sceNpGetAccountDateOfBirthA(GuestContext *ctx) {
    if (!ctx->rsi || INVALID_USER(ctx->rdi)) RET(E_ARG);
    RET(user_exists((int32_t)ctx->rdi) ? E_SIGNED_OUT : E_USER_NOT_FOUND);
}

// (online_id, int32_t *status): offline.
void shim_sceNpGetGamePresenceStatus(GuestContext *ctx) {
    int32_t *status = guest_ptr(ctx, ctx->rsi);
    if (!ctx->rdi || !status) RET(E_ARG);
    *status = ORBIS_NP_GAME_PRESENCE_STATUS_OFFLINE;
    RET(0);
}

// (user_id, int32_t *status): offline.
void shim_sceNpGetGamePresenceStatusA(GuestContext *ctx) {
    int32_t *status = guest_ptr(ctx, ctx->rsi);
    if (!status || INVALID_USER(ctx->rdi)) RET(E_ARG);
    *status = ORBIS_NP_GAME_PRESENCE_STATUS_OFFLINE;
    RET(0);
}

// (online_id, uint64_t *account_id)
void shim_sceNpGetAccountId(GuestContext *ctx) {
    uint64_t *account_id = guest_ptr(ctx, ctx->rsi);
    if (!ctx->rdi || !account_id) RET(E_ARG);
    *account_id = 0;
    RET(E_USER_NOT_FOUND);
}

// (user_id, uint64_t *account_id)
void shim_sceNpGetAccountIdA(GuestContext *ctx) {
    uint64_t *account_id = guest_ptr(ctx, ctx->rsi);
    if (!account_id || INVALID_USER(ctx->rdi)) RET(E_ARG);
    *account_id = 0;
    RET(E_SIGNED_OUT);
}

// An invalid user id: USER_NOT_FOUND for games built before firmware 9.00.
static int32_t invalid_user_error(const GuestContext *ctx) {
    return ps4_compiled_sdk_version(ctx) < ORBIS_NP_SDK_FW_900 ? E_USER_NOT_FOUND : E_ARG;
}

// (user_id, OrbisNpId *)
void shim_sceNpGetNpId(GuestContext *ctx) {
    if (INVALID_USER(ctx->rdi)) RET(invalid_user_error(ctx));
    if (!ctx->rsi) RET(E_ARG);
    RET(E_SIGNED_OUT);
}

// (user_id, OrbisNpOnlineId *)
void shim_sceNpGetOnlineId(GuestContext *ctx) {
    if (INVALID_USER(ctx->rdi)) RET(invalid_user_error(ctx));
    if (!ctx->rsi) RET(E_ARG);
    RET(E_SIGNED_OUT);
}

// (user_id, int32_t *state): Unavailable.
void shim_sceNpGetNpReachabilityState(GuestContext *ctx) {
    int32_t *state = guest_ptr(ctx, ctx->rsi);
    if (!state) RET(E_ARG);
    if (INVALID_USER(ctx->rdi) && ps4_compiled_sdk_version(ctx) >= ORBIS_NP_SDK_FW_400) RET(E_ARG);
    *state = ORBIS_NP_REACHABILITY_STATE_UNAVAILABLE;
    RET(0);
}

// (user_id, uint32_t *state): SignedOut.
void shim_sceNpGetState(GuestContext *ctx) {
    uint32_t *state = guest_ptr(ctx, ctx->rsi);
    if (!state) RET(E_ARG);
    if (INVALID_USER(ctx->rdi) && ps4_compiled_sdk_version(ctx) >= ORBIS_NP_SDK_FW_900) RET(E_ARG);
    *state = ORBIS_NP_STATE_SIGNED_OUT;
    RET(0);
}

// (account_id, int32_t *user_id)
void shim_sceNpGetUserIdByAccountId(GuestContext *ctx) {
    if (ctx->rdi == 0 || !ctx->rsi) RET(E_ARG);
    RET(E_SIGNED_OUT);
}

// (online_id, int32_t *user_id)
void shim_sceNpGetUserIdByOnlineId(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi) RET(E_ARG);
    RET(E_SIGNED_OUT);
}

// (user_id, bool *): a user has signed up when an NP account is configured for it; none is.
void shim_sceNpHasSignedUp(GuestContext *ctx) {
    bool *signed_up = guest_ptr(ctx, ctx->rsi);
    if (!signed_up) RET(E_ARG);
    *signed_up = false;
    if (INVALID_USER(ctx->rdi) && ps4_compiled_sdk_version(ctx) >= ORBIS_NP_SDK_FW_900) RET(E_ARG);
    RET(user_exists((int32_t)ctx->rdi) ? 0 : E_USER_NOT_FOUND);
}

// (const OrbisNpContentRestriction *): checked, then no effect (shadPS4 stubs the rest).
void shim_sceNpSetContentRestriction(GuestContext *ctx) {
    const OrbisNpContentRestriction *r = guest_ptr(ctx, ctx->rdi);
    if (!r) RET(E_ARG);
    if (r->size != sizeof(OrbisNpContentRestriction)) RET(E_SIZE);
    if (r->default_age_restriction < 0 || r->age_restriction_count > 0x100) RET(E_ARG);
    if (r->age_restriction_count > 0 && !r->age_restriction) RET(E_ARG);
    RET(0);
}

// (const OrbisNpTitleId *, const OrbisNpTitleSecret *): checked, then no effect.
void shim_sceNpSetNpTitleId(GuestContext *ctx) {
    RET(ctx->rdi && ctx->rsi ? 0 : E_ARG);
}

// ---------------------------------------------------------------------------------------------
// NP state and its callbacks

void ps4_np_manager_notify_user_event(int32_t eventType, int32_t userId) {
    if (eventType != ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN && eventType != ORBIS_USER_SERVICE_EVENT_TYPE_LOGOUT) {
        return;
    }
    pthread_mutex_lock(&g_cb_mutex);
    if (g_events_len == g_events_cap) {
        const size_t cap = g_events_cap ? g_events_cap * 2 : 4;
        NpStateEvent *grown = realloc(g_events, cap * sizeof(*grown));
        if (!grown) {
            pthread_mutex_unlock(&g_cb_mutex);
            fprintf(stderr, "[libSceNpManager] out of memory, NP state of user %d dropped\n", userId);
            return;
        }
        g_events = grown;
        g_events_cap = cap;
    }
    g_events[g_events_len++] = (NpStateEvent){.user_id = userId, .state = ORBIS_NP_STATE_SIGNED_OUT};
    pthread_mutex_unlock(&g_cb_mutex);
}

// Calls a guest callback with up to four integer arguments.
static void call_guest(GuestContext *ctx, uint64_t func, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    recomp_shim_call_guest(ctx, func, 0, a0, a1, a2, a3);
}

// Records the reachability of a user; true when it changed. g_cb_mutex held.
static bool reachability_changed(int32_t user_id, uint32_t state) {
    for (int i = 0; i < g_reach_last_len; i++) {
        if (g_reach_last[i].user_id == user_id) {
            if (g_reach_last[i].state == state) return false;
            g_reach_last[i].state = state;
            return true;
        }
    }
    if (g_reach_last_len < REACHABILITY_USERS) {
        g_reach_last[g_reach_last_len].user_id = user_id;
        g_reach_last[g_reach_last_len].state = state;
        g_reach_last_len++;
    }
    return true;
}

// shadPS4 DispatchPendingNpStateCallbacks.
static void dispatch_state_callbacks(GuestContext *ctx) {
    NpStateEvent local[8];
    NpStateEvent *events = local;
    int32_t reach_users[8];
    int32_t *reach = reach_users;
    size_t n, n_reach = 0;
    NpCallback legacy, toolkit, reachability, cbs_a[ORBIS_NP_STATE_CALLBACK_MAX];

    pthread_mutex_lock(&g_cb_mutex);
    n = g_events_len;
    if (n == 0) {
        pthread_mutex_unlock(&g_cb_mutex);
        return;
    }
    if (n > sizeof(local) / sizeof(local[0])) {
        events = malloc(n * sizeof(*events));
        reach = malloc(n * sizeof(*reach));
        if (!events || !reach) {
            free(events);
            free(reach);
            pthread_mutex_unlock(&g_cb_mutex);
            fprintf(stderr, "[libSceNpManager] out of memory, NP state callbacks deferred\n");
            return;
        }
    }
    memcpy(events, g_events, n * sizeof(*events));
    g_events_len = 0;
    legacy = g_legacy_cb;
    toolkit = g_toolkit_cb;
    reachability = g_reachability_cb;
    memcpy(cbs_a, g_state_cbs_a, sizeof(cbs_a));
    if (reachability.func) {
        for (size_t i = 0; i < n; i++) {
            const uint32_t r = events[i].state == ORBIS_NP_STATE_SIGNED_IN ? ORBIS_NP_REACHABILITY_STATE_REACHABLE
                                                                           : ORBIS_NP_REACHABILITY_STATE_UNAVAILABLE;
            if (reachability_changed(events[i].user_id, r)) {
                reach[n_reach++] = events[i].user_id;
            }
        }
    }
    pthread_mutex_unlock(&g_cb_mutex);

    for (size_t i = 0; i < n; i++) {
        const uint64_t user = (uint64_t)(int64_t)events[i].user_id, state = events[i].state;
        if (legacy.func) {
            call_guest(ctx, legacy.func, user, state, 0 /* no NpId while signed out */, legacy.userdata);
        }
        for (int k = 0; k < ORBIS_NP_STATE_CALLBACK_MAX; k++) {
            if (cbs_a[k].func) {
                call_guest(ctx, cbs_a[k].func, user, state, cbs_a[k].userdata, 0);
            }
        }
        if (toolkit.func) {
            call_guest(ctx, toolkit.func, user, state, toolkit.userdata, 0);
        }
    }
    for (size_t i = 0; i < n_reach; i++) {
        call_guest(ctx, reachability.func, (uint64_t)(int64_t)reach[i], ORBIS_NP_REACHABILITY_STATE_UNAVAILABLE,
                   reachability.userdata, 0);
    }
    if (events != local) {
        free(events);
        free(reach);
    }
}

void shim_sceNpCheckCallback(GuestContext *ctx) {
    dispatch_state_callbacks(ctx);
    RET(0);
}

void shim_sceNpCheckCallbackForLib(GuestContext *ctx) {
    dispatch_state_callbacks(ctx);
    RET(0);
}

// Sets a single-registration callback (legacy, reachability).
static int32_t register_single(NpCallback *slot, uint64_t func, uint64_t userdata, bool reset_reach) {
    if (!func) return E_ARG;
    pthread_mutex_lock(&g_cb_mutex);
    int32_t ret = 0;
    if (slot->func) {
        ret = E_ALREADY_REGISTERED;
    } else {
        *slot = (NpCallback){.func = func, .userdata = userdata};
        if (reset_reach) g_reach_last_len = 0;
    }
    pthread_mutex_unlock(&g_cb_mutex);
    return ret;
}

static int32_t unregister_single(NpCallback *slot, bool reset_reach) {
    pthread_mutex_lock(&g_cb_mutex);
    int32_t ret = 0;
    if (!slot->func) {
        ret = E_NOT_REGISTERED;
    } else {
        *slot = (NpCallback){0};
        if (reset_reach) g_reach_last_len = 0;
    }
    pthread_mutex_unlock(&g_cb_mutex);
    return ret;
}

void shim_sceNpRegisterStateCallback(GuestContext *ctx) {
    RET(register_single(&g_legacy_cb, ctx->rdi, ctx->rsi, false));
}

void shim_sceNpUnregisterStateCallback(GuestContext *ctx) {
    RET(unregister_single(&g_legacy_cb, false));
}

// Returns the callback id (1..8).
void shim_sceNpRegisterStateCallbackA(GuestContext *ctx) {
    const uint64_t func = ctx->rdi;
    if (!func) RET(E_ARG);
    pthread_mutex_lock(&g_cb_mutex);
    int32_t ret = (int32_t)ORBIS_NP_ERROR_CALLBACK_MAX;
    for (int i = 0; i < ORBIS_NP_STATE_CALLBACK_MAX; i++) {
        if (g_state_cbs_a[i].func == func) {
            ret = E_ALREADY_REGISTERED;
            break;
        }
    }
    if (ret != E_ALREADY_REGISTERED) {
        for (int i = 0; i < ORBIS_NP_STATE_CALLBACK_MAX; i++) {
            if (!g_state_cbs_a[i].func) {
                g_state_cbs_a[i] = (NpCallback){.func = func, .userdata = ctx->rsi};
                ret = i + 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_cb_mutex);
    RET(ret);
}

void shim_sceNpUnregisterStateCallbackA(GuestContext *ctx) {
    const int32_t id = (int32_t)ctx->rdi;
    if (id <= 0 || id > ORBIS_NP_STATE_CALLBACK_MAX) RET(E_ARG);
    pthread_mutex_lock(&g_cb_mutex);
    int32_t ret = 0;
    if (!g_state_cbs_a[id - 1].func) {
        ret = E_NOT_REGISTERED;
    } else {
        g_state_cbs_a[id - 1] = (NpCallback){0};
    }
    pthread_mutex_unlock(&g_cb_mutex);
    RET(ret);
}

void shim_sceNpRegisterNpReachabilityStateCallback(GuestContext *ctx) {
    RET(register_single(&g_reachability_cb, ctx->rdi, ctx->rsi, true));
}

void shim_sceNpUnregisterNpReachabilityStateCallback(GuestContext *ctx) {
    RET(unregister_single(&g_reachability_cb, true));
}

// shadPS4 sets the toolkit callback without checks (a null one unsets it).
void shim_sceNpRegisterStateCallbackForToolkit(GuestContext *ctx) {
    pthread_mutex_lock(&g_cb_mutex);
    g_toolkit_cb = (NpCallback){.func = ctx->rdi, .userdata = ctx->rsi};
    pthread_mutex_unlock(&g_cb_mutex);
    RET(0);
}

void shim_sceNpUnregisterStateCallbackForToolkit(GuestContext *ctx) {
    RET(unregister_single(&g_toolkit_cb, false));
}

// ---------------------------------------------------------------------------------------------
// Game presence and PS Plus

static int32_t set_game_presence(int32_t req_id, bool has_online_id) {
    if (req_id == 0 || !has_online_id) return E_ARG;
    return find_live(req_id);
}

// (req_id, const OrbisNpOnlineId *, presence)
void shim_sceNpSetGamePresenceOnline(GuestContext *ctx) {
    RET(set_game_presence((int32_t)ctx->rdi, ctx->rsi != 0));
}

// (req_id, user_id, presence): shadPS4 passes the user's (here empty) online id on.
void shim_sceNpSetGamePresenceOnlineA(GuestContext *ctx) {
    if (INVALID_USER(ctx->rsi)) RET(E_ARG);
    RET(set_game_presence((int32_t)ctx->rdi, true));
}

// void (callback, userdata)
void shim_sceNpRegisterGamePresenceCallback(GuestContext *ctx) {
    pthread_mutex_lock(&g_presence_mutex);
    g_presence_cb = (NpCallback){.func = ctx->rdi, .userdata = ctx->rsi};
    pthread_mutex_unlock(&g_presence_mutex);
    SHIM_RETURN();
}

void shim_sceNpRegisterGamePresenceCallbackA(GuestContext *ctx) {
    if (!ctx->rdi) RET(E_ARG);
    pthread_mutex_lock(&g_presence_mutex);
    int32_t ret = (int32_t)ORBIS_NP_ERROR_CALLBACK_MAX;
    for (int i = 0; i < ORBIS_NP_STATE_CALLBACK_MAX; i++) {
        if (!g_presence_cbs_a[i].cb.func) {
            g_presence_cbs_a[i] = (GamePresenceCallbackA){
                .handle = ++g_presence_next_handle, .cb = {.func = ctx->rdi, .userdata = ctx->rsi}};
            ret = g_presence_cbs_a[i].handle;
            break;
        }
    }
    pthread_mutex_unlock(&g_presence_mutex);
    RET(ret);
}

void shim_sceNpUnregisterGamePresenceCallbackA(GuestContext *ctx) {
    const int32_t id = (int32_t)ctx->rdi;
    pthread_mutex_lock(&g_presence_mutex);
    int32_t ret = E_NOT_REGISTERED;
    for (int i = 0; i < ORBIS_NP_STATE_CALLBACK_MAX; i++) {
        if (g_presence_cbs_a[i].cb.func && g_presence_cbs_a[i].handle == id) {
            g_presence_cbs_a[i] = (GamePresenceCallbackA){0};
            ret = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_presence_mutex);
    RET(ret);
}

// (user_id, bool *): not a member.
void shim_sceNpIsPlusMember(GuestContext *ctx) {
    bool *result = guest_ptr(ctx, ctx->rsi);
    if (!result) RET(E_ARG);
    *result = false;
    RET(0);
}

void shim_sceNpNotifyPlusFeature(GuestContext *ctx) {
    RET(0);
}

void shim_sceNpRegisterPlusEventCallback(GuestContext *ctx) {
    if (!ctx->rdi) RET(E_ARG);
    pthread_mutex_lock(&g_presence_mutex);
    int32_t ret = 0;
    if (g_plus_cb.func) {
        ret = E_ALREADY_REGISTERED;
    } else {
        g_plus_cb = (NpCallback){.func = ctx->rdi, .userdata = ctx->rsi};
    }
    pthread_mutex_unlock(&g_presence_mutex);
    RET(ret);
}

void shim_sceNpUnregisterPlusEventCallback(GuestContext *ctx) {
    pthread_mutex_lock(&g_presence_mutex);
    int32_t ret = 0;
    if (!g_plus_cb.func) {
        ret = E_NOT_REGISTERED;
    } else {
        g_plus_cb = (NpCallback){0};
    }
    pthread_mutex_unlock(&g_presence_mutex);
    RET(ret);
}
