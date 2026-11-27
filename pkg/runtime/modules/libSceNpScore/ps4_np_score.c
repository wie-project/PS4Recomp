// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpScore (and libSceNpScoreCompat), following shadPS4 src/core/libraries/np/np_score.
//
// Title contexts and requests are kept as in shadPS4: ids from 1 that are never reused, at most
// NP_SCORE_MAX_CTX_NUM title contexts and as many live requests, a request inherits the user and
// player character id of its title context, and every operation runs the argument checks in
// shadPS4's order (output arrays are cleared before the request is looked up) and then starts a
// new operation on the request (ScoreRequestCtx::BeginOp: an aborted request stays aborted, any
// other result is dropped). The first result an operation gets is final (SetResult); PollAsync
// and WaitAsync report it, and the synchronous variants wait for it.
//
// Ranking, board, score and game data operations go to the shadNet server through NpHandler,
// which needs a shadNet session for the request's user. This library is
// not connected to the NP handler's shadNet sessions (libSceNp/ps4_np_handler.c) yet, so those operations complete with
// ORBIS_NP_ERROR_SIGNED_OUT as NpHandler returns it for a user without a session, and
// sceNpScoreCreateNpTitleCtxA fails with it as well (NpHandler::IsPsnSignedIn). For the same
// reason no user has an NP id: a title context created from one belongs to user -1
// (GetUserIdByOnlineId). The game data receive cache of shadPS4's ScoreRequestCtx is filled only
// by server replies, so it is always empty here and GetGameData goes straight to the server.
//
// RecordGameData buffers the chunks of a transfer on the request until totalSize bytes have
// arrived (each partial chunk completes with ORBIS_OK) and only then sends the whole blob.
//
// CensorComment and SanitizeComment are local in shadPS4: the comment is accepted, or copied
// unchanged, and the operation completes with ORBIS_OK.
//
// Every other function of the library is a stub in shadPS4 and is a stub here.

#include "ps4_np_score.h"
#include "ps4_hle_stub.h"
#include "ps4_kernel_sys.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t id; // 0 = free slot
    uint32_t service_label;
    int32_t user_id;
    int32_t pc_id;
} NpScoreTitleCtx;

// A request. shadPS4 holds requests by shared_ptr, so a thread blocked in WaitAsync keeps its
// request alive after it was deleted. Here the slot stays reserved (deleted, waiters > 0) until
// the last waiter leaves; a deleted request cannot be found by id.
typedef struct {
    int32_t id; // 0 = free slot
    int32_t title_ctx_id;
    int32_t user_id;
    int32_t pc_id;
    bool deleted;
    bool has_result;
    int32_t result;
    uint32_t waiters;
    // RecordGameData transfer in progress: send_len of send_total bytes buffered.
    uint8_t *send_buf;
    uint64_t send_len;
    uint64_t send_total;
} NpScoreRequest;

// The live-request limit is NP_SCORE_MAX_CTX_NUM; the extra slots hold deleted requests that
// still have waiters.
#define NP_SCORE_REQUEST_SLOTS 64

static struct {
    pthread_mutex_t mutex;
    pthread_cond_t done; // broadcast whenever a request gets its result
    NpScoreTitleCtx ctx[NP_SCORE_MAX_CTX_NUM];
    NpScoreRequest req[NP_SCORE_REQUEST_SLOTS];
    int32_t live_requests;
    int32_t next_ctx_id;
    int32_t next_req_id;
} g_score = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .done = PTHREAD_COND_INITIALIZER,
    .next_ctx_id = 1,
    .next_req_id = 1,
};

// Lock held.
static NpScoreTitleCtx *ctx_find(int32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < NP_SCORE_MAX_CTX_NUM; i++) {
        if (g_score.ctx[i].id == id) return &g_score.ctx[i];
    }
    return NULL;
}

// Lock held. Deleted requests are not found.
static NpScoreRequest *req_find(int32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < NP_SCORE_REQUEST_SLOTS; i++) {
        if (g_score.req[i].id == id && !g_score.req[i].deleted) return &g_score.req[i];
    }
    return NULL;
}

// Lock held. ScoreRequestCtx::SetResult: the first result is final.
static void req_set_result(NpScoreRequest *q, int32_t result) {
    if (q->has_result) return;
    q->has_result = true;
    q->result = result;
    pthread_cond_broadcast(&g_score.done);
}

// Lock held. ScoreRequestCtx::BeginOp.
static bool req_begin_op(NpScoreRequest *q) {
    if (q->has_result && q->result == NP_COMMUNITY_ERROR_ABORTED) return false;
    q->has_result = false;
    return true;
}

static void req_clear(NpScoreRequest *q) {
    free(q->send_buf);
    memset(q, 0, sizeof(*q));
}

// Lock held. Removes a request from the id space; its slot is reused once no thread waits on it.
static void req_erase(NpScoreRequest *q) {
    g_score.live_requests--;
    if (q->waiters == 0) {
        req_clear(q);
    } else {
        q->deleted = true;
    }
}

// Lock held. Blocks until q has a result (ScoreRequestCtx::Wait); q stays valid meanwhile even
// if another thread deletes it.
static int32_t req_wait(NpScoreRequest *q) {
    q->waiters++;
    while (!q->has_result) {
        pthread_cond_wait(&g_score.done, &g_score.mutex);
    }
    const int32_t result = q->result;
    q->waiters--;
    if (q->deleted && q->waiters == 0) {
        req_clear(q);
    }
    return result;
}

