// Host test for the NpWordFilter title context / request lifecycle and its signed-out results.
// Build and run (from the repository root):
//   clang -std=c11 -I pkg/runtime/core -I pkg/runtime/modules/libSceNpUtility \
//     tests/np_utility_test/main.c pkg/runtime/modules/libSceNpUtility/ps4_np_utility.c \
//     -o /tmp/np_utility_test && /tmp/np_utility_test

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ps4_np_utility.h"
#include "recomp_context.h"

// The tested functions run no guest code: an empty dispatch table makes any guest call abort.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

int main(void) {
    const uint8_t np_id[36] = {'p', 'l', 'a', 'y', 'e', 'r'};
    const uint64_t async_param[3] = {NP_WORD_FILTER_ASYNC_PARAM_SIZE, 0, 0};
    const uint64_t bad_param[3] = {0x10, 0, 0};
    const char *comment = "hello";
    const size_t room = strlen(comment) + 1;
    char sanitized[NP_WORD_FILTER_COMMENT_MAX_LENGTH + 1];
    int32_t result = 0;

    // Title contexts: ids start above the title context id base.
    assert(ps4_np_word_filter_create_title_ctx(NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    int32_t ctx = ps4_np_word_filter_create_title_ctx(np_id);
    assert(ctx == NP_WORD_FILTER_TITLE_CTX_ID_OFFSET + 1);

    // Requests need a live title context; ids start above the request id base.
    assert(ps4_np_word_filter_create_request(ctx + 100) == NP_COMMUNITY_ERROR_INVALID_ID);
    int32_t sync = ps4_np_word_filter_create_request(ctx);
    assert(sync == NP_WORD_FILTER_REQUEST_ID_OFFSET + 1);
    assert(ps4_np_word_filter_create_async_request(ctx, NULL) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(ps4_np_word_filter_create_async_request(ctx, bad_param) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    int32_t async = ps4_np_word_filter_create_async_request(ctx, async_param);
    assert(async == sync + 1);

    // Argument checks come before the request lookup.
    assert(ps4_np_word_filter_censor(sync, NULL, 0, NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    assert(ps4_np_word_filter_censor(sync, comment, room, (void *)1) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(ps4_np_word_filter_sanitize(sync, comment, room, NULL, NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    static char long_comment[NP_WORD_FILTER_COMMENT_MAX_LENGTH + 2];
    memset(long_comment, 'a', NP_WORD_FILTER_COMMENT_MAX_LENGTH + 1);
    assert(ps4_np_word_filter_censor(sync, long_comment, sizeof(long_comment), NULL) ==
           NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    // A comment that runs to the end of guest memory without a terminator is rejected.
    assert(ps4_np_word_filter_censor(sync, comment, 3, NULL) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(ps4_np_word_filter_censor(sync + 100, comment, room, NULL) == NP_COMMUNITY_ERROR_INVALID_ID);

    // A synchronous request returns the signed-out result and carries a single operation.
    assert(ps4_np_word_filter_censor(sync, comment, room, NULL) == NP_ERROR_SIGNED_OUT);
    assert(ps4_np_word_filter_sanitize(sync, comment, room, sanitized, NULL) == NP_COMMUNITY_ERROR_INVALID_TYPE);
    // Polling is for asynchronous requests only.
    assert(ps4_np_word_filter_poll_async(sync, &result) == NP_COMMUNITY_ERROR_INVALID_ID);

    // An asynchronous request runs until started, then finishes signed out.
    assert(ps4_np_word_filter_poll_async(async, NULL) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(ps4_np_word_filter_poll_async(async, &result) == NP_WORD_FILTER_POLL_ASYNC_RET_RUNNING);
    memcpy(sanitized, "untouched", 10);
    assert(ps4_np_word_filter_sanitize(async, comment, room, sanitized, NULL) == 0);
    assert(strcmp(sanitized, "untouched") == 0);
    assert(ps4_np_word_filter_poll_async(async, &result) == NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED);
    assert(result == NP_ERROR_SIGNED_OUT);
    // Aborting a finished request keeps its result.
    assert(ps4_np_word_filter_abort_request(async) == 0);
    assert(ps4_np_word_filter_poll_async(async, &result) == NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED);
    assert(result == NP_ERROR_SIGNED_OUT);

    // Aborting a request that has not run makes it report ABORTED.
    int32_t aborted = ps4_np_word_filter_create_async_request(ctx, async_param);
    assert(ps4_np_word_filter_abort_request(aborted) == 0);
    assert(ps4_np_word_filter_poll_async(aborted, &result) == NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED);
    assert(result == NP_COMMUNITY_ERROR_ABORTED);
    assert(ps4_np_word_filter_censor(aborted, comment, room, NULL) == NP_COMMUNITY_ERROR_ABORTED);

    // Delete request: the id is gone afterwards.
    assert(ps4_np_word_filter_delete_request(sync) == 0);
    assert(ps4_np_word_filter_delete_request(sync) == NP_COMMUNITY_ERROR_INVALID_ID);
    assert(ps4_np_word_filter_abort_request(sync) == NP_COMMUNITY_ERROR_INVALID_ID);

    // Deleting the title context deletes its remaining requests.
    assert(ps4_np_word_filter_delete_title_ctx(ctx) == 0);
    assert(ps4_np_word_filter_delete_title_ctx(ctx) == NP_COMMUNITY_ERROR_INVALID_ID);
    assert(ps4_np_word_filter_poll_async(async, &result) == NP_COMMUNITY_ERROR_INVALID_ID);
    assert(ps4_np_word_filter_delete_request(aborted) == NP_COMMUNITY_ERROR_INVALID_ID);
    assert(ps4_np_word_filter_create_request(ctx) == NP_COMMUNITY_ERROR_INVALID_ID);

    // Limits: at most NP_WORD_FILTER_MAX_CTX_NUM live title contexts and
    // NP_WORD_FILTER_MAX_REQUEST_NUM live requests; freed slots are reused with fresh ids.
    int32_t ctxs[NP_WORD_FILTER_MAX_CTX_NUM];
    for (int i = 0; i < NP_WORD_FILTER_MAX_CTX_NUM; i++) {
        ctxs[i] = ps4_np_word_filter_create_title_ctx(np_id);
        assert(ctxs[i] > NP_WORD_FILTER_TITLE_CTX_ID_OFFSET);
    }
    assert(ps4_np_word_filter_create_title_ctx(np_id) == NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS);
    for (int i = 0; i < NP_WORD_FILTER_MAX_REQUEST_NUM; i++) {
        assert(ps4_np_word_filter_create_request(ctxs[0]) > NP_WORD_FILTER_REQUEST_ID_OFFSET);
    }
    assert(ps4_np_word_filter_create_request(ctxs[1]) == NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS);
    assert(ps4_np_word_filter_delete_title_ctx(ctxs[0]) == 0);
    int32_t again = ps4_np_word_filter_create_request(ctxs[1]);
    assert(again > NP_WORD_FILTER_REQUEST_ID_OFFSET + NP_WORD_FILTER_MAX_REQUEST_NUM);
    int32_t fresh = ps4_np_word_filter_create_title_ctx(np_id);
    assert(fresh > ctxs[NP_WORD_FILTER_MAX_CTX_NUM - 1]);

    printf("[np-utility-test] PASSED\n");
    return 0;
}
