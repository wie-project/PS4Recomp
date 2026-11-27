// SPDX-License-Identifier: GPL-2.0-or-later

// shadNet client, following shadPS4 src/shadnet/client.cpp; see shadnet_client.h.

#include "shadnet_client.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// Friend NP ids are kept up to this length (online ids have at most 16 characters).
#define SHADNET_NPID_CAP 64

typedef struct Packet {
    struct Packet *next;
    size_t len;
    uint8_t data[];
} Packet;

typedef struct FriendEntry {
    char npid[SHADNET_NPID_CAP];
    bool online;
} FriendEntry;

// std::binary_semaphore: release makes it available (at most once), acquire takes it.
typedef struct BinarySem {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool available;
} BinarySem;

struct ShadNetClient {
    ShadNetCallbacks cb;

    // Login (copied at start).
    char *host;
    uint16_t port;
    char *npid;
    char *password;
    char *token;
    char *title_id;
    char *title_name;
    char *client_version;
    _Atomic bool appear_offline;

    // The socket. Other threads only shut it down (waking the reader / writer); it is
    // closed by the thread that owns it.
    pthread_mutex_t sock_mutex;
    int sock;

    _Atomic bool terminate;
    _Atomic bool connected;
    _Atomic bool authenticated;
    _Atomic int state;
    BinarySem sem_connected;
    BinarySem sem_authenticated;

    bool thread_connect_started, threads_io_started;
    pthread_t thread_connect, thread_reader, thread_writer;

    pthread_mutex_t send_direct_mutex;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    Packet *queue_head, *queue_tail;

    pthread_mutex_t data_mutex; // avatar URL, bearer token, friends
    char *avatar_url;
    char *bearer_token;
    _Atomic uint64_t user_id;
    FriendEntry *friends;
    uint32_t friend_count;

    _Atomic uint32_t addr_local;
    _Atomic uint32_t addr_server;
    _Atomic uint32_t server_protocol_version;
    _Atomic bool matching2_enabled;
    _Atomic bool trophies_enabled;
    _Atomic bool server_features_received;

    _Atomic uint64_t pkt_counter;
};

static _Thread_local bool t_reader_thread;

static void sem_init_b(BinarySem *s) {
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->available = false;
}

static void sem_release(BinarySem *s) {
    pthread_mutex_lock(&s->mutex);
    s->available = true;
    pthread_cond_broadcast(&s->cond);
    pthread_mutex_unlock(&s->mutex);
}

static void sem_acquire(BinarySem *s) {
    pthread_mutex_lock(&s->mutex);
    while (!s->available) pthread_cond_wait(&s->cond, &s->mutex);
    s->available = false;
    pthread_mutex_unlock(&s->mutex);
}

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put_le64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static char *dup_or_empty(const char *s) {
    return strdup(s ? s : "");
}

//=============================================================================
// Lifetime and accessors
//=============================================================================

ShadNetClient *shadnet_client_create(const ShadNetCallbacks *callbacks) {
    ShadNetClient *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (callbacks) c->cb = *callbacks;
    pthread_mutex_init(&c->sock_mutex, NULL);
    c->sock = -1;
    sem_init_b(&c->sem_connected);
    sem_init_b(&c->sem_authenticated);
    pthread_mutex_init(&c->send_direct_mutex, NULL);
    pthread_mutex_init(&c->queue_mutex, NULL);
    pthread_cond_init(&c->queue_cond, NULL);
    pthread_mutex_init(&c->data_mutex, NULL);
    atomic_store(&c->pkt_counter, 1);
    atomic_store(&c->state, SHADNET_STATE_OK);
    return c;
}

static void free_login(ShadNetClient *c) {
    free(c->host);
    free(c->npid);
    free(c->password);
    free(c->token);
    free(c->title_id);
    free(c->title_name);
    free(c->client_version);
    c->host = c->npid = c->password = c->token = c->title_id = c->title_name = c->client_version = NULL;
}

void shadnet_client_destroy(ShadNetClient *c) {
    if (!c) return;
    shadnet_client_stop(c);
    free_login(c);
    free(c->avatar_url);
    free(c->bearer_token);
    free(c->friends);
    while (c->queue_head) {
        Packet *p = c->queue_head;
        c->queue_head = p->next;
        free(p);
    }
    free(c);
}

bool shadnet_client_is_connected(const ShadNetClient *c) {
    return atomic_load(&c->connected);
}

bool shadnet_client_is_authenticated(const ShadNetClient *c) {
    return atomic_load(&c->authenticated);
}

ShadNetState shadnet_client_state(const ShadNetClient *c) {
    return (ShadNetState)atomic_load(&c->state);
}

uint64_t shadnet_client_user_id(const ShadNetClient *c) {
    return atomic_load(&c->user_id);
}

const char *shadnet_client_npid(const ShadNetClient *c) {
    return c->npid ? c->npid : "";
}

uint32_t shadnet_client_server_protocol_version(const ShadNetClient *c) {
    return atomic_load(&c->server_protocol_version);
}

uint32_t shadnet_client_addr_local(const ShadNetClient *c) {
    return atomic_load(&c->addr_local);
}

