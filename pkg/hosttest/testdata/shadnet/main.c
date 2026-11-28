// SPDX-License-Identifier: GPL-2.0-or-later

// The shadNet protobuf codec and client.
//
// codec: encodings checked against bytes worked out by hand from the protobuf encoding
// specification (varints, sign-extended negative int32, packed repeated fields, nested
// messages), decoding of the packed and unpacked forms, unknown fields, and malformed input.
//
// The client cases run a local server that speaks shadPS4's shadNet wire protocol
// (src/shadnet/client.cpp): ServerInfo handshake, Login, GetToken, GetServerFeatures and
// SetClientVersion, then notifications and an asynchronous request.

#include "shadnet_client.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

static bool bytes_eq(ShadPbBytes b, const char *s) {
    return b.len == strlen(s) && memcmp(b.data, s, b.len) == 0;
}

// Encodes msg and compares with want.
static bool encodes_to(const ShadPbMessageDesc *desc, const void *msg, const uint8_t *want, size_t want_len) {
    uint8_t buf[256];
    const size_t n = shadpb_encoded_size(desc, msg);
    if (n != want_len || n > sizeof(buf)) return false;
    return shadpb_encode(desc, msg, buf) == n && memcmp(buf, want, n) == 0;
}

static int case_codec(void) {
    ShadPbArena arena = {0};

    // Empty message: proto3 omits default values.
    ShadPb_LoginRequest empty = {0};
    CHECK(shadpb_encoded_size(&shadpb_LoginRequest_desc, &empty) == 0);

    // string field 1 and bool field 6.
    ShadPb_LoginRequest login = {.npid = shadpb_str("abc"), .appear_offline = true};
    static const uint8_t login_bytes[] = {0x0A, 0x03, 'a', 'b', 'c', 0x30, 0x01};
    CHECK(encodes_to(&shadpb_LoginRequest_desc, &login, login_bytes, sizeof(login_bytes)));

    // The specification's example: varint 150 is 96 01.
    ShadPb_GetTokenReply token = {.user_id = 150};
    static const uint8_t token_bytes[] = {0x10, 0x96, 0x01};
    CHECK(encodes_to(&shadpb_GetTokenReply_desc, &token, token_bytes, sizeof(token_bytes)));

    // Repeated message with a negative int32 (ten-byte sign-extended varint).
    ShadPb_TrophyEntry entry = {.trophyId = -1, .timestamp = 150};
    ShadPb_SyncTrophiesRequest sync = {.trophies = &entry, .trophies_count = 1};
    static const uint8_t sync_bytes[] = {0x0A, 0x0E, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                         0xFF, 0xFF, 0xFF, 0x01, 0x10, 0x96, 0x01};
    CHECK(encodes_to(&shadpb_SyncTrophiesRequest_desc, &sync, sync_bytes, sizeof(sync_bytes)));

    // Packed repeated uint32 (field 6): 32 03 01 AC 02.
    uint32_t ids[2] = {1, 300};
    ShadPb_NotifyRoomMessage msg = {.ctx_id = 1, .dst_member_ids = ids, .dst_member_ids_count = 2};
    static const uint8_t msg_bytes[] = {0x08, 0x01, 0x32, 0x03, 0x01, 0xAC, 0x02};
    CHECK(encodes_to(&shadpb_NotifyRoomMessage_desc, &msg, msg_bytes, sizeof(msg_bytes)));

    // Decoding: packed and unpacked forms give the same values; unknown fields (a varint, a
    // fixed64, a fixed32 and a length) are skipped; strings are NUL-terminated copies.
    static const uint8_t mixed[] = {0x08, 0x07, 0x30, 0x05, 0x32, 0x02, 0x06, 0x07, 0x30, 0xAC, 0x02,
                                    0x98, 0x06, 0x01,                                     // field 99 varint
                                    0x99, 0x06, 1, 2, 3, 4, 5, 6, 7, 8,                   // field 99 fixed64
                                    0x9D, 0x06, 1, 2, 3, 4,                               // field 99 fixed32
                                    0x9A, 0x06, 0x01, 0x00,                               // field 99 bytes
                                    0x3A, 0x02, 'h', 'i'};
    ShadPb_NotifyRoomMessage d;
    CHECK(shadpb_decode(&shadpb_NotifyRoomMessage_desc, mixed, sizeof(mixed), &d, &arena));
    CHECK(d.ctx_id == 7 && d.dst_member_ids_count == 4);
    CHECK(d.dst_member_ids[0] == 5 && d.dst_member_ids[1] == 6 && d.dst_member_ids[2] == 7 &&
          d.dst_member_ids[3] == 300);
    CHECK(bytes_eq(d.src_npid, "hi") && d.src_npid.data[2] == '\0');

    // Nested messages round-trip.
    ShadPb_SyncTrophiesRequest back;
    CHECK(shadpb_decode(&shadpb_SyncTrophiesRequest_desc, sync_bytes, sizeof(sync_bytes), &back, &arena));
    CHECK(back.trophies_count == 1 && back.trophies[0].trophyId == -1 && back.trophies[0].timestamp == 150);

    // Malformed: truncated varint, length past the end, a group wire type, field number 0,
    // and a string field sent as a varint.
    static const uint8_t bad1[] = {0x08, 0x80};
    static const uint8_t bad2[] = {0x3A, 0x05, 'a'};
    static const uint8_t bad3[] = {0x0B};
    static const uint8_t bad4[] = {0x00, 0x01};
    static const uint8_t bad5[] = {0x38, 0x01};
    CHECK(!shadpb_decode(&shadpb_NotifyRoomMessage_desc, bad1, sizeof(bad1), &d, &arena));
    CHECK(!shadpb_decode(&shadpb_NotifyRoomMessage_desc, bad2, sizeof(bad2), &d, &arena));
    CHECK(!shadpb_decode(&shadpb_NotifyRoomMessage_desc, bad3, sizeof(bad3), &d, &arena));
    CHECK(!shadpb_decode(&shadpb_NotifyRoomMessage_desc, bad4, sizeof(bad4), &d, &arena));
    CHECK(!shadpb_decode(&shadpb_NotifyRoomMessage_desc, bad5, sizeof(bad5), &d, &arena));
    shadpb_arena_free(&arena);
    printf("PASS codec\n");
    return 0;
}

