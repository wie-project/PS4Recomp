// SPDX-License-Identifier: GPL-2.0-or-later

// A local server speaking shadPS4's shadNet wire protocol, for host tests of the NP libraries:
// it answers the NP handler's ServerInfo probe and one login session (Login, GetToken,
// GetServerFeatures, SetClientVersion), then LookupOnlineId, GetAuthorizationCode and score
// requests, until the client hangs up. Its score service keeps one board entry: the logged-in
// account's best score with its comment, game info and game data. Include it in one test program; it needs shadnet_codec.c and
// shadnet_pb.c.

#ifndef SHADNET_FAKE_SERVER_H
#define SHADNET_FAKE_SERVER_H

#include "shadnet_client.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct FakeShadNet {
    int listen_fd;
    uint16_t port;
    uint64_t account_id;     // LoginReply user_id
    const char *lookup_npid; // the only online id LookupOnlineId finds
    uint64_t lookup_account;
    // GetAuthorizationCode: replies with auth_code, or auth_error (an ErrorType) when set.
    const char *auth_code;
    uint8_t auth_error;
    // While set, the server holds its next GetAuthorizationCode or score reply back.
    volatile bool hold_replies;
    pthread_t thread;
    volatile bool stopping;
    // Filled by the server.
    char login_npid[32];
    char auth_client_id[160]; // of the last GetAuthorizationCode request
    char auth_scope[64];
    // Score service.
    char score_comm_id[13]; // of the last score request
    bool has_score;
    int64_t best_score;
    int32_t score_pc_id;
    char score_comment[64];
    uint8_t score_info[200];
    size_t score_info_len;
    uint8_t game_data[256];
    size_t game_data_len;
    int sessions; // login sessions served
    int failures;
} FakeShadNet;

static void fake_put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static bool fake_read(int fd, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        const ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static void fake_send(int fd, uint8_t type, uint16_t cmd, uint64_t id, const uint8_t *payload, size_t len) {
    uint8_t hdr[SHADNET_HEADER_SIZE];
    hdr[0] = type;
    hdr[1] = (uint8_t)cmd;
    hdr[2] = (uint8_t)(cmd >> 8);
    fake_put32(hdr + 3, (uint32_t)(SHADNET_HEADER_SIZE + len));
    for (int i = 0; i < 8; i++) hdr[7 + i] = (uint8_t)(id >> (8 * i));
    send(fd, hdr, sizeof(hdr), 0);
    if (len) send(fd, payload, len, 0);
}

static void fake_reply(int fd, uint16_t cmd, uint64_t id, uint8_t err, const ShadPbMessageDesc *desc, const void *msg) {
    uint8_t buf[512];
    buf[0] = err;
    size_t len = 1;
    if (desc) {
        const size_t n = shadpb_encode(desc, msg, buf + 5);
        fake_put32(buf + 1, (uint32_t)n);
        len += 4 + n;
    }
    fake_send(fd, SHADNET_PACKET_REPLY, cmd, id, buf, len);
}

// Reads one request; payload gets its body (at most cap bytes).
static bool fake_request(int fd, uint16_t *cmd, uint64_t *id, uint8_t *payload, size_t cap, size_t *len) {
    uint8_t hdr[SHADNET_HEADER_SIZE];
    if (!fake_read(fd, hdr, sizeof(hdr))) return false;
    *cmd = (uint16_t)(hdr[1] | (hdr[2] << 8));
    *id = 0;
    for (int i = 0; i < 8; i++) *id |= (uint64_t)hdr[7 + i] << (8 * i);
    const uint32_t total = (uint32_t)hdr[3] | ((uint32_t)hdr[4] << 8) | ((uint32_t)hdr[5] << 16) | ((uint32_t)hdr[6] << 24);
    if (total < SHADNET_HEADER_SIZE || total - SHADNET_HEADER_SIZE > cap) return false;
    *len = total - SHADNET_HEADER_SIZE;
    return fake_read(fd, payload, *len);
}

static bool fake_decode(const uint8_t *payload, size_t len, const ShadPbMessageDesc *desc, void *msg, ShadPbArena *a) {
    if (len < 4) return false;
    const uint32_t n = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) | ((uint32_t)payload[2] << 16) |
                       ((uint32_t)payload[3] << 24);
    return len == 4 + (size_t)n && shadpb_decode(desc, payload + 4, n, msg, a);
}

