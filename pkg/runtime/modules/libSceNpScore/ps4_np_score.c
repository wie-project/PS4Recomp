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
// Ranking, board, score and game data operations go to the shadNet server through the NP
// handler (libSceNp/ps4_np_handler.c; shadPS4 np_handler_score.cpp): the request's user needs
// an authenticated shadNet session (else ORBIS_NP_ERROR_SIGNED_OUT or
// ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN) and the title an NP communication id for the title
// context's service label in npbind.dat (else ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT). The
// reply fills the caller's outputs and completes the operation, on the client's reader thread;
// an operation still waiting when the session ends completes with ORBIS_NP_ERROR_SIGNED_OUT.
// A title context created from an NP id belongs to the signed-in user with that online id, or
// to user -1 (NpHandler::GetUserIdByOnlineId); sceNpScoreCreateNpTitleCtxA needs a signed-in
// user. GetGameData keeps a received blob on the request when the caller's buffer was filled,
// and serves the next calls for the same board and player from it.
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
#include "ps4_np_handler.h"
#include "ps4_npbind.h"
#include "shadnet_client.h"
#include "shadnet_pb.h"
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
    // GetGameData receive cache (shadPS4 gameDataRecv*): the blob of recv_key, served from
    // recv_offset on; recv_offset 0 means no transfer in progress.
    uint8_t *recv_cache;
    uint64_t recv_len;
    uint64_t recv_offset;
    char recv_key[64];
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
    free(q->recv_cache);
    memset(q, 0, sizeof(*q));
}

