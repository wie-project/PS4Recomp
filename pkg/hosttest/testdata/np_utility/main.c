// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpUtility against shadPS4's np_utility.cpp without a shadNet sign-in: NpLookup and
// NpWordFilter title contexts and requests (id bases, limits, argument checks in shadPS4's
// order, one operation per request, abort and delete), the signed-out completion of
// sceNpLookupNpId, the local completion of word filtering, SetTimeout validation, WaitAsync
// blocking until another thread completes the request, and the shims' guest pointer handling.

#include "ps4_np_utility.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../host_guest_mem.h"

// Runtime symbols the sources link against.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

void shim_sceNpLookupNpId(GuestContext *ctx);
void shim_sceNpLookupPollAsync(GuestContext *ctx);
void shim_sceNpWordFilterSanitizeComment(GuestContext *ctx);
void shim_sceNpBandwidthTestInitStart(GuestContext *ctx);
void shim_sceNpUtilityInit(GuestContext *ctx);

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    ctx.mem_size = ~0ull;
    ctx.rdi = a0;
    ctx.rsi = a1;
    ctx.rdx = a2;
    ctx.rcx = a3;
    ctx.rax = 99;
    shim(&ctx);
    return (int32_t)ctx.rax;
}

static const uint8_t g_np_id[36] = {'p', 'l', 'a', 'y', 'e', 'r'};
static const uint8_t g_online_id[20] = {'f', 'r', 'i', 'e', 'n', 'd'};

