// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpScore (and its libSceNpScoreCompat alias), following shadPS4
// src/core/libraries/np/np_score. See ps4_np_score.c.

#ifndef PS4_NP_SCORE_H
#define PS4_NP_SCORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "recomp_runtime.h"

// NP error codes (shadPS4 np_error.h).
#define NP_SCORE_ERROR_SIGNED_OUT                ((int32_t)0x80550006)
#define NP_COMMUNITY_ERROR_OUT_OF_MEMORY         ((int32_t)0x80550703)
#define NP_COMMUNITY_ERROR_INVALID_ARGUMENT      ((int32_t)0x80550704)
#define NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS      ((int32_t)0x80550706)
#define NP_COMMUNITY_ERROR_ABORTED               ((int32_t)0x80550707)
#define NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT ((int32_t)0x8055070C)
#define NP_COMMUNITY_ERROR_INVALID_ID            ((int32_t)0x8055070E)
#define NP_COMMUNITY_ERROR_INVALID_ALIGNMENT     ((int32_t)0x80550714)
#define NP_COMMUNITY_ERROR_TOO_MANY_NPID         ((int32_t)0x80550719)
#define NP_COMMUNITY_ERROR_BAD_RESPONSE          ((int32_t)0x80550708)
#define NP_COMMUNITY_SERVER_ERROR_FORBIDDEN                          ((int32_t)0x80550806)
#define NP_COMMUNITY_SERVER_ERROR_INTERNAL_SERVER_ERROR              ((int32_t)0x80550807)
#define NP_COMMUNITY_SERVER_ERROR_GAME_RANKING_NOT_FOUND             ((int32_t)0x80550812)
#define NP_COMMUNITY_SERVER_ERROR_RANKING_STORE_NOT_FOUND            ((int32_t)0x80550814)
#define NP_COMMUNITY_SERVER_ERROR_NOT_BEST_SCORE                     ((int32_t)0x80550815)
#define NP_COMMUNITY_SERVER_ERROR_RANKING_BOARD_MASTER_NOT_FOUND     ((int32_t)0x80550817)
#define NP_COMMUNITY_SERVER_ERROR_RANKING_GAME_DATA_MASTER_NOT_FOUND ((int32_t)0x80550818)
#define NP_COMMUNITY_SERVER_ERROR_TOO_LARGE_DATA                     ((int32_t)0x8055081A)
#define NP_COMMUNITY_SERVER_ERROR_INVALID_SCORE                      ((int32_t)0x80550823)
#define NP_COMMUNITY_SERVER_ERROR_GAME_DATA_ALREADY_EXISTS           ((int32_t)0x8055082C)
#define NP_COMMUNITY_SERVER_ERROR_CONDITIONS_NOT_SATISFIED           ((int32_t)0x80550873)

// Limits (shadPS4 np_score.h).
#define NP_SCORE_COMMENT_MAXLEN 63
#define NP_SCORE_GAMEINFO_MAXSIZE 189
#define NP_SCORE_CENSOR_COMMENT_MAXLEN 255
#define NP_SCORE_SANITIZE_COMMENT_MAXLEN 255
#define NP_SCORE_MAX_RANGE_NUM_PER_REQUEST 100
#define NP_SCORE_MAX_ID_NUM_PER_REQUEST 101
#define NP_SCORE_MAX_NPID_NUM_PER_REQUEST 101
#define NP_SCORE_MAX_CTX_NUM 32
#define NP_SCORE_INVALID_SERVICE_LABEL 0xFFFFFFFFu
// Common::ElfInfo::FW_250: from this SDK on, sceNpScoreGetBoardInfo needs a boardInfo.
#define NP_SCORE_SDK_FW_250 0x2500000

// sceNpScorePollAsync result while the operation runs.
#define NP_SCORE_POLL_ASYNC_RUNNING 1

// Guest structure sizes (shadPS4 np_score.h / np_types.h static_asserts).
#define NP_SCORE_NP_ID_SIZE 36                  // OrbisNpId
#define NP_SCORE_ACCOUNT_ID_SIZE 8              // OrbisNpAccountId
#define NP_SCORE_BOARD_INFO_SIZE 24             // OrbisNpScoreBoardInfo
#define NP_SCORE_COMMENT_SIZE 64                // OrbisNpScoreComment
#define NP_SCORE_GAME_INFO_SIZE 200             // OrbisNpScoreGameInfo
#define NP_SCORE_FRIEND_OPT_SIZE 24             // OrbisNpScoreGetFriendRankingOptParam
#define NP_SCORE_NP_ID_PC_ID_SIZE 44            // OrbisNpScoreNpIdPcId
#define NP_SCORE_ACCOUNT_ID_PC_ID_SIZE 16       // OrbisNpScoreAccountIdPcId
#define NP_SCORE_RANK_DATA_SIZE 0x80            // OrbisNpScoreRankData
#define NP_SCORE_RANK_DATA_A_SIZE 0x90          // OrbisNpScoreRankDataA
#define NP_SCORE_PLAYER_RANK_DATA_SIZE 0x88     // OrbisNpScorePlayerRankData
#define NP_SCORE_PLAYER_RANK_DATA_A_SIZE 0x98   // OrbisNpScorePlayerRankDataA

