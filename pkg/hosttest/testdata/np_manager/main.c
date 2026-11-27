// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpManager against shadPS4's np_manager.cpp without a shadNet sign-in, on host memory
// (guest base 0, so guest addresses are host pointers): request ids, states and the request
// limit; argument and size checks in shadPS4's order; the signed-out answers; the SDK version
// rules for an invalid user id; and the NP state that a libSceUserService login event queues and
// sceNpCheckCallback delivers to the state and reachability callbacks, which are C functions in
// the dispatch table.

#include "ps4_np_manager.h"
#include "ps4_user_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Runtime symbols the sources link against.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];
static GuestContext g_thread_ctx;
GuestContext *recomp_get_current_ctx(void) { return &g_thread_ctx; }
static uint32_t g_sdk = 0x9508001;
uint32_t ps4_compiled_sdk_version(const GuestContext *ctx) {
    (void)ctx;
    return g_sdk;
}

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#include "../host_guest_mem.h"
#include "../shadnet_fake_server.h"
#include "ps4_np_handler.h"

// ps4_sfo_find_and_load (the title id and name the handler logs in with): no application.
const char *ps4_vfs_get_app_root(void) { return NULL; }
int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    (void)guest_path;
    (void)host_path;
    (void)host_path_sz;
    return -1;
}
#define USER ORBIS_USER_SERVICE_INITIAL_USER_ID
#define BAD_USER ((uint64_t)(int64_t)-1)
#define FIRST ((uint64_t)(ORBIS_NP_MANAGER_REQUEST_ID_OFFSET + 1))
#define E(x) ((int32_t)ORBIS_NP_ERROR_##x)

static uint8_t g_stack[0x4000] __attribute__((aligned(16)));

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                    uint64_t a5) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    ctx.mem_size = ~0ull;
    ctx.rsp = A(g_stack + sizeof(g_stack) - 8);
    ctx.rdi = a0;
    ctx.rsi = a1;
    ctx.rdx = a2;
    ctx.rcx = a3;
    ctx.r8 = a4;
    ctx.r9 = a5;
    ctx.rax = 99;
    shim(&ctx);
    return (int32_t)ctx.rax;
}

#define C0(s) call(s, 0, 0, 0, 0, 0, 0)
#define C1(s, a) call(s, a, 0, 0, 0, 0, 0)
#define C2(s, a, b) call(s, a, b, 0, 0, 0, 0)
#define C3(s, a, b, c) call(s, a, b, c, 0, 0, 0)
#define C4(s, a, b, c, d) call(s, a, b, c, d, 0, 0)