//=============================================================================
// A local shadNet server
//=============================================================================

typedef struct {
    int listen_fd;
    uint16_t port;
    uint32_t protocol_version;
    uint8_t login_error; // the Login reply's error byte
    // Filled by the server thread.
    char npid[32], password[32], title_id[32], client_version[64];
    bool appear_offline;
    int failures;
    char failure[256];
} Server;

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_all(int fd, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        const ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static void send_packet(int fd, uint8_t type, uint16_t cmd, uint64_t id, const uint8_t *payload, size_t len) {
    uint8_t hdr[SHADNET_HEADER_SIZE];
    hdr[0] = type;
    hdr[1] = (uint8_t)cmd;
    hdr[2] = (uint8_t)(cmd >> 8);
    put32(hdr + 3, (uint32_t)(SHADNET_HEADER_SIZE + len));
    for (int i = 0; i < 8; i++) hdr[7 + i] = (uint8_t)(id >> (8 * i));
    send(fd, hdr, sizeof(hdr), 0);
    if (len) send(fd, payload, len, 0);
}

// Sends a reply: error byte, then a size-prefixed proto blob (when desc is set).
static void send_reply(int fd, uint16_t cmd, uint64_t id, uint8_t err, const ShadPbMessageDesc *desc,
                       const void *msg) {
    uint8_t buf[512];
    buf[0] = err;
    size_t len = 1;
    if (desc) {
        const size_t n = shadpb_encode(desc, msg, buf + 5);
        put32(buf + 1, (uint32_t)n);
        len += 4 + n;
    }
    send_packet(fd, SHADNET_PACKET_REPLY, cmd, id, buf, len);
}

static void send_notification(int fd, uint16_t type, const ShadPbMessageDesc *desc, const void *msg) {
    uint8_t buf[512];
    const size_t n = shadpb_encode(desc, msg, buf + 4);
    put32(buf, (uint32_t)n);
    send_packet(fd, SHADNET_PACKET_NOTIFICATION, type, 0, buf, n + 4);
}

typedef struct {
    uint8_t type;
    uint16_t cmd;
    uint64_t id;
    uint8_t payload[1024];
    uint32_t len;
} RxPacket;

static bool read_packet(int fd, RxPacket *p) {
    uint8_t hdr[SHADNET_HEADER_SIZE];
    if (!read_all(fd, hdr, sizeof(hdr))) return false;
    p->type = hdr[0];
    p->cmd = (uint16_t)(hdr[1] | (hdr[2] << 8));
    p->id = 0;
    for (int i = 0; i < 8; i++) p->id |= (uint64_t)hdr[7 + i] << (8 * i);
    const uint32_t total = get32(hdr + 3);
    if (total < SHADNET_HEADER_SIZE || total - SHADNET_HEADER_SIZE > sizeof(p->payload)) return false;
    p->len = total - SHADNET_HEADER_SIZE;
    return read_all(fd, p->payload, p->len);
}

static void fail(Server *s, const char *what) {
    s->failures++;
    snprintf(s->failure, sizeof(s->failure), "%s", what);
}

static void copy_str(char *dst, size_t cap, ShadPbBytes b) {
    const size_t n = b.len < cap - 1 ? b.len : cap - 1;
    memcpy(dst, b.data, n);
    dst[n] = '\0';
}

// Decodes a request's size-prefixed proto payload (after skip bytes).
static bool decode_request(const RxPacket *p, size_t skip, const ShadPbMessageDesc *desc, void *msg,
                           ShadPbArena *arena) {
    if (p->len < skip + 4) return false;
    const uint32_t n = get32(p->payload + skip);
    return p->len == skip + 4 + n && shadpb_decode(desc, p->payload + skip + 4, n, msg, arena);
}

static void *server_main(void *arg) {
    Server *s = arg;
    const int fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0) {
        fail(s, "accept");
        return NULL;
    }
    ShadPbArena arena = {0};
    uint8_t ver[4];
    put32(ver, s->protocol_version);
    send_packet(fd, SHADNET_PACKET_SERVER_INFO, 0, 0, ver, sizeof(ver));
    RxPacket p;
    if (s->protocol_version != SHADNET_PROTOCOL_VERSION) {
        // The client hangs up.
        if (read_packet(fd, &p)) fail(s, "client sent a packet after a version mismatch");
        close(fd);
        return NULL;
    }
    ShadPb_LoginRequest login;
    if (!read_packet(fd, &p) || p.type != SHADNET_PACKET_REQUEST || p.cmd != SHADNET_CMD_LOGIN ||
        !decode_request(&p, 0, &shadpb_LoginRequest_desc, &login, &arena)) {
        fail(s, "login request");
        close(fd);
        return NULL;
    }
    copy_str(s->npid, sizeof(s->npid), login.npid);
    copy_str(s->password, sizeof(s->password), login.password);
    copy_str(s->title_id, sizeof(s->title_id), login.title_id);
    s->appear_offline = login.appear_offline;
    if (s->login_error != SHADNET_ERR_NO_ERROR) {
        send_reply(fd, SHADNET_CMD_LOGIN, p.id, s->login_error, NULL, NULL);
        if (read_packet(fd, &p)) fail(s, "client kept the session after a rejected login");
        close(fd);
        shadpb_arena_free(&arena);
        return NULL;
    }
    ShadPb_FriendEntry friends[2] = {{.npid = shadpb_str("alice"), .online = true}, {.npid = shadpb_str("bob")}};
    ShadPb_LoginReply reply = {.avatar_url = shadpb_str("http://avatar/x.png"), .user_id = 0x1122334455667788ull,
                               .friends = friends, .friends_count = 2};
    send_reply(fd, SHADNET_CMD_LOGIN, p.id, 0, &shadpb_LoginReply_desc, &reply);

    if (!read_packet(fd, &p) || p.cmd != SHADNET_CMD_GET_TOKEN || p.len != 0) {
        fail(s, "GetToken");
    } else {
        ShadPb_GetTokenReply token = {.token = shadpb_str("bearer-123"), .user_id = 1};
        send_reply(fd, SHADNET_CMD_GET_TOKEN, p.id, 0, &shadpb_GetTokenReply_desc, &token);
    }
    if (!read_packet(fd, &p) || p.cmd != SHADNET_CMD_GET_SERVER_FEATURES) {
        fail(s, "GetServerFeatures");
    } else {
        ShadPb_ServerFeaturesReply features = {.matching2_enabled = true};
        send_reply(fd, SHADNET_CMD_GET_SERVER_FEATURES, p.id, 0, &shadpb_ServerFeaturesReply_desc, &features);
    }
    ShadPb_SetClientVersionRequest version;
    if (!read_packet(fd, &p) || p.cmd != SHADNET_CMD_SET_CLIENT_VERSION ||
        !decode_request(&p, 0, &shadpb_SetClientVersionRequest_desc, &version, &arena)) {
        fail(s, "SetClientVersion");
    } else {
        copy_str(s->client_version, sizeof(s->client_version), version.version);
    }

    // Notifications: a friend coming online and a WebAPI push event.
    ShadPb_NotifyFriendNew fn = {.npid = shadpb_str("carol"), .online = true};
    send_notification(fd, SHADNET_NOTIFY_FRIEND_NEW, &shadpb_NotifyFriendNew_desc, &fn);
    uint8_t push[128];
    size_t off = 0;
    const char *fields[] = {"svc", NULL, "type", "{}", "alice", "bob"};
    for (int i = 0; i < 6; i++) {
        if (!fields[i]) {
            put32(push + off, 42); // npServiceLabel
            off += 4;
            continue;
        }
        put32(push + off, (uint32_t)strlen(fields[i]));
        memcpy(push + off + 4, fields[i], strlen(fields[i]));
        off += 4 + strlen(fields[i]);
    }
    put32(push + off, 1); // one extended-data pair
    off += 4;
    put32(push + off, 1);
    push[off + 4] = 'k';
    put32(push + off + 5, 1);
    push[off + 9] = 'v';
    off += 10;
    for (int i = 0; i < 16; i++) push[off + i] = (uint8_t)(i < 8 ? (i == 0 ? 7 : 0) : (i == 8 ? 9 : 0));
    off += 16;
    send_packet(fd, SHADNET_PACKET_NOTIFICATION, SHADNET_NOTIFY_WEBAPI_PUSH_EVENT, 0, push, off);

    // An asynchronous request (LookupOnlineId) gets a reply with a body.
    ShadPb_LookupOnlineIdRequest lookup;
    if (!read_packet(fd, &p) || p.cmd != SHADNET_CMD_LOOKUP_ONLINE_ID ||
        !decode_request(&p, 0, &shadpb_LookupOnlineIdRequest_desc, &lookup, &arena) ||
        !bytes_eq(lookup.npid, "dave")) {
        fail(s, "LookupOnlineId");
    } else {
        ShadPb_LookupOnlineIdReply r = {.account_id = 99, .npid = shadpb_str("Dave")};
        send_reply(fd, SHADNET_CMD_LOOKUP_ONLINE_ID, p.id, 0, &shadpb_LookupOnlineIdReply_desc, &r);
    }
    // A trophy unlock carries the communication id before the proto blob.
    ShadPb_UnlockTrophyRequest unlock;
    if (!read_packet(fd, &p) || p.cmd != SHADNET_CMD_UNLOCK_TROPHY || p.len < 12 ||
        memcmp(p.payload, "NPWR01234_00", 12) != 0 ||
        !decode_request(&p, 12, &shadpb_UnlockTrophyRequest_desc, &unlock, &arena) || unlock.trophyId != 3 ||
        unlock.timestamp != 1234) {
        fail(s, "UnlockTrophy");
    }
    // The Appear-Offline toggle, acknowledged so the client knows both arrived.
    ShadPb_SetAppearOfflineRequest offline;
    if (!read_packet(fd, &p) || p.cmd != SHADNET_CMD_SET_APPEAR_OFFLINE ||
        !decode_request(&p, 0, &shadpb_SetAppearOfflineRequest_desc, &offline, &arena) || offline.appear_offline) {
        fail(s, "SetAppearOffline");
    }
    send_reply(fd, SHADNET_CMD_SET_APPEAR_OFFLINE, p.id, 0, NULL, NULL);
    // Wait for the client to hang up.
    while (read_packet(fd, &p)) {
    }
    close(fd);
    shadpb_arena_free(&arena);
    return NULL;
}