// Lock held. A request by id, deleted or not: a reply still reaches a deleted request that has
// waiters (ids are never reused).
static NpScoreRequest *req_find_any(int32_t id) {
    if (id == 0) return NULL;
    for (int i = 0; i < NP_SCORE_REQUEST_SLOTS; i++) {
        if (g_score.req[i].id == id) return &g_score.req[i];
    }
    return NULL;
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

//=============================================================================
// Server requests (shadPS4 np_handler_score.cpp)
//=============================================================================

// Guest structures the replies fill (shadPS4 np_score.h).
typedef struct {
    OrbisNpId np_id;
    uint8_t reserved[49];
    uint8_t pad0[3];
    int32_t pc_id;
    uint32_t serial_rank;
    uint32_t rank;
    uint32_t highest_rank;
    int32_t has_game_data;
    uint8_t pad1[4];
    int64_t score_value;
    uint64_t record_date;
} ScoreRankData;

typedef struct {
    OrbisNpOnlineId online_id;
    uint8_t reserved0[16];
    uint8_t reserved[49];
    uint8_t pad0[3];
    int32_t pc_id;
    uint32_t serial_rank;
    uint32_t rank;
    uint32_t highest_rank;
    int32_t has_game_data;
    uint8_t pad1[4];
    int64_t score_value;
    uint64_t record_date;
    uint64_t account_id;
    uint8_t pad2[8];
} ScoreRankDataA;

typedef struct {
    int32_t has_data;
    uint8_t pad0[4];
    ScoreRankData rank_data;
} ScorePlayerRankData;

typedef struct {
    int32_t has_data;
    uint8_t pad0[4];
    ScoreRankDataA rank_data;
} ScorePlayerRankDataA;

typedef struct {
    char utf8_comment[NP_SCORE_COMMENT_MAXLEN + 1];
} ScoreComment;

typedef struct {
    uint64_t info_size;
    uint8_t data[NP_SCORE_GAMEINFO_MAXSIZE];
    uint8_t pad2[3];
} ScoreGameInfo;

typedef struct {
    uint32_t rank_limit;
    uint32_t update_mode;
    uint32_t sort_mode;
    uint32_t upload_num_limit;
    uint64_t upload_size_limit;
} ScoreBoardInfo;

typedef struct {
    OrbisNpId np_id;
    int32_t pc_id;
    uint8_t pad[4];
} ScoreNpIdPcId;

typedef struct {
    uint64_t account_id;
    int32_t pc_id;
    uint8_t pad[4];
} ScoreAccountIdPcId;

_Static_assert(sizeof(ScoreRankData) == NP_SCORE_RANK_DATA_SIZE, "OrbisNpScoreRankData");
_Static_assert(sizeof(ScoreRankDataA) == NP_SCORE_RANK_DATA_A_SIZE, "OrbisNpScoreRankDataA");
_Static_assert(sizeof(ScorePlayerRankData) == NP_SCORE_PLAYER_RANK_DATA_SIZE, "OrbisNpScorePlayerRankData");
_Static_assert(sizeof(ScorePlayerRankDataA) == NP_SCORE_PLAYER_RANK_DATA_A_SIZE, "OrbisNpScorePlayerRankDataA");
_Static_assert(sizeof(ScoreComment) == NP_SCORE_COMMENT_SIZE, "OrbisNpScoreComment");
_Static_assert(sizeof(ScoreGameInfo) == NP_SCORE_GAME_INFO_SIZE, "OrbisNpScoreGameInfo");
_Static_assert(sizeof(ScoreBoardInfo) == NP_SCORE_BOARD_INFO_SIZE, "OrbisNpScoreBoardInfo");
_Static_assert(sizeof(ScoreNpIdPcId) == NP_SCORE_NP_ID_PC_ID_SIZE, "OrbisNpScoreNpIdPcId");
_Static_assert(sizeof(ScoreAccountIdPcId) == NP_SCORE_ACCOUNT_ID_PC_ID_SIZE, "OrbisNpScoreAccountIdPcId");

// The kind of rank array a ranking reply fills.
enum {
    RANK_PLAIN,    // OrbisNpScoreRankData (GetRankingByRange, GetFriendsRanking)
    RANK_A,        // OrbisNpScoreRankDataA (their A variants)
    RANK_PLAYER,   // OrbisNpScorePlayerRankData, one per requested NP id
    RANK_PLAYER_A, // OrbisNpScorePlayerRankDataA, one per requested account id
};

// An operation waiting for its reply (shadPS4 PendingScoreRequest): the outputs the reply fills.
typedef struct ScoreOp {
    int32_t req_id;
    uint16_t cmd;
    uint32_t *tmp_rank; // RecordScore
    int rank_kind;
    void *rank;
    ScoreComment *comment;
    ScoreGameInfo *info;
    uint64_t *last_sort_date;
    uint32_t *total_record;
    uint64_t array_num;
    ScoreBoardInfo *board_info;
    void *data_out; // GetGameData*
    uint64_t recv_size;
    uint64_t *total_size_out;
} ScoreOp;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static size_t min_size(size_t a, size_t b) {
    return a < b ? a : b;
}

// The comment and game info of response entry i into output slot out_i.
static void fill_comment_info(const ScoreOp *op, const ShadPb_GetScoreResponse *resp, uint32_t i, uint32_t out_i) {
    if (op->comment && i < resp->commentArray_count) {
        const ShadPbBytes c = resp->commentArray[i];
        memcpy(op->comment[out_i].utf8_comment, c.data, min_size(c.len, sizeof(op->comment[out_i].utf8_comment) - 1));
    }
    if (op->info && i < resp->infoArray_count) {
        const ShadPbBytes g = resp->infoArray[i].data;
        const size_t n = min_size(g.len, sizeof(op->info[out_i].data));
        op->info[out_i].info_size = n;
        memcpy(op->info[out_i].data, g.data, n);
    }
}

// The rank fields common to every rank data layout.
#define FILL_RANK_FIELDS(out, r)                                         \
    do {                                                                 \
        (out).pc_id = (r)->pcId;                                         \
        (out).serial_rank = (r)->rank;                                   \
        (out).rank = (r)->sharedRank != 0 ? (r)->sharedRank : (r)->rank; \
        (out).highest_rank = (out).rank;                                 \
        (out).has_game_data = (r)->hasGameData ? 1 : 0;                  \
        (out).score_value = (r)->score;                                  \
        (out).record_date = (r)->recordDate;                             \
    } while (0)

static void copy_online_id(OrbisNpOnlineId *dst, ShadPbBytes npid) {
    memcpy(dst->data, npid.data, min_size(npid.len, sizeof(dst->data)));
    dst->term = 0;
}

// FillPlainRankArray[A]FromProto: the entries with an NP id, packed from slot 0, at most
// array_num. FillRankArrayFromProto / FillPlayerRankArrayAFromProto: entry i goes to slot i
// (hasData set) for the first min(response, requested) entries. Returns the entries found.
static uint32_t fill_rank_array(const ScoreOp *op, const ShadPb_GetScoreResponse *resp) {
    const bool packed = op->rank_kind == RANK_PLAIN || op->rank_kind == RANK_A;
    const uint64_t n = packed ? resp->rankArray_count : min_size(resp->rankArray_count, (size_t)op->array_num);
    uint32_t found = 0;
    for (uint32_t i = 0; i < n && (!packed || found < op->array_num); i++) {
        const ShadPb_ScoreRankData *r = &resp->rankArray[i];
        if (r->npId.len == 0) continue; // no score for this entry
        const uint32_t slot = packed ? found : i;
        switch (op->rank_kind) {
        case RANK_PLAIN: {
            ScoreRankData *out = &((ScoreRankData *)op->rank)[slot];
            copy_online_id(&out->np_id.handle, r->npId);
            FILL_RANK_FIELDS(*out, r);
            break;
        }
        case RANK_A: {
            ScoreRankDataA *out = &((ScoreRankDataA *)op->rank)[slot];
            copy_online_id(&out->online_id, r->npId);
            FILL_RANK_FIELDS(*out, r);
            out->account_id = (uint64_t)r->accountId;
            break;
        }
        case RANK_PLAYER: {
            ScorePlayerRankData *out = &((ScorePlayerRankData *)op->rank)[slot];
            out->has_data = 1;
            copy_online_id(&out->rank_data.np_id.handle, r->npId);
            FILL_RANK_FIELDS(out->rank_data, r);
            break;
        }
        default: {
            ScorePlayerRankDataA *out = &((ScorePlayerRankDataA *)op->rank)[slot];
            out->has_data = 1;
            copy_online_id(&out->rank_data.online_id, r->npId);
            FILL_RANK_FIELDS(out->rank_data, r);
            out->rank_data.account_id = (uint64_t)r->accountId;
            break;
        }
        }
        fill_comment_info(op, resp, i, slot);
        found++;
    }
    return found;
}

// The ORBIS error for a shadNet error reply.
static int32_t score_error_from_reply(uint16_t cmd, uint8_t error) {
    switch (error) {
    case SHADNET_ERR_SCORE_NOT_BEST: return NP_COMMUNITY_SERVER_ERROR_NOT_BEST_SCORE;
    case SHADNET_ERR_SCORE_INVALID: return NP_COMMUNITY_SERVER_ERROR_INVALID_SCORE;
    case SHADNET_ERR_SCORE_HAS_DATA: return NP_COMMUNITY_SERVER_ERROR_GAME_DATA_ALREADY_EXISTS;
    case SHADNET_ERR_TOO_LARGE: return NP_COMMUNITY_SERVER_ERROR_TOO_LARGE_DATA;
    case SHADNET_ERR_COND_FAIL: return NP_COMMUNITY_SERVER_ERROR_CONDITIONS_NOT_SATISFIED;
    case SHADNET_ERR_NOT_FOUND:
        if (cmd == SHADNET_CMD_GET_SCORE_DATA || cmd == SHADNET_CMD_GET_SCORE_GAME_DATA_BY_ACC_ID) {
            return NP_COMMUNITY_SERVER_ERROR_RANKING_GAME_DATA_MASTER_NOT_FOUND;
        }
        if (cmd == SHADNET_CMD_RECORD_SCORE_DATA) return NP_COMMUNITY_SERVER_ERROR_RANKING_STORE_NOT_FOUND;
        return NP_COMMUNITY_SERVER_ERROR_RANKING_BOARD_MASTER_NOT_FOUND;
    case SHADNET_ERR_UNAUTHORIZED: return NP_COMMUNITY_SERVER_ERROR_FORBIDDEN;
    case SHADNET_ERR_DB_FAIL: return NP_COMMUNITY_SERVER_ERROR_INTERNAL_SERVER_ERROR;
    default: return NP_COMMUNITY_ERROR_BAD_RESPONSE;
    }
}

// Decodes a u32-prefixed protobuf reply body; false when it is short or malformed.
static bool decode_body(const uint8_t *body, size_t len, const ShadPbMessageDesc *desc, void *msg, ShadPbArena *a) {
    if (len < 4) return false;
    const uint32_t n = le32(body);
    return 4 + (size_t)n <= len && shadpb_decode(desc, body + 4, n, msg, a);
}

// Lock held. The request an operation belongs to, if it still exists.
static void op_complete(const ScoreOp *op, int32_t result) {
    NpScoreRequest *q = req_find_any(op->req_id);
    if (q) req_set_result(q, result);
}

// GetScoreData / GetScoreGameDataByAccId: the blob into the caller's buffer, and onto the
// request when the buffer was filled (the next calls read the rest from it).
static void on_game_data_reply(const ScoreOp *op, const uint8_t *body, size_t len) {
    if (len < 4 || 4 + (uint64_t)le32(body) > len) {
        pthread_mutex_lock(&g_score.mutex);
        op_complete(op, NP_COMMUNITY_ERROR_BAD_RESPONSE);
        pthread_mutex_unlock(&g_score.mutex);
        return;
    }
    const uint32_t stored = le32(body);
    if (op->total_size_out) *op->total_size_out = stored;
    uint64_t copy_n = 0;
    if (op->data_out && op->recv_size > 0) {
        copy_n = stored < op->recv_size ? stored : op->recv_size;
        memcpy(op->data_out, body + 4, (size_t)copy_n);
    }
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = req_find_any(op->req_id);
    if (q) {
        free(q->recv_cache);
        q->recv_cache = NULL;
        q->recv_len = 0;
        q->recv_offset = 0;
        if (copy_n >= op->recv_size && (q->recv_cache = malloc(stored ? stored : 1)) != NULL) {
            memcpy(q->recv_cache, body + 4, stored);
            q->recv_len = stored;
            q->recv_offset = copy_n;
        }
        req_set_result(q, (int32_t)copy_n);
    }
    pthread_mutex_unlock(&g_score.mutex);
}

// NpHandler::OnScoreReply.
static void on_score_reply(void *user, int32_t user_id, uint8_t error, const uint8_t *body, size_t len) {
    (void)user_id;
    ScoreOp *op = user;
    int32_t result = 0;
    ShadPbArena arena = {0};
    if (error != SHADNET_ERR_NO_ERROR) {
        result = score_error_from_reply(op->cmd, error);
    } else if (op->cmd == SHADNET_CMD_RECORD_SCORE) {
        if (op->tmp_rank && len >= 4) *op->tmp_rank = le32(body); // the temporary rank
    } else if (op->cmd == SHADNET_CMD_GET_SCORE_DATA || op->cmd == SHADNET_CMD_GET_SCORE_GAME_DATA_BY_ACC_ID) {
        on_game_data_reply(op, body, len);
        free(op);
        return;
    } else if (op->cmd == SHADNET_CMD_GET_BOARD_INFOS) {
        ShadPb_BoardInfo bi;
        if (!decode_body(body, len, &shadpb_BoardInfo_desc, &bi, &arena)) {
            result = NP_COMMUNITY_ERROR_BAD_RESPONSE;
        } else if (op->board_info) {
            op->board_info->rank_limit = bi.rankLimit;
            op->board_info->update_mode = bi.updateMode;
            op->board_info->sort_mode = bi.sortMode;
            op->board_info->upload_num_limit = bi.uploadNumLimit;
            op->board_info->upload_size_limit = bi.uploadSizeLimit;
        }
    } else if (op->cmd != SHADNET_CMD_RECORD_SCORE_DATA) {
        // A ranking: GetScoreNpId, GetScoreAccountId, GetScoreRange, GetScoreFriends.
        ShadPb_GetScoreResponse resp;
        if (!decode_body(body, len, &shadpb_GetScoreResponse_desc, &resp, &arena)) {
            result = NP_COMMUNITY_ERROR_BAD_RESPONSE;
        } else {
            const uint32_t found = fill_rank_array(op, &resp);
            const bool friends = op->cmd == SHADNET_CMD_GET_SCORE_FRIENDS;
            // An empty friends ranking leaves lastSortDate and totalRecord alone and is no error.
            if (!friends || found > 0) {
                if (op->last_sort_date) *op->last_sort_date = resp.lastSortDate;
                if (op->total_record) *op->total_record = resp.totalRecord;
            }
            result = found > 0 || friends ? (int32_t)found : NP_COMMUNITY_SERVER_ERROR_GAME_RANKING_NOT_FOUND;
        }
    }
    shadpb_arena_free(&arena);
    pthread_mutex_lock(&g_score.mutex);
    op_complete(op, result);
    pthread_mutex_unlock(&g_score.mutex);
    free(op);
}

// FailPendingRequests: the session ended before the reply.
static void on_score_fail(void *user, int32_t error) {
    ScoreOp *op = user;
    pthread_mutex_lock(&g_score.mutex);
    op_complete(op, error);
    pthread_mutex_unlock(&g_score.mutex);
    free(op);
}

static const Ps4NpReplyHandler g_score_reply = {on_score_reply, on_score_fail};

// Lock held. ServiceLabelForRequest: the service label of the request's title context (0 once
// it was deleted).
static int32_t req_service_label(const NpScoreRequest *q) {
    const NpScoreTitleCtx *c = ctx_find(q->title_ctx_id);
    return c ? (int32_t)c->service_label : 0;
}

// Lock held. The checks and request of an NpHandler score function: the user's session, the
// title's NP communication id, then cmd with the payload <comm id>[<u32 size><msg>][<tail>]
// (msg when desc is set). On success op belongs to the pending request; on failure it is freed
// and the error returned.
static int32_t score_send(const NpScoreRequest *q, ScoreOp *op, const ShadPbMessageDesc *desc, const void *msg,
                          const void *tail, size_t tail_len) {
    int32_t r = ps4_np_handler_session_status(q->user_id);
    char comm_id[PS4_NPBIND_COMM_ID_LEN];
    if (r == 0 && !ps4_np_handler_np_comm_id(req_service_label(q), comm_id)) {
        fprintf(stderr, "[libSceNpScore] no valid NP communication id (npbind.dat missing or blank); rejecting request\n");
        r = NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    }
    if (r == 0) {
        const size_t proto = desc ? shadpb_encoded_size(desc, msg) : 0;
        const size_t len = sizeof(comm_id) + (desc ? 4 + proto : 0) + tail_len;
        uint8_t *payload = malloc(len);
        if (!payload) {
            r = NP_COMMUNITY_ERROR_OUT_OF_MEMORY;
        } else {
            memcpy(payload, comm_id, sizeof(comm_id));
            size_t at = sizeof(comm_id);
            if (desc) {
                for (int i = 0; i < 4; i++) payload[at + i] = (uint8_t)(proto >> (8 * i));
                at += 4 + shadpb_encode(desc, msg, payload + at + 4);
            }
            if (tail_len) memcpy(payload + at, tail, tail_len);
            r = ps4_np_handler_send_raw(q->user_id, op->cmd, payload, len, &g_score_reply, op);
            free(payload);
        }
    }
    if (r != 0) free(op);
    return r;
}

// Lock held. A new operation record for q, or NULL when out of memory.
static ScoreOp *op_new(const NpScoreRequest *q, uint16_t cmd) {
    ScoreOp *op = calloc(1, sizeof(*op));
    if (op) {
        op->req_id = q->id;
        op->cmd = cmd;
    }
    return op;
}

// Lock held. The end of every operation that goes to the server: a failed dispatch completes
// the request with its error and returns it; otherwise an asynchronous variant returns 0 and a
// synchronous one waits for the reply's result.
static int32_t req_run(NpScoreRequest *q, ScoreOp *op, const ShadPbMessageDesc *desc, const void *msg,
                       const void *tail, size_t tail_len, bool is_async) {
    const int32_t r = op ? score_send(q, op, desc, msg, tail, tail_len) : NP_COMMUNITY_ERROR_OUT_OF_MEMORY;
    if (r != 0) {
        req_set_result(q, r);
        return r;
    }
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
    // NpHandler::GetUserIdByOnlineId(selfNpId->handle); OrbisNpId starts with its online id.
    ps4_np_handler_start();
    return create_title_ctx(service_label, ps4_np_handler_user_by_online_id(self_np_id));
}

int32_t ps4_np_score_create_np_title_ctx_a(uint32_t service_label, int32_t self_id) {
    ps4_np_handler_start();
    if (!ps4_np_handler_is_signed_in(self_id)) return NP_SCORE_ERROR_SIGNED_OUT; // NpHandler::IsPsnSignedIn
    if (service_label == NP_SCORE_INVALID_SERVICE_LABEL) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    return create_title_ctx(service_label, self_id);
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

int32_t ps4_np_score_record_score(int32_t req_id, uint32_t board_id, int64_t score, const void *comment,
                                  const void *game_info, uint32_t *tmp_rank, const void *compare_date,
                                  const void *option, bool is_async) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    const ScoreGameInfo *info = game_info;
    if (info && info->info_size > NP_SCORE_GAMEINFO_MAXSIZE) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = NULL;
    int32_t r = req_start(req_id, &q);
    if (r == 0) {
        ShadPb_RecordScoreRequest msg = {.boardId = board_id, .pcId = q->pc_id, .score = score};
        if (comment) msg.comment = shadpb_bytes(comment, strnlen(comment, NP_SCORE_COMMENT_MAXLEN));
        if (info) msg.data = shadpb_bytes(info->data, (size_t)info->info_size);
        if (compare_date) memcpy(&msg.compareDate, compare_date, sizeof(msg.compareDate));
        ScoreOp *op = op_new(q, SHADNET_CMD_RECORD_SCORE);
        if (op) op->tmp_rank = tmp_rank;
        r = req_run(q, op, &shadpb_RecordScoreRequest_desc, &msg, NULL, 0, is_async);
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
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

// Lock held. A ranking operation's record: where its reply goes.
static ScoreOp *rank_op_new(const NpScoreRequest *q, uint16_t cmd, int rank_kind, const NpScoreRankOut *out) {
    ScoreOp *op = op_new(q, cmd);
    if (op) {
        op->rank_kind = rank_kind;
        op->rank = out->rank;
        op->comment = out->comment;
        op->info = out->info;
        op->last_sort_date = out->last_sort_date;
        op->total_record = out->total_record;
        op->array_num = out->array_num;
    }
    return op;
}

// Lock released. Starts a ranking operation and sends msg.
static int32_t run_ranking(int32_t req_id, uint16_t cmd, int rank_kind, const NpScoreRankOut *out,
                           const ShadPbMessageDesc *desc, void *msg, bool *with_comment, bool *with_info,
                           bool is_async) {
    *with_comment = out->comment != NULL;
    *with_info = out->info != NULL;
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = NULL;
    int32_t r = req_start(req_id, &q);
    if (r == 0) {
        r = req_run(q, rank_op_new(q, cmd, rank_kind, out), desc, msg, NULL, 0, is_async);
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_get_ranking_by_range(int32_t req_id, uint32_t board_id, uint32_t start_serial_rank,
                                          const NpScoreRankOut *out, bool a_variant, const void *option,
                                          bool is_async) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (out->array_num > NP_SCORE_MAX_RANGE_NUM_PER_REQUEST) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (start_serial_rank == 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!out->rank) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (out->array_num == 0) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    const uint64_t entry = a_variant ? NP_SCORE_RANK_DATA_A_SIZE : NP_SCORE_RANK_DATA_SIZE;
    if (out->rank_size != out->array_num * entry) return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    memset(out->rank, 0, out->rank_size);
    const int32_t r = clear_comment_info(out);
    if (r != 0) return r;
    ShadPb_GetScoreRangeRequest msg = {
        .boardId = board_id, .startRank = start_serial_rank, .numRanks = (uint32_t)out->array_num};
    return run_ranking(req_id, SHADNET_CMD_GET_SCORE_RANGE, a_variant ? RANK_A : RANK_PLAIN, out,
                       &shadpb_GetScoreRangeRequest_desc, &msg, &msg.withComment, &msg.withGameInfo, is_async);
}

// GetRankingByNpId[PcId]: one entry per NP id (pc ids: the array's, or 0).
static int32_t ranking_by_np_ids(int32_t req_id, uint32_t board_id, const OrbisNpId *np_ids,
                                 const ScoreNpIdPcId *pairs, const NpScoreRankOut *out, bool is_async) {
    ShadPb_ScoreNpIdPcId ids[NP_SCORE_MAX_NPID_NUM_PER_REQUEST];
    for (uint64_t i = 0; i < out->array_num; i++) {
        const OrbisNpId *id = pairs ? &pairs[i].np_id : &np_ids[i];
        ids[i].npid = shadpb_bytes(id->handle.data, strnlen(id->handle.data, sizeof(id->handle.data)));
        ids[i].pcId = pairs ? pairs[i].pc_id : 0;
    }
    ShadPb_GetScoreNpIdRequest msg = {.boardId = board_id, .npids = ids, .npids_count = (uint32_t)out->array_num};
    return run_ranking(req_id, SHADNET_CMD_GET_SCORE_NPID, RANK_PLAYER, out, &shadpb_GetScoreNpIdRequest_desc, &msg,
                       &msg.withComment, &msg.withGameInfo, is_async);
}

int32_t ps4_np_score_get_ranking_by_np_id(int32_t req_id, uint32_t board_id, const void *np_id_array,
                                          uint64_t np_id_array_size, const NpScoreRankOut *out,
                                          const void *option, bool is_async) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (out->array_num > NP_SCORE_MAX_NPID_NUM_PER_REQUEST) return NP_COMMUNITY_ERROR_TOO_MANY_NPID;
    if (!np_id_array || !out->rank) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (np_id_array_size != out->array_num * NP_SCORE_NP_ID_SIZE ||
        out->rank_size != out->array_num * NP_SCORE_PLAYER_RANK_DATA_SIZE) {
        return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    }
    memset(out->rank, 0, out->rank_size);
    const int32_t r = clear_comment_info(out);
    return r != 0 ? r : ranking_by_np_ids(req_id, board_id, np_id_array, NULL, out, is_async);
}

int32_t ps4_np_score_get_ranking_by_np_id_pc_id(int32_t req_id, uint32_t board_id, const void *id_array,
                                                uint64_t id_array_size, const NpScoreRankOut *out,
                                                const void *option, bool is_async) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!id_array || out->array_num == 0 || !out->rank) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (out->array_num > NP_SCORE_MAX_NPID_NUM_PER_REQUEST) return NP_COMMUNITY_ERROR_TOO_MANY_NPID;
    if (id_array_size != out->array_num * NP_SCORE_NP_ID_PC_ID_SIZE ||
        out->rank_size != out->array_num * NP_SCORE_PLAYER_RANK_DATA_SIZE) {
        return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    }
    memset(out->rank, 0, out->rank_size);
    const int32_t r = clear_comment_info(out);
    return r != 0 ? r : ranking_by_np_ids(req_id, board_id, NULL, id_array, out, is_async);
}

// GetRankingByAccountIdImpl and GetRankingByAccountIdPcIdImpl: one entry per account id (pc ids:
// pairs' or 0).
static int32_t get_ranking_by_account(int32_t req_id, uint32_t board_id, const void *id_array, uint64_t id_array_size,
                                      uint64_t id_entry, const NpScoreRankOut *out, const void *option,
                                      bool is_async) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (out->array_num > NP_SCORE_MAX_ID_NUM_PER_REQUEST) return NP_COMMUNITY_ERROR_TOO_MANY_NPID;
    if (out->array_num == 0 || !out->rank || !id_array) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (id_array_size != out->array_num * id_entry) return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    if (out->rank_size != out->array_num * NP_SCORE_PLAYER_RANK_DATA_A_SIZE) return NP_COMMUNITY_ERROR_INVALID_ALIGNMENT;
    memset(out->rank, 0, out->rank_size);
    const int32_t r = clear_comment_info(out);
    if (r != 0) return r;
    const bool pairs = id_entry == NP_SCORE_ACCOUNT_ID_PC_ID_SIZE;
    ShadPb_ScoreAccountIdPcId ids[NP_SCORE_MAX_ID_NUM_PER_REQUEST];
    for (uint64_t i = 0; i < out->array_num; i++) {
        if (pairs) {
            const ScoreAccountIdPcId *p = &((const ScoreAccountIdPcId *)id_array)[i];
            ids[i].accountId = (int64_t)p->account_id;
            ids[i].pcId = p->pc_id;
        } else {
            uint64_t account_id;
            memcpy(&account_id, (const uint8_t *)id_array + i * NP_SCORE_ACCOUNT_ID_SIZE, sizeof(account_id));
            ids[i].accountId = (int64_t)account_id;
            ids[i].pcId = 0;
        }
    }
    ShadPb_GetScoreAccountIdRequest msg = {.boardId = board_id, .ids = ids, .ids_count = (uint32_t)out->array_num};
    return run_ranking(req_id, SHADNET_CMD_GET_SCORE_ACCOUNT_ID, RANK_PLAYER_A, out,
                       &shadpb_GetScoreAccountIdRequest_desc, &msg, &msg.withComment, &msg.withGameInfo, is_async);
}