uint32_t shadnet_client_addr_server(const ShadNetClient *c) {
    return atomic_load(&c->addr_server);
}

bool shadnet_client_matching2_enabled(const ShadNetClient *c) {
    return atomic_load(&c->matching2_enabled);
}

bool shadnet_client_trophies_enabled(const ShadNetClient *c) {
    return atomic_load(&c->trophies_enabled);
}

bool shadnet_client_on_reader_thread(void) {
    return t_reader_thread;
}

static size_t copy_out(const char *s, char *out, size_t out_size) {
    const size_t n = s ? strlen(s) : 0;
    if (out_size) {
        const size_t k = n < out_size - 1 ? n : out_size - 1;
        if (k) memcpy(out, s, k);
        out[k] = '\0';
    }
    return n;
}

size_t shadnet_client_avatar_url(ShadNetClient *c, char *out, size_t out_size) {
    pthread_mutex_lock(&c->data_mutex);
    const size_t n = copy_out(c->avatar_url, out, out_size);
    pthread_mutex_unlock(&c->data_mutex);
    return n;
}

size_t shadnet_client_bearer_token(ShadNetClient *c, char *out, size_t out_size) {
    pthread_mutex_lock(&c->data_mutex);
    const size_t n = copy_out(c->bearer_token, out, out_size);
    pthread_mutex_unlock(&c->data_mutex);
    return n;
}

uint32_t shadnet_client_num_friends(ShadNetClient *c) {
    pthread_mutex_lock(&c->data_mutex);
    const uint32_t n = c->friend_count;
    pthread_mutex_unlock(&c->data_mutex);
    return n;
}

bool shadnet_client_friend_npid(ShadNetClient *c, uint32_t index, char *out, size_t out_size) {
    pthread_mutex_lock(&c->data_mutex);
    const bool ok = index < c->friend_count;
    if (ok) copy_out(c->friends[index].npid, out, out_size);
    pthread_mutex_unlock(&c->data_mutex);
    return ok;
}

ShadNetState shadnet_client_wait_for_connection(ShadNetClient *c) {
    if (atomic_load(&c->connected)) return SHADNET_STATE_OK;
    sem_acquire(&c->sem_connected);
    return atomic_load(&c->connected) ? SHADNET_STATE_OK : (ShadNetState)atomic_load(&c->state);
}

ShadNetState shadnet_client_wait_for_authenticated(ShadNetClient *c) {
    if (atomic_load(&c->authenticated)) return SHADNET_STATE_OK;
    sem_acquire(&c->sem_authenticated);
    return atomic_load(&c->authenticated) ? SHADNET_STATE_OK : (ShadNetState)atomic_load(&c->state);
}

//=============================================================================
// Socket
//=============================================================================

// DoDisconnect: ends the TCP session; the reader and writer see it fail.
static void disconnect(ShadNetClient *c) {
    pthread_mutex_lock(&c->sock_mutex);
    if (c->sock >= 0) shutdown(c->sock, SHUT_RDWR);
    pthread_mutex_unlock(&c->sock_mutex);
}

static void close_socket(ShadNetClient *c) {
    pthread_mutex_lock(&c->sock_mutex);
    if (c->sock >= 0) {
        shutdown(c->sock, SHUT_RDWR);
        close(c->sock);
        c->sock = -1;
    }
    pthread_mutex_unlock(&c->sock_mutex);
}