static int server_start(Server *s, pthread_t *t) {
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) return 1;
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (bind(s->listen_fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s->listen_fd, 1) != 0) return 1;
    socklen_t len = sizeof(a);
    getsockname(s->listen_fd, (struct sockaddr *)&a, &len);
    s->port = ntohs(a.sin_port);
    return pthread_create(t, NULL, server_main, s) != 0;
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int login_calls;
    uint8_t login_error;
    char friend_new[16];
    bool push_seen;
    uint32_t push_label;
    uint64_t push_from_account;
    bool push_extd_ok;
    uint64_t lookup_pkt;
    uint64_t lookup_account;
    uint64_t offline_ack;
    bool reader_thread_flag;
} Events;

static void on_login(void *u, uint8_t err, const ShadPb_LoginReply *r) {
    Events *e = u;
    pthread_mutex_lock(&e->mutex);
    e->login_calls++;
    e->login_error = err;
    e->reader_thread_flag = shadnet_client_on_reader_thread();
    (void)r;
    pthread_mutex_unlock(&e->mutex);
}

static void on_friend_new(void *u, const ShadPb_NotifyFriendNew *n) {
    Events *e = u;
    pthread_mutex_lock(&e->mutex);
    copy_str(e->friend_new, sizeof(e->friend_new), n->npid);
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->mutex);
}