static void register_entry(uint64_t addr, recomp_fn_t fn) {
    const uint64_t l1 = (addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    const uint64_t l2 = (addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[l1]) g_dispatch_l1[l1] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    if (!g_dispatch_l1[l1][l2]) g_dispatch_l1[l1][l2] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    g_dispatch_l1[l1][l2][addr & DISPATCH_LEVEL_MASK] = fn;
}

// What the callbacks saw, in call order: kind, user, state, third argument.
static struct {
    char kind;
    int32_t user;
    uint32_t state;
    uint64_t arg3;
} g_calls[32];
static int g_ncalls, g_misaligned;

#define CALLBACK(fn, k, third)                                     \
    static void fn(GuestContext *ctx) {                            \
        if ((ctx->rsp + 8) % 16 != 0) g_misaligned++;              \
        g_calls[g_ncalls].kind = k;                                \
        g_calls[g_ncalls].user = (int32_t)ctx->rdi;                \
        g_calls[g_ncalls].state = (uint32_t)ctx->rsi;              \
        g_calls[g_ncalls].arg3 = third;                            \
        g_ncalls++;                                                \
        SHIM_RETURN();                                             \
    }
CALLBACK(cb_legacy, 'L', ctx->rcx * 1000 + ctx->rdx) // userdata, then the NpId pointer (0)
CALLBACK(cb_a1, 'A', ctx->rdx)
CALLBACK(cb_a2, 'B', ctx->rdx)
CALLBACK(cb_toolkit, 'T', ctx->rdx)
CALLBACK(cb_reach, 'R', ctx->rdx)

#define F_LEGACY 0x500000ull
#define F_A1 0x500100ull
#define F_A2 0x500200ull
#define F_TOOLKIT 0x500300ull
#define F_REACH 0x500400ull

static int case_requests(void) {
    OrbisNpCreateAsyncRequestParameter ap = {sizeof(ap), 0, 0, {0}};
    CHECK(C1(shim_sceNpCreateAsyncRequest, 0) == E(INVALID_ARGUMENT));
    ap.size = 8;
    CHECK(C1(shim_sceNpCreateAsyncRequest, A(&ap)) == E(INVALID_SIZE));
    ap.size = sizeof(ap);
    for (int i = 0; i < ORBIS_NP_MANAGER_REQUEST_LIMIT; i++) {
        CHECK(C0(shim_sceNpCreateRequest) == (int32_t)(FIRST + i));
    }
    CHECK(C0(shim_sceNpCreateRequest) == E(REQUEST_MAX));
    CHECK(C1(shim_sceNpDeleteRequest, FIRST + 5) == 0);
    CHECK(C1(shim_sceNpDeleteRequest, FIRST + 5) == E(REQUEST_NOT_FOUND));
    CHECK(C1(shim_sceNpCreateAsyncRequest, A(&ap)) == (int32_t)(FIRST + 5));
    for (int i = 0; i < ORBIS_NP_MANAGER_REQUEST_LIMIT; i++) CHECK(C1(shim_sceNpDeleteRequest, FIRST + i) == 0);
    CHECK(C1(shim_sceNpDeleteRequest, FIRST) == E(REQUEST_NOT_FOUND));

    // Synchronous: SIGNED_OUT, then the request is complete.
    int32_t sync = C0(shim_sceNpCreateRequest), result = 0;
    CHECK(C2(shim_sceNpCheckNpAvailabilityA, (uint64_t)sync, USER) == E(SIGNED_OUT));
    CHECK(C2(shim_sceNpCheckNpAvailabilityA, (uint64_t)sync, USER) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpPollAsync, (uint64_t)sync, A(&result)) == E(INVALID_ID));
    CHECK(C2(shim_sceNpCheckNpAvailabilityA, 0, USER) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpCheckNpAvailabilityA, FIRST + 20, USER) == E(REQUEST_NOT_FOUND));
    CHECK(C2(shim_sceNpCheckNpAvailabilityA, (uint64_t)sync, BAD_USER) == E(INVALID_ARGUMENT));

    // Asynchronous: 0, the result through the poll and the wait.
    int32_t as = C1(shim_sceNpCreateAsyncRequest, A(&ap));
    CHECK(C2(shim_sceNpWaitAsync, (uint64_t)as, A(&result)) == E(INVALID_ID)); // not run yet
    OrbisNpLanguageCode lang;
    CHECK(C3(shim_sceNpGetAccountLanguageA, (uint64_t)as, USER, A(&lang)) == 0);
    CHECK(C2(shim_sceNpPollAsync, (uint64_t)as, 0) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpPollAsync, (uint64_t)as, A(&result)) == 0 && result == E(SIGNED_OUT));
    result = 0;
    CHECK(C2(shim_sceNpWaitAsync, (uint64_t)as, A(&result)) == 0 && result == E(SIGNED_OUT));
    CHECK(C1(shim_sceNpAbortRequest, (uint64_t)as) == 0); // complete: ignored
    CHECK(C2(shim_sceNpPollAsync, (uint64_t)as, A(&result)) == 0 && result == E(SIGNED_OUT));

    // Aborted before running.
    int32_t ab = C0(shim_sceNpCreateRequest);
    CHECK(C1(shim_sceNpAbortRequest, (uint64_t)ab) == 0);
    CHECK(C2(shim_sceNpCheckNpReachability, (uint64_t)ab, USER) == E(ABORTED));
    CHECK(C1(shim_sceNpAbortRequest, FIRST + 30) == E(REQUEST_NOT_FOUND));

    // Timeouts.
    CHECK(call(shim_sceNpSetTimeout, (uint64_t)ab, 0, 0, 0, 0, 0) == E(INVALID_ARGUMENT));
    CHECK(call(shim_sceNpSetTimeout, 0, 1, 0, 0, 0, 0) == E(INVALID_ARGUMENT));
    CHECK(call(shim_sceNpSetTimeout, (uint64_t)ab, 1, 999999, 0, 0, 0) == E(INVALID_ARGUMENT));
    CHECK(call(shim_sceNpSetTimeout, (uint64_t)ab, 1, 1000000, 10000000, 0, 0) == 0);
    CHECK(call(shim_sceNpSetTimeout, FIRST + 30, 1, 0, 0, 0, 0) == E(REQUEST_NOT_FOUND));

    // Game presence takes a live request.
    OrbisNpOnlineId oid;
    memset(&oid, 0, sizeof(oid));
    CHECK(C3(shim_sceNpSetGamePresenceOnline, (uint64_t)ab, A(&oid), 1) == 0);
    CHECK(C3(shim_sceNpSetGamePresenceOnline, 0, A(&oid), 1) == E(INVALID_ARGUMENT));
    CHECK(C3(shim_sceNpSetGamePresenceOnlineA, FIRST + 30, USER, 1) == E(REQUEST_NOT_FOUND));
    CHECK(C3(shim_sceNpSetGamePresenceOnlineA, (uint64_t)ab, BAD_USER, 1) == E(INVALID_ARGUMENT));
    printf("PASS requests\n");
    return 0;
}