static bool recv_n(ShadNetClient *c, int fd, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        if (atomic_load(&c->terminate)) return false;
        const ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static bool send_all(ShadNetClient *c, const uint8_t *data, size_t len) {
    pthread_mutex_lock(&c->send_direct_mutex);
    const int fd = c->sock;
    size_t sent = 0;
    bool ok = fd >= 0;
    while (ok && sent < len) {
        const ssize_t r = send(fd, data + sent, len - sent, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) {
            fprintf(stderr, "[shadNet] send() failed: %s\n", strerror(errno));
            ok = false;
        } else {
            sent += (size_t)r;
        }
    }
    pthread_mutex_unlock(&c->send_direct_mutex);
    return ok;
}

static void set_recv_timeout(int fd, uint32_t ms) {
    struct timeval tv = {(time_t)(ms / 1000), (suseconds_t)((ms % 1000) * 1000)};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// DoConnect: TCP connect with a timeout, then the ServerInfo handshake.
static bool do_connect(ShadNetClient *c) {
    atomic_store(&c->state, SHADNET_STATE_OK);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port[8];
    snprintf(port, sizeof(port), "%u", (unsigned)c->port);
    if (getaddrinfo(c->host, port, &hints, &res) != 0 || !res) {
        fprintf(stderr, "[shadNet] DNS resolution failed for '%s'\n", c->host);
        atomic_store(&c->state, SHADNET_STATE_FAILURE_RESOLVE);
        return false;
    }
    const int fd = socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        freeaddrinfo(res);
        atomic_store(&c->state, SHADNET_STATE_FAILURE_CONNECT);
        return false;
    }
    // Published at once, so shadnet_client_stop can cut a connect in progress short.
    pthread_mutex_lock(&c->sock_mutex);
    c->sock = fd;
    pthread_mutex_unlock(&c->sock_mutex);
    const int nosigpipe = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
    const int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    const int cr = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    bool ok = false;
    if (cr == 0 || errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = {SHADNET_CONNECT_TIMEOUT_MS / 1000, (SHADNET_CONNECT_TIMEOUT_MS % 1000) * 1000};
        if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            ok = err == 0;
        }
    }
    if (!ok) {
        fprintf(stderr, "[shadNet] connect() timed out or failed for %s:%u\n", c->host, (unsigned)c->port);
        close_socket(c);
        atomic_store(&c->state, SHADNET_STATE_FAILURE_CONNECT);
        return false;
    }
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);

    struct sockaddr_in a;
    socklen_t alen = sizeof(a);
    if (getsockname(fd, (struct sockaddr *)&a, &alen) == 0) atomic_store(&c->addr_local, a.sin_addr.s_addr);
    alen = sizeof(a);
    if (getpeername(fd, (struct sockaddr *)&a, &alen) == 0) atomic_store(&c->addr_server, a.sin_addr.s_addr);

    // ServerInfo handshake, with a receive timeout cleared once it is done.
    set_recv_timeout(fd, SHADNET_CONNECT_TIMEOUT_MS);
    uint8_t hdr[SHADNET_HEADER_SIZE];
    if (!recv_n(c, fd, hdr, sizeof(hdr)) || hdr[0] != SHADNET_PACKET_SERVER_INFO) {
        fprintf(stderr, "[shadNet] no ServerInfo packet from the server\n");
        close_socket(c);
        atomic_store(&c->state, SHADNET_STATE_FAILURE_SERVER_INFO);
        return false;
    }
    const uint32_t total = get_le32(hdr + 3);
    const uint32_t payload_size = total > SHADNET_HEADER_SIZE ? total - SHADNET_HEADER_SIZE : 0;
    if (payload_size > SHADNET_MAX_PACKET_SIZE) {
        close_socket(c);
        atomic_store(&c->state, SHADNET_STATE_FAILURE_SERVER_INFO);
        return false;
    }
    uint8_t *si = payload_size ? malloc(payload_size) : NULL;
    if (payload_size && (!si || !recv_n(c, fd, si, payload_size))) {
        free(si);
        fprintf(stderr, "[shadNet] timeout reading the ServerInfo payload\n");
        close_socket(c);
        atomic_store(&c->state, SHADNET_STATE_FAILURE_SERVER_INFO);
        return false;
    }
    if (payload_size >= 4) {
        const uint32_t ver = get_le32(si);
        atomic_store(&c->server_protocol_version, ver);
        if (ver != SHADNET_PROTOCOL_VERSION) {
            free(si);
            fprintf(stderr, "[shadNet] protocol version mismatch server=%u client=%u\n", ver,
                    SHADNET_PROTOCOL_VERSION);
            close_socket(c);
            atomic_store(&c->state, SHADNET_STATE_FAILURE_PROTOCOL);
            return false;
        }
    }
    free(si);
    set_recv_timeout(fd, 0);
    atomic_store(&c->connected, true);
    return true;
}

//=============================================================================
// Requests
//=============================================================================

// BuildPacket: header and payload in one block.
static Packet *build_packet(uint16_t cmd, uint64_t id, const uint8_t *payload, size_t len, size_t extra) {
    Packet *p = malloc(sizeof(Packet) + SHADNET_HEADER_SIZE + len + extra);
    if (!p) return NULL;
    p->next = NULL;
    p->len = SHADNET_HEADER_SIZE + len + extra;
    p->data[0] = SHADNET_PACKET_REQUEST;
    put_le16(p->data + 1, cmd);
    put_le32(p->data + 3, (uint32_t)p->len);
    put_le64(p->data + 7, id);
    if (len) memcpy(p->data + SHADNET_HEADER_SIZE, payload, len);
    return p;
}

static void enqueue(ShadNetClient *c, Packet *p) {
    pthread_mutex_lock(&c->queue_mutex);
    if (c->queue_tail) {
        c->queue_tail->next = p;
    } else {
        c->queue_head = p;
    }
    c->queue_tail = p;
    pthread_cond_broadcast(&c->queue_cond);
    pthread_mutex_unlock(&c->queue_mutex);
}

uint64_t shadnet_client_submit(ShadNetClient *c, uint16_t cmd, const uint8_t *payload, size_t len) {
    const uint64_t id = atomic_fetch_add(&c->pkt_counter, 1);
    Packet *p = build_packet(cmd, id, payload, len, 0);
    if (!p) return 0;
    enqueue(c, p);
    return id;
}

// A packet whose payload is prefix, then msg's encoding with a u32 LE size (MakeProtoPayload /
// MakeComIdPayload).
static Packet *build_proto_packet(uint16_t cmd, uint64_t id, const void *prefix, size_t prefix_len,
                                  const ShadPbMessageDesc *desc, const void *msg) {
    const size_t blob = shadpb_encoded_size(desc, msg);
    Packet *p = build_packet(cmd, id, prefix, prefix_len, 4 + blob);
    if (!p) return NULL;
    uint8_t *w = p->data + SHADNET_HEADER_SIZE + prefix_len;
    put_le32(w, (uint32_t)blob);
    shadpb_encode(desc, msg, w + 4);
    return p;
}