static void fake_reply_raw(int fd, uint16_t cmd, uint64_t id, uint8_t err, const uint8_t *body, size_t len) {
    uint8_t buf[512];
    buf[0] = err;
    memcpy(buf + 1, body, len);
    fake_send(fd, SHADNET_PACKET_REPLY, cmd, id, buf, 1 + len);
}

// A score request: the 12-byte communication id (recorded), then a u32-prefixed message;
// *tail gets what follows it.
static bool fake_score_decode(FakeShadNet *s, const uint8_t *payload, size_t len, const ShadPbMessageDesc *desc,
                              void *msg, ShadPbArena *a, const uint8_t **tail, size_t *tail_len) {
    if (len < 16) return false;
    memcpy(s->score_comm_id, payload, 12);
    s->score_comm_id[12] = '\0';
    const uint32_t n = (uint32_t)payload[12] | ((uint32_t)payload[13] << 8) | ((uint32_t)payload[14] << 16) |
                       ((uint32_t)payload[15] << 24);
    if (16 + (size_t)n > len || !shadpb_decode(desc, payload + 16, n, msg, a)) return false;
    if (tail) {
        *tail = payload + 16 + n;
        *tail_len = len - 16 - n;
    }
    return true;
}

// The board's entry for a GetScoreResponse slot, or an empty one when entry is false.
static ShadPb_ScoreRankData fake_rank(const FakeShadNet *s, bool entry) {
    ShadPb_ScoreRankData r = {0};
    if (entry) {
        r.npId = shadpb_str(s->login_npid);
        r.pcId = s->score_pc_id;
        r.rank = 1;
        r.score = s->best_score;
        r.hasGameData = s->game_data_len > 0;
        r.recordDate = 1234;
        r.accountId = (int64_t)s->account_id;
    }
    return r;
}

// Answers a ranking with slots[i] (true: the board's entry) for each requested slot.
static void fake_ranking_reply(FakeShadNet *s, int fd, uint16_t cmd, uint64_t id, const bool *slots, uint32_t n,
                               bool with_comment, bool with_info) {
    ShadPb_ScoreRankData ranks[8];
    ShadPbBytes comments[8];
    ShadPb_ScoreInfo infos[8];
    ShadPb_GetScoreResponse resp = {.rankArray = ranks, .rankArray_count = n, .lastSortDate = 5678, .totalRecord = 1};
    for (uint32_t i = 0; i < n; i++) {
        ranks[i] = fake_rank(s, slots[i]);
        comments[i] = slots[i] ? shadpb_str(s->score_comment) : shadpb_str("");
        infos[i].data = slots[i] ? shadpb_bytes(s->score_info, s->score_info_len) : shadpb_bytes(NULL, 0);
    }
    if (with_comment) {
        resp.commentArray = comments;
        resp.commentArray_count = n;
    }
    if (with_info) {
        resp.infoArray = infos;
        resp.infoArray_count = n;
    }
    fake_reply(fd, cmd, id, 0, &shadpb_GetScoreResponse_desc, &resp);
}

