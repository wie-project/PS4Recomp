// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpScore against shadPS4's np_score.cpp without a shadNet sign-in: title contexts and
// requests (ids, limits, inherited player character ids, delete / abort semantics), the
// argument checks of every operation in shadPS4's order with the output arrays cleared before
// the request is looked up, the signed-out completion of server operations, RecordGameData
// chunk buffering, the local comment operations, WaitAsync across threads, and the shims'
// register and stack argument handling.

#include "ps4_np_score.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../host_guest_mem.h"
#include "../shadnet_fake_server.h"
#include "ps4_np_handler.h"
#include "ps4_npbind.h"
#include "ps4_user_service.h"

// ps4_vfs_resolve: /app0/sce_sys/npbind.dat is g_npbind_path when set (the online case).
static const char *g_npbind_path;
const char *ps4_vfs_get_app_root(void) { return NULL; }
int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    if (!g_npbind_path || strcmp(guest_path, "/app0/sce_sys/npbind.dat") != 0) return -1;
    snprintf(host_path, host_path_sz, "%s", g_npbind_path);
    return 0;
}

// Runtime symbols the sources link against.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];
static uint32_t g_sdk = 0x9508001;
uint32_t ps4_compiled_sdk_version(const GuestContext *ctx) {
    (void)ctx;
    return g_sdk;
}

void shim_sceNpScoreCreateNpTitleCtx(GuestContext *ctx);
void shim_sceNpScoreCreateRequest(GuestContext *ctx);
void shim_sceNpScoreGetRankingByRange(GuestContext *ctx);
void shim_sceNpScoreGetRankingByNpIdAsync(GuestContext *ctx);
void shim_sceNpScoreGetFriendsRankingA(GuestContext *ctx);
void shim_sceNpScoreGetBoardInfo(GuestContext *ctx);
void shim_sceNpScoreRecordScoreAsync(GuestContext *ctx);
void shim_sceNpScoreSanitizeComment(GuestContext *ctx);
void shim_sceNpScorePollAsync(GuestContext *ctx);
void shim_sceNpScoreSetTimeout(GuestContext *ctx);

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

// Calls a shim with up to six register arguments and the stack arguments in stack[1..]
// (stack[0] stands for the return address).
static int32_t call_args(void (*shim)(GuestContext *), const uint64_t reg[6], uint64_t *stack) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    ctx.mem_size = ~0ull;
    ctx.rdi = reg[0];
    ctx.rsi = reg[1];
    ctx.rdx = reg[2];
    ctx.rcx = reg[3];
    ctx.r8 = reg[4];
    ctx.r9 = reg[5];
    ctx.rsp = stack ? A(stack) : 0;
    ctx.rax = 99;
    shim(&ctx);
    return (int32_t)ctx.rax;
}

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    const uint64_t reg[6] = {a0, a1, a2, a3, 0, 0};
    return call_args(shim, reg, NULL);
}

static const uint8_t g_np_id[NP_SCORE_NP_ID_SIZE] = {'p', 'l', 'a', 'y', 'e', 'r'};

static NpScoreRankOut rank_out(void *rank, uint64_t rank_size, void *comment, uint64_t comment_size, void *info,
                               uint64_t info_size, uint64_t num) {
    NpScoreRankOut o = {rank, rank_size, comment, comment_size, info, info_size, num, NULL, NULL};
    return o;
}

static bool all_bytes(const void *p, size_t n, uint8_t v) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        if (b[i] != v) return false;
    }
    return true;
}

