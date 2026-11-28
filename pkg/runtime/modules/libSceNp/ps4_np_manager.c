// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpManager after shadPS4 src/core/libraries/np/np_manager.cpp. The shadNet session of a
// user comes from the NP handler (ps4_np_handler.c); with shadNet off, or for a user who is not
// signed in, the answers are shadPS4's signed-out ones.
//
// Requests are tracked as in shadPS4 (at most 32 active, ids from 0x20000001, a free slot is
// reused first). A request completes at once: ORBIS_NP_ERROR_SIGNED_OUT for a user who is not
// signed in, else ORBIS_OK with its answer (PS Plus is granted: shadNet has no subscriptions;
// language, age and parental control come from the profile in shadnet.json). A synchronous
// call returns the result, an asynchronous one returns 0 and reports it through
// sceNpPollAsync / sceNpWaitAsync. Calls that name an online id look up the signed-in user who
// has it.
//
// NP state: the handler reports SignedIn / SignedOut whenever a user's session starts or ends;
// with shadNet off, each login or logout event the game takes from libSceUserService queues
// SignedOut. sceNpCheckCallback / sceNpCheckCallbackForLib deliver the queued states to the
// state callbacks on the calling thread (a SignedIn state with the user's NP id), then the
// reachability callback when a user's reachability changed. Game presence and PS Plus
// callbacks are stored and never called: nothing produces those events (as in shadPS4).

#include "ps4_np_manager.h"
#include "ps4_kernel_sys.h"
#include "ps4_np_handler.h"
#include "ps4_user_service.h"
#include <ctype.h>
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
    bool has_np_id; // SignedIn events carry the user's NP id
    OrbisNpId np_id;
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
// shadNet (shadPS4 RegisterLib: g_shadnet_enabled, the NpHandler state bridge and Initialize)

static void queue_state_event(int32_t user_id, uint32_t state);

static void on_handler_state(void *user, int32_t user_id, int32_t state) {
    (void)user;
    queue_state_event(user_id, (uint32_t)state);
}

// shadPS4 registers the state bridge when the library loads, before NpHandler::Initialize, so
// the initial SignedIn events are queued whichever NP library starts the handler.
__attribute__((constructor)) static void np_manager_register_state_bridge(void) {
    ps4_np_handler_register_state_callback(on_handler_state, NULL);
}

// g_shadnet_enabled: shadNet is configured on (the configuration value, which the server probe
// does not change, as shadPS4 reads the setting before NpHandler::Initialize).
static bool shadnet_enabled(void) {
    ps4_np_handler_start();
    return ps4_np_handler_config()->enabled;
}

// g_shadnet_enabled && NpHandler::IsPsnSignedIn(user_id).
static bool signed_in(int32_t user_id) {
    return shadnet_enabled() && ps4_np_handler_is_signed_in(user_id);
}

// NpHandler::GetUserIdByOnlineId: -1 when no signed-in user has the online id.
static int32_t user_by_online_id(const GuestContext *ctx, uint64_t online_id) {
    shadnet_enabled();
    return ps4_np_handler_user_by_online_id((const OrbisNpOnlineId *)(ctx->mem_base + online_id));
}

// The profile values (shadPS4 Fill*FromProfile / GetAgeFromProfile), with their defaults.
static bool two_letters(const char *s) {
    return strlen(s) == 2 && isalpha((unsigned char)s[0]) && isalpha((unsigned char)s[1]);
}

static void fill_country(OrbisNpCountryCode *out) {
    const char *cfg = ps4_np_handler_config()->np_country;
    memset(out, 0, sizeof(*out));
    memcpy(out->country_code, two_letters(cfg) ? cfg : "us", 2);
}

static void fill_language(OrbisNpLanguageCode *out) {
    const char *cfg = ps4_np_handler_config()->np_language;
    memset(out, 0, sizeof(*out));
    memcpy(out->code, two_letters(cfg) ? cfg : "en", 2);
}

static int8_t profile_age(void) {
    const int32_t v = ps4_np_handler_config()->np_age;
    return v <= 0 || v > 127 ? 13 : (int8_t)v;
}