static int case_queries(void) {
    OrbisNpOnlineId oid;
    OrbisNpId npid;
    OrbisNpCountryCode cc;
    OrbisNpDate dob;
    uint64_t acc = 7;
    int32_t st = -1, uid = 0;
    bool b = true;
    int8_t age;
    OrbisNpParentalControlInfo pci;
    int32_t req = C0(shim_sceNpCreateRequest);

    // By online id: nobody has one.
    CHECK(C2(shim_sceNpCheckNpAvailability, (uint64_t)req, 0) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpCheckNpAvailability, (uint64_t)req, A(&oid)) == E(USER_NOT_FOUND));
    CHECK(C2(shim_sceNpGetAccountCountry, A(&oid), A(&cc)) == E(USER_NOT_FOUND));
    CHECK(C2(shim_sceNpGetAccountDateOfBirth, A(&oid), 0) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpGetAccountId, A(&oid), A(&acc)) == E(USER_NOT_FOUND) && acc == 0);
    CHECK(C4(shim_sceNpGetParentalControlInfo, (uint64_t)req, A(&oid), A(&age), A(&pci)) == E(USER_NOT_FOUND));
    CHECK(C2(shim_sceNpGetUserIdByOnlineId, A(&oid), A(&uid)) == E(SIGNED_OUT));
    CHECK(C2(shim_sceNpGetUserIdByAccountId, 0, A(&uid)) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpGetUserIdByAccountId, 5, A(&uid)) == E(SIGNED_OUT));

    // By user id.
    CHECK(C2(shim_sceNpGetAccountCountryA, USER, A(&cc)) == E(SIGNED_OUT));
    CHECK(C2(shim_sceNpGetAccountCountryA, USER + 1, A(&cc)) == E(USER_NOT_FOUND));
    CHECK(C2(shim_sceNpGetAccountDateOfBirthA, BAD_USER, A(&dob)) == E(INVALID_ARGUMENT));
    acc = 7;
    CHECK(C2(shim_sceNpGetAccountIdA, USER, A(&acc)) == E(SIGNED_OUT) && acc == 0);
    CHECK(C2(shim_sceNpGetNpId, USER, A(&npid)) == E(SIGNED_OUT));
    CHECK(C2(shim_sceNpGetNpId, USER, 0) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpGetOnlineId, USER, A(&oid)) == E(SIGNED_OUT));
    CHECK(C2(shim_sceNpGetState, USER, A(&st)) == 0 && st == ORBIS_NP_STATE_SIGNED_OUT);
    CHECK(C2(shim_sceNpGetNpReachabilityState, USER, A(&st)) == 0 && st == ORBIS_NP_REACHABILITY_STATE_UNAVAILABLE);
    CHECK(C2(shim_sceNpGetGamePresenceStatusA, USER, A(&st)) == 0 && st == ORBIS_NP_GAME_PRESENCE_STATUS_OFFLINE);
    CHECK(C2(shim_sceNpGetGamePresenceStatus, A(&oid), A(&st)) == 0 && st == ORBIS_NP_GAME_PRESENCE_STATUS_OFFLINE);
    CHECK(C2(shim_sceNpHasSignedUp, USER, A(&b)) == 0 && !b);
    CHECK(C2(shim_sceNpHasSignedUp, USER + 1, A(&b)) == E(USER_NOT_FOUND));
    b = true;
    CHECK(C2(shim_sceNpIsPlusMember, USER, A(&b)) == 0 && !b);

    // An invalid user id depends on the SDK version.
    g_sdk = 0x9508001;
    CHECK(C2(shim_sceNpGetNpId, BAD_USER, A(&npid)) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpGetState, BAD_USER, A(&st)) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpHasSignedUp, BAD_USER, A(&b)) == E(INVALID_ARGUMENT));
    g_sdk = 0x5050001;
    CHECK(C2(shim_sceNpGetNpId, BAD_USER, A(&npid)) == E(USER_NOT_FOUND));
    CHECK(C2(shim_sceNpGetOnlineId, BAD_USER, A(&oid)) == E(USER_NOT_FOUND));
    CHECK(C2(shim_sceNpGetState, BAD_USER, A(&st)) == 0 && st == ORBIS_NP_STATE_SIGNED_OUT);
    CHECK(C2(shim_sceNpGetNpReachabilityState, BAD_USER, A(&st)) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpHasSignedUp, BAD_USER, A(&b)) == E(USER_NOT_FOUND));
    g_sdk = 0x3508001;
    CHECK(C2(shim_sceNpGetNpReachabilityState, BAD_USER, A(&st)) == 0);

    // Check Plus: its parameter checks, then signed out.
    OrbisNpCheckPlusParameter p;
    OrbisNpCheckPlusResult r;
    memset(&p, 0, sizeof(p));
    p.size = sizeof(p);
    p.user_id = USER;
    p.features = 1;
    CHECK(C3(shim_sceNpCheckPlus, (uint64_t)req, A(&p), 0) == E(INVALID_ARGUMENT));
    p.size = 48;
    CHECK(C3(shim_sceNpCheckPlus, (uint64_t)req, A(&p), A(&r)) == E(INVALID_SIZE));
    p.size = sizeof(p);
    p.features = 4;
    CHECK(C3(shim_sceNpCheckPlus, (uint64_t)req, A(&p), A(&r)) == E(INVALID_ARGUMENT));
    p.features = 1;
    p.reserved[31] = 1;
    CHECK(C3(shim_sceNpCheckPlus, (uint64_t)req, A(&p), A(&r)) == E(INVALID_ARGUMENT));
    p.reserved[31] = 0;
    CHECK(C3(shim_sceNpCheckPlus, (uint64_t)req, A(&p), A(&r)) == E(SIGNED_OUT));

    // Content restriction and title id: checked, no effect.
    OrbisNpContentRestriction cr;
    memset(&cr, 0, sizeof(cr));
    CHECK(C1(shim_sceNpSetContentRestriction, A(&cr)) == E(INVALID_SIZE));
    cr.size = sizeof(cr);
    cr.age_restriction_count = 2;
    CHECK(C1(shim_sceNpSetContentRestriction, A(&cr)) == E(INVALID_ARGUMENT));
    cr.age_restriction = A(&cr);
    CHECK(C1(shim_sceNpSetContentRestriction, A(&cr)) == 0);
    CHECK(C2(shim_sceNpSetNpTitleId, A(&cr), 0) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpSetNpTitleId, A(&cr), A(&cr)) == 0);
    printf("PASS queries\n");
    return 0;
}