static int case_contexts(void) {
    int32_t result = 0;

    // CreateNpTitleCtx: the service label, then the NP id. Ids count from 1.
    CHECK(ps4_np_score_create_np_title_ctx(NP_SCORE_INVALID_SERVICE_LABEL, g_np_id) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_create_np_title_ctx(0, NULL) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    const int32_t ctx = ps4_np_score_create_np_title_ctx(0, g_np_id);
    CHECK(ctx == 1);
    // The A variant needs a signed-in user, whatever the label.
    CHECK(ps4_np_score_create_np_title_ctx_a(0, 1) == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_create_np_title_ctx_a(NP_SCORE_INVALID_SERVICE_LABEL, 1) == NP_SCORE_ERROR_SIGNED_OUT);

    // At most 32 title contexts; ids are not reused.
    int32_t ids[NP_SCORE_MAX_CTX_NUM];
    for (int i = 1; i < NP_SCORE_MAX_CTX_NUM; i++) {
        ids[i] = ps4_np_score_create_np_title_ctx(0, g_np_id);
        CHECK(ids[i] == i + 1);
    }
    CHECK(ps4_np_score_create_np_title_ctx(0, g_np_id) == NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS);
    CHECK(ps4_np_score_delete_np_title_ctx(ids[5]) == 0);
    CHECK(ps4_np_score_delete_np_title_ctx(ids[5]) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_score_create_np_title_ctx(0, g_np_id) == NP_SCORE_MAX_CTX_NUM + 1);

    // Requests need a live title context; their ids count from 1 on their own.
    CHECK(ps4_np_score_create_request(999) == NP_COMMUNITY_ERROR_INVALID_ID);
    const int32_t req = ps4_np_score_create_request(ctx);
    CHECK(req == 1);
    // A new request is running until its first operation completes.
    CHECK(ps4_np_score_poll_async(req, &result) == NP_SCORE_POLL_ASYNC_RUNNING);
    CHECK(ps4_np_score_poll_async(999, &result) == NP_COMMUNITY_ERROR_INVALID_ID);

    // At most 32 live requests over all title contexts.
    for (int i = 1; i < NP_SCORE_MAX_CTX_NUM; i++) {
        CHECK(ps4_np_score_create_request(ids[1 + i % 3]) == i + 1);
    }
    CHECK(ps4_np_score_create_request(ctx) == NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS);
    // Deleting a title context deletes its requests.
    CHECK(ps4_np_score_delete_np_title_ctx(ids[2]) == 0);
    // (Requests 2, 5, 8, ... belong to it; request 3 belongs to another title context.)
    CHECK(ps4_np_score_poll_async(2, &result) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_score_poll_async(3, &result) == NP_SCORE_POLL_ASYNC_RUNNING);
    const int32_t next = ps4_np_score_create_request(ctx);
    CHECK(next == NP_SCORE_MAX_CTX_NUM + 1);
    for (int i = 1; i < NP_SCORE_MAX_CTX_NUM; i++) {
        if (ids[i] != 0 && i != 2 && i != 5) CHECK(ps4_np_score_delete_np_title_ctx(ids[i]) == 0);
    }

    // SetPlayerCharacterId: a negative id, then a title context, then a request, else no id.
    CHECK(ps4_np_score_set_player_character_id(ctx, -1) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_set_player_character_id(ctx, 7) == 0);
    CHECK(ps4_np_score_set_player_character_id(next, 3) == 0);
    CHECK(ps4_np_score_set_player_character_id(12345, 3) == NP_COMMUNITY_ERROR_INVALID_ID);

    // Abort completes a running request as aborted; it stays until deleted, and an aborted
    // request refuses new operations.
    CHECK(ps4_np_score_abort_request(req) == 0);
    CHECK(ps4_np_score_poll_async(req, &result) == 0 && result == NP_COMMUNITY_ERROR_ABORTED);
    CHECK(ps4_np_score_get_game_data_by_account_id(req, 1, 5, NULL, 0, NULL, NULL, true) ==
          NP_COMMUNITY_ERROR_ABORTED);
    CHECK(ps4_np_score_delete_request(req) == 0);
    CHECK(ps4_np_score_delete_request(req) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_score_abort_request(req) == NP_COMMUNITY_ERROR_INVALID_ID);

    // A completed request keeps its result when aborted and takes the next operation.
    CHECK(ps4_np_score_get_game_data_by_account_id(next, 1, 5, NULL, 0, NULL, NULL, true) ==
          NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_abort_request(next) == 0);
    CHECK(ps4_np_score_poll_async(next, &result) == 0 && result == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_get_game_data_by_account_id(next, 1, 5, NULL, 0, NULL, NULL, false) ==
          NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_delete_np_title_ctx(ctx) == 0);
    CHECK(ps4_np_score_poll_async(next, &result) == NP_COMMUNITY_ERROR_INVALID_ID);
    printf("PASS contexts\n");
    return 0;
}

static int case_operations(void) {
    const int32_t ctx = ps4_np_score_create_np_title_ctx(0, g_np_id);
    const int32_t req = ps4_np_score_create_request(ctx);
    int32_t result = 0;
    uint8_t rank[3 * NP_SCORE_PLAYER_RANK_DATA_A_SIZE];
    uint8_t comment[3 * NP_SCORE_COMMENT_SIZE];
    uint8_t info[3 * NP_SCORE_GAME_INFO_SIZE];
    uint8_t ids[3 * NP_SCORE_NP_ID_PC_ID_SIZE];
    const uint8_t opt = 0;

    // RecordScore: option, then gameInfo->infoSize, then the request.
    uint8_t game_info[NP_SCORE_GAME_INFO_SIZE] = {0};
    game_info[0] = NP_SCORE_GAMEINFO_MAXSIZE + 1;
    CHECK(ps4_np_score_record_score(req, 1, 10, NULL, NULL, NULL, NULL, &opt, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_record_score(999, 1, 10, NULL, game_info, NULL, NULL, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_record_score(999, 1, 10, NULL, NULL, NULL, NULL, NULL, false) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_score_record_score(req, 1, 10, NULL, NULL, NULL, NULL, NULL, false) == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_poll_async(req, &result) == 0 && result == NP_SCORE_ERROR_SIGNED_OUT);
    // The asynchronous variant returns the dispatch error as well.
    CHECK(ps4_np_score_record_score(req, 1, 10, NULL, NULL, NULL, NULL, NULL, true) == NP_SCORE_ERROR_SIGNED_OUT);

    // GetRankingByRange: option, arrayNum, startSerialRank, rankArray, arrayNum 0, sizes; the
    // arrays are cleared before the request is looked up.
    NpScoreRankOut o = rank_out(rank, 2 * NP_SCORE_RANK_DATA_SIZE, comment, 2 * NP_SCORE_COMMENT_SIZE, info,
                                2 * NP_SCORE_GAME_INFO_SIZE, 2);
    CHECK(ps4_np_score_get_ranking_by_range(req, 1, 1, &o, false, &opt, false) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    o.array_num = NP_SCORE_MAX_RANGE_NUM_PER_REQUEST + 1;
    CHECK(ps4_np_score_get_ranking_by_range(req, 1, 1, &o, false, NULL, false) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    o.array_num = 2;
    CHECK(ps4_np_score_get_ranking_by_range(req, 1, 0, &o, false, NULL, false) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    NpScoreRankOut no_rank = o;
    no_rank.rank = NULL;
    CHECK(ps4_np_score_get_ranking_by_range(req, 1, 1, &no_rank, true, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    NpScoreRankOut zero = o;
    zero.array_num = 0;
    CHECK(ps4_np_score_get_ranking_by_range(req, 1, 1, &zero, false, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_get_ranking_by_range(req, 1, 1, &o, true, NULL, false) == NP_COMMUNITY_ERROR_INVALID_ALIGNMENT);
    memset(rank, 0xAA, sizeof(rank));
    memset(comment, 0xAA, sizeof(comment));
    NpScoreRankOut bad_comment = o;
    bad_comment.comment_size = 1;
    CHECK(ps4_np_score_get_ranking_by_range(req, 1, 1, &bad_comment, false, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ALIGNMENT);
    CHECK(all_bytes(rank, 2 * NP_SCORE_RANK_DATA_SIZE, 0) && rank[2 * NP_SCORE_RANK_DATA_SIZE] == 0xAA);
    CHECK(comment[0] == 0xAA);
    memset(rank, 0xAA, sizeof(rank));
    memset(info, 0xAA, sizeof(info));
    CHECK(ps4_np_score_get_ranking_by_range(999, 1, 1, &o, false, NULL, false) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(all_bytes(rank, 2 * NP_SCORE_RANK_DATA_SIZE, 0) && all_bytes(comment, 2 * NP_SCORE_COMMENT_SIZE, 0) &&
          all_bytes(info, 2 * NP_SCORE_GAME_INFO_SIZE, 0) && info[2 * NP_SCORE_GAME_INFO_SIZE] == 0xAA);
    CHECK(ps4_np_score_get_ranking_by_range(req, 1, 1, &o, false, NULL, true) == NP_SCORE_ERROR_SIGNED_OUT);
    o.rank_size = 2 * NP_SCORE_RANK_DATA_A_SIZE;
    CHECK(ps4_np_score_get_ranking_by_range(req, 1, 1, &o, true, NULL, false) == NP_SCORE_ERROR_SIGNED_OUT);

    // GetRankingByNpId: option, too many ids, pointers, sizes (arrayNum 0 is accepted).
    NpScoreRankOut p = rank_out(rank, 3 * NP_SCORE_PLAYER_RANK_DATA_SIZE, NULL, 0, NULL, 0, 3);
    CHECK(ps4_np_score_get_ranking_by_np_id(req, 1, ids, 3 * NP_SCORE_NP_ID_SIZE, &p, &opt, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    NpScoreRankOut many = p;
    many.array_num = NP_SCORE_MAX_NPID_NUM_PER_REQUEST + 1;
    CHECK(ps4_np_score_get_ranking_by_np_id(req, 1, NULL, 0, &many, NULL, false) == NP_COMMUNITY_ERROR_TOO_MANY_NPID);
    CHECK(ps4_np_score_get_ranking_by_np_id(req, 1, NULL, 3 * NP_SCORE_NP_ID_SIZE, &p, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_get_ranking_by_np_id(req, 1, ids, 3 * NP_SCORE_NP_ID_SIZE + 1, &p, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ALIGNMENT);
    CHECK(ps4_np_score_get_ranking_by_np_id(req, 1, ids, 3 * NP_SCORE_NP_ID_SIZE, &p, NULL, false) ==
          NP_SCORE_ERROR_SIGNED_OUT);
    NpScoreRankOut none = rank_out(rank, 0, NULL, 0, NULL, 0, 0);
    CHECK(ps4_np_score_get_ranking_by_np_id(req, 1, ids, 0, &none, NULL, true) == NP_SCORE_ERROR_SIGNED_OUT);

    // GetRankingByNpIdPcId: the pointers and arrayNum 0 come before the id count.
    CHECK(ps4_np_score_get_ranking_by_np_id_pc_id(req, 1, ids, 0, &none, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_get_ranking_by_np_id_pc_id(req, 1, NULL, 0, &many, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_get_ranking_by_np_id_pc_id(req, 1, ids, 0, &many, NULL, false) ==
          NP_COMMUNITY_ERROR_TOO_MANY_NPID);
    CHECK(ps4_np_score_get_ranking_by_np_id_pc_id(req, 1, ids, 3 * NP_SCORE_NP_ID_SIZE, &p, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ALIGNMENT);
    CHECK(ps4_np_score_get_ranking_by_np_id_pc_id(req, 1, ids, 3 * NP_SCORE_NP_ID_PC_ID_SIZE, &p, NULL, false) ==
          NP_SCORE_ERROR_SIGNED_OUT);

    // GetRankingByAccountId(PcId): player rank data A entries.
    NpScoreRankOut a = rank_out(rank, 3 * NP_SCORE_PLAYER_RANK_DATA_A_SIZE, NULL, 0, NULL, 0, 3);
    CHECK(ps4_np_score_get_ranking_by_account_id(req, 1, ids, 3 * NP_SCORE_ACCOUNT_ID_SIZE, &many, NULL, false) ==
          NP_COMMUNITY_ERROR_TOO_MANY_NPID);
    CHECK(ps4_np_score_get_ranking_by_account_id(req, 1, ids, 0, &none, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_get_ranking_by_account_id(req, 1, ids, 3 * NP_SCORE_ACCOUNT_ID_SIZE, &p, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ALIGNMENT);
    CHECK(ps4_np_score_get_ranking_by_account_id(req, 1, ids, 3 * NP_SCORE_ACCOUNT_ID_SIZE, &a, NULL, false) ==
          NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_get_ranking_by_account_id_pc_id(req, 1, ids, 3 * NP_SCORE_ACCOUNT_ID_SIZE, &a, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ALIGNMENT);
    CHECK(ps4_np_score_get_ranking_by_account_id_pc_id(req, 1, ids, 3 * NP_SCORE_ACCOUNT_ID_PC_ID_SIZE, &a, NULL,
                                                       true) == NP_SCORE_ERROR_SIGNED_OUT);

    // GetFriendsRanking: the rank array and its size come first, then the option rules.
    NpScoreRankOut f = rank_out(rank, 2 * NP_SCORE_RANK_DATA_SIZE, NULL, 0, NULL, 0, 2);
    NpScoreFriendRankingOpt fo = {NP_SCORE_FRIEND_OPT_SIZE, 0, 0};
    const uint32_t start_zero = 0;
    CHECK(ps4_np_score_get_friends_ranking(req, 1, 0, &no_rank, false, &fo, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_get_friends_ranking(req, 1, 0, &f, true, &fo, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ALIGNMENT);
    CHECK(ps4_np_score_get_friends_ranking(req, 1, 0, &f, false, &fo, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    fo.size = 8;
    CHECK(ps4_np_score_get_friends_ranking(req, 1, 1, &f, false, &fo, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    fo.size = NP_SCORE_FRIEND_OPT_SIZE;
    CHECK(ps4_np_score_get_friends_ranking(req, 1, 1, &f, false, &fo, &start_zero, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    NpScoreRankOut f0 = rank_out(rank, 0, NULL, 0, NULL, 0, 0);
    CHECK(ps4_np_score_get_friends_ranking(req, 1, 1, &f0, false, &fo, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_get_friends_ranking(req, 1, 1, &f, false, &fo, NULL, false) == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_get_friends_ranking(req, 1, 0, &f, false, NULL, NULL, true) == NP_SCORE_ERROR_SIGNED_OUT);

    // GetBoardInfo: from SDK 2.50 a boardInfo is required; before it a NULL boardInfo succeeds.
    uint8_t board[NP_SCORE_BOARD_INFO_SIZE];
    memset(board, 0xAA, sizeof(board));
    CHECK(ps4_np_score_get_board_info(req, 1, board, &opt, NP_SCORE_SDK_FW_250, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_get_board_info(req, 1, NULL, NULL, NP_SCORE_SDK_FW_250, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_get_board_info(999, 1, NULL, NULL, NP_SCORE_SDK_FW_250 - 1, false) ==
          NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_score_get_board_info(req, 1, NULL, NULL, NP_SCORE_SDK_FW_250 - 1, false) == 0);
    CHECK(ps4_np_score_poll_async(req, &result) == 0 && result == 0);
    CHECK(ps4_np_score_get_board_info(req, 1, board, NULL, NP_SCORE_SDK_FW_250, false) == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(all_bytes(board, sizeof(board), 0));

    CHECK(ps4_np_score_delete_np_title_ctx(ctx) == 0);
    printf("PASS operations\n");
    return 0;
}

static int case_game_data(void) {
    const int32_t ctx = ps4_np_score_create_np_title_ctx(0, g_np_id);
    const int32_t req = ps4_np_score_create_request(ctx);
    int32_t result = 0;
    const uint8_t opt = 0;
    uint8_t buf[64] = {0};
    uint64_t total = 0;
    const uint8_t empty_id[NP_SCORE_NP_ID_SIZE] = {0};

    // GetGameData: the NP id, the option, the request, then an empty online id completes the
    // request with INVALID_ARGUMENT.
    CHECK(ps4_np_score_get_game_data(req, 1, NULL, &total, 8, buf, &opt, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_get_game_data(req, 1, g_np_id, &total, 8, buf, &opt, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_get_game_data(999, 1, g_np_id, &total, 8, buf, NULL, false) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_score_get_game_data(req, 1, empty_id, &total, 8, buf, NULL, true) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_poll_async(req, &result) == 0 && result == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_get_game_data(req, 1, g_np_id, &total, 8, buf, NULL, false) == NP_SCORE_ERROR_SIGNED_OUT);

    // GetGameDataByAccountId: the option, then a zero account id.
    CHECK(ps4_np_score_get_game_data_by_account_id(req, 1, 0, &total, 8, buf, &opt, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_get_game_data_by_account_id(req, 1, 0, &total, 8, buf, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_get_game_data_by_account_id(req, 1, 9, &total, 8, buf, NULL, true) ==
          NP_SCORE_ERROR_SIGNED_OUT);

    // RecordGameData: option, totalSize, data, sendSize.
    CHECK(ps4_np_score_record_game_data(req, 1, 5, 10, 4, buf, &opt, false) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_record_game_data(req, 1, 5, 0, 4, buf, NULL, false) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_record_game_data(req, 1, 5, 10, 4, NULL, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_record_game_data(req, 1, 5, 10, 0, buf, NULL, false) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    // Chunks are buffered until totalSize bytes arrived; each partial chunk completes with OK.
    CHECK(ps4_np_score_record_game_data(req, 1, 5, 10, 4, buf, NULL, false) == 0);
    CHECK(ps4_np_score_poll_async(req, &result) == 0 && result == 0);
    // The first chunk fixed the total: a different totalSize later does not change it.
    CHECK(ps4_np_score_record_game_data(req, 1, 5, 99, 4, buf, NULL, true) == 0);
    // The last chunk sends the blob (only the missing bytes of a longer chunk are taken).
    CHECK(ps4_np_score_record_game_data(req, 1, 5, 10, 64, buf, NULL, true) == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_poll_async(req, &result) == 0 && result == NP_SCORE_ERROR_SIGNED_OUT);
    // The next transfer starts over with its own total.
    CHECK(ps4_np_score_record_game_data(req, 1, 5, 3, 3, buf, NULL, false) == NP_SCORE_ERROR_SIGNED_OUT);
    // Deleting a request in the middle of a transfer drops its buffer.
    CHECK(ps4_np_score_record_game_data(req, 1, 5, 30, 3, buf, NULL, false) == 0);
    CHECK(ps4_np_score_delete_request(req) == 0);
    CHECK(ps4_np_score_delete_np_title_ctx(ctx) == 0);
    printf("PASS game-data\n");
    return 0;
}

static int case_comments(void) {
    const int32_t ctx = ps4_np_score_create_np_title_ctx(0, g_np_id);
    const int32_t req = ps4_np_score_create_request(ctx);
    int32_t result = 0;
    const uint8_t opt = 0;
    char sanitized[NP_SCORE_SANITIZE_COMMENT_MAXLEN + 2];
    char long_comment[NP_SCORE_SANITIZE_COMMENT_MAXLEN + 2];
    memset(long_comment, 'x', sizeof(long_comment) - 1);
    long_comment[sizeof(long_comment) - 1] = '\0';

    // Censor: option, comment, request, length; a valid comment completes with OK.
    CHECK(ps4_np_score_censor_comment(req, "hi", 3, &opt, false) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_censor_comment(req, NULL, 0, NULL, false) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_censor_comment(999, "hi", 3, NULL, false) == NP_COMMUNITY_ERROR_INVALID_ID);
    CHECK(ps4_np_score_censor_comment(req, "hi", 3, NULL, false) == 0);
    CHECK(ps4_np_score_poll_async(req, &result) == 0 && result == 0);
    // A too long comment fails after the operation started: the request stays running.
    CHECK(ps4_np_score_censor_comment(req, long_comment, sizeof(long_comment), NULL, true) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_poll_async(req, &result) == NP_SCORE_POLL_ASYNC_RUNNING);

    // Sanitize: option, comment, output, request, length; the comment is copied unchanged.
    CHECK(ps4_np_score_sanitize_comment(req, "hi", 3, sanitized, &opt, false) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_np_score_sanitize_comment(req, NULL, 0, sanitized, NULL, false) ==
          NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_sanitize_comment(req, "hi", 3, NULL, NULL, false) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    CHECK(ps4_np_score_sanitize_comment(req, long_comment, sizeof(long_comment), sanitized, NULL, false) ==
          NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    memset(sanitized, 'z', sizeof(sanitized));
    CHECK(ps4_np_score_sanitize_comment(req, "bad word", 9, sanitized, NULL, true) == 0);
    CHECK(strcmp(sanitized, "bad word") == 0 && sanitized[9] == 'z');
    CHECK(ps4_np_score_wait_async(req, &result) == 0 && result == 0);
    // The longest accepted comment.
    long_comment[NP_SCORE_SANITIZE_COMMENT_MAXLEN] = '\0';
    CHECK(ps4_np_score_sanitize_comment(req, long_comment, sizeof(long_comment), sanitized, NULL, false) == 0);
    CHECK(strlen(sanitized) == NP_SCORE_SANITIZE_COMMENT_MAXLEN);
    CHECK(ps4_np_score_delete_np_title_ctx(ctx) == 0);
    printf("PASS comments\n");
    return 0;
}

typedef struct {
    int32_t req;
    int32_t rc;
    int32_t result;
} Waiter;

static void *waiter_main(void *arg) {
    Waiter *w = arg;
    w->rc = ps4_np_score_wait_async(w->req, &w->result);
    return NULL;
}

// Starts a thread blocked in WaitAsync on req; it cannot return before the request completes.
static int start_waiter(pthread_t *t, Waiter *w) {
    if (pthread_create(t, NULL, waiter_main, w) != 0) return 1;
    usleep(50000);
    return 0;
}

static int case_wait(void) {
    const int32_t ctx = ps4_np_score_create_np_title_ctx(0, g_np_id);
    pthread_t t;

    // Deleting the request a thread waits on completes it as aborted for that thread.
    Waiter w = {ps4_np_score_create_request(ctx), 99, 99};
    CHECK(start_waiter(&t, &w) == 0);
    CHECK(w.rc == 99);
    CHECK(ps4_np_score_delete_request(w.req) == 0);
    pthread_join(t, NULL);
    CHECK(w.rc == 0 && w.result == NP_COMMUNITY_ERROR_ABORTED);

    // An operation on another thread completes the waiter with its result.
    Waiter w2 = {ps4_np_score_create_request(ctx), 99, 99};
    CHECK(start_waiter(&t, &w2) == 0);
    CHECK(ps4_np_score_censor_comment(w2.req, "ok", 3, NULL, true) == 0);
    pthread_join(t, NULL);
    CHECK(w2.rc == 0 && w2.result == 0);

    // Deleting the title context completes its requests' waiters.
    Waiter w3 = {ps4_np_score_create_request(ctx), 99, 99};
    CHECK(start_waiter(&t, &w3) == 0);
    CHECK(ps4_np_score_delete_np_title_ctx(ctx) == 0);
    pthread_join(t, NULL);
    CHECK(w3.rc == 0 && w3.result == NP_COMMUNITY_ERROR_ABORTED);
    printf("PASS wait\n");
    return 0;
}

static int case_shims(void) {
    int32_t result = 0;
    CHECK(call(shim_sceNpScoreCreateNpTitleCtx, 0, 0, 0, 0) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);
    const int32_t ctx = call(shim_sceNpScoreCreateNpTitleCtx, 0, A(g_np_id), 0, 0);
    CHECK(ctx > 0);
    const int32_t req = call(shim_sceNpScoreCreateRequest, (uint64_t)ctx, 0, 0, 0);
    CHECK(req > 0);

    // GetRankingByRange: commentArraySize, infoArray, infoArraySize, arrayNum, lastSortDate,
    // totalRecord and option are stack arguments.
    uint8_t rank[2 * NP_SCORE_RANK_DATA_SIZE];
    uint8_t comment[2 * NP_SCORE_COMMENT_SIZE];
    memset(rank, 0xAA, sizeof(rank));
    memset(comment, 0xAA, sizeof(comment));
    uint64_t opt = 0;
    uint64_t stack[9] = {0, sizeof(comment), 0, 0, 2, 0, 0, 0, 0};
    const uint64_t range[6] = {(uint64_t)req, 1, 1, A(rank), sizeof(rank), A(comment)};
    CHECK(call_args(shim_sceNpScoreGetRankingByRange, range, stack) == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(all_bytes(rank, sizeof(rank), 0) && all_bytes(comment, sizeof(comment), 0));
    stack[7] = A(&opt);
    CHECK(call_args(shim_sceNpScoreGetRankingByRange, range, stack) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    stack[7] = 0;
    stack[4] = 3;
    CHECK(call_args(shim_sceNpScoreGetRankingByRange, range, stack) == NP_COMMUNITY_ERROR_INVALID_ALIGNMENT);

    // GetRankingByNpIdAsync: the id array in rdx / rcx, the rank array in r8 / r9, arrayNum is
    // the fifth stack argument and option the eighth.
    uint8_t ids[2 * NP_SCORE_NP_ID_SIZE] = {0};
    uint8_t prank[2 * NP_SCORE_PLAYER_RANK_DATA_SIZE];
    memset(prank, 0xAA, sizeof(prank));
    uint64_t id_stack[9] = {0, 0, 0, 0, 0, 2, 0, 0, 0};
    const uint64_t by_id[6] = {(uint64_t)req, 1, A(ids), sizeof(ids), A(prank), sizeof(prank)};
    CHECK(call_args(shim_sceNpScoreGetRankingByNpIdAsync, by_id, id_stack) == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(all_bytes(prank, sizeof(prank), 0));
    CHECK(call(shim_sceNpScorePollAsync, (uint64_t)req, A(&result), 0, 0) == 0 && result == NP_SCORE_ERROR_SIGNED_OUT);
    id_stack[8] = A(&opt);
    CHECK(call_args(shim_sceNpScoreGetRankingByNpIdAsync, by_id, id_stack) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);

    // GetFriendsRankingA: the option's startSerialRank is read through guest memory.
    uint8_t arank[NP_SCORE_RANK_DATA_A_SIZE];
    const uint32_t start_zero = 0;
    uint64_t fopt[3] = {NP_SCORE_FRIEND_OPT_SIZE, A(&start_zero), 0};
    uint64_t f_stack[9] = {0, 0, 0, 0, 1, 0, 0, A(fopt), 0};
    const uint64_t friends[6] = {(uint64_t)req, 1, 1, A(arank), sizeof(arank), 0};
    CHECK(call_args(shim_sceNpScoreGetFriendsRankingA, friends, f_stack) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    fopt[1] = 0;
    CHECK(call_args(shim_sceNpScoreGetFriendsRankingA, friends, f_stack) == NP_SCORE_ERROR_SIGNED_OUT);

    // GetBoardInfo reads the title's SDK version.
    g_sdk = NP_SCORE_SDK_FW_250 - 1;
    CHECK(call(shim_sceNpScoreGetBoardInfo, (uint64_t)req, 1, 0, 0) == 0);
    g_sdk = NP_SCORE_SDK_FW_250;
    CHECK(call(shim_sceNpScoreGetBoardInfo, (uint64_t)req, 1, 0, 0) == NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT);

    // RecordScoreAsync: compareDate and option are stack arguments.
    uint8_t game_info[NP_SCORE_GAME_INFO_SIZE] = {0};
    game_info[0] = NP_SCORE_GAMEINFO_MAXSIZE;
    uint64_t r_stack[3] = {0, 0, 0};
    const uint64_t record[6] = {(uint64_t)req, 1, 100, 0, A(game_info), 0};
    CHECK(call_args(shim_sceNpScoreRecordScoreAsync, record, r_stack) == NP_SCORE_ERROR_SIGNED_OUT);
    r_stack[2] = A(&opt);
    CHECK(call_args(shim_sceNpScoreRecordScoreAsync, record, r_stack) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);

    // SanitizeComment copies the comment to the guest buffer.
    char out[NP_SCORE_SANITIZE_COMMENT_MAXLEN + 1];
    CHECK(call(shim_sceNpScoreSanitizeComment, (uint64_t)req, A("text"), A(out), 0) == 0);
    CHECK(strcmp(out, "text") == 0);

    // A shadPS4 stub.
    CHECK(call(shim_sceNpScoreSetTimeout, (uint64_t)req, 0, 0, 0) == 0);
    printf("PASS shim-args\n");
    return 0;
}

// Guest layouts the online case reads (shadPS4 np_score.h).
typedef struct {
    OrbisNpId np_id;
    uint8_t reserved[52];
    int32_t pc_id;
    uint32_t serial_rank, rank, highest_rank;
    int32_t has_game_data;
    uint8_t pad1[4];
    int64_t score_value;
    uint64_t record_date;
} RankData;
typedef struct {
    OrbisNpOnlineId online_id;
    uint8_t reserved[68];
    int32_t pc_id;
    uint32_t serial_rank, rank, highest_rank;
    int32_t has_game_data;
    uint8_t pad1[4];
    int64_t score_value;
    uint64_t record_date;
    uint64_t account_id;
    uint8_t pad2[8];
} RankDataA;
typedef struct {
    int32_t has_data;
    uint8_t pad[4];
    RankData d;
} PlayerRankData;
typedef struct {
    int32_t has_data;
    uint8_t pad[4];
    RankDataA d;
} PlayerRankDataA;
typedef struct {
    uint64_t info_size;
    uint8_t data[192];
} GameInfo;
typedef struct {
    uint32_t rank_limit, update_mode, sort_mode, upload_num_limit;
    uint64_t upload_size_limit;
} BoardInfo;
_Static_assert(sizeof(RankData) == NP_SCORE_RANK_DATA_SIZE && sizeof(RankDataA) == NP_SCORE_RANK_DATA_A_SIZE &&
                   sizeof(PlayerRankData) == NP_SCORE_PLAYER_RANK_DATA_SIZE &&
                   sizeof(PlayerRankDataA) == NP_SCORE_PLAYER_RANK_DATA_A_SIZE &&
                   sizeof(GameInfo) == NP_SCORE_GAME_INFO_SIZE && sizeof(BoardInfo) == NP_SCORE_BOARD_INFO_SIZE,
               "layouts");

// An npbind.dat with entries (NULL: an empty communication id record); returns its size.
static size_t make_npbind(uint8_t *out, const char *const *ids, int n) {
    memset(out, 0, 0x80);
    const uint8_t magic[4] = {0xD2, 0x94, 0xA0, 0x18};
    memcpy(out, magic, 4);
    out[31] = (uint8_t)n;
    size_t at = 0x80;
    static const uint16_t sizes[4] = {12, 12, 176, 16};
    for (int i = 0; i < n; i++) {
        for (int e = 0; e < 4; e++) {
            const uint16_t len = (e == 0 && !ids[i]) ? 0 : sizes[e];
            out[at] = 0;
            out[at + 1] = (uint8_t)(0x10 + e);
            out[at + 2] = (uint8_t)(len >> 8);
            out[at + 3] = (uint8_t)len;
            memset(out + at + 4, 0, len);
            if (e == 0 && ids[i]) memcpy(out + at + 4, ids[i], 12);
            at += 4 + len;
        }
        memset(out + at, 0, 0x98);
        at += 0x98;
    }
    return at;
}

static NpScoreRankOut rank_outs(void *rank, uint64_t entry, void *comment, void *info, uint64_t n, uint64_t *sort_date,
                                uint32_t *total) {
    NpScoreRankOut o = {rank,     n * entry, comment, comment ? n * NP_SCORE_COMMENT_SIZE : 0, info,
                        info ? n * NP_SCORE_GAME_INFO_SIZE : 0, n, sort_date, total};
    return o;
}

// Signed in as Player1 (account 0x1234) to a local shadNet server, for a title whose npbind.dat
// lists NPWR12345_00.
static int case_online(void) {
    // npbind.dat parsing: the ids in order, empty ones skipped, truncated files rejected.
    static uint8_t bind[4096];
    const char *two[] = {"NPWR11111_00", NULL, "NPWR22222_00"};
    const size_t two_len = make_npbind(bind, two, 3);
    char ids[4][PS4_NPBIND_COMM_ID_LEN];
    CHECK(ps4_npbind_comm_ids(bind, two_len, ids, 4) == 2);
    CHECK(memcmp(ids[0], "NPWR11111_00", 12) == 0 && memcmp(ids[1], "NPWR22222_00", 12) == 0);
    CHECK(ps4_npbind_comm_ids(bind, two_len, ids, 1) == 2);
    CHECK(ps4_npbind_comm_ids(bind, 0x80 + 100, ids, 4) == -1);
    bind[0] = 0;
    CHECK(ps4_npbind_comm_ids(bind, two_len, ids, 4) == -1);

    FakeShadNet server = {.account_id = 0x1234};
    CHECK(fake_shadnet_start(&server) == 0);
    static char bind_path[] = "/tmp/ps4recomp_npbind_XXXXXX"; // g_npbind_path refers to it
    int fd = mkstemp(bind_path);
    CHECK(fd >= 0);
    const char *one[] = {"NPWR12345_00"};
    const size_t one_len = make_npbind(bind, one, 1);
    CHECK(write(fd, bind, one_len) == (ssize_t)one_len);
    close(fd);
    g_npbind_path = bind_path;
    char path[] = "/tmp/ps4recomp_np_score_XXXXXX";
    fd = mkstemp(path);
    CHECK(fd >= 0);
    char json[256];
    const int n = snprintf(json, sizeof(json),
                           "{\"enabled\": true, \"server\": \"127.0.0.1:%u\", \"npid\": \"Player1\", "
                           "\"password\": \"pw\"}",
                           (unsigned)server.port);
    CHECK(write(fd, json, (size_t)n) == n);
    close(fd);
    setenv("PS4_SHADNET_CONFIG", path, 1);

    // Title contexts: from the signed-in NP id (signing in), or by user id when signed in.
    OrbisNpId self;
    memset(&self, 0, sizeof(self));
    memcpy(self.handle.data, "Player1", 7);
    const int32_t ctx = ps4_np_score_create_np_title_ctx(5, &self);
    CHECK(ctx > 0 && server.sessions == 1);
    const int32_t user = ORBIS_USER_SERVICE_INITIAL_USER_ID;
    CHECK(ps4_np_score_create_np_title_ctx_a(0, user + 1) == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_create_np_title_ctx_a(NP_SCORE_INVALID_SERVICE_LABEL, user) == NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    const int32_t ctx_a = ps4_np_score_create_np_title_ctx_a(0, user);
    CHECK(ctx_a > 0);
    // The player character id set on the title context is inherited by its requests.
    CHECK(ps4_np_score_set_player_character_id(ctx, 3) == 0);
    const int32_t req = ps4_np_score_create_request(ctx);

    // Board info; the request carries the communication id (service label 5 falls back to the
    // only entry).
    BoardInfo bi;
    memset(&bi, 0xEE, sizeof(bi));
    CHECK(ps4_np_score_get_board_info(req, 7, &bi, NULL, g_sdk, false) == 0);
    CHECK(bi.rank_limit == 100 && bi.update_mode == 1 && bi.sort_mode == 0 && bi.upload_num_limit == 10 &&
          bi.upload_size_limit == 1024);
    CHECK(strcmp(server.score_comm_id, "NPWR12345_00") == 0);
    CHECK(ps4_np_score_get_board_info(req, 99, &bi, NULL, g_sdk, false) ==
          NP_COMMUNITY_SERVER_ERROR_RANKING_BOARD_MASTER_NOT_FOUND);

    // An empty friends ranking: 0 entries, lastSortDate and totalRecord untouched.
    RankData plain[2];
    uint64_t sort_date = 0xEE;
    uint32_t total = 0xEE;
    NpScoreRankOut out = rank_outs(plain, sizeof(RankData), NULL, NULL, 2, &sort_date, &total);
    CHECK(ps4_np_score_get_friends_ranking(req, 7, 1, &out, false, NULL, NULL, false) == 0);
    CHECK(sort_date == 0xEE && total == 0xEE);
    // An empty range is GAME_RANKING_NOT_FOUND.
    CHECK(ps4_np_score_get_ranking_by_range(req, 7, 1, &out, false, NULL, false) ==
          NP_COMMUNITY_SERVER_ERROR_GAME_RANKING_NOT_FOUND);

    // RecordScore with a comment and game info: the temporary rank; a lower score is not best.
    char comment[NP_SCORE_COMMENT_SIZE] = "best run";
    GameInfo info;
    memset(&info, 0, sizeof(info));
    info.info_size = 3;
    memcpy(info.data, "abc", 3);
    uint32_t tmp_rank = 0;
    CHECK(ps4_np_score_record_score(req, 7, 500, comment, &info, &tmp_rank, NULL, NULL, false) == 0);
    CHECK(tmp_rank == 1 && server.best_score == 500 && server.score_pc_id == 3);
    CHECK(strcmp(server.score_comment, "best run") == 0 && server.score_info_len == 3);
    CHECK(ps4_np_score_record_score(req, 7, 400, NULL, NULL, NULL, NULL, NULL, false) ==
          NP_COMMUNITY_SERVER_ERROR_NOT_BEST_SCORE);

    // Range with comments and game info.
    char comments[2][NP_SCORE_COMMENT_SIZE];
    GameInfo infos[2];
    out = rank_outs(plain, sizeof(RankData), comments, infos, 2, &sort_date, &total);
    CHECK(ps4_np_score_get_ranking_by_range(req, 7, 1, &out, false, NULL, false) == 1);
    CHECK(strcmp(plain[0].np_id.handle.data, "Player1") == 0 && plain[0].pc_id == 3 && plain[0].serial_rank == 1);
    CHECK(plain[0].rank == 1 && plain[0].highest_rank == 1 && plain[0].score_value == 500 &&
          plain[0].record_date == 1234 && plain[0].has_game_data == 0);
    CHECK(plain[1].np_id.handle.data[0] == 0 && sort_date == 5678 && total == 1);
    CHECK(strcmp(comments[0], "best run") == 0 && infos[0].info_size == 3 && memcmp(infos[0].data, "abc", 3) == 0);

    // The A variant, asynchronously: the account id too.
    RankDataA a_ranks[1];
    out = rank_outs(a_ranks, sizeof(RankDataA), NULL, NULL, 1, NULL, NULL);
    const int32_t async_req = ps4_np_score_create_request(ctx_a);
    CHECK(ps4_np_score_get_ranking_by_range(async_req, 7, 1, &out, true, NULL, true) == 0);
    int32_t result = -1;
    CHECK(ps4_np_score_wait_async(async_req, &result) == 0 && result == 1);
    CHECK(strcmp(a_ranks[0].online_id.data, "Player1") == 0 && a_ranks[0].account_id == 0x1234);

    // By NP id: one slot per id, hasData where the board has a score.
    OrbisNpId who[2];
    memset(who, 0, sizeof(who));
    memcpy(who[0].handle.data, "Nobody", 6);
    memcpy(who[1].handle.data, "Player1", 7);
    PlayerRankData players[2];
    out = rank_outs(players, sizeof(PlayerRankData), NULL, NULL, 2, &sort_date, &total);
    CHECK(ps4_np_score_get_ranking_by_np_id(req, 7, who, sizeof(who), &out, NULL, false) == 1);
    CHECK(players[0].has_data == 0 && players[1].has_data == 1 && players[1].d.score_value == 500);
    // By account id and pc id.
    const uint64_t acc_pc[2] = {0x1234, 3};
    PlayerRankDataA players_a[1];
    out = rank_outs(players_a, sizeof(PlayerRankDataA), NULL, NULL, 1, NULL, NULL);
    CHECK(ps4_np_score_get_ranking_by_account_id_pc_id(req, 7, acc_pc, sizeof(acc_pc), &out, NULL, false) == 1);
    CHECK(players_a[0].has_data == 1 && players_a[0].d.account_id == 0x1234);
    // Friends with self.
    out = rank_outs(plain, sizeof(RankData), NULL, NULL, 2, NULL, NULL);
    CHECK(ps4_np_score_get_friends_ranking(req, 7, 1, &out, false, NULL, NULL, false) == 1);

    // RecordGameData in two chunks: the first is buffered, the second sends the blob.
    CHECK(ps4_np_score_record_game_data(req, 7, 500, 10, 6, "012345", NULL, false) == 0);
    CHECK(server.game_data_len == 0);
    CHECK(ps4_np_score_record_game_data(req, 7, 500, 10, 4, "6789", NULL, false) == 0);
    CHECK(server.game_data_len == 10 && memcmp(server.game_data, "0123456789", 10) == 0);

    // GetGameData in 6-byte pieces: the server's blob, then the rest from the request's cache.
    char data[8];
    uint64_t total_size = 0;
    memset(data, 0, sizeof(data));
    CHECK(ps4_np_score_get_game_data(req, 7, &self, &total_size, 6, data, NULL, false) == 6);
    CHECK(total_size == 10 && memcmp(data, "012345", 6) == 0);
    const int sessions = server.sessions;
    memset(data, 0, sizeof(data));
    CHECK(ps4_np_score_get_game_data(req, 7, &self, &total_size, 6, data, NULL, false) == 4);
    CHECK(memcmp(data, "6789", 4) == 0 && server.sessions == sessions);
    // By account id, all at once; another player's has none.
    char whole[16];
    CHECK(ps4_np_score_get_game_data_by_account_id(req, 7, 0x1234, &total_size, sizeof(whole), whole, NULL, false) ==
          10);
    CHECK(memcmp(whole, "0123456789", 10) == 0);
    CHECK(ps4_np_score_get_game_data(req, 7, &who[0], &total_size, 6, data, NULL, false) ==
          NP_COMMUNITY_SERVER_ERROR_RANKING_GAME_DATA_MASTER_NOT_FOUND);

    // A title context of a user without a session is signed out.
    OrbisNpId stranger;
    memset(&stranger, 0, sizeof(stranger));
    memcpy(stranger.handle.data, "Stranger", 8);
    const int32_t other = ps4_np_score_create_request(ps4_np_score_create_np_title_ctx(0, &stranger));
    CHECK(ps4_np_score_get_board_info(other, 7, &bi, NULL, g_sdk, false) == NP_SCORE_ERROR_SIGNED_OUT);
    CHECK(ps4_np_score_poll_async(other, &result) == 0 && result == NP_SCORE_ERROR_SIGNED_OUT);

    // An operation in flight when the session ends completes signed out.
    server.hold_replies = true;
    const int32_t pending = ps4_np_score_create_request(ctx);
    CHECK(ps4_np_score_get_board_info(pending, 7, &bi, NULL, g_sdk, true) == 0);
    CHECK(ps4_np_score_poll_async(pending, &result) == NP_SCORE_POLL_ASYNC_RUNNING);
    ps4_np_handler_shutdown();
    server.hold_replies = false;
    CHECK(ps4_np_score_wait_async(pending, &result) == 0 && result == NP_SCORE_ERROR_SIGNED_OUT);

    fake_shadnet_stop(&server);
    unlink(path);
    unlink(bind_path);
    g_npbind_path = NULL;
    CHECK(server.failures == 0);
    printf("PASS online\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "online") == 0) return case_online();
    // The other cases run with shadNet off.
    setenv("PS4_SHADNET_CONFIG", "/nonexistent/shadnet.json", 1);
    if (strcmp(argv[1], "contexts") == 0) return case_contexts();
    if (strcmp(argv[1], "operations") == 0) return case_operations();
    if (strcmp(argv[1], "game-data") == 0) return case_game_data();
    if (strcmp(argv[1], "comments") == 0) return case_comments();
    if (strcmp(argv[1], "wait") == 0) return case_wait();
    if (strcmp(argv[1], "shim-args") == 0) return case_shims();
    return 2;
}
