#ifndef PS4_NP_UTILITY_H
#define PS4_NP_UTILITY_H
#include <stdint.h>
#include "recomp_runtime.h"

// NP community error codes (shadPS4 np_error.h).
#define NP_COMMUNITY_ERROR_INVALID_ARGUMENT      ((int32_t)0x80550704)
#define NP_COMMUNITY_ERROR_NO_LOGIN              ((int32_t)0x80550705)
#define NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS      ((int32_t)0x80550706)
#define NP_COMMUNITY_ERROR_ABORTED               ((int32_t)0x80550707)
#define NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT ((int32_t)0x8055070C)
#define NP_COMMUNITY_ERROR_INVALID_ID            ((int32_t)0x8055070E)
#define NP_COMMUNITY_ERROR_INVALID_TYPE          ((int32_t)0x80550711)
// What a request that needs the NP servers fails with on a signed-out host: shadPS4's
// NpHandler returns it for every server request without a signed-in client.
#define NP_ERROR_SIGNED_OUT                      ((int32_t)0x80550006)
#define NP_BANDWIDTH_TEST_ERROR_CONTEXT_NOT_AVAILABLE ((int32_t)0x80551F07)

// NpWordFilter limits and id bases (shadPS4 np_utility.h).
#define NP_WORD_FILTER_MAX_CTX_NUM 32
#define NP_WORD_FILTER_MAX_REQUEST_NUM 32
#define NP_WORD_FILTER_TITLE_CTX_ID_OFFSET 0x01000000
#define NP_WORD_FILTER_REQUEST_ID_OFFSET 0x02000000
#define NP_WORD_FILTER_COMMENT_MAX_LENGTH 0x400
#define NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED 0
#define NP_WORD_FILTER_POLL_ASYNC_RET_RUNNING 1
// sizeof(SceNpWordFilterCreateAsyncRequestParameter): size, cpuAffinityMask, threadPriority.
#define NP_WORD_FILTER_ASYNC_PARAM_SIZE 0x18

// Host side of the NpWordFilter entry points. comment_room is how many bytes are readable at
// comment (the rest of guest memory).
int32_t ps4_np_word_filter_create_title_ctx(const void *self_np_id);
int32_t ps4_np_word_filter_delete_title_ctx(int32_t title_ctx_id);
int32_t ps4_np_word_filter_create_async_request(int32_t title_ctx_id, const void *param);
int32_t ps4_np_word_filter_create_request(int32_t title_ctx_id);
int32_t ps4_np_word_filter_delete_request(int32_t req_id);
int32_t ps4_np_word_filter_abort_request(int32_t req_id);
int32_t ps4_np_word_filter_censor(int32_t req_id, const char *comment, size_t comment_room,
                                  const void *option);
int32_t ps4_np_word_filter_sanitize(int32_t req_id, const char *comment, size_t comment_room,
                                    char *sanitized, const void *option);
int32_t ps4_np_word_filter_poll_async(int32_t req_id, int32_t *result);

#endif