// Score commands; false when the request is malformed.
static bool fake_score_request(FakeShadNet *s, int fd, uint16_t cmd, uint64_t id, const uint8_t *payload, size_t len,
                               ShadPbArena *a) {
    const uint8_t *tail;
    size_t tail_len;
    while (s->hold_replies) usleep(1000);
    switch (cmd) {
    case SHADNET_CMD_GET_BOARD_INFOS: {
        if (len != 16) return false;
        memcpy(s->score_comm_id, payload, 12);
        s->score_comm_id[12] = '\0';
        const uint32_t board = (uint32_t)payload[12] | ((uint32_t)payload[13] << 8);
        if (board == 99) {
            fake_reply(fd, cmd, id, SHADNET_ERR_NOT_FOUND, NULL, NULL);
        } else {
            ShadPb_BoardInfo bi = {.rankLimit = 100, .updateMode = 1, .sortMode = 0, .uploadNumLimit = 10,
                                   .uploadSizeLimit = 1024};
            fake_reply(fd, cmd, id, 0, &shadpb_BoardInfo_desc, &bi);
        }
        return true;
    }
    case SHADNET_CMD_RECORD_SCORE: {
        ShadPb_RecordScoreRequest r;
        if (!fake_score_decode(s, payload, len, &shadpb_RecordScoreRequest_desc, &r, a, NULL, NULL)) return false;
        if (s->has_score && r.score <= s->best_score) {
            fake_reply(fd, cmd, id, SHADNET_ERR_SCORE_NOT_BEST, NULL, NULL);
            return true;
        }
        s->has_score = true;
        s->best_score = r.score;
        s->score_pc_id = r.pcId;
        snprintf(s->score_comment, sizeof(s->score_comment), "%.*s", (int)r.comment.len, (const char *)r.comment.data);
        s->score_info_len = r.data.len < sizeof(s->score_info) ? r.data.len : sizeof(s->score_info);
        memcpy(s->score_info, r.data.data, s->score_info_len);
        const uint8_t rank[4] = {1, 0, 0, 0};
        fake_reply_raw(fd, cmd, id, 0, rank, sizeof(rank));
        return true;
    }
    case SHADNET_CMD_RECORD_SCORE_DATA: {
        ShadPb_RecordScoreGameDataRequest r;
        if (!fake_score_decode(s, payload, len, &shadpb_RecordScoreGameDataRequest_desc, &r, a, &tail, &tail_len) ||
            tail_len > sizeof(s->game_data)) {
            return false;
        }
        memcpy(s->game_data, tail, tail_len);
        s->game_data_len = tail_len;
        fake_reply_raw(fd, cmd, id, 0, NULL, 0);
        return true;
    }
    case SHADNET_CMD_GET_SCORE_DATA:
    case SHADNET_CMD_GET_SCORE_GAME_DATA_BY_ACC_ID: {
        bool mine;
        if (cmd == SHADNET_CMD_GET_SCORE_DATA) {
            ShadPb_GetScoreGameDataRequest r;
            if (!fake_score_decode(s, payload, len, &shadpb_GetScoreGameDataRequest_desc, &r, a, NULL, NULL)) return false;
            mine = r.npId.len == strlen(s->login_npid) && memcmp(r.npId.data, s->login_npid, r.npId.len) == 0;
        } else {
            ShadPb_GetScoreGameDataByAccountIdRequest r;
            if (!fake_score_decode(s, payload, len, &shadpb_GetScoreGameDataByAccountIdRequest_desc, &r, a, NULL, NULL)) {
                return false;
            }
            mine = (uint64_t)r.accountId == s->account_id;
        }
        if (!mine || s->game_data_len == 0) {
            fake_reply(fd, cmd, id, SHADNET_ERR_NOT_FOUND, NULL, NULL);
        } else {
            uint8_t body[4 + sizeof(s->game_data)];
            fake_put32(body, (uint32_t)s->game_data_len);
            memcpy(body + 4, s->game_data, s->game_data_len);
            fake_reply_raw(fd, cmd, id, 0, body, 4 + s->game_data_len);
        }
        return true;
    }
    case SHADNET_CMD_GET_SCORE_RANGE: {
        ShadPb_GetScoreRangeRequest r;
        if (!fake_score_decode(s, payload, len, &shadpb_GetScoreRangeRequest_desc, &r, a, NULL, NULL)) return false;
        const bool slot = s->has_score && r.startRank == 1;
        fake_ranking_reply(s, fd, cmd, id, &slot, slot ? 1 : 0, r.withComment, r.withGameInfo);
        return true;
    }
    case SHADNET_CMD_GET_SCORE_FRIENDS: {
        ShadPb_GetScoreFriendsRequest r;
        if (!fake_score_decode(s, payload, len, &shadpb_GetScoreFriendsRequest_desc, &r, a, NULL, NULL)) return false;
        const bool slot = s->has_score && r.includeSelf;
        fake_ranking_reply(s, fd, cmd, id, &slot, slot ? 1 : 0, r.withComment, r.withGameInfo);
        return true;
    }
    case SHADNET_CMD_GET_SCORE_NPID: {
        ShadPb_GetScoreNpIdRequest r;
        bool slots[8];
        if (!fake_score_decode(s, payload, len, &shadpb_GetScoreNpIdRequest_desc, &r, a, NULL, NULL) ||
            r.npids_count > 8) {
            return false;
        }
        for (uint32_t i = 0; i < r.npids_count; i++) {
            slots[i] = s->has_score && r.npids[i].npid.len == strlen(s->login_npid) &&
                       memcmp(r.npids[i].npid.data, s->login_npid, r.npids[i].npid.len) == 0;
        }
        fake_ranking_reply(s, fd, cmd, id, slots, r.npids_count, r.withComment, r.withGameInfo);
        return true;
    }
    case SHADNET_CMD_GET_SCORE_ACCOUNT_ID: {
        ShadPb_GetScoreAccountIdRequest r;
        bool slots[8];
        if (!fake_score_decode(s, payload, len, &shadpb_GetScoreAccountIdRequest_desc, &r, a, NULL, NULL) ||
            r.ids_count > 8) {
            return false;
        }
        for (uint32_t i = 0; i < r.ids_count; i++) slots[i] = s->has_score && (uint64_t)r.ids[i].accountId == s->account_id;
        fake_ranking_reply(s, fd, cmd, id, slots, r.ids_count, r.withComment, r.withGameInfo);
        return true;
    }
    default: return true; // not a score command
    }
}