int32_t ps4_np_score_get_ranking_by_account_id(int32_t req_id, uint32_t board_id, const void *account_id_array,
                                               uint64_t account_id_array_size, const NpScoreRankOut *out,
                                               const void *option, bool is_async) {
    return get_ranking_by_account(req_id, board_id, account_id_array, account_id_array_size, NP_SCORE_ACCOUNT_ID_SIZE,
                                  out, option, is_async);
}

int32_t ps4_np_score_get_ranking_by_account_id_pc_id(int32_t req_id, uint32_t board_id, const void *id_array,
                                                     uint64_t id_array_size, const NpScoreRankOut *out,
                                                     const void *option, bool is_async) {
    return get_ranking_by_account(req_id, board_id, id_array, id_array_size, NP_SCORE_ACCOUNT_ID_PC_ID_SIZE, out,
                                  option, is_async);
}

int32_t ps4_np_score_get_friends_ranking(int32_t req_id, uint32_t board_id, int32_t include_self,
                                         const NpScoreRankOut *out, bool a_variant,
                                         const NpScoreFriendRankingOpt *option,
                                         const uint32_t *opt_start_serial_rank, bool is_async) {
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
    if (r != 0) return r;
    ShadPb_GetScoreFriendsRequest msg = {.boardId = board_id,
                                         .includeSelf = include_self != 0,
                                         .max = (uint32_t)out->array_num,
                                         .startRank = start_serial_rank};
    return run_ranking(req_id, SHADNET_CMD_GET_SCORE_FRIENDS, a_variant ? RANK_A : RANK_PLAIN, out,
                       &shadpb_GetScoreFriendsRequest_desc, &msg, &msg.withComment, &msg.withGameInfo, is_async);
}

