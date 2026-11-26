// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_NP_UTILITY_H
#define PS4_NP_UTILITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "recomp_runtime.h"

// NP error codes (shadPS4 np_error.h).
#define NP_ERROR_SIGNED_OUT                      ((int32_t)0x80550006)
#define NP_COMMUNITY_ERROR_INVALID_ARGUMENT      ((int32_t)0x80550704)
#define NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS      ((int32_t)0x80550706)
#define NP_COMMUNITY_ERROR_ABORTED               ((int32_t)0x80550707)
#define NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT ((int32_t)0x8055070C)
#define NP_COMMUNITY_ERROR_INVALID_ID            ((int32_t)0x8055070E)
#define NP_COMMUNITY_ERROR_INVALID_TYPE          ((int32_t)0x80550711)

// NpLookup limits (shadPS4 np_utility.h).
#define NP_LOOKUP_MAX_CTX_NUM 32
#define NP_LOOKUP_MAX_REQUEST_NUM 32
#define NP_LOOKUP_POLL_ASYNC_RET_FINISHED 0
#define NP_LOOKUP_POLL_ASYNC_RET_RUNNING 1
#define NP_LOOKUP_TIMEOUT_NO_EFFECT 0u

// NpWordFilter limits and id bases (shadPS4 np_utility.h).
#define NP_WORD_FILTER_MAX_CTX_NUM 32
#define NP_WORD_FILTER_MAX_REQUEST_NUM 32
#define NP_WORD_FILTER_TITLE_CTX_ID_OFFSET 0x01000000
#define NP_WORD_FILTER_REQUEST_ID_OFFSET 0x02000000
#define NP_WORD_FILTER_COMMENT_MAX_LENGTH 0x400
#define NP_WORD_FILTER_POLL_ASYNC_RET_FINISHED 0
#define NP_WORD_FILTER_POLL_ASYNC_RET_RUNNING 1
// sizeof(OrbisNpWordFilterCreateAsyncRequestParameter): size, cpuAffinityMask, threadPriority.
#define NP_WORD_FILTER_ASYNC_PARAM_SIZE 0x18

// Host side of the NpLookup entry points. Pointer arguments are host pointers (NULL for a
// guest NULL); the shims translate them.
int32_t ps4_np_lookup_create_title_ctx(const void *self_np_id);
int32_t ps4_np_lookup_create_title_ctx_a(int32_t user_id);
int32_t ps4_np_lookup_delete_title_ctx(int32_t title_ctx_id);
int32_t ps4_np_lookup_create_request(int32_t title_ctx_id, bool is_async);
int32_t ps4_np_lookup_delete_request(int32_t req_id);
int32_t ps4_np_lookup_abort_request(int32_t req_id);
int32_t ps4_np_lookup_np_id(int32_t req_id, const void *online_id, void *np_id, const void *option);
int32_t ps4_np_lookup_poll_async(int32_t req_id, int32_t *result);
int32_t ps4_np_lookup_wait_async(int32_t req_id, int32_t *result);
int32_t ps4_np_lookup_set_timeout(int32_t id, int32_t resolve_retry, uint32_t resolve_timeout,
                                  uint32_t conn_timeout, uint32_t send_timeout, uint32_t recv_timeout);

// Host side of the NpWordFilter entry points. comment_room is how many bytes are readable at
// comment (the rest of guest memory).
int32_t ps4_np_word_filter_create_title_ctx(const void *self_np_id);
int32_t ps4_np_word_filter_create_title_ctx_a(int32_t user_id);
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
int32_t ps4_np_word_filter_wait_async(int32_t req_id, int32_t *result);
int32_t ps4_np_word_filter_set_timeout(int32_t id, int32_t resolve_retry, uint32_t resolve_timeout,
                                       uint32_t conn_timeout, uint32_t send_timeout,
                                       uint32_t recv_timeout);

#endif