static void on_push(void *u, const ShadNetWebApiPushEvent *n) {
    Events *e = u;
    pthread_mutex_lock(&e->mutex);
    e->push_seen = bytes_eq(n->np_service_name, "svc") && bytes_eq(n->data_type, "type") && bytes_eq(n->data, "{}") &&
                   bytes_eq(n->from_npid, "alice") && bytes_eq(n->to_npid, "bob");
    e->push_label = n->np_service_label;
    e->push_extd_ok = n->extd_data_count == 1 && bytes_eq(n->extd_data[0].key, "k") &&
                      bytes_eq(n->extd_data[0].value, "v") && n->to_account_id == 9;
    e->push_from_account = n->from_account_id;
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->mutex);
}

static void on_async(void *u, uint16_t cmd, uint64_t id, uint8_t err, const uint8_t *body, size_t len) {
    Events *e = u;
    ShadPbArena arena = {0};
    ShadPb_LookupOnlineIdReply r;
    pthread_mutex_lock(&e->mutex);
    if (cmd == SHADNET_CMD_LOOKUP_ONLINE_ID && err == 0 && len >= 4 &&
        shadpb_decode(&shadpb_LookupOnlineIdReply_desc, body + 4, len - 4, &r, &arena)) {
        e->lookup_pkt = id;
        e->lookup_account = r.account_id;
    }
    if (cmd == SHADNET_CMD_SET_APPEAR_OFFLINE && err == 0 && len == 0) e->offline_ack = id;
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->mutex);
    shadpb_arena_free(&arena);
}