static int case_lookup(void) {
    uint8_t np_id[36];
    int32_t result = 0;

    // Title contexts: ids from 1, from an NP id or a user id.
    CHECK(ps4_np_lookup_create_title_ctx(NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    const int32_t ctx = ps4_np_lookup_create_title_ctx(g_np_id);
    CHECK(ctx == 1);
    const int32_t ctx_a = ps4_np_lookup_create_title_ctx_a(1);
    CHECK(ctx_a == 2);

    // Requests need a live title context; their ids count from 1 on their own.
    CHECK(ps4_np_lookup_create_request(77, false) == NP_COMMUNITY_ERROR_INVALID_ID);
    const int32_t sync = ps4_np_lookup_create_request(ctx, false);
    CHECK(sync == 1);
    const int32_t async = ps4_np_lookup_create_request(ctx_a, true);
    CHECK(async == 2);

    // Argument checks: option, then the pointers, then the id.
    CHECK(ps4_np_lookup_np_id(sync, NULL, NULL, (void *)1) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_lookup_np_id(sync, g_online_id, NULL, NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_lookup_np_id(sync, NULL, np_id, NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_lookup_np_id(99, g_online_id, np_id, NULL) == NP_COMMUNITY_ERROR_INVALID_ID);

    // Polling works on any request; before its operation it is running.
    CHECK(ps4_np_lookup_poll_async(sync, &result) == NP_LOOKUP_POLL_ASYNC_RET_RUNNING);
    CHECK(ps4_np_lookup_poll_async(99, &result) == NP_COMMUNITY_ERROR_INVALID_ID);

    // A synchronous lookup returns the signed-out result and leaves *npId alone.
    memset(np_id, 0xEE, sizeof(np_id));
    CHECK(ps4_np_lookup_np_id(sync, g_online_id, np_id, NULL) == NP_ERROR_SIGNED_OUT);
    CHECK(np_id[0] == 0xEE && np_id[35] == 0xEE);
    CHECK(ps4_np_lookup_np_id(sync, g_online_id, np_id, NULL) == NP_COMMUNITY_ERROR_INVALID_TYPE);
    result = 0;
    CHECK(ps4_np_lookup_poll_async(sync, &result) == 0 && result == NP_ERROR_SIGNED_OUT);

    // An asynchronous lookup returns 0 and reports the result through PollAsync / WaitAsync.
    CHECK(ps4_np_lookup_np_id(async, g_online_id, np_id, NULL) == 0);
    result = 0;
    CHECK(ps4_np_lookup_poll_async(async, &result) == 0 && result == NP_ERROR_SIGNED_OUT);
    CHECK(ps4_np_lookup_poll_async(async, NULL) == 0);
    result = 0;
    CHECK(ps4_np_lookup_wait_async(async, &result) == 0 && result == NP_ERROR_SIGNED_OUT);
    CHECK(ps4_np_lookup_wait_async(async, NULL) == 0);
    CHECK(ps4_np_lookup_wait_async(99, &result) == NP_COMMUNITY_ERROR_INVALID_ID);
    // Aborting after completion keeps the result.
    CHECK(ps4_np_lookup_abort_request(async) == 0);
    CHECK(ps4_np_lookup_poll_async(async, &result) == 0 && result == NP_ERROR_SIGNED_OUT);

    // An aborted request completes as aborted and cannot run.
    const int32_t aborted = ps4_np_lookup_create_request(ctx, true);
    CHECK(ps4_np_lookup_abort_request(aborted) == 0);
    CHECK(ps4_np_lookup_poll_async(aborted, &result) == 0 && result == NP_COMMUNITY_ERROR_ABORTED);
    CHECK(ps4_np_lookup_np_id(aborted, g_online_id, np_id, NULL) == NP_COMMUNITY_ERROR_ABORTED);
    CHECK(ps4_np_lookup_abort_request(99) == NP_COMMUNITY_ERROR_INVALID_ID);

    // Delete request; deleting a title context deletes its requests.
    CHECK(ps4_np_lookup_delete_request(sync) == 0);
    CHECK(ps4_np_lookup_delete_request(sync) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_lookup_delete_title_ctx(ctx) == 0);
    CHECK(ps4_np_lookup_delete_title_ctx(ctx) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_lookup_poll_async(aborted, &result) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_lookup_poll_async(async, &result) == 0); // belongs to ctx_a
    CHECK(ps4_np_lookup_delete_title_ctx(ctx_a) == 0);
    CHECK(ps4_np_lookup_poll_async(async, &result) == NP_COMMUNITY_ERROR_INVALID_ID);

    // Limits: 32 live title contexts and 32 live requests, ids never reused.
    int32_t ctxs[NP_LOOKUP_MAX_CTX_NUM];
    for (int i = 0; i < NP_LOOKUP_MAX_CTX_NUM; i++) {
        ctxs[i] = ps4_np_lookup_create_title_ctx_a(1);
        CHECK(ctxs[i] == 3 + i);
    }
    CHECK(ps4_np_lookup_create_title_ctx(g_np_id) == NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS);
    CHECK(ps4_np_lookup_create_title_ctx_a(1) == NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS);
    for (int i = 0; i < NP_LOOKUP_MAX_REQUEST_NUM; i++) {
        CHECK(ps4_np_lookup_create_request(ctxs[0], i & 1) == 4 + i);
    }
    CHECK(ps4_np_lookup_create_request(ctxs[1], false) == NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS);
    CHECK(ps4_np_lookup_delete_title_ctx(ctxs[0]) == 0);
    CHECK(ps4_np_lookup_create_request(ctxs[1], false) == 4 + NP_LOOKUP_MAX_REQUEST_NUM);
    CHECK(ps4_np_lookup_create_title_ctx_a(1) == 3 + NP_LOOKUP_MAX_CTX_NUM);
    printf("PASS lookup\n");
    return 0;
}

static int case_timeouts(void) {
    const int32_t ctx = ps4_np_lookup_create_title_ctx_a(1);
    CHECK(ctx > 0);
    // Valid: a retry count alone, or timeouts at their minimums; 0 is "no effect".
    CHECK(ps4_np_lookup_set_timeout(ctx, 1, 0, 0, 0, 0) == 0);
    CHECK(ps4_np_lookup_set_timeout(ctx, 0, 1000000, 10000000, 10000000, 10000000) == 0);
    const int32_t req = ps4_np_lookup_create_request(ctx, false);
    CHECK(ps4_np_lookup_set_timeout(req, 0, 0, 0, 0, 20000000) == 0);
    // Invalid values.
    CHECK(ps4_np_lookup_set_timeout(ctx, -1, 0, 0, 0, 0) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_lookup_set_timeout(ctx, 0, 999999, 0, 0, 0) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_lookup_set_timeout(ctx, 0, 0, 9999999, 0, 0) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_lookup_set_timeout(ctx, 0, 0, 0, 9999999, 0) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_lookup_set_timeout(ctx, 0, 0, 0, 0, 9999999) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_lookup_set_timeout(ctx, 0, 0, 0, 0, 0) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    // Values are checked before the id.
    CHECK(ps4_np_lookup_set_timeout(12345, 0, 0, 0, 0, 0) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_lookup_set_timeout(12345, 1, 0, 0, 0, 0) == NP_COMMUNITY_ERROR_INVALID_ID);

    const int32_t wf = ps4_np_word_filter_create_title_ctx_a(1);
    CHECK(ps4_np_word_filter_set_timeout(wf, 2, 0, 0, 0, 0) == 0);
    const int32_t wf_req = ps4_np_word_filter_create_request(wf);
    CHECK(ps4_np_word_filter_set_timeout(wf_req, 0, 1000000, 0, 0, 0) == 0);
    CHECK(ps4_np_word_filter_set_timeout(wf, 0, 0, 0, 0, 0) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_word_filter_set_timeout(wf, 0, 0, 5, 0, 0) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_word_filter_set_timeout(ctx, 1, 0, 0, 0, 0) == NP_COMMUNITY_ERROR_INVALID_ID);
    printf("PASS timeouts\n");
    return 0;
}

static int case_word_filter(void) {
    const uint64_t async_param[3] = {NP_WORD_FILTER_ASYNC_PARAM_SIZE, 0, 0};
    const uint64_t bad_param[3] = {0x10, 0, 0};
    const char *comment = "hello";
    const size_t room = strlen(comment) + 1;
    char sanitized[NP_WORD_FILTER_COMMENT_MAX_LENGTH + 1];
    int32_t result = 0;

    CHECK(ps4_np_word_filter_create_title_ctx(NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    const int32_t ctx = ps4_np_word_filter_create_title_ctx(g_np_id);
    CHECK(ctx == NP_WORD_FILTER_TITLE_CTX_ID_OFFSET + 1);
    CHECK(ps4_np_word_filter_create_title_ctx_a(1) == ctx + 1);

    CHECK(ps4_np_word_filter_create_request(ctx + 100) == NP_COMMUNITY_ERROR_INVALID_ID);
    const int32_t sync = ps4_np_word_filter_create_request(ctx);
    CHECK(sync == NP_WORD_FILTER_REQUEST_ID_OFFSET + 1);
    CHECK(ps4_np_word_filter_create_async_request(ctx, NULL) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_word_filter_create_async_request(ctx, bad_param) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    const int32_t async = ps4_np_word_filter_create_async_request(ctx, async_param);
    CHECK(async == sync + 1);

    // Argument checks come before the request lookup.
    CHECK(ps4_np_word_filter_censor(sync, NULL, 0, NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_word_filter_censor(sync, comment, room, (void *)1) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_word_filter_sanitize(sync, comment, room, NULL, NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    static char long_comment[NP_WORD_FILTER_COMMENT_MAX_LENGTH + 2];
    memset(long_comment, 'a', NP_WORD_FILTER_COMMENT_MAX_LENGTH + 1);
    CHECK(ps4_np_word_filter_censor(sync, long_comment, sizeof(long_comment), NULL) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    // A comment of exactly the maximum length is accepted (checked on a fresh request below).
    // One that runs to the end of guest memory without a terminator is rejected.
    CHECK(ps4_np_word_filter_censor(sync, comment, 3, NULL) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_word_filter_censor(sync + 100, comment, room, NULL) == NP_COMMUNITY_ERROR_INVALID_ID);

    // A synchronous request completes with 0 and carries a single operation.
    CHECK(ps4_np_word_filter_censor(sync, comment, room, NULL) == 0);
    CHECK(ps4_np_word_filter_sanitize(sync, comment, room, sanitized, NULL) == NP_COMMUNITY_ERROR_INVALID_TYPE);
    // PollAsync and WaitAsync are for asynchronous requests only.
    CHECK(ps4_np_word_filter_poll_async(sync, &result) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_word_filter_wait_async(sync, &result) == NP_COMMUNITY_ERROR_INVALID_ID);

    // An asynchronous request runs until started; sanitizing copies the comment.
    CHECK(ps4_np_word_filter_poll_async(async, NULL) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_word_filter_wait_async(async, NULL) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_word_filter_poll_async(async, &result) == NP_WORD_FILTER_POLL_ASYNC_RET_RUNNING);
    memset(sanitized, 'x', sizeof(sanitized));
    CHECK(ps4_np_word_filter_sanitize(async, comment, room, sanitized, NULL) == 0);
    CHECK(strcmp(sanitized, comment) == 0 && sanitized[room] == 'x');
    result = 99;
    CHECK(ps4_np_word_filter_poll_async(async, &result) == NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED && result == 0);
    result = 99;
    CHECK(ps4_np_word_filter_wait_async(async, &result) == 0 && result == 0);
    // Aborting a finished request keeps its result.
    CHECK(ps4_np_word_filter_abort_request(async) == 0);
    CHECK(ps4_np_word_filter_poll_async(async, &result) == NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED && result == 0);

    // A comment of the maximum length.
    const int32_t max_req = ps4_np_word_filter_create_request(ctx);
    long_comment[NP_WORD_FILTER_COMMENT_MAX_LENGTH] = 0;
    CHECK(ps4_np_word_filter_censor(max_req, long_comment, sizeof(long_comment), NULL) == 0);

    // Aborting a request that has not run makes it report ABORTED.
    const int32_t aborted = ps4_np_word_filter_create_async_request(ctx, async_param);
    CHECK(ps4_np_word_filter_abort_request(aborted) == 0);
    CHECK(ps4_np_word_filter_poll_async(aborted, &result) == NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED);
    CHECK(result == NP_COMMUNITY_ERROR_ABORTED);
    CHECK(ps4_np_word_filter_censor(aborted, comment, room, NULL) == NP_COMMUNITY_ERROR_ABORTED);

    CHECK(ps4_np_word_filter_delete_request(sync) == 0);
    CHECK(ps4_np_word_filter_delete_request(sync) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_word_filter_abort_request(sync) == NP_COMMUNITY_ERROR_INVALID_ID);

    CHECK(ps4_np_word_filter_delete_title_ctx(ctx) == 0);
    CHECK(ps4_np_word_filter_delete_title_ctx(ctx) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_word_filter_poll_async(async, &result) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_word_filter_delete_request(aborted) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_word_filter_create_request(ctx) == NP_COMMUNITY_ERROR_INVALID_ID);

    // Limits; freed slots are reused with fresh ids.
    int32_t ctxs[NP_WORD_FILTER_MAX_CTX_NUM - 1];
    for (int i = 0; i < NP_WORD_FILTER_MAX_CTX_NUM - 1; i++) {
        ctxs[i] = ps4_np_word_filter_create_title_ctx(g_np_id);
        CHECK(ctxs[i] > NP_WORD_FILTER_TITLE_CTX_ID_OFFSET + 2);
    }
    CHECK(ps4_np_word_filter_create_title_ctx(g_np_id) == NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS);
    for (int i = 0; i < NP_WORD_FILTER_MAX_REQUEST_NUM; i++) {
        CHECK(ps4_np_word_filter_create_request(ctxs[0]) > NP_WORD_FILTER_REQUEST_ID_OFFSET);
    }
    CHECK(ps4_np_word_filter_create_request(ctxs[1]) == NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS);
    CHECK(ps4_np_word_filter_delete_title_ctx(ctxs[0]) == 0);
    CHECK(ps4_np_word_filter_create_request(ctxs[1]) > NP_WORD_FILTER_REQUEST_ID_OFFSET + NP_WORD_FILTER_MAX_REQUEST_NUM);
    CHECK(ps4_np_word_filter_create_title_ctx(g_np_id) > ctxs[NP_WORD_FILTER_MAX_CTX_NUM - 2]);
    printf("PASS word-filter\n");
    return 0;
}

typedef struct {
    int32_t (*wait)(int32_t, int32_t *);
    int32_t req;
    int32_t rc;
    int32_t result;
} Waiter;

static void *waiter_main(void *arg) {
    Waiter *w = arg;
    w->rc = w->wait(w->req, &w->result);
    return NULL;
}

// Starts a thread blocked in WaitAsync on req; it cannot return before the request completes.
static int start_waiter(pthread_t *t, Waiter *w) {
    if (pthread_create(t, NULL, waiter_main, w) != 0) return 1;
    usleep(50000);
    return 0;
}

static int case_wait(void) {
    // Lookup: deleting the request a thread waits on completes it as aborted for that thread.
    const int32_t ctx = ps4_np_lookup_create_title_ctx_a(1);
    Waiter w = {ps4_np_lookup_wait_async, ps4_np_lookup_create_request(ctx, true), 99, 99};
    pthread_t t;
    CHECK(start_waiter(&t, &w) == 0);
    CHECK(w.rc == 99);
    CHECK(ps4_np_lookup_delete_request(w.req) == 0);
    pthread_join(t, NULL);
    CHECK(w.rc == 0 && w.result == NP_COMMUNITY_ERROR_ABORTED);

    // Lookup: deleting the title context completes its requests' waiters.
    Waiter w2 = {ps4_np_lookup_wait_async, ps4_np_lookup_create_request(ctx, false), 99, 99};
    CHECK(start_waiter(&t, &w2) == 0);
    CHECK(ps4_np_lookup_delete_title_ctx(ctx) == 0);
    pthread_join(t, NULL);
    CHECK(w2.rc == 0 && w2.result == NP_COMMUNITY_ERROR_ABORTED);

    // Word filter: aborting completes the waiter; the request stays.
    const uint64_t async_param[3] = {NP_WORD_FILTER_ASYNC_PARAM_SIZE, 0, 0};
    const int32_t wf = ps4_np_word_filter_create_title_ctx_a(1);
    Waiter w3 = {ps4_np_word_filter_wait_async, ps4_np_word_filter_create_async_request(wf, async_param), 99, 99};
    CHECK(start_waiter(&t, &w3) == 0);
    CHECK(w3.rc == 99);
    CHECK(ps4_np_word_filter_abort_request(w3.req) == 0);
    pthread_join(t, NULL);
    CHECK(w3.rc == 0 && w3.result == NP_COMMUNITY_ERROR_ABORTED);
    int32_t result = 0;
    CHECK(ps4_np_word_filter_poll_async(w3.req, &result) == NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED);

    // Word filter: an operation on another thread completes the waiter with its result.
    Waiter w4 = {ps4_np_word_filter_wait_async, ps4_np_word_filter_create_async_request(wf, async_param), 99, 99};
    CHECK(start_waiter(&t, &w4) == 0);
    CHECK(ps4_np_word_filter_censor(w4.req, "ok", 3, NULL) == 0);
    pthread_join(t, NULL);
    CHECK(w4.rc == 0 && w4.result == 0);
    printf("PASS wait\n");
    return 0;
}

static int case_shims(void) {
    // Guest NULL and guest pointers reach the host functions translated.
    const int32_t ctx = ps4_np_lookup_create_title_ctx_a(1);
    const int32_t req = ps4_np_lookup_create_request(ctx, true);
    uint8_t np_id[36];
    int32_t result = 0;
    CHECK(call(shim_sceNpLookupNpId, (uint64_t)req, 0, A(np_id), 0) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(call(shim_sceNpLookupNpId, (uint64_t)req, A(g_online_id), A(np_id), A(np_id)) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceNpLookupNpId, (uint64_t)req, A(g_online_id), A(np_id), 0) == 0);
    CHECK(call(shim_sceNpLookupPollAsync, (uint64_t)req, A(&result), 0, 0) == 0);
    CHECK(result == NP_ERROR_SIGNED_OUT);

    const int32_t wf = ps4_np_word_filter_create_title_ctx_a(1);
    const int32_t wf_req = ps4_np_word_filter_create_request(wf);
    char out[NP_WORD_FILTER_COMMENT_MAX_LENGTH + 1];
    CHECK(call(shim_sceNpWordFilterSanitizeComment, (uint64_t)wf_req, A("shim"), A(out), 0) == 0);
    CHECK(strcmp(out, "shim") == 0);

    // shadPS4 stubs.
    CHECK(call(shim_sceNpBandwidthTestInitStart, 0, 0, 0, 0) == 0);
    CHECK(call(shim_sceNpUtilityInit, 0, 0, 0, 0) == 0);
    printf("PASS shims\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "lookup") == 0) return case_lookup();
    if (strcmp(argv[1], "timeouts") == 0) return case_timeouts();
    if (strcmp(argv[1], "word-filter") == 0) return case_word_filter();
    if (strcmp(argv[1], "wait") == 0) return case_wait();
    if (strcmp(argv[1], "shims") == 0) return case_shims();
    return 2;
}