static int case_state_callbacks(void) {
    register_entry(F_LEGACY, cb_legacy);
    register_entry(F_A1, cb_a1);
    register_entry(F_A2, cb_a2);
    register_entry(F_TOOLKIT, cb_toolkit);
    register_entry(F_REACH, cb_reach);

    CHECK(C2(shim_sceNpRegisterStateCallback, 0, 1) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpRegisterStateCallback, F_LEGACY, 11) == 0);
    CHECK(C2(shim_sceNpRegisterStateCallback, F_A1, 11) == E(CALLBACK_ALREADY_REGISTERED));
    CHECK(C2(shim_sceNpRegisterStateCallbackA, F_A1, 21) == 1);
    CHECK(C2(shim_sceNpRegisterStateCallbackA, F_A1, 22) == E(CALLBACK_ALREADY_REGISTERED));
    CHECK(C2(shim_sceNpRegisterStateCallbackA, F_A2, 23) == 2);
    CHECK(C2(shim_sceNpRegisterStateCallbackForToolkit, F_TOOLKIT, 31) == 0);
    CHECK(C2(shim_sceNpRegisterNpReachabilityStateCallback, F_REACH, 41) == 0);
    CHECK(C2(shim_sceNpRegisterNpReachabilityStateCallback, F_REACH, 41) == E(CALLBACK_ALREADY_REGISTERED));

    // Nothing queued: no calls.
    CHECK(C0(shim_sceNpCheckCallback) == 0 && g_ncalls == 0);

    // The game takes the boot login from libSceUserService: SignedOut is queued for the user.
    OrbisUserServiceEvent ev;
    CHECK(sceUserServiceGetEvent(&ev) == 0 && ev.eventType == ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN);
    CHECK(C0(shim_sceNpCheckCallback) == 0);
    CHECK(g_ncalls == 5 && g_misaligned == 0);
    CHECK(g_calls[0].kind == 'L' && g_calls[0].user == USER && g_calls[0].state == ORBIS_NP_STATE_SIGNED_OUT);
    CHECK(g_calls[0].arg3 == 11 * 1000 + 0);
    CHECK(g_calls[1].kind == 'A' && g_calls[1].arg3 == 21);
    CHECK(g_calls[2].kind == 'B' && g_calls[2].arg3 == 23);
    CHECK(g_calls[3].kind == 'T' && g_calls[3].arg3 == 31);
    CHECK(g_calls[4].kind == 'R' && g_calls[4].state == ORBIS_NP_REACHABILITY_STATE_UNAVAILABLE && g_calls[4].arg3 == 41);
    CHECK(C0(shim_sceNpCheckCallbackForLib) == 0 && g_ncalls == 5); // delivered once

    // A logout: the state callbacks again; the reachability did not change.
    ps4_user_service_add_event(NULL, ORBIS_USER_SERVICE_EVENT_TYPE_LOGOUT, USER);
    CHECK(sceUserServiceGetEvent(&ev) == 0);
    CHECK(C0(shim_sceNpCheckCallbackForLib) == 0 && g_ncalls == 9 && g_calls[8].kind == 'T');

    // Unregistering.
    CHECK(C1(shim_sceNpUnregisterStateCallbackA, 0) == E(INVALID_ARGUMENT));
    CHECK(C1(shim_sceNpUnregisterStateCallbackA, 9) == E(INVALID_ARGUMENT));
    CHECK(C1(shim_sceNpUnregisterStateCallbackA, 1) == 0);
    CHECK(C1(shim_sceNpUnregisterStateCallbackA, 1) == E(CALLBACK_NOT_REGISTERED));
    CHECK(C2(shim_sceNpRegisterStateCallbackA, F_A1, 24) == 1); // the free slot again
    CHECK(C0(shim_sceNpUnregisterStateCallback) == 0);
    CHECK(C0(shim_sceNpUnregisterStateCallback) == E(CALLBACK_NOT_REGISTERED));
    CHECK(C0(shim_sceNpUnregisterStateCallbackForToolkit) == 0);
    CHECK(C0(shim_sceNpUnregisterStateCallbackForToolkit) == E(CALLBACK_NOT_REGISTERED));
    CHECK(C0(shim_sceNpUnregisterNpReachabilityStateCallback) == 0);
    CHECK(C0(shim_sceNpUnregisterNpReachabilityStateCallback) == E(CALLBACK_NOT_REGISTERED));
    // A re-registered reachability callback reports again.
    CHECK(C2(shim_sceNpRegisterNpReachabilityStateCallback, F_REACH, 42) == 0);
    ps4_user_service_add_event(NULL, ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN, USER);
    CHECK(sceUserServiceGetEvent(&ev) == 0);
    g_ncalls = 0;
    CHECK(C0(shim_sceNpCheckCallback) == 0);
    CHECK(g_ncalls == 3 && g_calls[0].kind == 'A' && g_calls[1].kind == 'B' && g_calls[2].kind == 'R');
    CHECK(g_calls[2].arg3 == 42);

    // State callback A slots: eight at most.
    for (uint64_t i = 0; i < 6; i++) CHECK(C2(shim_sceNpRegisterStateCallbackA, 0x600000 + i * 16, 0) == (int32_t)(3 + i));
    CHECK(C2(shim_sceNpRegisterStateCallbackA, 0x600100, 0) == E(CALLBACK_MAX));

    // Game presence and Plus callbacks are stored.
    CHECK(C2(shim_sceNpRegisterGamePresenceCallbackA, 0, 0) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpRegisterGamePresenceCallbackA, F_A1, 0) == 1);
    CHECK(C2(shim_sceNpRegisterGamePresenceCallbackA, F_A2, 0) == 2);
    CHECK(C1(shim_sceNpUnregisterGamePresenceCallbackA, 1) == 0);
    CHECK(C1(shim_sceNpUnregisterGamePresenceCallbackA, 1) == E(CALLBACK_NOT_REGISTERED));
    CHECK(C2(shim_sceNpRegisterGamePresenceCallbackA, F_A1, 0) == 3);
    CHECK(C2(shim_sceNpRegisterPlusEventCallback, 0, 0) == E(INVALID_ARGUMENT));
    CHECK(C2(shim_sceNpRegisterPlusEventCallback, F_A1, 0) == 0);
    CHECK(C2(shim_sceNpRegisterPlusEventCallback, F_A1, 0) == E(CALLBACK_ALREADY_REGISTERED));
    CHECK(C0(shim_sceNpUnregisterPlusEventCallback) == 0);
    CHECK(C0(shim_sceNpUnregisterPlusEventCallback) == E(CALLBACK_NOT_REGISTERED));
    printf("PASS state-callbacks\n");
    return 0;
}

