// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpAuth against shadPS4's np_auth.cpp without a shadNet sign-in, on host memory (guest
// base 0, so guest addresses are host pointers): request ids and the request limit, argument
// and size checks in shadPS4's order, the signed-out outcome of synchronous and asynchronous
// requests, aborting, timeouts and deleting.

#include "ps4_np_auth.h"
#include "ps4_user_service.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#define A(p) ((uint64_t)(uintptr_t)(p))
#define FIRST_ID (ORBIS_NP_AUTH_REQUEST_ID_OFFSET + 1)
#define NP_SIGNED_OUT ((int32_t)ORBIS_NP_ERROR_SIGNED_OUT)
#define NP_USER_NOT_FOUND ((int32_t)ORBIS_NP_ERROR_USER_NOT_FOUND)

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                    uint64_t a5) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
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

#define CALL1(s, a) call(s, a, 0, 0, 0, 0, 0)
#define CALL2(s, a, b) call(s, a, b, 0, 0, 0, 0)
#define CALL4(s, a, b, c, d) call(s, a, b, c, d, 0, 0)

static OrbisNpClientId g_client;
static OrbisNpClientSecret g_secret;
static OrbisNpOnlineId g_online;
static const char g_scope[] = "psn:s2s";
static OrbisNpAuthorizationCode g_code;
static OrbisNpIdToken g_token;
static OrbisNpAuthCreateAsyncRequestParameter g_async = {sizeof(OrbisNpAuthCreateAsyncRequestParameter), 0, 0, {0}};

static OrbisNpAuthGetAuthorizationCodeParameterA code_a(int32_t user) {
    OrbisNpAuthGetAuthorizationCodeParameterA p;
    memset(&p, 0, sizeof(p));
    p.size = sizeof(p);
    p.user_id = user;
    p.client_id = A(&g_client);
    p.scope = A(g_scope);
    return p;
}

static OrbisNpAuthGetIdTokenParameterA token_a(int32_t user) {
    OrbisNpAuthGetIdTokenParameterA p;
    memset(&p, 0, sizeof(p));
    p.size = sizeof(p);
    p.user_id = user;
    p.client_id = A(&g_client);
    p.client_secret = A(&g_secret);
    p.scope = A(g_scope);
    return p;
}