// OrbisNpScoreGetFriendRankingOptParam. startSerialRank and hits are guest addresses of u32.
typedef struct NpScoreFriendRankingOpt {
    uint64_t size;
    uint64_t start_serial_rank;
    uint64_t hits;
} NpScoreFriendRankingOpt;
_Static_assert(sizeof(NpScoreFriendRankingOpt) == NP_SCORE_FRIEND_OPT_SIZE, "OrbisNpScoreGetFriendRankingOptParam");

// The outputs of the ranking queries, as host pointers (NULL for a guest NULL): the arrays with
// the sizes the caller declared, and lastSortDate (an OrbisRtcTick) and totalRecord.
typedef struct NpScoreRankOut {
    void *rank;
    uint64_t rank_size;
    void *comment;
    uint64_t comment_size;
    void *info;
    uint64_t info_size;
    uint64_t array_num;
    uint64_t *last_sort_date;
    uint32_t *total_record;
} NpScoreRankOut;

// Host side of the entry points. Pointer arguments are host pointers (NULL for a guest NULL);
// the shims translate them. is_async selects the *Async variant.
int32_t ps4_np_score_create_np_title_ctx(uint32_t service_label, const void *self_np_id);
int32_t ps4_np_score_create_np_title_ctx_a(uint32_t service_label, int32_t self_id);
int32_t ps4_np_score_delete_np_title_ctx(int32_t title_ctx_id);
int32_t ps4_np_score_create_request(int32_t title_ctx_id);
int32_t ps4_np_score_delete_request(int32_t req_id);
int32_t ps4_np_score_abort_request(int32_t req_id);
int32_t ps4_np_score_poll_async(int32_t req_id, int32_t *result);
int32_t ps4_np_score_wait_async(int32_t req_id, int32_t *result);
int32_t ps4_np_score_set_player_character_id(int32_t id, int32_t pc_id);

int32_t ps4_np_score_record_score(int32_t req_id, uint32_t board_id, int64_t score, const void *comment,
                                  const void *game_info, uint32_t *tmp_rank, const void *compare_date,
                                  const void *option, bool is_async);
// a_variant: rank.rank holds OrbisNpScoreRankDataA entries instead of OrbisNpScoreRankData.
int32_t ps4_np_score_get_ranking_by_range(int32_t req_id, uint32_t board_id, uint32_t start_serial_rank,
                                          const NpScoreRankOut *out, bool a_variant, const void *option,
                                          bool is_async);
int32_t ps4_np_score_get_ranking_by_np_id(int32_t req_id, uint32_t board_id, const void *np_id_array,
                                          uint64_t np_id_array_size, const NpScoreRankOut *out,
                                          const void *option, bool is_async);
int32_t ps4_np_score_get_ranking_by_np_id_pc_id(int32_t req_id, uint32_t board_id, const void *id_array,
                                                uint64_t id_array_size, const NpScoreRankOut *out,
                                                const void *option, bool is_async);
int32_t ps4_np_score_get_ranking_by_account_id(int32_t req_id, uint32_t board_id, const void *account_id_array,
                                               uint64_t account_id_array_size, const NpScoreRankOut *out,
                                               const void *option, bool is_async);
int32_t ps4_np_score_get_ranking_by_account_id_pc_id(int32_t req_id, uint32_t board_id, const void *id_array,
                                                     uint64_t id_array_size, const NpScoreRankOut *out,
                                                     const void *option, bool is_async);
// opt_start_serial_rank: the u32 the option's startSerialRank points to (NULL when the option
// or its pointer is NULL).
int32_t ps4_np_score_get_friends_ranking(int32_t req_id, uint32_t board_id, int32_t include_self,
                                         const NpScoreRankOut *out, bool a_variant,
                                         const NpScoreFriendRankingOpt *option,
                                         const uint32_t *opt_start_serial_rank, bool is_async);
// sdk_version: the title's compiled SDK version (sceKernelGetCompiledSdkVersion).
int32_t ps4_np_score_get_board_info(int32_t req_id, uint32_t board_id, void *board_info, const void *option,
                                    int32_t sdk_version, bool is_async);
int32_t ps4_np_score_get_game_data(int32_t req_id, uint32_t board_id, const void *np_id, uint64_t *total_size,
                                   uint64_t recv_size, void *data, const void *option, bool is_async);
int32_t ps4_np_score_get_game_data_by_account_id(int32_t req_id, uint32_t board_id, uint64_t account_id,
                                                 uint64_t *total_size, uint64_t recv_size, void *data,
                                                 const void *option, bool is_async);
int32_t ps4_np_score_record_game_data(int32_t req_id, uint32_t board_id, int64_t score, uint64_t total_size,
                                      uint64_t send_size, const void *data, const void *option, bool is_async);
// comment_room: how many bytes are readable at comment.
int32_t ps4_np_score_censor_comment(int32_t req_id, const char *comment, size_t comment_room, const void *option,
                                    bool is_async);
int32_t ps4_np_score_sanitize_comment(int32_t req_id, const char *comment, size_t comment_room, char *sanitized,
                                      const void *option, bool is_async);

#endif