// The legacy callback of the online case: the NP id it is passed.
static OrbisNpId g_legacy_np_id;
static uint64_t g_legacy_np_id_addr;
static void cb_legacy_np_id(GuestContext *ctx) {
    g_calls[g_ncalls].kind = 'N';
    g_calls[g_ncalls].user = (int32_t)ctx->rdi;
    g_calls[g_ncalls].state = (uint32_t)ctx->rsi;
    g_calls[g_ncalls].arg3 = ctx->rcx;
    g_ncalls++;
    g_legacy_np_id_addr = ctx->rdx;
    if (ctx->rdx) memcpy(&g_legacy_np_id, ctx->mem_base + ctx->rdx, sizeof(g_legacy_np_id));
    SHIM_RETURN();
}
#define F_LEGACY_NP_ID 0x500500ull

// Signed in to a local shadNet server through the NP handler.
static int case_online(void) {
    FakeShadNet server = {.account_id = 0x1234};
    CHECK(fake_shadnet_start(&server) == 0);
    char path[] = "/tmp/ps4recomp_np_manager_XXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    char json[512];
    const int n = snprintf(json, sizeof(json),
                           "{\"enabled\": true, \"server\": \"127.0.0.1:%u\", \"npid\": \"Player1\", "
                           "\"password\": \"pw\", \"np_country\": \"de\", \"np_age\": 20, "
                           "\"np_date_of_birth\": \"1999-12-31\"}",
                           (unsigned)server.port);
    CHECK(write(fd, json, (size_t)n) == n);
    close(fd);
    setenv("PS4_SHADNET_CONFIG", path, 1);

    register_entry(F_LEGACY_NP_ID, cb_legacy_np_id);
    register_entry(F_REACH, cb_reach);
    CHECK(C2(shim_sceNpRegisterStateCallback, F_LEGACY_NP_ID, 11) == 0);
    CHECK(C2(shim_sceNpRegisterNpReachabilityStateCallback, F_REACH, 41) == 0);

    // The first query signs the user in (shadPS4 does it when the library loads).
    int32_t st = -1;
    CHECK(C2(shim_sceNpGetState, USER, A(&st)) == 0 && st == ORBIS_NP_STATE_SIGNED_IN);
    CHECK(server.sessions == 1 && strcmp(server.login_npid, "Player1") == 0);
    CHECK(C2(shim_sceNpGetState, USER + 1, A(&st)) == 0 && st == ORBIS_NP_STATE_SIGNED_OUT);

    // SignedIn reaches the legacy callback with the NP id, then the reachability callback.
    CHECK(C0(shim_sceNpCheckCallback) == 0);
    CHECK(g_ncalls == 2 && g_calls[0].kind == 'N' && g_calls[0].state == ORBIS_NP_STATE_SIGNED_IN);
    CHECK(g_calls[0].arg3 == 11 && g_legacy_np_id_addr != 0 && strcmp(g_legacy_np_id.handle.data, "Player1") == 0);
    CHECK(g_calls[1].kind == 'R' && g_calls[1].state == ORBIS_NP_REACHABILITY_STATE_REACHABLE);
    // The game's own login event finds the user connected: nothing new.
    OrbisUserServiceEvent ev;
    CHECK(sceUserServiceGetEvent(&ev) == 0 && ev.eventType == ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN);
    CHECK(C0(shim_sceNpCheckCallback) == 0 && g_ncalls == 2 && server.sessions == 1);

    // Identity.
    OrbisNpId npid;
    OrbisNpOnlineId oid;
    uint64_t acc = 0;
    int32_t uid = 0;
    CHECK(C2(shim_sceNpGetNpId, USER, A(&npid)) == 0 && strcmp(npid.handle.data, "Player1") == 0);
    CHECK(C2(shim_sceNpGetOnlineId, USER, A(&oid)) == 0 && strcmp(oid.data, "Player1") == 0);
    CHECK(C2(shim_sceNpGetAccountIdA, USER, A(&acc)) == 0 && acc == 0x1234);
    acc = 0;
    CHECK(C2(shim_sceNpGetAccountId, A(&oid), A(&acc)) == 0 && acc == 0x1234);
    CHECK(C2(shim_sceNpGetUserIdByOnlineId, A(&oid), A(&uid)) == 0 && uid == USER);
    uid = 0;
    CHECK(C2(shim_sceNpGetUserIdByAccountId, 0x1234, A(&uid)) == 0 && uid == USER);
    CHECK(C2(shim_sceNpGetUserIdByAccountId, 0x9999, A(&uid)) == E(SIGNED_OUT));
    OrbisNpOnlineId other;
    memset(&other, 0, sizeof(other));
    memcpy(other.data, "Nobody", 6);
    CHECK(C2(shim_sceNpGetAccountId, A(&other), A(&acc)) == E(USER_NOT_FOUND) && acc == 0);
    bool b = false;
    CHECK(C2(shim_sceNpHasSignedUp, USER, A(&b)) == 0 && b);

    // Profile answers.
    OrbisNpCountryCode cc;
    OrbisNpDate dob;
    CHECK(C2(shim_sceNpGetAccountCountryA, USER, A(&cc)) == 0 && memcmp(cc.country_code, "de", 2) == 0 && cc.end == 0);
    CHECK(C2(shim_sceNpGetAccountDateOfBirth, A(&oid), A(&dob)) == 0);
    CHECK(dob.year == 1999 && dob.month == 12 && dob.day == 31);
    CHECK(C2(shim_sceNpGetGamePresenceStatus, A(&oid), A(&st)) == 0 && st == ORBIS_NP_GAME_PRESENCE_STATUS_ONLINE);
    CHECK(C2(shim_sceNpGetNpReachabilityState, USER, A(&st)) == 0 && st == ORBIS_NP_REACHABILITY_STATE_REACHABLE);

    // Requests complete with their answers.
    OrbisNpCreateAsyncRequestParameter ap = {sizeof(ap), 0, 0, {0}};
    const int32_t req = C0(shim_sceNpCreateRequest);
    CHECK(C2(shim_sceNpCheckNpAvailabilityA, (uint64_t)req, USER) == 0);
    const int32_t as = C1(shim_sceNpCreateAsyncRequest, A(&ap));
    OrbisNpLanguageCode lang;
    int32_t result = -1;
    CHECK(C3(shim_sceNpGetAccountLanguageA, (uint64_t)as, USER, A(&lang)) == 0);
    CHECK(C2(shim_sceNpPollAsync, (uint64_t)as, A(&result)) == 0 && result == 0 && strcmp(lang.code, "en") == 0);
    OrbisNpCheckPlusParameter p;
    OrbisNpCheckPlusResult r;
    memset(&p, 0, sizeof(p));
    memset(&r, 0, sizeof(r));
    p.size = sizeof(p);
    p.user_id = USER;
    p.features = 1;
    const int32_t plus = C0(shim_sceNpCreateRequest);
    CHECK(C3(shim_sceNpCheckPlus, (uint64_t)plus, A(&p), A(&r)) == 0 && r.authorized);
    int8_t age = 0;
    OrbisNpParentalControlInfo pci;
    memset(&pci, 0xFF, sizeof(pci));
    const int32_t pc = C0(shim_sceNpCreateRequest);
    CHECK(C4(shim_sceNpGetParentalControlInfo, (uint64_t)pc, A(&oid), A(&age), A(&pci)) == 0);
    CHECK(age == 20 && !pci.content_restriction && !pci.chat_restriction);
    // Another user is signed out.
    const int32_t other_req = C0(shim_sceNpCreateRequest);
    CHECK(C2(shim_sceNpCheckNpReachability, (uint64_t)other_req, USER + 1) == E(SIGNED_OUT));

    // Signing out: SignedOut without an NP id, the reachability back to Unavailable.
    g_ncalls = 0;
    ps4_np_handler_shutdown();
    CHECK(C2(shim_sceNpGetState, USER, A(&st)) == 0 && st == ORBIS_NP_STATE_SIGNED_OUT);
    CHECK(C0(shim_sceNpCheckCallback) == 0);
    CHECK(g_ncalls == 2 && g_calls[0].state == ORBIS_NP_STATE_SIGNED_OUT && g_legacy_np_id_addr == 0);
    CHECK(g_calls[1].kind == 'R' && g_calls[1].state == ORBIS_NP_REACHABILITY_STATE_UNAVAILABLE);
    CHECK(C2(shim_sceNpGetAccountIdA, USER, A(&acc)) == E(SIGNED_OUT) && acc == 0);

    fake_shadnet_stop(&server);
    unlink(path);
    CHECK(server.failures == 0);
    printf("PASS online\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "online") == 0) return case_online();
    // The other cases run with shadNet off.
    setenv("PS4_SHADNET_CONFIG", "/nonexistent/shadnet.json", 1);
    if (strcmp(argv[1], "requests") == 0) return case_requests();
    if (strcmp(argv[1], "queries") == 0) return case_queries();
    if (strcmp(argv[1], "state-callbacks") == 0) return case_state_callbacks();
    return 2;
}