uint64_t shadnet_client_submit_proto(ShadNetClient *c, uint16_t cmd, const void *prefix, size_t prefix_len,
                                     const ShadPbMessageDesc *desc, const void *msg) {
    const uint64_t id = atomic_fetch_add(&c->pkt_counter, 1);
    Packet *p = build_proto_packet(cmd, id, prefix, prefix_len, desc, msg);
    if (!p) return 0;
    enqueue(c, p);
    return id;
}

static uint64_t submit_npid(ShadNetClient *c, uint16_t cmd, const char *npid) {
    ShadPb_FriendCommandRequest req = {.npid = shadpb_str(npid)};
    return shadnet_client_submit_proto(c, cmd, NULL, 0, &shadpb_FriendCommandRequest_desc, &req);
}

uint64_t shadnet_client_add_friend(ShadNetClient *c, const char *npid) {
    return submit_npid(c, SHADNET_CMD_ADD_FRIEND, npid);
}

uint64_t shadnet_client_remove_friend(ShadNetClient *c, const char *npid) {
    return submit_npid(c, SHADNET_CMD_REMOVE_FRIEND, npid);
}

uint64_t shadnet_client_add_block(ShadNetClient *c, const char *npid) {
    return submit_npid(c, SHADNET_CMD_ADD_BLOCK, npid);
}

uint64_t shadnet_client_remove_block(ShadNetClient *c, const char *npid) {
    return submit_npid(c, SHADNET_CMD_REMOVE_BLOCK, npid);
}

uint64_t shadnet_client_set_appear_offline(ShadNetClient *c, bool enable) {
    atomic_store(&c->appear_offline, enable);
    if (!shadnet_client_is_authenticated(c)) return 0; // the login carries it
    ShadPb_SetAppearOfflineRequest req = {.appear_offline = enable};
    return shadnet_client_submit_proto(c, SHADNET_CMD_SET_APPEAR_OFFLINE, NULL, 0, &shadpb_SetAppearOfflineRequest_desc,
                                       &req);
}

uint64_t shadnet_client_lookup_online_id(ShadNetClient *c, const char *npid) {
    ShadPb_LookupOnlineIdRequest req = {.npid = shadpb_str(npid)};
    return shadnet_client_submit_proto(c, SHADNET_CMD_LOOKUP_ONLINE_ID, NULL, 0, &shadpb_LookupOnlineIdRequest_desc,
                                       &req);
}

uint64_t shadnet_client_get_authorization_code(ShadNetClient *c, const char *client_id, const char *scope) {
    ShadPb_GetAuthorizationCodeRequest req = {.client_id = shadpb_str(client_id), .scope = shadpb_str(scope)};
    return shadnet_client_submit_proto(c, SHADNET_CMD_GET_AUTHORIZATION_CODE, NULL, 0,
                                       &shadpb_GetAuthorizationCodeRequest_desc, &req);
}

uint64_t shadnet_client_unlock_trophy(ShadNetClient *c, const char *com_id, int32_t trophy_id, uint64_t timestamp) {
    ShadPb_UnlockTrophyRequest req = {.trophyId = trophy_id, .timestamp = timestamp};
    return shadnet_client_submit_proto(c, SHADNET_CMD_UNLOCK_TROPHY, com_id, strlen(com_id),
                                       &shadpb_UnlockTrophyRequest_desc, &req);
}

uint64_t shadnet_client_sync_trophies(ShadNetClient *c, const char *com_id, const int32_t *ids,
                                      const uint64_t *timestamps, uint32_t count) {
    ShadPb_TrophyEntry *entries = count ? calloc(count, sizeof(*entries)) : NULL;
    if (count && !entries) return 0;
    for (uint32_t i = 0; i < count; i++) {
        entries[i].trophyId = ids[i];
        entries[i].timestamp = timestamps[i];
    }
    ShadPb_SyncTrophiesRequest req = {.trophies = entries, .trophies_count = count};
    const uint64_t id = shadnet_client_submit_proto(c, SHADNET_CMD_SYNC_TROPHIES, com_id, strlen(com_id),
                                                    &shadpb_SyncTrophiesRequest_desc, &req);
    free(entries);
    return id;
}

// Requests the client sends itself (GetToken, GetServerFeatures, SetClientVersion).
static void submit_empty(ShadNetClient *c, uint16_t cmd) {
    shadnet_client_submit(c, cmd, NULL, 0);
}

static void report_client_version(ShadNetClient *c) {
    ShadPb_SetClientVersionRequest req = {.version = shadpb_str(c->client_version)};
    shadnet_client_submit_proto(c, SHADNET_CMD_SET_CLIENT_VERSION, NULL, 0, &shadpb_SetClientVersionRequest_desc,
                                &req);
}

//=============================================================================
// Replies and notifications
//=============================================================================