int32_t ps4_np_score_get_board_info(int32_t req_id, uint32_t board_id, void *board_info, const void *option,
                                    int32_t sdk_version, bool is_async) {
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
            ScoreOp *op = op_new(q, SHADNET_CMD_GET_BOARD_INFOS);
            if (op) op->board_info = board_info;
            const uint8_t id[4] = {(uint8_t)board_id, (uint8_t)(board_id >> 8), (uint8_t)(board_id >> 16),
                                   (uint8_t)(board_id >> 24)};
            r = req_run(q, op, NULL, NULL, id, sizeof(id), is_async);
        }
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

// Lock held. ServeGameDataFromCache: a transfer of key in progress on q continues from its
// cache; any other key starts over (false: ask the server).
static bool serve_game_data_from_cache(NpScoreRequest *q, const char *key, uint64_t *total_size, uint64_t recv_size,
                                       void *data, bool is_async, int32_t *ret) {
    if (q->recv_offset == 0 || strcmp(q->recv_key, key) != 0) {
        free(q->recv_cache);
        q->recv_cache = NULL;
        q->recv_len = 0;
        q->recv_offset = 0;
        snprintf(q->recv_key, sizeof(q->recv_key), "%s", key);
        return false;
    }
    const uint64_t start = q->recv_offset < q->recv_len ? q->recv_offset : q->recv_len;
    uint64_t n = 0;
    if (data) n = q->recv_len - start < recv_size ? q->recv_len - start : recv_size;
    if (n > 0) memcpy(data, q->recv_cache + start, (size_t)n);
    if (total_size) *total_size = q->recv_len;
    if (n < recv_size) {
        free(q->recv_cache); // transfer complete
        q->recv_cache = NULL;
        q->recv_len = 0;
        q->recv_offset = 0;
    } else {
        q->recv_offset = start + n;
    }
    req_set_result(q, (int32_t)n);
    *ret = is_async ? 0 : (int32_t)n;
    return true;
}