static void fake_session(FakeShadNet *s, int fd) {
    uint8_t payload[1024];
    size_t len;
    uint16_t cmd;
    uint64_t id;
    ShadPbArena arena = {0};
    // The probe hangs up after the ServerInfo packet: no request arrives.
    if (!fake_request(fd, &cmd, &id, payload, sizeof(payload), &len)) return;
    ShadPb_LoginRequest login;
    if (cmd != SHADNET_CMD_LOGIN || !fake_decode(payload, len, &shadpb_LoginRequest_desc, &login, &arena)) {
        s->failures++;
        return;
    }
    const size_t n = login.npid.len < sizeof(s->login_npid) - 1 ? login.npid.len : sizeof(s->login_npid) - 1;
    memcpy(s->login_npid, login.npid.data, n);
    s->login_npid[n] = '\0';
    ShadPb_LoginReply reply = {.user_id = s->account_id, .avatar_url = shadpb_str("http://avatar")};
    fake_reply(fd, SHADNET_CMD_LOGIN, id, 0, &shadpb_LoginReply_desc, &reply);
    s->sessions++;
    while (fake_request(fd, &cmd, &id, payload, sizeof(payload), &len)) {
        if (cmd == SHADNET_CMD_GET_TOKEN) {
            ShadPb_GetTokenReply token = {.token = shadpb_str("token")};
            fake_reply(fd, cmd, id, 0, &shadpb_GetTokenReply_desc, &token);
        } else if (cmd == SHADNET_CMD_GET_SERVER_FEATURES) {
            ShadPb_ServerFeaturesReply features = {0};
            fake_reply(fd, cmd, id, 0, &shadpb_ServerFeaturesReply_desc, &features);
        } else if (cmd == SHADNET_CMD_LOOKUP_ONLINE_ID) {
            ShadPb_LookupOnlineIdRequest req;
            if (!fake_decode(payload, len, &shadpb_LookupOnlineIdRequest_desc, &req, &arena)) {
                fake_reply(fd, cmd, id, SHADNET_ERR_MALFORMED, NULL, NULL);
            } else if (s->lookup_npid && req.npid.len == strlen(s->lookup_npid) &&
                       strncasecmp((const char *)req.npid.data, s->lookup_npid, req.npid.len) == 0) {
                ShadPb_LookupOnlineIdReply r = {.account_id = s->lookup_account, .npid = shadpb_str(s->lookup_npid)};
                fake_reply(fd, cmd, id, 0, &shadpb_LookupOnlineIdReply_desc, &r);
            } else {
                fake_reply(fd, cmd, id, SHADNET_ERR_NOT_FOUND, NULL, NULL);
            }
        } else if (cmd == SHADNET_CMD_GET_AUTHORIZATION_CODE) {
            ShadPb_GetAuthorizationCodeRequest req;
            if (!fake_decode(payload, len, &shadpb_GetAuthorizationCodeRequest_desc, &req, &arena)) {
                s->failures++;
                continue;
            }
            snprintf(s->auth_client_id, sizeof(s->auth_client_id), "%.*s", (int)req.client_id.len,
                     (const char *)req.client_id.data);
            snprintf(s->auth_scope, sizeof(s->auth_scope), "%.*s", (int)req.scope.len, (const char *)req.scope.data);
            while (s->hold_replies) usleep(1000);
            if (s->auth_error) {
                fake_reply(fd, cmd, id, s->auth_error, NULL, NULL);
            } else {
                ShadPb_GetAuthorizationCodeReply r = {.code = shadpb_str(s->auth_code), .expires_in = 300};
                fake_reply(fd, cmd, id, 0, &shadpb_GetAuthorizationCodeReply_desc, &r);
            }
        } else if (cmd >= SHADNET_CMD_GET_BOARD_INFOS && cmd <= SHADNET_CMD_GET_SCORE_GAME_DATA_BY_ACC_ID) {
            if (!fake_score_request(s, fd, cmd, id, payload, len, &arena)) s->failures++;
        }
        // SetClientVersion and other requests get no reply.
    }
    shadpb_arena_free(&arena);
}