static ShadNetLogin login_for(const Server *s) {
    ShadNetLogin l = {.host = "127.0.0.1", .port = s->port, .npid = "player", .password = "secret",
                      .title_id = "CUSA00001", .title_name = "Test", .client_version = "PS4Recomp test"};
    return l;
}

static int case_client_login(void) {
    Server s = {.protocol_version = SHADNET_PROTOCOL_VERSION};
    pthread_t st;
    CHECK(server_start(&s, &st) == 0);
    Events e = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
    ShadNetCallbacks cb = {.user = &e, .login_result = on_login, .friend_new = on_friend_new,
                           .webapi_push_event = on_push, .async_reply = on_async};
    ShadNetClient *c = shadnet_client_create(&cb);
    shadnet_client_set_appear_offline(c, true); // recorded, carried by the login
    const ShadNetLogin l = login_for(&s);
    shadnet_client_start(c, &l);
    CHECK(shadnet_client_wait_for_connection(c) == SHADNET_STATE_OK);
    CHECK(shadnet_client_wait_for_authenticated(c) == SHADNET_STATE_OK);
    CHECK(shadnet_client_is_authenticated(c));
    CHECK(shadnet_client_user_id(c) == 0x1122334455667788ull);
    CHECK(shadnet_client_matching2_enabled(c) && !shadnet_client_trophies_enabled(c));
    CHECK(shadnet_client_server_protocol_version(c) == SHADNET_PROTOCOL_VERSION);
    CHECK(shadnet_client_addr_server(c) == htonl(INADDR_LOOPBACK));
    char buf[64];
    CHECK(shadnet_client_avatar_url(c, buf, sizeof(buf)) == 19 && strcmp(buf, "http://avatar/x.png") == 0);
    CHECK(shadnet_client_bearer_token(c, buf, sizeof(buf)) == 10 && strcmp(buf, "bearer-123") == 0);
    CHECK(shadnet_client_bearer_token(c, buf, 4) == 10 && strcmp(buf, "bea") == 0);
    CHECK(shadnet_client_num_friends(c) == 2);
    CHECK(shadnet_client_friend_npid(c, 1, buf, sizeof(buf)) && strcmp(buf, "bob") == 0);
    CHECK(!shadnet_client_friend_npid(c, 2, buf, sizeof(buf)));
    CHECK(!shadnet_client_on_reader_thread());

    // Notifications reach the callbacks.
    pthread_mutex_lock(&e.mutex);
    while (!e.friend_new[0] || !e.push_label) pthread_cond_wait(&e.cond, &e.mutex);
    pthread_mutex_unlock(&e.mutex);
    CHECK(strcmp(e.friend_new, "carol") == 0);
    CHECK(e.push_seen && e.push_label == 42 && e.push_extd_ok && e.push_from_account == 7);
    CHECK(e.login_calls == 1 && e.login_error == 0 && e.reader_thread_flag);

    // An asynchronous request and its reply.
    const uint64_t id = shadnet_client_lookup_online_id(c, "dave");
    CHECK(id != 0);
    pthread_mutex_lock(&e.mutex);
    while (e.lookup_pkt == 0) pthread_cond_wait(&e.cond, &e.mutex);
    pthread_mutex_unlock(&e.mutex);
    CHECK(e.lookup_pkt == id && e.lookup_account == 99);
    CHECK(shadnet_client_unlock_trophy(c, "NPWR01234_00", 3, 1234) != 0);
    // Once authenticated, the Appear-Offline toggle is sent.
    const uint64_t offline_id = shadnet_client_set_appear_offline(c, false);
    CHECK(offline_id != 0);
    pthread_mutex_lock(&e.mutex);
    while (e.offline_ack == 0) pthread_cond_wait(&e.cond, &e.mutex);
    pthread_mutex_unlock(&e.mutex);
    CHECK(e.offline_ack == offline_id);

    shadnet_client_destroy(c);
    pthread_join(st, NULL);
    close(s.listen_fd);
    if (s.failures) fprintf(stderr, "server: %s\n", s.failure);
    CHECK(s.failures == 0);
    CHECK(strcmp(s.npid, "player") == 0 && strcmp(s.password, "secret") == 0);
    CHECK(strcmp(s.title_id, "CUSA00001") == 0 && s.appear_offline);
    CHECK(strcmp(s.client_version, "PS4Recomp test") == 0);
    printf("PASS client-login\n");
    return 0;
}