// Lock held. The game data operation for key: from the cache, or a request to the server.
static int32_t game_data_run(NpScoreRequest *q, const char *key, uint16_t cmd, const ShadPbMessageDesc *desc,
                             const void *msg, uint64_t *total_size, uint64_t recv_size, void *data, bool is_async) {
    int32_t cached;
    if (serve_game_data_from_cache(q, key, total_size, recv_size, data, is_async, &cached)) return cached;
    ScoreOp *op = op_new(q, cmd);
    if (op) {
        op->data_out = data;
        op->recv_size = recv_size;
        op->total_size_out = total_size;
    }
    return req_run(q, op, desc, msg, NULL, 0, is_async);
}

int32_t ps4_np_score_get_game_data(int32_t req_id, uint32_t board_id, const void *np_id, uint64_t *total_size,
                                   uint64_t recv_size, void *data, const void *option, bool is_async) {
    if (!np_id) return NP_COMMUNITY_ERROR_INSUFFICIENT_ARGUMENT;
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = NULL;
    int32_t r = req_start(req_id, &q);
    if (r == 0) {
        const OrbisNpId *id = np_id;
        const size_t len = strnlen(id->handle.data, sizeof(id->handle.data));
        if (len == 0) {
            req_set_result(q, NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
            r = NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
        } else {
            char key[64];
            snprintf(key, sizeof(key), "%u:npid:%.*s", board_id, (int)len, id->handle.data);
            const ShadPb_GetScoreGameDataRequest msg = {
                .boardId = board_id, .npId = shadpb_bytes(id->handle.data, len), .pcId = q->pc_id};
            r = game_data_run(q, key, SHADNET_CMD_GET_SCORE_DATA, &shadpb_GetScoreGameDataRequest_desc, &msg,
                              total_size, recv_size, data, is_async);
        }
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_get_game_data_by_account_id(int32_t req_id, uint32_t board_id, uint64_t account_id,
                                                 uint64_t *total_size, uint64_t recv_size, void *data,
                                                 const void *option, bool is_async) {
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (account_id == 0) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_score.mutex);
    NpScoreRequest *q = NULL;
    int32_t r = req_start(req_id, &q);
    if (r == 0) {
        char key[64];
        snprintf(key, sizeof(key), "%u:acc:%llu", board_id, (unsigned long long)account_id);
        const ShadPb_GetScoreGameDataByAccountIdRequest msg = {
            .boardId = board_id, .accountId = (int64_t)account_id, .pcId = q->pc_id};
        r = game_data_run(q, key, SHADNET_CMD_GET_SCORE_GAME_DATA_BY_ACC_ID,
                          &shadpb_GetScoreGameDataByAccountIdRequest_desc, &msg, total_size, recv_size, data, is_async);
    }
    pthread_mutex_unlock(&g_score.mutex);
    return r;
}

int32_t ps4_np_score_record_game_data(int32_t req_id, uint32_t board_id, int64_t score, uint64_t total_size,
                                      uint64_t send_size, const void *data, const void *option, bool is_async) {
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
                // The whole blob goes to the server after the request message; the request's
                // buffer starts over.
                uint8_t *blob = q->send_buf;
                const uint64_t blob_len = q->send_len;
                q->send_buf = NULL;
                q->send_len = 0;
                q->send_total = 0;
                const ShadPb_RecordScoreGameDataRequest msg = {.boardId = board_id, .pcId = q->pc_id, .score = score};
                r = req_run(q, op_new(q, SHADNET_CMD_RECORD_SCORE_DATA), &shadpb_RecordScoreGameDataRequest_desc,
                            &msg, blob, (size_t)blob_len, is_async);
                free(blob);
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
    out.last_sort_date = gptr(ctx, stack_arg(ctx, 4), sizeof(uint64_t));
    out.total_record = gptr(ctx, stack_arg(ctx, 5), sizeof(uint32_t));
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
    out.last_sort_date = gptr(ctx, stack_arg(ctx, 5), sizeof(uint64_t));
    out.total_record = gptr(ctx, stack_arg(ctx, 6), sizeof(uint32_t));
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