static int case_requests(void) {
    CHECK(sizeof(OrbisNpAuthGetAuthorizationCodeParameterA) == 32 && sizeof(OrbisNpAuthGetIdTokenParameterA) == 40);
    CHECK(sizeof(OrbisNpAuthGetAuthorizationCodeParameter) == 32 && sizeof(OrbisNpAuthGetIdTokenParameter) == 40);
    CHECK(CALL1(shim_sceNpAuthCreateAsyncRequest, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    OrbisNpAuthCreateAsyncRequestParameter bad = g_async;
    bad.size = 16;
    CHECK(CALL1(shim_sceNpAuthCreateAsyncRequest, A(&bad)) == ORBIS_NP_AUTH_ERROR_INVALID_SIZE);

    // Sixteen requests at most; a deleted id is handed out again first.
    for (int i = 0; i < ORBIS_NP_AUTH_REQUEST_LIMIT; i++) {
        CHECK(CALL1(shim_sceNpAuthCreateRequest, 0) == FIRST_ID + i);
    }
    CHECK(CALL1(shim_sceNpAuthCreateRequest, 0) == ORBIS_NP_AUTH_ERROR_REQUEST_MAX);
    CHECK(CALL1(shim_sceNpAuthCreateAsyncRequest, A(&g_async)) == ORBIS_NP_AUTH_ERROR_REQUEST_MAX);
    CHECK(CALL1(shim_sceNpAuthDeleteRequest, FIRST_ID + 3) == 0);
    CHECK(CALL1(shim_sceNpAuthDeleteRequest, FIRST_ID + 3) == ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND);
    CHECK(CALL1(shim_sceNpAuthCreateAsyncRequest, A(&g_async)) == FIRST_ID + 3);
    CHECK(CALL1(shim_sceNpAuthDeleteRequest, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(CALL1(shim_sceNpAuthDeleteRequest, 5) == ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND);
    for (int i = 0; i < ORBIS_NP_AUTH_REQUEST_LIMIT; i++) {
        CHECK(CALL1(shim_sceNpAuthDeleteRequest, FIRST_ID + i) == 0);
    }

    // Timeouts: zero means "no effect", others have minimums, all zero is refused.
    const int32_t id = CALL1(shim_sceNpAuthCreateRequest, 0);
    CHECK(id == FIRST_ID);
    CHECK(call(shim_sceNpAuthSetTimeout, id, 0, 0, 0, 0, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceNpAuthSetTimeout, id, (uint64_t)-1, 0, 0, 0, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceNpAuthSetTimeout, id, 0, 999999, 0, 0, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceNpAuthSetTimeout, id, 0, 0, 9999999, 0, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceNpAuthSetTimeout, id, 2, 1000000, 10000000, 0, 10000000) == 0);
    CHECK(call(shim_sceNpAuthSetTimeout, id + 1, 2, 0, 0, 0, 0) == ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND);
    printf("PASS requests\n");
    return 0;
}

static int case_signed_out(void) {
    int32_t result = 0;
    OrbisNpAuthGetAuthorizationCodeParameterA pa = code_a(ORBIS_USER_SERVICE_INITIAL_USER_ID);
    OrbisNpAuthGetIdTokenParameterA ta = token_a(ORBIS_USER_SERVICE_INITIAL_USER_ID);

    // Synchronous: the request returns SIGNED_OUT, and is then started.
    const int32_t sync = CALL1(shim_sceNpAuthCreateRequest, 0);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, sync, A(&pa), A(&g_code), 0) == NP_SIGNED_OUT);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, sync, A(&pa), A(&g_code), 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(CALL2(shim_sceNpAuthPollAsync, sync, A(&result)) == ORBIS_NP_AUTH_ERROR_INVALID_ID);
    CHECK(CALL2(shim_sceNpAuthWaitAsync, sync, A(&result)) == ORBIS_NP_AUTH_ERROR_INVALID_ID);

    // Asynchronous: 0, then the result through the poll and the wait.
    const int32_t as = CALL1(shim_sceNpAuthCreateAsyncRequest, A(&g_async));
    CHECK(CALL2(shim_sceNpAuthPollAsync, as, A(&result)) == ORBIS_NP_AUTH_ERROR_INVALID_ID); // not started
    CHECK(CALL4(shim_sceNpAuthGetIdTokenA, as, A(&ta), A(&g_token), 0) == 0);
    CHECK(CALL2(shim_sceNpAuthPollAsync, as, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(CALL2(shim_sceNpAuthPollAsync, as, A(&result)) == ORBIS_NP_AUTH_POLL_ASYNC_RET_FINISHED);
    CHECK(result == NP_SIGNED_OUT);
    result = 0;
    CHECK(CALL2(shim_sceNpAuthWaitAsync, as, A(&result)) == 0 && result == NP_SIGNED_OUT);
    // Aborting a finished request keeps its result.
    CHECK(CALL1(shim_sceNpAuthAbortRequest, as) == 0);
    CHECK(CALL2(shim_sceNpAuthPollAsync, as, A(&result)) == 0 && result == NP_SIGNED_OUT);

    // V3 variants behave as A.
    const int32_t v3 = CALL1(shim_sceNpAuthCreateRequest, 0);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeV3, v3, A(&pa), A(&g_code), 0) == NP_SIGNED_OUT);
    const int32_t v3t = CALL1(shim_sceNpAuthCreateRequest, 0);
    CHECK(CALL4(shim_sceNpAuthGetIdTokenV3, v3t, A(&ta), A(&g_token), 0) == NP_SIGNED_OUT);

    // An aborted request does not start.
    const int32_t ab = CALL1(shim_sceNpAuthCreateRequest, 0);
    CHECK(CALL1(shim_sceNpAuthAbortRequest, ab) == 0);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, ab, A(&pa), A(&g_code), 0) == ORBIS_NP_AUTH_ERROR_ABORTED);
    CHECK(CALL1(shim_sceNpAuthAbortRequest, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(CALL1(shim_sceNpAuthAbortRequest, ab + 100) == ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND);
    printf("PASS signed-out\n");
    return 0;
}

static int case_checks(void) {
    const int32_t id = CALL1(shim_sceNpAuthCreateRequest, 0);
    OrbisNpAuthGetAuthorizationCodeParameterA pa = code_a(ORBIS_USER_SERVICE_INITIAL_USER_ID);
    OrbisNpAuthGetIdTokenParameterA ta = token_a(ORBIS_USER_SERVICE_INITIAL_USER_ID);

    // Authorization code A: pointers, size, client and scope, then the user, then the request.
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, id, 0, A(&g_code), 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, id, A(&pa), 0, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    pa.size = 24;
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, id, A(&pa), A(&g_code), 0) == ORBIS_NP_AUTH_ERROR_INVALID_SIZE);
    pa = code_a(ORBIS_USER_SERVICE_INITIAL_USER_ID);
    pa.scope = 0;
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, id, A(&pa), A(&g_code), 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    pa = code_a(ORBIS_USER_SERVICE_INITIAL_USER_ID + 1);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, 0, A(&pa), A(&g_code), 0) == NP_USER_NOT_FOUND);
    pa = code_a(ORBIS_USER_SERVICE_INITIAL_USER_ID);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, 0, A(&pa), A(&g_code), 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCodeA, id + 50, A(&pa), A(&g_code), 0) ==
          ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND);

    // Id token A: an unknown user is accepted, the invalid user id is not.
    ta.size = 32;
    CHECK(CALL4(shim_sceNpAuthGetIdTokenA, id, A(&ta), A(&g_token), 0) == ORBIS_NP_AUTH_ERROR_INVALID_SIZE);
    ta = token_a(ORBIS_USER_SERVICE_USER_ID_INVALID);
    CHECK(CALL4(shim_sceNpAuthGetIdTokenA, id, A(&ta), A(&g_token), 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    ta = token_a(ORBIS_USER_SERVICE_INITIAL_USER_ID);
    ta.client_secret = 0;
    CHECK(CALL4(shim_sceNpAuthGetIdTokenA, id, A(&ta), A(&g_token), 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    ta = token_a(ORBIS_USER_SERVICE_INITIAL_USER_ID + 7);
    CHECK(CALL4(shim_sceNpAuthGetIdTokenA, id, A(&ta), A(&g_token), 0) == NP_SIGNED_OUT);

    // By online id: the checks, then no user has one.
    OrbisNpAuthGetAuthorizationCodeParameter p;
    memset(&p, 0, sizeof(p));
    p.size = sizeof(p);
    p.online_id = A(&g_online);
    p.client_id = A(&g_client);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCode, id, A(&p), A(&g_code), 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    p.scope = A(g_scope);
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCode, id, A(&p), A(&g_code), 0) == NP_USER_NOT_FOUND);
    p.size = 8;
    CHECK(CALL4(shim_sceNpAuthGetAuthorizationCode, id, A(&p), A(&g_code), 0) == ORBIS_NP_AUTH_ERROR_INVALID_SIZE);
    OrbisNpAuthGetIdTokenParameter t;
    memset(&t, 0, sizeof(t));
    t.size = sizeof(t);
    t.online_id = A(&g_online);
    t.client_id = A(&g_client);
    t.client_secret = A(&g_secret);
    t.scope = A(g_scope);
    CHECK(CALL4(shim_sceNpAuthGetIdToken, id, A(&t), 0, 0) == ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT);
    CHECK(CALL4(shim_sceNpAuthGetIdToken, id, A(&t), A(&g_token), 0) == NP_USER_NOT_FOUND);
    printf("PASS checks\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "requests") == 0) return case_requests();
    if (strcmp(argv[1], "signed-out") == 0) return case_signed_out();
    if (strcmp(argv[1], "checks") == 0) return case_checks();
    return 2;
}