// Lock held. The lookup and BeginOp every operation starts with: *out is the request on success.
static int32_t req_start(int32_t req_id, NpScoreRequest **out) {
    NpScoreRequest *q = req_find(req_id);
    if (!q) return NP_COMMUNITY_ERROR_INVALID_ID;
    if (!req_begin_op(q)) return NP_COMMUNITY_ERROR_ABORTED;
    *out = q;
    return 0;
}

// Lock held. Sends the operation to the shadNet server through NpHandler (RecordScore,
// GetRankingBy*, GetFriendsRanking*, GetBoardInfo, GetGameData*, RecordGameData). The request's
// user has no shadNet session, so NpHandler refuses it with ORBIS_NP_ERROR_SIGNED_OUT; the
// request completes with that error and the entry point returns it, as in shadPS4.
static int32_t req_dispatch(NpScoreRequest *q) {
    static bool logged;
    if (!logged) {
        logged = true;
        fprintf(stderr, "[libSceNpScore] server request for user %d: not signed in to shadNet -> SIGNED_OUT\n",
                q->user_id);
    }
    req_set_result(q, NP_SCORE_ERROR_SIGNED_OUT);
    return NP_SCORE_ERROR_SIGNED_OUT;
}

// The end of every operation that reached the server: dispatch, then for the synchronous
// variants wait for the result (a failed dispatch returns its error at once).
static int32_t req_run(NpScoreRequest *q, bool is_async) {
    const int32_t r = req_dispatch(q);
    if (r != 0) return r;
    return is_async ? 0 : req_wait(q);
}

//=============================================================================
// Title contexts and requests
//=============================================================================