static int case_client_rejected(void) {
    Server s = {.protocol_version = SHADNET_PROTOCOL_VERSION, .login_error = SHADNET_ERR_LOGIN_INVALID_PASSWORD};
    pthread_t st;
    CHECK(server_start(&s, &st) == 0);
    Events e = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
    ShadNetCallbacks cb = {.user = &e, .login_result = on_login};
    ShadNetClient *c = shadnet_client_create(&cb);
    const ShadNetLogin l = login_for(&s);
    shadnet_client_start(c, &l);
    CHECK(shadnet_client_wait_for_connection(c) == SHADNET_STATE_OK);
    CHECK(shadnet_client_wait_for_authenticated(c) == SHADNET_STATE_FAILURE_PASSWORD);
    CHECK(!shadnet_client_is_authenticated(c));
    pthread_join(st, NULL); // the client hung up
    CHECK(e.login_calls == 1 && e.login_error == SHADNET_ERR_LOGIN_INVALID_PASSWORD);
    shadnet_client_destroy(c);
    close(s.listen_fd);
    CHECK(s.failures == 0);
    printf("PASS client-rejected\n");
    return 0;
}

static int case_client_version(void) {
    Server s = {.protocol_version = SHADNET_PROTOCOL_VERSION + 1};
    pthread_t st;
    CHECK(server_start(&s, &st) == 0);
    ShadNetClient *c = shadnet_client_create(NULL);
    const ShadNetLogin l = login_for(&s);
    shadnet_client_start(c, &l);
    // A protocol mismatch is not retried.
    CHECK(shadnet_client_wait_for_connection(c) == SHADNET_STATE_FAILURE_PROTOCOL);
    CHECK(shadnet_client_server_protocol_version(c) == SHADNET_PROTOCOL_VERSION + 1);
    pthread_join(st, NULL);
    shadnet_client_destroy(c);
    close(s.listen_fd);
    CHECK(s.failures == 0);

    // Nothing listening: the connection fails after its retries (stopped early here).
    ShadNetClient *d = shadnet_client_create(NULL);
    ShadNetLogin none = login_for(&s);
    shadnet_client_start(d, &none);
    shadnet_client_stop(d);
    CHECK(!shadnet_client_is_connected(d));
    shadnet_client_destroy(d);
    printf("PASS client-version\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "codec") == 0) return case_codec();
    if (strcmp(argv[1], "client-login") == 0) return case_client_login();
    if (strcmp(argv[1], "client-rejected") == 0) return case_client_rejected();
    if (strcmp(argv[1], "client-version") == 0) return case_client_version();
    return 2;
}