static void fill_date_of_birth(OrbisNpDate *out) {
    const char *s = ps4_np_handler_config()->np_date_of_birth;
    bool ok = strlen(s) == 10 && s[4] == '-' && s[7] == '-';
    static const int digits[] = {0, 1, 2, 3, 5, 6, 8, 9};
    for (size_t i = 0; ok && i < sizeof(digits) / sizeof(digits[0]); i++) ok = isdigit((unsigned char)s[digits[i]]);
    int y = 0, m = 0, d = 0;
    if (ok) {
        y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
        m = (s[5] - '0') * 10 + (s[6] - '0');
        d = (s[8] - '0') * 10 + (s[9] - '0');
        ok = y >= 1900 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
    }
    out->year = ok ? (uint16_t)y : 2000;
    out->month = ok ? (uint8_t)m : 1;
    out->day = ok ? (uint8_t)d : 1;
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

#define E_SIGNED_OUT ((int32_t)ORBIS_NP_ERROR_SIGNED_OUT)

// shadPS4 CompleteRequest: the request ends with result; a synchronous call returns it.
// g_req_mutex held.
static int32_t complete_request(NpRequest *r, int32_t result) {
    r->state = REQ_COMPLETE;
    r->result = result;
    return r->async ? 0 : result;
}

// Runs req_id for user_id: SignedOut unless the user is signed in to shadNet; then fill (may be
// NULL) produces the answer and the request completes with ORBIS_OK.
static int32_t run_request(int32_t req_id, int32_t user_id, void (*fill)(void *arg), void *arg) {
    pthread_mutex_lock(&g_req_mutex);
    int32_t ret;
    NpRequest *r = get_request(req_id, &ret);
    if (r) {
        if (!signed_in(user_id)) {
            ret = complete_request(r, E_SIGNED_OUT);
        } else {
            if (fill) fill(arg);
            ret = complete_request(r, 0);
        }
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

// (req_id, user_id)
void shim_sceNpCheckNpAvailabilityA(GuestContext *ctx) {
    if (INVALID_USER(ctx->rsi)) RET(E_ARG);
    RET(run_request((int32_t)ctx->rdi, (int32_t)ctx->rsi, NULL, NULL));
}

// (req_id, online_id): the user signed in with that online id.
void shim_sceNpCheckNpAvailability(GuestContext *ctx) {
    if (!ctx->rsi) RET(E_ARG);
    const int32_t user = user_by_online_id(ctx, ctx->rsi);
    if (user == -1) RET(E_USER_NOT_FOUND);
    RET(run_request((int32_t)ctx->rdi, user, NULL, NULL));
}

// (req_id, user_id)
void shim_sceNpCheckNpReachability(GuestContext *ctx) {
    if (INVALID_USER(ctx->rsi)) RET(E_ARG);
    RET(run_request((int32_t)ctx->rdi, (int32_t)ctx->rsi, NULL, NULL));
}

// shadNet has no subscription gating: a signed-in user has PS Plus.
static void grant_plus(void *result) {
    ((OrbisNpCheckPlusResult *)result)->authorized = true;
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
    RET(run_request((int32_t)ctx->rdi, param->user_id, grant_plus, guest_ptr(ctx, ctx->rdx)));
}

static void fill_language_arg(void *out) {
    fill_language(out);
}

// (req_id, user_id, OrbisNpLanguageCode *)
static int32_t account_language(GuestContext *ctx, int32_t req_id, int32_t user_id, uint64_t language) {
    if (!language || INVALID_USER(user_id)) return E_ARG;
    return run_request(req_id, user_id, fill_language_arg, guest_ptr(ctx, language));
}

// (req_id, online_id, OrbisNpLanguageCode *)
void shim_sceNpGetAccountLanguage(GuestContext *ctx) {
    if (!ctx->rsi || !ctx->rdx) RET(E_ARG);
    const int32_t user = user_by_online_id(ctx, ctx->rsi);
    if (user == -1) RET(E_USER_NOT_FOUND);
    RET(account_language(ctx, (int32_t)ctx->rdi, user, ctx->rdx));
}

void shim_sceNpGetAccountLanguageA(GuestContext *ctx) {
    RET(account_language(ctx, (int32_t)ctx->rdi, (int32_t)ctx->rsi, ctx->rdx));
}

typedef struct ParentalControl {
    int8_t *age;
    OrbisNpParentalControlInfo *info;
} ParentalControl;

static void fill_parental_control(void *arg) {
    ParentalControl *p = arg;
    *p->age = profile_age();
    memset(p->info, 0, sizeof(*p->info));
}

// (req_id, user_id, int8_t *age, OrbisNpParentalControlInfo *)
static int32_t parental_control(GuestContext *ctx, int32_t req_id, int32_t user_id, uint64_t age, uint64_t info) {
    if (!age || !info || INVALID_USER(user_id)) return E_ARG;
    ParentalControl p = {guest_ptr(ctx, age), guest_ptr(ctx, info)};
    return run_request(req_id, user_id, fill_parental_control, &p);
}

// (req_id, online_id, int8_t *age, OrbisNpParentalControlInfo *)
void shim_sceNpGetParentalControlInfo(GuestContext *ctx) {
    if (!ctx->rsi || !ctx->rdx || !ctx->rcx) RET(E_ARG);
    const int32_t user = user_by_online_id(ctx, ctx->rsi);
    if (user == -1) RET(E_USER_NOT_FOUND);
    RET(parental_control(ctx, (int32_t)ctx->rdi, user, ctx->rdx, ctx->rcx));
}

void shim_sceNpGetParentalControlInfoA(GuestContext *ctx) {
    RET(parental_control(ctx, (int32_t)ctx->rdi, (int32_t)ctx->rsi, ctx->rdx, ctx->rcx));
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
// timeouts are checked and kept nowhere, as in shadPS4: NpManager's requests are answered
// from the shadNet session without network I/O of their own.
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
    OrbisNpCountryCode *country = guest_ptr(ctx, ctx->rsi);
    if (!ctx->rdi || !country) RET(E_ARG);
    const int32_t user = user_by_online_id(ctx, ctx->rdi);
    if (user == -1) RET(E_USER_NOT_FOUND);
    if (!signed_in(user)) RET(E_SIGNED_OUT);
    fill_country(country);
    RET(0);
}

// (user_id, OrbisNpCountryCode *)
void shim_sceNpGetAccountCountryA(GuestContext *ctx) {
    OrbisNpCountryCode *country = guest_ptr(ctx, ctx->rsi);
    if (!country || INVALID_USER(ctx->rdi)) RET(E_ARG);
    if (!user_exists((int32_t)ctx->rdi)) RET(E_USER_NOT_FOUND);
    if (!signed_in((int32_t)ctx->rdi)) RET(E_SIGNED_OUT);
    fill_country(country);
    RET(0);
}

// (online_id, OrbisNpDate *)
void shim_sceNpGetAccountDateOfBirth(GuestContext *ctx) {
    OrbisNpDate *date = guest_ptr(ctx, ctx->rsi);
    if (!ctx->rdi || !date) RET(E_ARG);
    const int32_t user = user_by_online_id(ctx, ctx->rdi);
    if (user == -1) RET(E_USER_NOT_FOUND);
    if (!signed_in(user)) RET(E_SIGNED_OUT);
    fill_date_of_birth(date);
    RET(0);
}

// (user_id, OrbisNpDate *)
void shim_sceNpGetAccountDateOfBirthA(GuestContext *ctx) {
    OrbisNpDate *date = guest_ptr(ctx, ctx->rsi);
    if (!date || INVALID_USER(ctx->rdi)) RET(E_ARG);
    if (!user_exists((int32_t)ctx->rdi)) RET(E_USER_NOT_FOUND);
    if (!signed_in((int32_t)ctx->rdi)) RET(E_SIGNED_OUT);
    fill_date_of_birth(date);
    RET(0);
}

// (online_id, int32_t *status): Online for a signed-in user.
void shim_sceNpGetGamePresenceStatus(GuestContext *ctx) {
    int32_t *status = guest_ptr(ctx, ctx->rsi);
    if (!ctx->rdi || !status) RET(E_ARG);
    const int32_t user = user_by_online_id(ctx, ctx->rdi);
    *status = user != -1 && signed_in(user) ? ORBIS_NP_GAME_PRESENCE_STATUS_ONLINE : ORBIS_NP_GAME_PRESENCE_STATUS_OFFLINE;
    RET(0);
}

// (user_id, int32_t *status)
void shim_sceNpGetGamePresenceStatusA(GuestContext *ctx) {
    int32_t *status = guest_ptr(ctx, ctx->rsi);
    if (!status || INVALID_USER(ctx->rdi)) RET(E_ARG);
    *status = signed_in((int32_t)ctx->rdi) ? ORBIS_NP_GAME_PRESENCE_STATUS_ONLINE : ORBIS_NP_GAME_PRESENCE_STATUS_OFFLINE;
    RET(0);
}

// (online_id, uint64_t *account_id)
void shim_sceNpGetAccountId(GuestContext *ctx) {
    uint64_t *account_id = guest_ptr(ctx, ctx->rsi);
    if (!ctx->rdi || !account_id) RET(E_ARG);
    const int32_t user = user_by_online_id(ctx, ctx->rdi);
    *account_id = 0;
    if (user == -1) RET(E_USER_NOT_FOUND);
    if (!signed_in(user)) RET(E_SIGNED_OUT);
    *account_id = ps4_np_handler_account_id(user);
    RET(0);
}

// (user_id, uint64_t *account_id)
void shim_sceNpGetAccountIdA(GuestContext *ctx) {
    uint64_t *account_id = guest_ptr(ctx, ctx->rsi);
    if (!account_id || INVALID_USER(ctx->rdi)) RET(E_ARG);
    if (!signed_in((int32_t)ctx->rdi)) {
        *account_id = 0;
        RET(E_SIGNED_OUT);
    }
    *account_id = ps4_np_handler_account_id((int32_t)ctx->rdi);
    RET(0);
}

// An invalid user id: USER_NOT_FOUND for games built before firmware 9.00.
static int32_t invalid_user_error(const GuestContext *ctx) {
    return ps4_compiled_sdk_version(ctx) < ORBIS_NP_SDK_FW_900 ? E_USER_NOT_FOUND : E_ARG;
}

// (user_id, OrbisNpId *)
void shim_sceNpGetNpId(GuestContext *ctx) {
    if (INVALID_USER(ctx->rdi)) RET(invalid_user_error(ctx));
    OrbisNpId *np_id = guest_ptr(ctx, ctx->rsi);
    if (!np_id) RET(E_ARG);
    if (!signed_in((int32_t)ctx->rdi)) RET(E_SIGNED_OUT);
    *np_id = ps4_np_handler_np_id((int32_t)ctx->rdi);
    RET(0);
}

// (user_id, OrbisNpOnlineId *)
void shim_sceNpGetOnlineId(GuestContext *ctx) {
    if (INVALID_USER(ctx->rdi)) RET(invalid_user_error(ctx));
    OrbisNpOnlineId *online_id = guest_ptr(ctx, ctx->rsi);
    if (!online_id) RET(E_ARG);
    if (!signed_in((int32_t)ctx->rdi)) RET(E_SIGNED_OUT);
    *online_id = ps4_np_handler_np_id((int32_t)ctx->rdi).handle;
    RET(0);
}

// (user_id, int32_t *state): Reachable for a signed-in user, else Unavailable.
void shim_sceNpGetNpReachabilityState(GuestContext *ctx) {
    int32_t *state = guest_ptr(ctx, ctx->rsi);
    if (!state) RET(E_ARG);
    if (INVALID_USER(ctx->rdi) && ps4_compiled_sdk_version(ctx) >= ORBIS_NP_SDK_FW_400) RET(E_ARG);
    *state = signed_in((int32_t)ctx->rdi) ? ORBIS_NP_REACHABILITY_STATE_REACHABLE : ORBIS_NP_REACHABILITY_STATE_UNAVAILABLE;
    RET(0);
}

// (user_id, uint32_t *state)
void shim_sceNpGetState(GuestContext *ctx) {
    uint32_t *state = guest_ptr(ctx, ctx->rsi);
    if (!state) RET(E_ARG);
    if (INVALID_USER(ctx->rdi) && ps4_compiled_sdk_version(ctx) >= ORBIS_NP_SDK_FW_900) RET(E_ARG);
    *state = signed_in((int32_t)ctx->rdi) ? ORBIS_NP_STATE_SIGNED_IN : ORBIS_NP_STATE_SIGNED_OUT;
    RET(0);
}

// The signed-in local user found by account id or online id (shadPS4 sceNpGetUserIdBy*).
static int32_t found_user(int32_t found, int32_t *user_id) {
    if (found == -1) return E_SIGNED_OUT;
    if (!user_exists(found)) return E_USER_NOT_FOUND; // logged in locally
    *user_id = found;
    return 0;
}

// (account_id, int32_t *user_id)
void shim_sceNpGetUserIdByAccountId(GuestContext *ctx) {
    int32_t *user_id = guest_ptr(ctx, ctx->rsi);
    if (ctx->rdi == 0 || !user_id) RET(E_ARG);
    if (!shadnet_enabled()) RET(E_SIGNED_OUT);
    RET(found_user(ps4_np_handler_user_by_account_id(ctx->rdi), user_id));
}

// (online_id, int32_t *user_id)
void shim_sceNpGetUserIdByOnlineId(GuestContext *ctx) {
    int32_t *user_id = guest_ptr(ctx, ctx->rsi);
    if (!ctx->rdi || !user_id) RET(E_ARG);
    if (!shadnet_enabled()) RET(E_SIGNED_OUT);
    RET(found_user(user_by_online_id(ctx, ctx->rdi), user_id));
}

// (user_id, bool *): a user has signed up when a shadNet account (npid) is configured for it,
// whether or not shadNet is on or connected.
void shim_sceNpHasSignedUp(GuestContext *ctx) {
    bool *signed_up = guest_ptr(ctx, ctx->rsi);
    if (!signed_up) RET(E_ARG);
    *signed_up = false;
    if (INVALID_USER(ctx->rdi) && ps4_compiled_sdk_version(ctx) >= ORBIS_NP_SDK_FW_900) RET(E_ARG);
    if (!user_exists((int32_t)ctx->rdi)) RET(E_USER_NOT_FOUND);
    shadnet_enabled();
    *signed_up = ps4_np_handler_config()->npid[0] != '\0';
    RET(0);
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

// shadPS4 QueueNpStateEvent.
static void queue_state_event(int32_t userId, uint32_t state) {
    NpStateEvent e = {.user_id = userId, .state = state, .has_np_id = state == ORBIS_NP_STATE_SIGNED_IN};
    if (e.has_np_id) e.np_id = ps4_np_handler_np_id(userId);
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
    g_events[g_events_len++] = e;
    pthread_mutex_unlock(&g_cb_mutex);
}

// shadPS4 NotifyNpStateFromUserServiceEvent: with shadNet on, the handler connects or
// disconnects the user and reports the state through its callback; otherwise the user is
// SignedOut.
void ps4_np_manager_notify_user_event(int32_t eventType, int32_t userId) {
    if (eventType != ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN && eventType != ORBIS_USER_SERVICE_EVENT_TYPE_LOGOUT) {
        return;
    }
    if (!shadnet_enabled()) {
        queue_state_event(userId, ORBIS_NP_STATE_SIGNED_OUT);
    } else if (eventType == ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN) {
        ps4_np_handler_on_user_logged_in(userId);
    } else {
        ps4_np_handler_on_user_logged_out(userId);
    }
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
    NpStateEvent reach_users[8];
    NpStateEvent *reach = reach_users;
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
                reach[n_reach].user_id = events[i].user_id;
                reach[n_reach++].state = r;
            }
        }
    }
    pthread_mutex_unlock(&g_cb_mutex);

    for (size_t i = 0; i < n; i++) {
        const uint64_t user = (uint64_t)(int64_t)events[i].user_id, state = events[i].state;
        if (legacy.func) {
            // A SignedIn event passes a pointer to the user's NP id, on the guest stack.
            uint64_t np_id = 0;
            if (events[i].has_np_id) {
                np_id = (ctx->rsp - 128 - sizeof(OrbisNpId)) & ~(uint64_t)15;
                memcpy(ctx->mem_base + np_id, &events[i].np_id, sizeof(OrbisNpId));
            }
            recomp_shim_call_guest(ctx, legacy.func, np_id ? (np_id - 16) & ~(uint64_t)15 : 0, user, state, np_id,
                                   legacy.userdata);
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
        call_guest(ctx, reachability.func, (uint64_t)(int64_t)reach[i].user_id, reach[i].state, reachability.userdata,
                   0);
    }
    if (events != local) {
        free(events);
        free(reach);
    }
}

void shim_sceNpCheckCallback(GuestContext *ctx) {
    shadnet_enabled();
    dispatch_state_callbacks(ctx);
    RET(0);
}

void shim_sceNpCheckCallbackForLib(GuestContext *ctx) {
    shadnet_enabled();
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