// ExtractBlob: a u32 LE size-prefixed blob at *pos; false when it does not fit.
static bool extract_blob(const uint8_t *p, size_t len, size_t *pos, ShadPbBytes *out) {
    out->data = NULL;
    out->len = 0;
    if (*pos + 4 > len) return false;
    const uint32_t n = get_le32(p + *pos);
    if (*pos + 4 + n > len) return false;
    out->data = p + *pos + 4;
    out->len = n;
    *pos += 4 + (size_t)n;
    return true;
}

// The proto blob after a reply's error byte.
static bool decode_reply_blob(const uint8_t *payload, size_t len, const ShadPbMessageDesc *desc, void *msg,
                              ShadPbArena *arena) {
    size_t pos = 1;
    ShadPbBytes blob;
    return extract_blob(payload, len, &pos, &blob) && blob.len > 0 &&
           shadpb_decode(desc, blob.data, blob.len, msg, arena);
}

static char *bytes_dup(ShadPbBytes b) {
    char *s = malloc((size_t)b.len + 1);
    if (!s) return NULL;
    if (b.len) memcpy(s, b.data, b.len);
    s[b.len] = '\0';
    return s;
}

static void login_failed(ShadNetClient *c, ShadNetState st) {
    atomic_store(&c->state, st);
    sem_release(&c->sem_authenticated);
    disconnect(c);
}

static void handle_login_reply(ShadNetClient *c, const uint8_t *payload, size_t len) {
    uint8_t err = SHADNET_ERR_MALFORMED;
    ShadPbArena arena = {0};
    ShadPb_LoginReply reply;
    const ShadPb_LoginReply *ok_reply = NULL;
    if (len == 0) {
        fprintf(stderr, "[shadNet] empty Login reply\n");
    } else if ((err = payload[0]) == SHADNET_ERR_NO_ERROR) {
        if (decode_reply_blob(payload, len, &shadpb_LoginReply_desc, &reply, &arena)) {
            FriendEntry *friends = reply.friends_count ? calloc(reply.friends_count, sizeof(*friends)) : NULL;
            char *avatar = bytes_dup(reply.avatar_url);
            pthread_mutex_lock(&c->data_mutex);
            for (uint32_t i = 0; friends && i < reply.friends_count; i++) {
                const ShadPbBytes n = reply.friends[i].npid;
                const size_t k = n.len < SHADNET_NPID_CAP - 1 ? n.len : SHADNET_NPID_CAP - 1;
                memcpy(friends[i].npid, n.data, k);
                friends[i].online = reply.friends[i].online;
            }
            free(c->friends);
            c->friends = friends;
            c->friend_count = friends ? reply.friends_count : 0;
            free(c->avatar_url);
            c->avatar_url = avatar;
            pthread_mutex_unlock(&c->data_mutex);
            atomic_store(&c->user_id, reply.user_id);
            atomic_store(&c->authenticated, true);
            fprintf(stderr, "[shadNet] logged in npid='%s' userId=%llu friends=%u\n", c->npid,
                    (unsigned long long)reply.user_id, reply.friends_count);
            ok_reply = &reply;
            submit_empty(c, SHADNET_CMD_GET_TOKEN);
        } else {
            err = SHADNET_ERR_MALFORMED;
            fprintf(stderr, "[shadNet] failed to parse LoginReply\n");
            login_failed(c, SHADNET_STATE_FAILURE_PROTOCOL);
        }
    } else {
        ShadNetState st;
        switch (err) {
        case SHADNET_ERR_LOGIN_ALREADY_LOGGED_IN: st = SHADNET_STATE_FAILURE_ALREADY_IN; break;
        case SHADNET_ERR_LOGIN_INVALID_USERNAME: st = SHADNET_STATE_FAILURE_USERNAME; break;
        case SHADNET_ERR_LOGIN_INVALID_PASSWORD: st = SHADNET_STATE_FAILURE_PASSWORD; break;
        case SHADNET_ERR_LOGIN_INVALID_TOKEN: st = SHADNET_STATE_FAILURE_TOKEN; break;
        default: st = SHADNET_STATE_FAILURE_AUTH; break;
        }
        fprintf(stderr, "[shadNet] login rejected, error code %u\n", err);
        login_failed(c, st);
    }
    if (c->cb.login_result) c->cb.login_result(c->cb.user, err, ok_reply);
    shadpb_arena_free(&arena);
}

static void handle_get_token_reply(ShadNetClient *c, const uint8_t *payload, size_t len) {
    ShadPbArena arena = {0};
    ShadPb_GetTokenReply reply;
    if (len == 0) {
        fprintf(stderr, "[shadNet] empty GetToken reply\n");
    } else if (payload[0] != SHADNET_ERR_NO_ERROR) {
        fprintf(stderr, "[shadNet] GetToken returned error=%u: WebAPI calls will be unauthenticated\n", payload[0]);
    } else if (!decode_reply_blob(payload, len, &shadpb_GetTokenReply_desc, &reply, &arena)) {
        fprintf(stderr, "[shadNet] failed to parse GetTokenReply\n");
    } else {
        char *token = bytes_dup(reply.token);
        pthread_mutex_lock(&c->data_mutex);
        free(c->bearer_token);
        c->bearer_token = token;
        pthread_mutex_unlock(&c->data_mutex);
    }
    shadpb_arena_free(&arena);
    submit_empty(c, SHADNET_CMD_GET_SERVER_FEATURES);
}