static void *fake_main(void *arg) {
    FakeShadNet *s = arg;
    for (;;) {
        const int fd = accept(s->listen_fd, NULL, NULL);
        if (fd < 0) return NULL;
        if (s->stopping) {
            close(fd);
            return NULL;
        }
        uint8_t ver[4];
        fake_put32(ver, SHADNET_PROTOCOL_VERSION);
        fake_send(fd, SHADNET_PACKET_SERVER_INFO, 0, 0, ver, sizeof(ver));
        fake_session(s, fd);
        close(fd);
    }
}

static int fake_shadnet_start(FakeShadNet *s) {
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) return 1;
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (bind(s->listen_fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s->listen_fd, 4) != 0) return 1;
    socklen_t len = sizeof(a);
    getsockname(s->listen_fd, (struct sockaddr *)&a, &len);
    s->port = ntohs(a.sin_port);
    return pthread_create(&s->thread, NULL, fake_main, s) != 0;
}

// Call once the clients hung up: wakes the accept loop with a connection of its own.
static void fake_shadnet_stop(FakeShadNet *s) {
    s->stopping = true;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(s->port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (fd >= 0) connect(fd, (struct sockaddr *)&a, sizeof(a));
    pthread_join(s->thread, NULL);
    if (fd >= 0) close(fd);
    close(s->listen_fd);
}

#endif