static int32_t create_title_ctx(uint32_t service_label, int32_t user_id) {
    pthread_mutex_lock(&g_score.mutex);
    NpScoreTitleCtx *slot = NULL;
    for (int i = 0; i < NP_SCORE_MAX_CTX_NUM && !slot; i++) {
        if (g_score.ctx[i].id == 0) slot = &g_score.ctx[i];
    }
    int32_t r = NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS;
    if (slot) {
        slot->id = g_score.next_ctx_id++;
        slot->service_label = service_label;
        slot->user_id = user_id;
        slot->pc_id = 0;
        r = slot->id;
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_create_np_title_ctx(uint32_t service_label, const void *self_np_id) {
    if (service_label == NP_SCORE_INVALID_SERVICE_LABEL) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!self_np_id) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    // NpHandler::GetUserIdByOnlineId(selfNpId->handle): no user has an NP id while signed out.
    return create_title_ctx(service_label, -1);
}

int32_t ps4_np_score_create_np_title_ctx_a(uint32_t service_label, int32_t self_id) {
    // NpHandler::IsPsnSignedIn(selfId) is false for every user.
    (void)service_label;
    (void)self_id;
    return NP_SCORE_ERROR_SIGNED_OUT;
}

int32_t ps4_np_score_delete_np_title_ctx(int32_t title_ctx_id) {
    pthread_mutex_lock(&g_score.mutex);
    NpScoreTitleCtx *c = ctx_find(title_ctx_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (c) {
        for (int i = 0; i < NP_SCORE_REQUEST_SLOTS; i++) {
            NpScoreRequest *q = &g_score.req[i];
            if (q->id == 0 || q->deleted || q->title_ctx_id != title_ctx_id) continue;
            req_set_result(q, NP_COMMUNITY_ERROR_ABORTED);
            req_erase(q);
        }
        memset(c, 0, sizeof(*c));
        r = 0;
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_create_request(int32_t title_ctx_id) {
    pthread_mutex_lock(&g_score.mutex);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    const NpScoreTitleCtx *c = ctx_find(title_ctx_id);
    if (c) {
        NpScoreRequest *slot = NULL;
        if (g_score.live_requests < NP_SCORE_MAX_CTX_NUM) {
            for (int i = 0; i < NP_SCORE_REQUEST_SLOTS && !slot; i++) {
                if (g_score.req[i].id == 0) slot = &g_score.req[i];
            }
        }
        r = NP_COMMUNITY_ERROR_TOO_MANY_OBJECTS;
        if (slot) {
            slot->id = g_score.next_req_id++;
            slot->title_ctx_id = title_ctx_id;
            slot->user_id = c->user_id;
            slot->pc_id = c->pc_id;
            g_score.live_requests++;
            r = slot->id;
        }
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_delete_request(int32_t req_id) {
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = req_find(req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q) {
        req_set_result(q, NP_COMMUNITY_ERROR_ABORTED);
        req_erase(q);
        r = 0;
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_abort_request(int32_t req_id) {
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = req_find(req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q) {
        req_set_result(q, NP_COMMUNITY_ERROR_ABORTED);
        r = 0;
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_poll_async(int32_t req_id, int32_t *result) {
    pthread_mutex_lock(&g_score.mutex);
    const NpScoreRequest *q = req_find(req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q) {
        r = NP_SCORE_POLL_ASYNC_RUNNING;
        if (q->has_result) {
            if (result) *result = q->result;
            r = 0;
        }
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_wait_async(int32_t req_id, int32_t *result) {
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = req_find(req_id);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    if (q) {
        const int32_t done = req_wait(q);
        if (result) *result = done;
        r = 0;
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_set_player_character_id(int32_t id, int32_t pc_id) {
    if (pc_id < 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_score.mutex);
    int32_t r = NP_COMMUNITY_ERROR_INVALID_ID;
    // A title context id first: the default later requests inherit. Then a request id: only
    // that request's operations use it.
    NpScoreTitleCtx *c = ctx_find(id);
    if (c) {
        c->pc_id = pc_id;
        r = 0;
    } else {
        NpScoreRequest *q = req_find(id);
        if (q) {
            q->pc_id = pc_id;
            r = 0;
        }
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

//=============================================================================
// Operations
//=============================================================================

// Lock held. Starts an operation that goes to the server and runs it.
static int32_t start_and_run(int32_t req_id, bool is_async) {
    NpScoreRequest *q = NULL;
    const int32_t r = req_start(req_id, &q);
    return r != 0 ? r : req_run(q, is_async);
}

static int32_t locked_start_and_run(int32_t req_id, bool is_async) {
    pthread_mutex_lock(&g_score.mutex);
    const int32_t r = start_and_run(req_id, is_async);
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_record_score(int32_t req_id, uint32_t board_id, int64_t score, const void *comment,
                                  const void *game_info, uint32_t *tmp_rank, const void *compare_date,
                                  const void *option, bool is_async) {
    // The board, score, comment, compare date and tmpRank (written by the server's reply) are
    // only read by the server request.
    (void)board_id;
    (void)score;
    (void)comment;
    (void)tmp_rank;
    (void)compare_date;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (game_info) {
        uint64_t info_size;
        memcpy(&info_size, game_info, sizeof(info_size)); // OrbisNpScoreGameInfo::infoSize
        if (info_size > NP_SCORE_GAMEINFO_MAXSIZE) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    }
    return locked_start_and_run(req_id, is_async);
}

// The comment and game info arrays of a ranking query: their sizes must match arrayNum entries,
// and they are cleared.
static int32_t clear_comment_info(const NpScoreRankOut *out) {
    if (out->comment) {
        if (out->comment_size != out->array_num * NP_SCORE_COMMENT_SIZE) return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
        memset(out->comment, 0, out->comment_size);
    }
    if (out->info) {
        if (out->info_size != out->array_num * NP_SCORE_GAME_INFO_SIZE) return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
        memset(out->info, 0, out->info_size);
    }
    return 0;
}

int32_t ps4_np_score_get_ranking_by_range(int32_t req_id, uint32_t board_id, uint32_t start_serial_rank,
                                          const NpScoreRankOut *out, bool a_variant, const void *option,
                                          bool is_async) {
    (void)board_id;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (out->array_num > NP_SCORE_MAX_RANGE_NUM_PER_REQUEST) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (start_serial_rank == 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!out->rank) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (out->array_num == 0) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    const uint64_t entry = a_variant ? NP_SCORE_RANK_DATA_A_SIZE : NP_SCORE_RANK_DATA_SIZE;
    if (out->rank_size != out->array_num * entry) return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    memset(out->rank, 0, out->rank_size);
    const int32_t r = clear_comment_info(out);
    return r != 0 ? r : locked_start_and_run(req_id, is_async);
}

int32_t ps4_np_score_get_ranking_by_np_id(int32_t req_id, uint32_t board_id, const void *np_id_array,
                                          uint64_t np_id_array_size, const NpScoreRankOut *out,
                                          const void *option, bool is_async) {
    (void)board_id;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (out->array_num > NP_SCORE_MAX_NPID_NUM_PER_REQUEST) return NP_COMMUNITY_ERROR_TOO_MANY_NPID;
    if (!np_id_array || !out->rank) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (np_id_array_size != out->array_num * NP_SCORE_NP_ID_SIZE ||
        out->rank_size != out->array_num * NP_SCORE_PLAYER_RANK_DATA_SIZE) {
        return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    }
    memset(out->rank, 0, out->rank_size);
    const int32_t r = clear_comment_info(out);
    return r != 0 ? r : locked_start_and_run(req_id, is_async);
}

int32_t ps4_np_score_get_ranking_by_np_id_pc_id(int32_t req_id, uint32_t board_id, const void *id_array,
                                                uint64_t id_array_size, const NpScoreRankOut *out,
                                                const void *option, bool is_async) {
    (void)board_id;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!id_array || out->array_num == 0 || !out->rank) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (out->array_num > NP_SCORE_MAX_NPID_NUM_PER_REQUEST) return NP_COMMUNITY_ERROR_TOO_MANY_NPID;
    if (id_array_size != out->array_num * NP_SCORE_NP_ID_PC_ID_SIZE ||
        out->rank_size != out->array_num * NP_SCORE_PLAYER_RANK_DATA_SIZE) {
        return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    }
    memset(out->rank, 0, out->rank_size);
    const int32_t r = clear_comment_info(out);
    return r != 0 ? r : locked_start_and_run(req_id, is_async);
}

// GetRankingByAccountIdImpl and GetRankingByAccountIdPcIdImpl; id_entry is the size of one id.
static int32_t get_ranking_by_account(int32_t req_id, const void *id_array, uint64_t id_array_size,
                                      uint64_t id_entry, const NpScoreRankOut *out, const void *option,
                                      bool is_async) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (out->array_num > NP_SCORE_MAX_ID_NUM_PER_REQUEST) return NP_COMMUNITY_ERROR_TOO_MANY_NPID;
    if (out->array_num == 0 || !out->rank || !id_array) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (id_array_size != out->array_num * id_entry) return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    if (out->rank_size != out->array_num * NP_SCORE_PLAYER_RANK_DATA_A_SIZE) return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    memset(out->rank, 0, out->rank_size);
    const int32_t r = clear_comment_info(out);
    return r != 0 ? r : locked_start_and_run(req_id, is_async);
}

int32_t ps4_np_score_get_ranking_by_account_id(int32_t req_id, uint32_t board_id, const void *account_id_array,
                                               uint64_t account_id_array_size, const NpScoreRankOut *out,
                                               const void *option, bool is_async) {
    (void)board_id;
    return get_ranking_by_account(req_id, account_id_array, account_id_array_size, NP_SCORE_ACCOUNT_ID_SIZE, out,
                                  option, is_async);
}

int32_t ps4_np_score_get_ranking_by_account_id_pc_id(int32_t req_id, uint32_t board_id, const void *id_array,
                                                     uint64_t id_array_size, const NpScoreRankOut *out,
                                                     const void *option, bool is_async) {
    (void)board_id;
    return get_ranking_by_account(req_id, id_array, id_array_size, NP_SCORE_ACCOUNT_ID_PC_ID_SIZE, out, option,
                                  is_async);
}

int32_t ps4_np_score_get_friends_ranking(int32_t req_id, uint32_t board_id, int32_t include_self,
                                         const NpScoreRankOut *out, bool a_variant,
                                         const NpScoreFriendRankingOpt *option,
                                         const uint32_t *opt_start_serial_rank, bool is_async) {
    (void)board_id;
    if (!out->rank) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    const uint64_t entry = a_variant ? NP_SCORE_RANK_DATA_A_SIZE : NP_SCORE_RANK_DATA_SIZE;
    if (out->rank_size != out->array_num * entry) return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    uint32_t start_serial_rank = 1;
    if (include_self == 0) {
        if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    } else if (option) {
        if (option->size != NP_SCORE_FRIEND_OPT_SIZE) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
        if (opt_start_serial_rank) start_serial_rank = *opt_start_serial_rank;
    }
    if (out->array_num > NP_SCORE_MAX_RANGE_NUM_PER_REQUEST) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (start_serial_rank == 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (out->array_num == 0) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    memset(out->rank, 0, out->rank_size);
    const int32_t r = clear_comment_info(out);
    return r != 0 ? r : locked_start_and_run(req_id, is_async);
}

int32_t ps4_np_score_get_board_info(int32_t req_id, uint32_t board_id, void *board_info, const void *option,
                                    int32_t sdk_version, bool is_async) {
    (void)board_id;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (sdk_version >= NP_SCORE_SDK_FW_250 && !board_info) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = NULL;
    int32_t r = req_start(req_id, &q);
    if (r == 0) {
        if (!board_info) {
            // An older SDK may pass no boardInfo: the operation succeeds without a request.
            req_set_result(q, 0);
        } else {
            memset(board_info, 0, NP_SCORE_BOARD_INFO_SIZE);
            r = req_run(q, is_async);
        }
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_get_game_data(int32_t req_id, uint32_t board_id, const void *np_id, uint64_t *total_size,
                                   uint64_t recv_size, void *data, const void *option, bool is_async) {
    // The server's reply writes data and *totalSize.
    (void)board_id;
    (void)total_size;
    (void)recv_size;
    (void)data;
    if (!np_id) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = NULL;
    int32_t r = req_start(req_id, &q);
    if (r == 0) {
        // OrbisNpId starts with its online id handle.
        if (((const char *)np_id)[0] == '\0') {
            req_set_result(q, NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
            r = NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
        } else {
            r = req_run(q, is_async);
        }
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_get_game_data_by_account_id(int32_t req_id, uint32_t board_id, uint64_t account_id,
                                                 uint64_t *total_size, uint64_t recv_size, void *data,
                                                 const void *option, bool is_async) {
    (void)board_id;
    (void)total_size;
    (void)recv_size;
    (void)data;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (account_id == 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    return locked_start_and_run(req_id, is_async);
}

int32_t ps4_np_score_record_game_data(int32_t req_id, uint32_t board_id, int64_t score, uint64_t total_size,
                                      uint64_t send_size, const void *data, const void *option, bool is_async) {
    (void)board_id;
    (void)score;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (total_size == 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!data) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (send_size == 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = NULL;
    int32_t r = req_start(req_id, &q);
    if (r == 0) {
        // The first chunk of a transfer fixes its total size.
        if (q->send_len == 0) q->send_total = total_size;
        const uint64_t remaining = q->send_total - q->send_len;
        const uint64_t take = send_size < remaining ? send_size : remaining;
        uint8_t *grown = take ? realloc(q->send_buf, (size_t)(q->send_len + take)) : q->send_buf;
        if (take && !grown) {
            // shadPS4's std::vector would throw; the transfer cannot be buffered.
            r = NP_COMMUNITY_ERROR_OUT_OF_MEMORY;
            req_set_result(q, r);
        } else {
            q->send_buf = grown;
            memcpy(q->send_buf + q->send_len, data, (size_t)take);
            q->send_len += take;
            if (q->send_len < q->send_total) {
                req_set_result(q, 0);
            } else {
                // The whole blob goes to the server; the request's buffer starts over.
                free(q->send_buf);
                q->send_buf = NULL;
                q->send_len = 0;
                q->send_total = 0;
                r = req_run(q, is_async);
            }
        }
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

// The checks and completion shared by CensorComment and SanitizeComment. sanitized: NULL for
// CensorComment, else it receives a copy of the comment.
static int32_t local_comment_op(int32_t req_id, const char *comment, size_t comment_room, char *sanitized,
                                size_t max_len, bool is_async) {
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = NULL;
    int32_t r = req_start(req_id, &q);
    if (r == 0) {
        const size_t probe = max_len + 1 < comment_room ? max_len + 1 : comment_room;
        const size_t len = strnlen(comment, probe);
        if (len > max_len) {
            // As in shadPS4 the operation was started, but never completes.
            r = NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
        } else {
            if (sanitized) {
                memcpy(sanitized, comment, len);
                sanitized[len] = '\0';
            }
            req_set_result(q, 0);
            r = is_async ? 0 : req_wait(q);
        }
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_censor_comment(int32_t req_id, const char *comment, size_t comment_room, const void *option,
                                    bool is_async) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!comment) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    return local_comment_op(req_id, comment, comment_room, NULL, NP_SCORE_CENSOR_COMMENT_MAXLEN, is_async);
}

int32_t ps4_np_score_sanitize_comment(int32_t req_id, const char *comment, size_t comment_room, char *sanitized,
                                      const void *option, bool is_async) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!comment) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (!sanitized) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    return local_comment_op(req_id, comment, comment_room, sanitized, NP_SCORE_SANITIZE_COMMENT_MAXLEN, is_async);
}

//=============================================================================
// Shims
//=============================================================================

// A guest buffer of size bytes, or NULL for a guest NULL or a buffer outside guest memory.
static void *gptr(GuestContext *ctx, uint64_t addr, uint64_t size) {
    return (addr && addr < ctx->mem_size && size <= ctx->mem_size - addr) ? (void *)(ctx->mem_base + addr) : NULL;
}

static size_t groom(GuestContext *ctx, uint64_t addr) {
    return (addr && addr < ctx->mem_size) ? (size_t)(ctx->mem_size - addr) : 0;
}

// The n-th stack argument (0 = the 7th integer argument; [rsp] is the return address).
static uint64_t stack_arg(GuestContext *ctx, int n) {
    const uint64_t addr = ctx->rsp + 8 * (uint64_t)(n + 1);
    return addr + 8 <= ctx->mem_size ? *(const uint64_t *)(ctx->mem_base + addr) : 0;
}

#define RET(v)                                      \
    do {                                            \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v); \
        SHIM_RETURN();                              \
    } while (0)

// The output arrays of a ranking query whose rankArray / rankArraySize are arguments 3 and 4
// (0-based) and whose commentArray is argument 5, followed on the stack by commentArraySize,
// infoArray, infoArraySize and arrayNum. Its lastSortDate, totalRecord and option follow.
static NpScoreRankOut rank_out(GuestContext *ctx) {
    NpScoreRankOut out;
    out.rank_size = ctx->r8;
    out.rank = gptr(ctx, ctx->rcx, out.rank_size);
    out.comment_size = stack_arg(ctx, 0);
    out.comment = gptr(ctx, ctx->r9, out.comment_size);
    out.info_size = stack_arg(ctx, 2);
    out.info = gptr(ctx, stack_arg(ctx, 1), out.info_size);
    out.array_num = stack_arg(ctx, 3);
    return out;
}

// The output arrays of a ranking query by id list: idArray / idArraySize are arguments 2 and
// 3, rankArray / rankArraySize 4 and 5; commentArray, commentArraySize, infoArray,
// infoArraySize and arrayNum are on the stack, then lastSortDate, totalRecord and option.
static NpScoreRankOut id_rank_out(GuestContext *ctx) {
    NpScoreRankOut out;
    out.rank_size = ctx->r9;
    out.rank = gptr(ctx, ctx->r8, out.rank_size);
    out.comment_size = stack_arg(ctx, 1);
    out.comment = gptr(ctx, stack_arg(ctx, 0), out.comment_size);
    out.info_size = stack_arg(ctx, 3);
    out.info = gptr(ctx, stack_arg(ctx, 2), out.info_size);
    out.array_num = stack_arg(ctx, 4);
    return out;
}

// int sceNpScoreCreateNpTitleCtx(OrbisNpServiceLabel serviceLabel, const OrbisNpId *selfNpId)
void shim_sceNpScoreCreateNpTitleCtx(GuestContext *ctx) {
    RET(ps4_np_score_create_np_title_ctx((uint32_t)ctx->rdi, gptr(ctx, ctx->rsi, NP_SCORE_NP_ID_SIZE)));
}

// int sceNpScoreCreateNpTitleCtxA(OrbisNpServiceLabel serviceLabel, OrbisUserServiceUserId selfId)
void shim_sceNpScoreCreateNpTitleCtxA(GuestContext *ctx) {
    RET(ps4_np_score_create_np_title_ctx_a((uint32_t)ctx->rdi, (int32_t)ctx->rsi));
}

// int sceNpScoreDeleteNpTitleCtx(int titleCtxId)
void shim_sceNpScoreDeleteNpTitleCtx(GuestContext *ctx) {
    RET(ps4_np_score_delete_np_title_ctx((int32_t)ctx->rdi));
}

// int sceNpScoreCreateRequest(int titleCtxId)
void shim_sceNpScoreCreateRequest(GuestContext *ctx) {
    RET(ps4_np_score_create_request((int32_t)ctx->rdi));
}

// int sceNpScoreDeleteRequest(int reqId)
void shim_sceNpScoreDeleteRequest(GuestContext *ctx) {
    RET(ps4_np_score_delete_request((int32_t)ctx->rdi));
}

// int sceNpScoreAbortRequest(int reqId)
void shim_sceNpScoreAbortRequest(GuestContext *ctx) {
    RET(ps4_np_score_abort_request((int32_t)ctx->rdi));
}

// int sceNpScorePollAsync(int reqId, int *result)
void shim_sceNpScorePollAsync(GuestContext *ctx) {
    RET(ps4_np_score_poll_async((int32_t)ctx->rdi, gptr(ctx, ctx->rsi, sizeof(int32_t))));
}

// int sceNpScoreWaitAsync(int reqId, int *result)
void shim_sceNpScoreWaitAsync(GuestContext *ctx) {
    RET(ps4_np_score_wait_async((int32_t)ctx->rdi, gptr(ctx, ctx->rsi, sizeof(int32_t))));
}

// int sceNpScoreSetPlayerCharacterId(int ctxId, OrbisNpScorePcId pcId)
void shim_sceNpScoreSetPlayerCharacterId(GuestContext *ctx) {
    RET(ps4_np_score_set_player_character_id((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

// int sceNpScoreRecordScore(int reqId, OrbisNpScoreBoardId boardId, OrbisNpScoreValue score,
//                           const OrbisNpScoreComment *scoreComment,
//                           const OrbisNpScoreGameInfo *gameInfo, OrbisNpScoreRankNumber *tmpRank,
//                           const OrbisRtcTick *compareDate, void *option)
static void record_score(GuestContext *ctx, bool is_async) {
    RET(ps4_np_score_record_score((int32_t)ctx->rdi, (uint32_t)ctx->rsi, (int64_t)ctx->rdx,
                                  gptr(ctx, ctx->rcx, NP_SCORE_COMMENT_SIZE), gptr(ctx, ctx->r8, NP_SCORE_GAME_INFO_SIZE),
                                  gptr(ctx, ctx->r9, sizeof(uint32_t)), gptr(ctx, stack_arg(ctx, 0), sizeof(uint64_t)),
                                  gptr(ctx, stack_arg(ctx, 1), 1), is_async));
}

void shim_sceNpScoreRecordScore(GuestContext *ctx) {
    record_score(ctx, false);
}

void shim_sceNpScoreRecordScoreAsync(GuestContext *ctx) {
    record_score(ctx, true);
}

// int sceNpScoreGetRankingByRange[A][Async](int reqId, OrbisNpScoreBoardId boardId,
//     OrbisNpScoreRankNumber startSerialRank, OrbisNpScoreRankData[A] *rankArray,
//     u64 rankArraySize, OrbisNpScoreComment *commentArray, u64 commentArraySize,
//     OrbisNpScoreGameInfo *infoArray, u64 infoArraySize, u64 arrayNum,
//     OrbisRtcTick *lastSortDate, OrbisNpScoreRankNumber *totalRecord, void *option)
static void get_ranking_by_range(GuestContext *ctx, bool a_variant, bool is_async) {
    const NpScoreRankOut out = rank_out(ctx);
    RET(ps4_np_score_get_ranking_by_range((int32_t)ctx->rdi, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, &out, a_variant,
                                          gptr(ctx, stack_arg(ctx, 6), 1), is_async));
}

void shim_sceNpScoreGetRankingByRange(GuestContext *ctx) {
    get_ranking_by_range(ctx, false, false);
}

void shim_sceNpScoreGetRankingByRangeAsync(GuestContext *ctx) {
    get_ranking_by_range(ctx, false, true);
}

void shim_sceNpScoreGetRankingByRangeA(GuestContext *ctx) {
    get_ranking_by_range(ctx, true, false);
}

void shim_sceNpScoreGetRankingByRangeAAsync(GuestContext *ctx) {
    get_ranking_by_range(ctx, true, true);
}

// int sceNpScoreGetRankingByNpId[Async](int reqId, OrbisNpScoreBoardId boardId,
//     const OrbisNpId *npIdArray, u64 npIdArraySize, OrbisNpScorePlayerRankData *rankArray,
//     u64 rankArraySize, OrbisNpScoreComment *commentArray, u64 commentArraySize,
//     OrbisNpScoreGameInfo *infoArray, u64 infoArraySize, u64 arrayNum,
//     OrbisRtcTick *lastSortDate, OrbisNpScoreRankNumber *totalRecord, void *option)
static void get_ranking_by_np_id(GuestContext *ctx, bool is_async) {
    const NpScoreRankOut out = id_rank_out(ctx);
    RET(ps4_np_score_get_ranking_by_np_id((int32_t)ctx->rdi, (uint32_t)ctx->rsi, gptr(ctx, ctx->rdx, ctx->rcx),
                                          ctx->rcx, &out, gptr(ctx, stack_arg(ctx, 7), 1), is_async));
}

void shim_sceNpScoreGetRankingByNpId(GuestContext *ctx) {
    get_ranking_by_np_id(ctx, false);
}

void shim_sceNpScoreGetRankingByNpIdAsync(GuestContext *ctx) {
    get_ranking_by_np_id(ctx, true);
}

// sceNpScoreGetRankingByNpIdPcId[Async]: as GetRankingByNpId with an OrbisNpScoreNpIdPcId array.
static void get_ranking_by_np_id_pc_id(GuestContext *ctx, bool is_async) {
    const NpScoreRankOut out = id_rank_out(ctx);
    RET(ps4_np_score_get_ranking_by_np_id_pc_id((int32_t)ctx->rdi, (uint32_t)ctx->rsi, gptr(ctx, ctx->rdx, ctx->rcx),
                                                ctx->rcx, &out, gptr(ctx, stack_arg(ctx, 7), 1), is_async));
}

void shim_sceNpScoreGetRankingByNpIdPcId(GuestContext *ctx) {
    get_ranking_by_np_id_pc_id(ctx, false);
}

void shim_sceNpScoreGetRankingByNpIdPcIdAsync(GuestContext *ctx) {
    get_ranking_by_np_id_pc_id(ctx, true);
}

// sceNpScoreGetRankingByAccountId[Async]: as GetRankingByNpId with an OrbisNpAccountId array
// and OrbisNpScorePlayerRankDataA entries.
static void get_ranking_by_account_id(GuestContext *ctx, bool is_async) {
    const NpScoreRankOut out = id_rank_out(ctx);
    RET(ps4_np_score_get_ranking_by_account_id((int32_t)ctx->rdi, (uint32_t)ctx->rsi, gptr(ctx, ctx->rdx, ctx->rcx),
                                               ctx->rcx, &out, gptr(ctx, stack_arg(ctx, 7), 1), is_async));
}

void shim_sceNpScoreGetRankingByAccountId(GuestContext *ctx) {
    get_ranking_by_account_id(ctx, false);
}

void shim_sceNpScoreGetRankingByAccountIdAsync(GuestContext *ctx) {
    get_ranking_by_account_id(ctx, true);
}

// sceNpScoreGetRankingByAccountIdPcId[Async]: with an OrbisNpScoreAccountIdPcId array.
static void get_ranking_by_account_id_pc_id(GuestContext *ctx, bool is_async) {
    const NpScoreRankOut out = id_rank_out(ctx);
    RET(ps4_np_score_get_ranking_by_account_id_pc_id((int32_t)ctx->rdi, (uint32_t)ctx->rsi,
                                                     gptr(ctx, ctx->rdx, ctx->rcx), ctx->rcx, &out,
                                                     gptr(ctx, stack_arg(ctx, 7), 1), is_async));
}

void shim_sceNpScoreGetRankingByAccountIdPcId(GuestContext *ctx) {
    get_ranking_by_account_id_pc_id(ctx, false);
}

void shim_sceNpScoreGetRankingByAccountIdPcIdAsync(GuestContext *ctx) {
    get_ranking_by_account_id_pc_id(ctx, true);
}

// int sceNpScoreGetFriendsRanking[A][Async](int reqId, OrbisNpScoreBoardId boardId,
//     int includeSelf, OrbisNpScoreRankData[A] *rankArray, u64 rankArraySize,
//     OrbisNpScoreComment *commentArray, u64 commentArraySize, OrbisNpScoreGameInfo *infoArray,
//     u64 infoArraySize, u64 arrayNum, OrbisRtcTick *lastSortDate,
//     OrbisNpScoreRankNumber *totalRecord, OrbisNpScoreGetFriendRankingOptParam *option)
static void get_friends_ranking(GuestContext *ctx, bool a_variant, bool is_async) {
    const NpScoreRankOut out = rank_out(ctx);
    const uint64_t opt_addr = stack_arg(ctx, 6);
    const NpScoreFriendRankingOpt *opt = gptr(ctx, opt_addr, NP_SCORE_FRIEND_OPT_SIZE);
    // A guest option too close to the end of guest memory to hold the structure is still an
    // option: its size field cannot be the structure's size.
    static const NpScoreFriendRankingOpt truncated = {0, 0, 0};
    if (!opt && opt_addr) opt = &truncated;
    const uint32_t *start = opt ? gptr(ctx, opt->start_serial_rank, sizeof(uint32_t)) : NULL;
    RET(ps4_np_score_get_friends_ranking((int32_t)ctx->rdi, (uint32_t)ctx->rsi, (int32_t)ctx->rdx, &out, a_variant,
                                         opt, start, is_async));
}

void shim_sceNpScoreGetFriendsRanking(GuestContext *ctx) {
    get_friends_ranking(ctx, false, false);
}

void shim_sceNpScoreGetFriendsRankingAsync(GuestContext *ctx) {
    get_friends_ranking(ctx, false, true);
}

void shim_sceNpScoreGetFriendsRankingA(GuestContext *ctx) {
    get_friends_ranking(ctx, true, false);
}

void shim_sceNpScoreGetFriendsRankingAAsync(GuestContext *ctx) {
    get_friends_ranking(ctx, true, true);
}

// int sceNpScoreGetBoardInfo[Async](int reqId, OrbisNpScoreBoardId boardId,
//                                   OrbisNpScoreBoardInfo *boardInfo, void *option)
static void get_board_info(GuestContext *ctx, bool is_async) {
    RET(ps4_np_score_get_board_info((int32_t)ctx->rdi, (uint32_t)ctx->rsi, gptr(ctx, ctx->rdx, NP_SCORE_BOARD_INFO_SIZE),
                                    gptr(ctx, ctx->rcx, 1), (int32_t)ps4_compiled_sdk_version(ctx), is_async));
}

void shim_sceNpScoreGetBoardInfo(GuestContext *ctx) {
    get_board_info(ctx, false);
}

void shim_sceNpScoreGetBoardInfoAsync(GuestContext *ctx) {
    get_board_info(ctx, true);
}

// int sceNpScoreGetGameData[Async](int reqId, OrbisNpScoreBoardId boardId, const OrbisNpId *npId,
//                                  u64 *totalSize, u64 recvSize, void *data, void *option)
static void get_game_data(GuestContext *ctx, bool is_async) {
    RET(ps4_np_score_get_game_data((int32_t)ctx->rdi, (uint32_t)ctx->rsi, gptr(ctx, ctx->rdx, NP_SCORE_NP_ID_SIZE),
                                   gptr(ctx, ctx->rcx, sizeof(uint64_t)), ctx->r8, gptr(ctx, ctx->r9, ctx->r8),
                                   gptr(ctx, stack_arg(ctx, 0), 1), is_async));
}

void shim_sceNpScoreGetGameData(GuestContext *ctx) {
    get_game_data(ctx, false);
}

void shim_sceNpScoreGetGameDataAsync(GuestContext *ctx) {
    get_game_data(ctx, true);
}

// int sceNpScoreGetGameDataByAccountId[Async](int reqId, OrbisNpScoreBoardId boardId,
//     OrbisNpAccountId accountId, u64 *totalSize, u64 recvSize, void *data, void *option)
static void get_game_data_by_account_id(GuestContext *ctx, bool is_async) {
    RET(ps4_np_score_get_game_data_by_account_id((int32_t)ctx->rdi, (uint32_t)ctx->rsi, ctx->rdx,
                                                 gptr(ctx, ctx->rcx, sizeof(uint64_t)), ctx->r8,
                                                 gptr(ctx, ctx->r9, ctx->r8), gptr(ctx, stack_arg(ctx, 0), 1),
                                                 is_async));
}

void shim_sceNpScoreGetGameDataByAccountId(GuestContext *ctx) {
    get_game_data_by_account_id(ctx, false);
}

void shim_sceNpScoreGetGameDataByAccountIdAsync(GuestContext *ctx) {
    get_game_data_by_account_id(ctx, true);
}

// int sceNpScoreRecordGameData[Async](int reqId, OrbisNpScoreBoardId boardId,
//     OrbisNpScoreValue score, u64 totalSize, u64 sendSize, const void *data, void *option)
static void record_game_data(GuestContext *ctx, bool is_async) {
    RET(ps4_np_score_record_game_data((int32_t)ctx->rdi, (uint32_t)ctx->rsi, (int64_t)ctx->rdx, ctx->rcx, ctx->r8,
                                      gptr(ctx, ctx->r9, ctx->r8), gptr(ctx, stack_arg(ctx, 0), 1), is_async));
}

void shim_sceNpScoreRecordGameData(GuestContext *ctx) {
    record_game_data(ctx, false);
}

void shim_sceNpScoreRecordGameDataAsync(GuestContext *ctx) {
    record_game_data(ctx, true);
}

// int sceNpScoreCensorComment[Async](int reqId, const char *comment, void *option)
static void censor_comment(GuestContext *ctx, bool is_async) {
    RET(ps4_np_score_censor_comment((int32_t)ctx->rdi, gptr(ctx, ctx->rsi, 1), groom(ctx, ctx->rsi),
                                    gptr(ctx, ctx->rdx, 1), is_async));
}

void shim_sceNpScoreCensorComment(GuestContext *ctx) {
    censor_comment(ctx, false);
}

void shim_sceNpScoreCensorCommentAsync(GuestContext *ctx) {
    censor_comment(ctx, true);
}

// int sceNpScoreSanitizeComment[Async](int reqId, const char *comment, char *sanitizedComment,
//                                      void *option)
// sanitizedComment receives up to SANITIZE_COMMENT_MAXLEN + 1 bytes; it must have that room.
static void sanitize_comment(GuestContext *ctx, bool is_async) {
    RET(ps4_np_score_sanitize_comment((int32_t)ctx->rdi, gptr(ctx, ctx->rsi, 1), groom(ctx, ctx->rsi),
                                      gptr(ctx, ctx->rdx, NP_SCORE_SANITIZE_COMMENT_MAXLEN + 1),
                                      gptr(ctx, ctx->rcx, 1), is_async));
}

void shim_sceNpScoreSanitizeComment(GuestContext *ctx) {
    sanitize_comment(ctx, false);
}

void shim_sceNpScoreSanitizeCommentAsync(GuestContext *ctx) {
    sanitize_comment(ctx, true);
}

// Stubs in shadPS4's np_score.cpp, in its source order.
PS4_HLE_STUB(sceNpScoreChangeModeForOtherSaveDataOwners)
PS4_HLE_STUB(sceNpScoreCreateTitleCtx)
PS4_HLE_STUB(sceNpScoreGetFriendsRankingForCrossSave)
PS4_HLE_STUB(sceNpScoreGetFriendsRankingForCrossSaveAsync)
PS4_HLE_STUB(sceNpScoreGetRankingByAccountIdForCrossSave)
PS4_HLE_STUB(sceNpScoreGetRankingByAccountIdForCrossSaveAsync)
PS4_HLE_STUB(sceNpScoreGetRankingByAccountIdPcIdForCrossSave)
PS4_HLE_STUB(sceNpScoreGetRankingByAccountIdPcIdForCrossSaveAsync)
PS4_HLE_STUB(sceNpScoreGetRankingByRangeForCrossSave)
PS4_HLE_STUB(sceNpScoreGetRankingByRangeForCrossSaveAsync)
PS4_HLE_STUB(sceNpScoreSetThreadParam)
PS4_HLE_STUB(sceNpScoreSetTimeout)