static void handle_server_features_reply(ShadNetClient *c, const uint8_t *payload, size_t len) {
    bool matching2 = false, trophies = false, parsed = false;
    ShadPbArena arena = {0};
    ShadPb_ServerFeaturesReply reply;
    if (len > 0) {
        if (payload[0] == SHADNET_ERR_NO_ERROR) {
            if (decode_reply_blob(payload, len, &shadpb_ServerFeaturesReply_desc, &reply, &arena)) {
                matching2 = reply.matching2_enabled;
                trophies = reply.trophies_enabled;
                parsed = true;
            }
        } else {
            fprintf(stderr, "[shadNet] GetServerFeatures returned error=%u: Matching2 disabled\n", payload[0]);
        }
    }
    shadpb_arena_free(&arena);
    atomic_store(&c->matching2_enabled, matching2);
    atomic_store(&c->trophies_enabled, trophies);
    atomic_store(&c->server_features_received, parsed);
    report_client_version(c);
    sem_release(&c->sem_authenticated);
}

// Decodes a notification's proto blob and passes it to cb, when cb is set.
#define NOTIFY(type, field)                                                                         \
    do {                                                                                            \
        ShadPb_##type n;                                                                            \
        if (!shadpb_decode(&shadpb_##type##_desc, blob.data, blob.len, &n, &arena)) {               \
            fprintf(stderr, "[shadNet] " #type " parse error\n");                                   \
        } else if (c->cb.field) {                                                                   \
            c->cb.field(c->cb.user, &n);                                                            \
        }                                                                                           \
    } while (0)

static void handle_webapi_push_event(ShadNetClient *c, const uint8_t *p, size_t len, ShadPbArena *arena) {
    // Length-prefixed fields: service name, label (u32 LE), data type, data, from, to; then an
    // optional extended-data section (u32 LE count of key / value blobs, at most 256) and the
    // two account ids. Missing trailing fields stay empty.
    ShadNetWebApiPushEvent n;
    memset(&n, 0, sizeof(n));
    size_t off = 0;
    extract_blob(p, len, &off, &n.np_service_name);
    if (off + 4 <= len) {
        n.np_service_label = get_le32(p + off);
        off += 4;
    }
    extract_blob(p, len, &off, &n.data_type);
    extract_blob(p, len, &off, &n.data);
    extract_blob(p, len, &off, &n.from_npid);
    extract_blob(p, len, &off, &n.to_npid);
    if (off + 4 <= len) {
        const uint32_t count = get_le32(p + off);
        off += 4;
        bool complete = count <= 256;
        const uint32_t take = count < 256 ? count : 256;
        ShadNetKeyValue *kv = take ? shadpb_arena_alloc(arena, take * sizeof(*kv)) : NULL;
        uint32_t got = 0;
        for (uint32_t i = 0; kv && i < take; i++) {
            if (!extract_blob(p, len, &off, &kv[i].key) || !extract_blob(p, len, &off, &kv[i].value)) {
                complete = false;
                break;
            }
            got++;
        }
        n.extd_data = kv;
        n.extd_data_count = got;
        if (complete && off + 16 <= len) {
            n.from_account_id = get_le64(p + off);
            n.to_account_id = get_le64(p + off + 8);
        }
    }
    if (c->cb.webapi_push_event) c->cb.webapi_push_event(c->cb.user, &n);
}

static void handle_notification(ShadNetClient *c, uint16_t type, const uint8_t *payload, size_t len) {
    ShadPbArena arena = {0};
    if (type == SHADNET_NOTIFY_WEBAPI_PUSH_EVENT) {
        handle_webapi_push_event(c, payload, len, &arena);
        shadpb_arena_free(&arena);
        return;
    }
    size_t pos = 0;
    ShadPbBytes blob;
    if (!extract_blob(payload, len, &pos, &blob) || blob.len == 0) {
        fprintf(stderr, "[shadNet] empty notification payload type=%u\n", type);
        return;
    }
    switch (type) {
    case SHADNET_NOTIFY_FRIEND_QUERY: NOTIFY(NotifyFriendQuery, friend_query); break;
    case SHADNET_NOTIFY_FRIEND_NEW: NOTIFY(NotifyFriendNew, friend_new); break;
    case SHADNET_NOTIFY_FRIEND_LOST: NOTIFY(NotifyFriendLost, friend_lost); break;
    case SHADNET_NOTIFY_FRIEND_STATUS: NOTIFY(NotifyFriendStatus, friend_status); break;
    case SHADNET_NOTIFY_ROOM_EVENT: NOTIFY(NotifyRoomEvent, room_event); break;
    case SHADNET_NOTIFY_ROOM_MESSAGE: NOTIFY(NotifyRoomMessage, room_message); break;
    case SHADNET_NOTIFY_SERVICE_UNAVAILABLE: NOTIFY(NotifyServiceUnavailable, service_unavailable); break;
    case SHADNET_NOTIFY_CLIENT_REJECTED: NOTIFY(NotifyClientRejected, client_rejected); break;
    default: break; // unknown notification
    }
    shadpb_arena_free(&arena);
}

static void dispatch_packet(ShadNetClient *c, uint8_t type, uint16_t cmd, uint64_t pkt_id, const uint8_t *payload,
                            size_t len) {
    switch (type) {
    case SHADNET_PACKET_REPLY:
        switch (cmd) {
        case SHADNET_CMD_LOGIN: handle_login_reply(c, payload, len); break;
        case SHADNET_CMD_GET_TOKEN: handle_get_token_reply(c, payload, len); break;
        case SHADNET_CMD_GET_SERVER_FEATURES: handle_server_features_reply(c, payload, len); break;
        default:
            if (c->cb.async_reply) {
                const uint8_t err = len ? payload[0] : SHADNET_ERR_MALFORMED;
                c->cb.async_reply(c->cb.user, cmd, pkt_id, err, len > 1 ? payload + 1 : NULL, len > 1 ? len - 1 : 0);
            }
            break;
        }
        break;
    case SHADNET_PACKET_NOTIFICATION: handle_notification(c, cmd, payload, len); break;
    case SHADNET_PACKET_SERVER_INFO: break; // ServerInfo update
    default: fprintf(stderr, "[shadNet] unexpected packet type %u from the server\n", type); break;
    }
}

//=============================================================================
// Threads
//=============================================================================

static void *reader_thread(void *arg) {
    ShadNetClient *c = arg;
    pthread_setname_np("ShadNet:Reader");
    t_reader_thread = true;
    const int fd = c->sock;
    uint8_t *buf = NULL;
    size_t cap = 0;
    while (!atomic_load(&c->terminate)) {
        uint8_t hdr[SHADNET_HEADER_SIZE];
        if (!recv_n(c, fd, hdr, sizeof(hdr))) break;
        const uint32_t total = get_le32(hdr + 3);
        if (total < SHADNET_HEADER_SIZE || total > SHADNET_MAX_PACKET_SIZE) {
            fprintf(stderr, "[shadNet] corrupt packet (total_sz=%u)\n", total);
            atomic_store(&c->state, SHADNET_STATE_FAILURE_PROTOCOL);
            break;
        }
        const size_t n = total - SHADNET_HEADER_SIZE;
        if (n > cap) {
            uint8_t *grown = realloc(buf, n);
            if (!grown) break;
            buf = grown;
            cap = n;
        }
        if (n && !recv_n(c, fd, buf, n)) break;
        dispatch_packet(c, hdr[0], get_le16(hdr + 1), get_le64(hdr + 7), buf, n);
    }
    free(buf);
    if (!atomic_load(&c->authenticated)) {
        if (atomic_load(&c->state) == SHADNET_STATE_OK) atomic_store(&c->state, SHADNET_STATE_FAILURE_OTHER);
        sem_release(&c->sem_authenticated);
    }
    atomic_store(&c->connected, false);
    atomic_store(&c->authenticated, false);
    // Wake the writer: the session is over.
    pthread_mutex_lock(&c->queue_mutex);
    pthread_cond_broadcast(&c->queue_cond);
    pthread_mutex_unlock(&c->queue_mutex);
    return NULL;
}

static void *writer_thread(void *arg) {
    ShadNetClient *c = arg;
    pthread_setname_np("ShadNet:Writer");
    for (;;) {
        pthread_mutex_lock(&c->queue_mutex);
        while (!atomic_load(&c->terminate) && atomic_load(&c->connected) && !c->queue_head) {
            pthread_cond_wait(&c->queue_cond, &c->queue_mutex);
        }
        if (atomic_load(&c->terminate) || !atomic_load(&c->connected)) {
            pthread_mutex_unlock(&c->queue_mutex);
            return NULL;
        }
        Packet *batch = c->queue_head;
        c->queue_head = c->queue_tail = NULL;
        pthread_mutex_unlock(&c->queue_mutex);
        bool ok = true;
        while (batch) {
            Packet *p = batch;
            batch = p->next;
            if (ok && atomic_load(&c->connected) && !send_all(c, p->data, p->len)) ok = false;
            free(p);
        }
        if (!ok) return NULL;
    }
}

static void *connect_thread(void *arg) {
    ShadNetClient *c = arg;
    pthread_setname_np("ShadNet:Connect");
    bool connected = false;
    uint32_t backoff = SHADNET_CONNECT_RETRY_BACKOFF_MS;
    for (uint32_t attempt = 1; attempt <= SHADNET_CONNECT_MAX_ATTEMPTS && !atomic_load(&c->terminate); attempt++) {
        if (do_connect(c)) {
            connected = true;
            break;
        }
        if (atomic_load(&c->state) == SHADNET_STATE_FAILURE_PROTOCOL) break;
        if (atomic_load(&c->terminate) || attempt == SHADNET_CONNECT_MAX_ATTEMPTS) break;
        fprintf(stderr, "[shadNet] connect attempt %u/%u to %s:%u failed, retrying in %u ms\n", attempt,
                SHADNET_CONNECT_MAX_ATTEMPTS, c->host, (unsigned)c->port, backoff);
        for (uint32_t waited = 0; waited < backoff && !atomic_load(&c->terminate); waited += 100) {
            const struct timespec ts = {0, 100 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
        backoff = backoff * 2 < 8000 ? backoff * 2 : 8000;
    }
    if (!connected) {
        sem_release(&c->sem_connected);
        sem_release(&c->sem_authenticated);
        return NULL;
    }
    pthread_create(&c->thread_reader, NULL, reader_thread, c);
    pthread_create(&c->thread_writer, NULL, writer_thread, c);
    c->threads_io_started = true;
    sem_release(&c->sem_connected);

    ShadPb_LoginRequest req = {
        .npid = shadpb_str(c->npid),
        .password = shadpb_str(c->password),
        .token = shadpb_str(c->token),
        .title_id = shadpb_str(c->title_id),
        .title_name = shadpb_str(c->title_name),
        .appear_offline = atomic_load(&c->appear_offline),
    };
    const uint64_t id = atomic_fetch_add(&c->pkt_counter, 1);
    Packet *p = build_proto_packet(SHADNET_CMD_LOGIN, id, NULL, 0, &shadpb_LoginRequest_desc, &req);
    const bool sent = p && send_all(c, p->data, p->len);
    free(p);
    if (!sent) {
        fprintf(stderr, "[shadNet] failed to send the Login packet\n");
        atomic_store(&c->state, SHADNET_STATE_FAILURE_OTHER);
        disconnect(c);
    }
    return NULL;
}

void shadnet_client_start(ShadNetClient *c, const ShadNetLogin *login) {
    free_login(c);
    c->host = dup_or_empty(login->host);
    c->port = login->port;
    c->npid = dup_or_empty(login->npid);
    c->password = dup_or_empty(login->password);
    c->token = dup_or_empty(login->token);
    c->title_id = dup_or_empty(login->title_id);
    c->title_name = dup_or_empty(login->title_name);
    c->client_version = dup_or_empty(login->client_version);
    atomic_store(&c->terminate, false);
    c->thread_connect_started = pthread_create(&c->thread_connect, NULL, connect_thread, c) == 0;
    if (!c->thread_connect_started) {
        atomic_store(&c->state, SHADNET_STATE_FAILURE_OTHER);
        sem_release(&c->sem_connected);
        sem_release(&c->sem_authenticated);
    }
}

void shadnet_client_stop(ShadNetClient *c) {
    atomic_store(&c->terminate, true);
    sem_release(&c->sem_connected);
    sem_release(&c->sem_authenticated);
    pthread_mutex_lock(&c->queue_mutex);
    pthread_cond_broadcast(&c->queue_cond);
    pthread_mutex_unlock(&c->queue_mutex);
    disconnect(c);
    if (c->thread_connect_started) {
        pthread_join(c->thread_connect, NULL);
        c->thread_connect_started = false;
    }
    if (c->threads_io_started) {
        pthread_join(c->thread_reader, NULL);
        pthread_join(c->thread_writer, NULL);
        c->threads_io_started = false;
    }
    close_socket(c);
    atomic_store(&c->connected, false);
    atomic_store(&c->authenticated, false);
}

//=============================================================================
// Server probe (server_probe.cpp)
//=============================================================================

static bool probe_recv(int fd, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        const ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

ShadNetProbeResult shadnet_probe(const char *host, uint16_t port, uint32_t timeout_ms, uint32_t *server_version) {
    *server_version = 0;
    if (!host || !host[0]) return SHADNET_PROBE_UNREACHABLE;
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return SHADNET_PROBE_UNREACHABLE;
    const int fd = socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        freeaddrinfo(res);
        return SHADNET_PROBE_UNREACHABLE;
    }
    const int nosigpipe = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
    const int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    const int cr = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    bool connected = false;
    if (cr == 0 || errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = {(time_t)(timeout_ms / 1000), (suseconds_t)((timeout_ms % 1000) * 1000)};
        if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            connected = err == 0;
        }
    }
    if (!connected) {
        close(fd);
        return SHADNET_PROBE_UNREACHABLE;
    }
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    set_recv_timeout(fd, timeout_ms);
    uint8_t hdr[SHADNET_HEADER_SIZE];
    if (!probe_recv(fd, hdr, sizeof(hdr)) || hdr[0] != SHADNET_PACKET_SERVER_INFO) {
        close(fd);
        return SHADNET_PROBE_PROTOCOL_ERROR;
    }
    const uint32_t total = get_le32(hdr + 3);
    if (total < SHADNET_HEADER_SIZE || total > SHADNET_MAX_PACKET_SIZE) {
        close(fd);
        return SHADNET_PROBE_PROTOCOL_ERROR;
    }
    const uint32_t n = total - SHADNET_HEADER_SIZE;
    uint8_t *payload = n ? malloc(n) : NULL;
    const bool ok = n == 0 || (payload && probe_recv(fd, payload, n));
    close(fd);
    if (!ok) {
        free(payload);
        return SHADNET_PROBE_PROTOCOL_ERROR;
    }
    if (n < 4) {
        free(payload);
        return SHADNET_PROBE_OK;
    }
    *server_version = get_le32(payload);
    free(payload);
    return *server_version == SHADNET_PROTOCOL_VERSION ? SHADNET_PROBE_OK : SHADNET_PROBE_VERSION_MISMATCH;
}
