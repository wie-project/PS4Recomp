// SPDX-License-Identifier: GPL-2.0-or-later

// NP handler core, following shadPS4 np_handler/np_handler.cpp and np_handler_friends.cpp;
// see ps4_np_handler.h.
//
// shadPS4 keeps each user's ShadNetClient in a shared_ptr map; here a session holds the client
// and a reference count, so a request that took the client keeps it alive while another thread
// signs the user out.
//
// shadPS4 also shows its on-screen shadNet notifications (friend requests, a refused
// matchmaking service, a rejected client) through an ImGui layer; they are written to the log
// here.

#include "ps4_np_handler.h"
#include "ps4_json_object.h"
#include "ps4_np_manager.h"
#include "ps4_sfo.h"
#include "ps4_user_service.h"
#include "shadnet_client.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_USERS ORBIS_USER_SERVICE_MAX_LOGIN_USERS
#define MAX_STATE_CALLBACKS 8
#define WORKER_INTERVAL_MS 500
#define RECONNECT_INITIAL_BACKOFF_MS 2000
#define RECONNECT_MAX_BACKOFF_MS 30000
// The client version reported to the server (SetClientVersion).
#define CLIENT_VERSION "PS4Recomp"

typedef struct NameList {
    char (*items)[PS4_NP_HANDLER_NPID_CAP];
    uint32_t count;
} NameList;

typedef struct FriendState {
    Ps4NpFriend *friends;
    uint32_t friend_count;
    NameList received, sent, blocked;
} FriendState;

typedef struct Session {
    ShadNetClient *client;
    int refs; // the session table's reference plus each request's
    int32_t user_id;
    OrbisNpId np_id;
} Session;

typedef struct Reconnect {
    bool active;
    int32_t user_id;
    uint64_t backoff_ms;
    uint64_t next_attempt_ms;
} Reconnect;

typedef struct PendingLookup {
    struct PendingLookup *next;
    uint64_t pkt_id;
    int32_t user_id;
    Ps4NpLookupResult on_result;
    void *user;
} PendingLookup;

typedef struct StateCallback {
    int32_t handle;
    Ps4NpStateCallback cb;
    void *user;
} StateCallback;

static struct {
    pthread_mutex_t clients_mutex;
    Session *sessions[MAX_USERS];
    Reconnect reconnect[MAX_USERS];
    Ps4ShadNetConfig config;
    char host[256];
    uint16_t port;
    _Atomic bool initialized;
    _Atomic bool session_disabled;
    _Atomic bool appear_offline;

    pthread_mutex_t worker_mutex;
    pthread_cond_t worker_cond;
    _Atomic bool worker_running;
    pthread_t worker;

    pthread_mutex_t friends_mutex;
    int32_t friend_user[MAX_USERS];
    FriendState friend_state[MAX_USERS];

    pthread_mutex_t lookup_mutex;
    PendingLookup *lookups;

    pthread_mutex_t cbs_mutex;
    StateCallback cbs[MAX_STATE_CALLBACKS];
    int32_t next_cb_handle;
} g_np = {
    .clients_mutex = PTHREAD_MUTEX_INITIALIZER,
    .worker_mutex = PTHREAD_MUTEX_INITIALIZER,
    .worker_cond = PTHREAD_COND_INITIALIZER,
    .friends_mutex = PTHREAD_MUTEX_INITIALIZER,
    .lookup_mutex = PTHREAD_MUTEX_INITIALIZER,
    .cbs_mutex = PTHREAD_MUTEX_INITIALIZER,
    .next_cb_handle = 1,
    .friend_user = {-1, -1, -1, -1},
};

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void copy_cstr(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

static void set_np_id(OrbisNpId *dst, const char *online_id) {
    memset(dst, 0, sizeof(*dst));
    const size_t n = strnlen(online_id, ORBIS_NP_ONLINEID_MAX_LENGTH);
    memcpy(dst->handle.data, online_id, n);
}

//=============================================================================
// Configuration
//=============================================================================

static void config_member(void *user, const char *key, Ps4JsonKind kind, const char *value) {
    Ps4ShadNetConfig *c = user;
    if (kind == PS4_JSON_LITERAL) {
        if (strcmp(key, "enabled") == 0) c->enabled = strcmp(value, "true") == 0;
        else if (strcmp(key, "appear_offline") == 0) c->appear_offline = strcmp(value, "true") == 0;
        else if (strcmp(key, "np_age") == 0) c->np_age = (int32_t)strtol(value, NULL, 10);
        return;
    }
    if (strcmp(key, "np_country") == 0) copy_cstr(c->np_country, sizeof(c->np_country), value);
    else if (strcmp(key, "np_language") == 0) copy_cstr(c->np_language, sizeof(c->np_language), value);
    else if (strcmp(key, "np_date_of_birth") == 0) copy_cstr(c->np_date_of_birth, sizeof(c->np_date_of_birth), value);
    if (strcmp(key, "server") == 0) copy_cstr(c->server, sizeof(c->server), value);
    else if (strcmp(key, "npid") == 0) copy_cstr(c->npid, sizeof(c->npid), value);
    else if (strcmp(key, "password") == 0) copy_cstr(c->password, sizeof(c->password), value);
    else if (strcmp(key, "token") == 0) copy_cstr(c->token, sizeof(c->token), value);
}

void ps4_np_handler_load_config(Ps4ShadNetConfig *out) {
    memset(out, 0, sizeof(*out));
    char path[1024];
    const char *env = getenv("PS4_SHADNET_CONFIG");
    const char *data = getenv("PS4RECOMP_DATA_DIR");
    const char *home = getenv("HOME");
    if (env && *env) {
        copy_cstr(path, sizeof(path), env);
    } else if (data && *data) {
        snprintf(path, sizeof(path), "%s/shadnet.json", data);
    } else {
        snprintf(path, sizeof(path), "%s/Library/Application Support/PS4Recomp/shadnet.json",
                 home && *home ? home : ".");
    }
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char *text = NULL;
    size_t len = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (text = malloc((size_t)n))) len = fread(text, 1, (size_t)n, f);
    }
    fclose(f);
    if (text && ps4_json_object_members(text, len, config_member, out) < 0) {
        fprintf(stderr, "[NpHandler] %s: malformed JSON object; shadNet stays offline\n", path);
        memset(out, 0, sizeof(*out));
    }
    free(text);
}

// NpHandler::ParseServerAddress: "host[:port]", default port 31313.
static void parse_server_address(const char *server, char *host, size_t host_size, uint16_t *port) {
    *port = SHADNET_DEFAULT_PORT;
    if (!server[0]) {
        fprintf(stderr, "[NpHandler] shadNet server address is empty: set \"server\" in shadnet.json\n");
        host[0] = '\0';
        return;
    }
    copy_cstr(host, host_size, server);
    char *colon = strrchr(host, ':');
    if (colon) {
        *colon = '\0';
        char *end = NULL;
        const unsigned long p = strtoul(colon + 1, &end, 10);
        if (end != colon + 1 && *end == '\0' && p <= 65535) {
            *port = (uint16_t)p;
        } else {
            fprintf(stderr, "[NpHandler] shadNet server port '%s' is not a valid number; using %u\n", colon + 1,
                    *port);
        }
    }
}

const Ps4ShadNetConfig *ps4_np_handler_config(void) {
    return &g_np.config;
}

bool ps4_np_handler_enabled(void) {
    return g_np.config.enabled && !atomic_load(&g_np.session_disabled);
}

//=============================================================================
// Sessions
//=============================================================================

// clients_mutex held.
static int session_slot(int32_t user_id) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_np.sessions[i] && g_np.sessions[i]->user_id == user_id) return i;
    }
    return -1;
}

static void session_release(Session *s) {
    pthread_mutex_lock(&g_np.clients_mutex);
    const bool last = --s->refs == 0;
    pthread_mutex_unlock(&g_np.clients_mutex);
    if (last) {
        shadnet_client_destroy(s->client);
        free(s);
    }
}

// The user's session with a reference, or NULL.
static Session *session_acquire(int32_t user_id) {
    pthread_mutex_lock(&g_np.clients_mutex);
    const int slot = session_slot(user_id);
    Session *s = slot >= 0 ? g_np.sessions[slot] : NULL;
    if (s) s->refs++;
    pthread_mutex_unlock(&g_np.clients_mutex);
    return s;
}

static void fire_state_callback(int32_t user_id, int32_t state) {
    pthread_mutex_lock(&g_np.cbs_mutex);
    for (int i = 0; i < MAX_STATE_CALLBACKS; i++) {
        if (g_np.cbs[i].cb) g_np.cbs[i].cb(g_np.cbs[i].user, user_id, state);
    }
    pthread_mutex_unlock(&g_np.cbs_mutex);
}

int32_t ps4_np_handler_register_state_callback(Ps4NpStateCallback cb, void *user) {
    pthread_mutex_lock(&g_np.cbs_mutex);
    int32_t h = -1;
    for (int i = 0; i < MAX_STATE_CALLBACKS && h < 0; i++) {
        if (!g_np.cbs[i].cb) {
            h = g_np.next_cb_handle++;
            g_np.cbs[i] = (StateCallback){h, cb, user};
        }
    }
    pthread_mutex_unlock(&g_np.cbs_mutex);
    return h;
}

void ps4_np_handler_unregister_state_callback(int32_t handle) {
    pthread_mutex_lock(&g_np.cbs_mutex);
    for (int i = 0; i < MAX_STATE_CALLBACKS; i++) {
        if (g_np.cbs[i].handle == handle) memset(&g_np.cbs[i], 0, sizeof(g_np.cbs[i]));
    }
    pthread_mutex_unlock(&g_np.cbs_mutex);
}

//=============================================================================
// Friend state (np_handler_friends.cpp)
//=============================================================================

// friends_mutex held. The user's friend state, created on first use.
static FriendState *friend_state(int32_t user_id) {
    int free_slot = -1;
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_np.friend_user[i] == user_id) return &g_np.friend_state[i];
        if (g_np.friend_user[i] == -1 && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return NULL;
    g_np.friend_user[free_slot] = user_id;
    memset(&g_np.friend_state[free_slot], 0, sizeof(FriendState));
    return &g_np.friend_state[free_slot];
}

static void friend_state_clear(FriendState *st) {
    free(st->friends);
    free(st->received.items);
    free(st->sent.items);
    free(st->blocked.items);
    memset(st, 0, sizeof(*st));
}

// friends_mutex held.
static void friend_state_erase(int32_t user_id) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_np.friend_user[i] == user_id) {
            friend_state_clear(&g_np.friend_state[i]);
            g_np.friend_user[i] = -1;
        }
    }
}

static void copy_npid(char *dst, ShadPbBytes b) {
    const size_t n = b.len < PS4_NP_HANDLER_NPID_CAP - 1 ? b.len : PS4_NP_HANDLER_NPID_CAP - 1;
    memcpy(dst, b.data, n);
    dst[n] = '\0';
}

static bool npid_eq(const char *a, ShadPbBytes b) {
    return strlen(a) == b.len && memcmp(a, b.data, b.len) == 0;
}

static int name_find(const NameList *l, ShadPbBytes name) {
    for (uint32_t i = 0; i < l->count; i++) {
        if (npid_eq(l->items[i], name)) return (int)i;
    }
    return -1;
}

static void name_add(NameList *l, ShadPbBytes name) {
    if (name_find(l, name) >= 0) return;
    void *grown = realloc(l->items, (l->count + 1) * sizeof(*l->items));
    if (!grown) return;
    l->items = grown;
    copy_npid(l->items[l->count++], name);
}

static void name_drop(NameList *l, ShadPbBytes name) {
    const int i = name_find(l, name);
    if (i < 0) return;
    memmove(&l->items[i], &l->items[i + 1], (l->count - (uint32_t)i - 1) * sizeof(*l->items));
    l->count--;
}

static int friend_find(const FriendState *st, ShadPbBytes name) {
    for (uint32_t i = 0; i < st->friend_count; i++) {
        if (npid_eq(st->friends[i].npid, name)) return (int)i;
    }
    return -1;
}

static void friend_set(FriendState *st, ShadPbBytes name, bool online) {
    const int i = friend_find(st, name);
    if (i >= 0) {
        st->friends[i].online = online;
        return;
    }
    void *grown = realloc(st->friends, (st->friend_count + 1) * sizeof(*st->friends));
    if (!grown) return;
    st->friends = grown;
    copy_npid(st->friends[st->friend_count].npid, name);
    st->friends[st->friend_count++].online = online;
}

static void friend_drop(FriendState *st, ShadPbBytes name) {
    const int i = friend_find(st, name);
    if (i < 0) return;
    memmove(&st->friends[i], &st->friends[i + 1], (st->friend_count - (uint32_t)i - 1) * sizeof(*st->friends));
    st->friend_count--;
}

static char *bytes_cstr(ShadPbBytes b, char *buf, size_t cap) {
    const size_t n = b.len < cap - 1 ? b.len : cap - 1;
    memcpy(buf, b.data, n);
    buf[n] = '\0';
    return buf;
}

// Callbacks of a user's client; user is the user id.
static int32_t cb_user(void *user) {
    return (int32_t)(intptr_t)user;
}

static void on_login_result(void *user, uint8_t error, const ShadPb_LoginReply *res) {
    if (error != SHADNET_ERR_NO_ERROR || !res) return;
    pthread_mutex_lock(&g_np.friends_mutex);
    FriendState *st = friend_state(cb_user(user));
    if (st) {
        friend_state_clear(st);
        for (uint32_t i = 0; i < res->friends_count; i++) friend_set(st, res->friends[i].npid, res->friends[i].online);
        for (uint32_t i = 0; i < res->friend_requests_sent_count; i++) name_add(&st->sent, res->friend_requests_sent[i]);
        for (uint32_t i = 0; i < res->friend_requests_received_count; i++) {
            name_add(&st->received, res->friend_requests_received[i]);
        }
        for (uint32_t i = 0; i < res->blocked_count; i++) name_add(&st->blocked, res->blocked[i]);
    }
    pthread_mutex_unlock(&g_np.friends_mutex);
    if (res->friend_requests_received_count) {
        fprintf(stderr, "[NpHandler] user %d: %u pending friend request(s)\n", cb_user(user),
                res->friend_requests_received_count);
    }
}

static void on_friend_query(void *user, const ShadPb_NotifyFriendQuery *n) {
    char name[PS4_NP_HANDLER_NPID_CAP];
    fprintf(stderr, "[NpHandler] user %d: friend request from %s\n", cb_user(user),
            bytes_cstr(n->from_npid, name, sizeof(name)));
    pthread_mutex_lock(&g_np.friends_mutex);
    FriendState *st = friend_state(cb_user(user));
    if (st) name_add(&st->received, n->from_npid);
    pthread_mutex_unlock(&g_np.friends_mutex);
}

static void on_friend_new(void *user, const ShadPb_NotifyFriendNew *n) {
    char name[PS4_NP_HANDLER_NPID_CAP];
    fprintf(stderr, "[NpHandler] user %d: %s is now your friend\n", cb_user(user), bytes_cstr(n->npid, name, sizeof(name)));
    pthread_mutex_lock(&g_np.friends_mutex);
    FriendState *st = friend_state(cb_user(user));
    if (st) {
        friend_set(st, n->npid, n->online);
        name_drop(&st->received, n->npid);
        name_drop(&st->sent, n->npid);
    }
    pthread_mutex_unlock(&g_np.friends_mutex);
}

static void on_friend_lost(void *user, const ShadPb_NotifyFriendLost *n) {
    char name[PS4_NP_HANDLER_NPID_CAP];
    fprintf(stderr, "[NpHandler] user %d: %s removed you as a friend\n", cb_user(user),
            bytes_cstr(n->npid, name, sizeof(name)));
    pthread_mutex_lock(&g_np.friends_mutex);
    FriendState *st = friend_state(cb_user(user));
    if (st) friend_drop(st, n->npid);
    pthread_mutex_unlock(&g_np.friends_mutex);
}

static void on_friend_status(void *user, const ShadPb_NotifyFriendStatus *n) {
    if (n->online) {
        char name[PS4_NP_HANDLER_NPID_CAP];
        fprintf(stderr, "[NpHandler] user %d: %s is online\n", cb_user(user), bytes_cstr(n->npid, name, sizeof(name)));
    }
    pthread_mutex_lock(&g_np.friends_mutex);
    FriendState *st = friend_state(cb_user(user));
    if (st) friend_set(st, n->npid, n->online);
    pthread_mutex_unlock(&g_np.friends_mutex);
}

uint32_t ps4_np_handler_friend_list(int32_t user_id, Ps4NpFriend *out, uint32_t cap, uint32_t *received,
                                    uint32_t *sent, uint32_t *blocked) {
    pthread_mutex_lock(&g_np.friends_mutex);
    uint32_t n = 0, r = 0, s = 0, b = 0;
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_np.friend_user[i] != user_id) continue;
        const FriendState *st = &g_np.friend_state[i];
        n = st->friend_count;
        r = st->received.count;
        s = st->sent.count;
        b = st->blocked.count;
        if (out) memcpy(out, st->friends, (n < cap ? n : cap) * sizeof(*out));
    }
    pthread_mutex_unlock(&g_np.friends_mutex);
    if (received) *received = r;
    if (sent) *sent = s;
    if (blocked) *blocked = b;
    return n;
}

//=============================================================================
// Service notifications
//=============================================================================

static void on_client_rejected(void *user, const ShadPb_NotifyClientRejected *n) {
    atomic_store(&g_np.session_disabled, true);
    const int32_t user_id = cb_user(user);
    pthread_mutex_lock(&g_np.clients_mutex);
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_np.reconnect[i].active && g_np.reconnect[i].user_id == user_id) g_np.reconnect[i].active = false;
    }
    pthread_mutex_unlock(&g_np.clients_mutex);
    char msg[512], ver[64];
    bytes_cstr(n->message, msg, sizeof(msg));
    fprintf(stderr, "[NpHandler] user %d signed out by the server (reason %u, version '%s'): %s Online features are off "
                    "for this session.\n",
            user_id, n->reason, bytes_cstr(n->version, ver, sizeof(ver)),
            msg[0] ? msg : "The shadNet server doesn't accept this client.");
}

static void on_service_unavailable(void *user, const ShadPb_NotifyServiceUnavailable *n) {
    if (n->service != SHADNET_SERVICE_MATCHING2) {
        fprintf(stderr, "[NpHandler] user %d: server refused service %u (reason %u)\n", cb_user(user), n->service,
                n->reason);
        return;
    }
    switch (n->reason) {
    case SHADNET_UNAVAILABLE_SERVER_FULL:
        if (n->limit > 0) {
            fprintf(stderr, "[NpHandler] Online matchmaking is full right now (%u/%u players). Try again in a few minutes.\n",
                    n->in_use, n->limit);
        } else {
            fprintf(stderr, "[NpHandler] Online matchmaking is full right now. Try again in a few minutes.\n");
        }
        break;
    case SHADNET_UNAVAILABLE_NOT_ALLOWED:
        fprintf(stderr, "[NpHandler] Your shadNet account isn't allowed to use online matchmaking.\n");
        break;
    case SHADNET_UNAVAILABLE_DISABLED:
        fprintf(stderr, "[NpHandler] Online matchmaking is turned off on this shadNet server.\n");
        break;
    default: fprintf(stderr, "[NpHandler] The shadNet server refused online matchmaking.\n"); break;
    }
}

//=============================================================================
// Replies
//=============================================================================

static PendingLookup *take_lookup(uint64_t pkt_id) {
    pthread_mutex_lock(&g_np.lookup_mutex);
    PendingLookup **pp = &g_np.lookups;
    while (*pp && (*pp)->pkt_id != pkt_id) pp = &(*pp)->next;
    PendingLookup *p = *pp;
    if (p) *pp = p->next;
    pthread_mutex_unlock(&g_np.lookup_mutex);
    return p;
}

// NpHandler::OnLookupReply.
static void on_lookup_reply(uint64_t pkt_id, uint8_t error, const uint8_t *body, size_t len) {
    PendingLookup *p = take_lookup(pkt_id);
    if (!p) return; // flushed by a disconnect, or a stray reply
    ShadPbArena arena = {0};
    ShadPb_LookupOnlineIdReply reply;
    if (error == SHADNET_ERR_NOT_FOUND) {
        p->on_result(p->user, ORBIS_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_USER_NPID, 0, "");
    } else if (error == SHADNET_ERR_INVALID_INPUT) {
        p->on_result(p->user, (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT, 0, "");
    } else if (error != SHADNET_ERR_NO_ERROR || len < 4) {
        p->on_result(p->user, ORBIS_NP_COMMUNITY_ERROR_BAD_RESPONSE, 0, "");
    } else {
        const uint32_t n = (uint32_t)body[0] | ((uint32_t)body[1] << 8) | ((uint32_t)body[2] << 16) |
                           ((uint32_t)body[3] << 24);
        if (len - 4 < n || !shadpb_decode(&shadpb_LookupOnlineIdReply_desc, body + 4, n, &reply, &arena)) {
            p->on_result(p->user, ORBIS_NP_COMMUNITY_ERROR_BAD_RESPONSE, 0, "");
        } else {
            char name[PS4_NP_HANDLER_NPID_CAP];
            p->on_result(p->user, 0, reply.account_id, bytes_cstr(reply.npid, name, sizeof(name)));
        }
    }
    shadpb_arena_free(&arena);
    free(p);
}

// NpHandler::OnAsyncReply: routes a reply by its command. Score, TUS, trophy, Matching2 and
// authorization-code replies belong to libraries whose server requests are not connected to
// shadNet yet; nothing waits for them.
static void on_async_reply(void *user, uint16_t cmd, uint64_t pkt_id, uint8_t error, const uint8_t *body, size_t len) {
    (void)user;
    if (cmd == SHADNET_CMD_LOOKUP_ONLINE_ID) on_lookup_reply(pkt_id, error, body, len);
}

void ps4_np_handler_resolve_online_id(int32_t user_id, const char *online_id, Ps4NpLookupResult on_result,
                                      void *user) {
    Session *s = session_acquire(user_id);
    if (!s) {
        on_result(user, (int32_t)ORBIS_NP_ERROR_SIGNED_OUT, 0, "");
        return;
    }
    if (!shadnet_client_is_authenticated(s->client)) {
        session_release(s);
        on_result(user, (int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN, 0, "");
        return;
    }
    if (!online_id || !online_id[0]) {
        session_release(s);
        on_result(user, ORBIS_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_USER_NPID, 0, "");
        return;
    }
    PendingLookup *p = calloc(1, sizeof(*p));
    if (!p) {
        session_release(s);
        on_result(user, ORBIS_NP_COMMUNITY_ERROR_BAD_RESPONSE, 0, "");
        return;
    }
    p->user_id = user_id;
    p->on_result = on_result;
    p->user = user;
    // Registered before the request goes out, so its reply cannot arrive first.
    pthread_mutex_lock(&g_np.lookup_mutex);
    p->pkt_id = shadnet_client_lookup_online_id(s->client, online_id);
    p->next = g_np.lookups;
    g_np.lookups = p;
    pthread_mutex_unlock(&g_np.lookup_mutex);
    session_release(s);
}

// NpHandler::FailPendingRequests.
static void fail_pending_requests(int32_t user_id, int32_t error) {
    for (;;) {
        pthread_mutex_lock(&g_np.lookup_mutex);
        PendingLookup **pp = &g_np.lookups;
        while (*pp && (*pp)->user_id != user_id) pp = &(*pp)->next;
        PendingLookup *p = *pp;
        if (p) *pp = p->next;
        pthread_mutex_unlock(&g_np.lookup_mutex);
        if (!p) return;
        p->on_result(p->user, error, 0, "");
        free(p);
    }
}

//=============================================================================
// Connecting
//=============================================================================

static void title_info(char *id, size_t id_size, char *name, size_t name_size) {
    id[0] = name[0] = '\0';
    Ps4SfoContext sfo = {0};
    if (ps4_sfo_find_and_load(&sfo)) {
        ps4_sfo_get_string(&sfo, "TITLE_ID", id, id_size);
        ps4_sfo_get_string(&sfo, "TITLE", name, name_size);
        ps4_sfo_free(&sfo);
    }
}

static void protocol_mismatch(ShadNetClient *c, ShadNetState st) {
    if (st != SHADNET_STATE_FAILURE_PROTOCOL) return;
    atomic_store(&g_np.session_disabled, true);
    const uint32_t ver = shadnet_client_server_protocol_version(c);
    if (ver) {
        fprintf(stderr, "[NpHandler] shadNet protocol version mismatch (server v%u, runtime v%u): online features are "
                        "disabled for this session\n",
                ver, SHADNET_PROTOCOL_VERSION);
    } else {
        fprintf(stderr, "[NpHandler] shadNet protocol error during the handshake: online features are disabled for "
                        "this session\n");
    }
}

// NpHandler::ConnectUser.
static bool connect_user(int32_t user_id) {
    char title_id[32], title_name[256];
    title_info(title_id, sizeof(title_id), title_name, sizeof(title_name));
    const ShadNetCallbacks cb = {
        .user = (void *)(intptr_t)user_id,
        .login_result = on_login_result,
        .friend_query = on_friend_query,
        .friend_new = on_friend_new,
        .friend_lost = on_friend_lost,
        .friend_status = on_friend_status,
        .service_unavailable = on_service_unavailable,
        .client_rejected = on_client_rejected,
        .async_reply = on_async_reply,
    };
    ShadNetClient *c = shadnet_client_create(&cb);
    if (!c) return false;
    shadnet_client_set_appear_offline(c, atomic_load(&g_np.appear_offline));
    const ShadNetLogin login = {
        .host = g_np.host,
        .port = g_np.port,
        .npid = g_np.config.npid,
        .password = g_np.config.password,
        .token = g_np.config.token,
        .title_id = title_id,
        .title_name = title_name,
        .client_version = CLIENT_VERSION,
    };
    fprintf(stderr, "[NpHandler] connecting user %d npid='%s' to %s:%u\n", user_id, login.npid, login.host,
            (unsigned)login.port);
    shadnet_client_start(c, &login);
    ShadNetState st = shadnet_client_wait_for_connection(c);
    if (st == SHADNET_STATE_OK) st = shadnet_client_wait_for_authenticated(c);
    if (st != SHADNET_STATE_OK) {
        fprintf(stderr, "[NpHandler] user %d could not sign in to shadNet (state %d)\n", user_id, (int)st);
        protocol_mismatch(c, st);
        shadnet_client_destroy(c);
        return false;
    }
    Session *s = calloc(1, sizeof(*s));
    if (!s) {
        shadnet_client_destroy(c);
        return false;
    }
    s->client = c;
    s->refs = 1;
    s->user_id = user_id;
    set_np_id(&s->np_id, g_np.config.npid);
    pthread_mutex_lock(&g_np.clients_mutex);
    int slot = session_slot(user_id);
    for (int i = 0; i < MAX_USERS && slot < 0; i++) {
        if (!g_np.sessions[i]) slot = i;
    }
    const bool stored = slot >= 0 && !g_np.sessions[slot];
    if (stored) g_np.sessions[slot] = s;
    pthread_mutex_unlock(&g_np.clients_mutex);
    if (!stored) {
        shadnet_client_destroy(c);
        free(s);
        return false;
    }
    fprintf(stderr, "[NpHandler] user %d signed in npid='%s' accountId=%llu\n", user_id, g_np.config.npid,
            (unsigned long long)shadnet_client_user_id(c));
    fire_state_callback(user_id, ORBIS_NP_STATE_SIGNED_IN);
    return true;
}

// NpHandler::ConnectUserById: connects a logged-in user with an account, once.
static bool connect_user_by_id(int32_t user_id) {
    if (!ps4_np_handler_enabled()) return false;
    pthread_mutex_lock(&g_np.clients_mutex);
    const bool connected = session_slot(user_id) >= 0;
    pthread_mutex_unlock(&g_np.clients_mutex);
    if (connected) return false;
    if (user_id != ORBIS_USER_SERVICE_INITIAL_USER_ID) return false; // the only logged-in user
    if (!g_np.config.npid[0] || !g_np.config.password[0]) {
        fprintf(stderr, "[NpHandler] user %d: shadNet enabled but the account is missing \"npid\" or \"password\"\n",
                user_id);
        return false;
    }
    return connect_user(user_id);
}

// NpHandler::DisconnectUser.
static void disconnect_user(int32_t user_id) {
    pthread_mutex_lock(&g_np.clients_mutex);
    const int slot = session_slot(user_id);
    Session *s = slot >= 0 ? g_np.sessions[slot] : NULL;
    if (s) g_np.sessions[slot] = NULL;
    pthread_mutex_unlock(&g_np.clients_mutex);
    if (!s) return;
    pthread_mutex_lock(&g_np.friends_mutex);
    friend_state_erase(user_id);
    pthread_mutex_unlock(&g_np.friends_mutex);
    // The reader thread is joined before the pending requests fail, so no reply races them.
    shadnet_client_stop(s->client);
    session_release(s);
    fail_pending_requests(user_id, (int32_t)ORBIS_NP_ERROR_SIGNED_OUT);
    fire_state_callback(user_id, ORBIS_NP_STATE_SIGNED_OUT);
    fprintf(stderr, "[NpHandler] user %d disconnected\n", user_id);
}

// clients_mutex held.
static void mark_for_reconnect_locked(int32_t user_id) {
    Reconnect *r = NULL;
    for (int i = 0; i < MAX_USERS && !r; i++) {
        if (g_np.reconnect[i].active && g_np.reconnect[i].user_id == user_id) r = &g_np.reconnect[i];
    }
    for (int i = 0; i < MAX_USERS && !r; i++) {
        if (!g_np.reconnect[i].active) r = &g_np.reconnect[i];
    }
    if (!r) return;
    r->active = true;
    r->user_id = user_id;
    r->backoff_ms = RECONNECT_INITIAL_BACKOFF_MS;
    r->next_attempt_ms = now_ms() + r->backoff_ms;
}

static void try_reconnect(void) {
    const uint64_t now = now_ms();
    int32_t due[MAX_USERS];
    int n = 0;
    pthread_mutex_lock(&g_np.clients_mutex);
    for (int i = 0; i < MAX_USERS; i++) {
        const Reconnect *r = &g_np.reconnect[i];
        if (r->active && (session_slot(r->user_id) >= 0 || now >= r->next_attempt_ms)) due[n++] = r->user_id;
    }
    pthread_mutex_unlock(&g_np.clients_mutex);
    for (int k = 0; k < n; k++) {
        if (!atomic_load(&g_np.worker_running)) return;
        const bool ok = ps4_np_handler_enabled() && connect_user_by_id(due[k]);
        pthread_mutex_lock(&g_np.clients_mutex);
        for (int i = 0; i < MAX_USERS; i++) {
            Reconnect *r = &g_np.reconnect[i];
            if (!r->active || r->user_id != due[k]) continue;
            if (!ps4_np_handler_enabled() || ok || session_slot(due[k]) >= 0) {
                r->active = false;
                if (ok) fprintf(stderr, "[NpHandler] user %d reconnected to shadNet\n", due[k]);
            } else {
                r->backoff_ms = r->backoff_ms * 2 < RECONNECT_MAX_BACKOFF_MS ? r->backoff_ms * 2 : RECONNECT_MAX_BACKOFF_MS;
                r->next_attempt_ms = now_ms() + r->backoff_ms;
            }
        }
        pthread_mutex_unlock(&g_np.clients_mutex);
    }
}

// NpHandler::WorkerThread: every 500 ms, signs out users whose connection dropped and
// schedules their reconnection.
static void *worker_main(void *arg) {
    (void)arg;
    pthread_setname_np("NpHandler:Worker");
    pthread_mutex_lock(&g_np.worker_mutex);
    while (atomic_load(&g_np.worker_running)) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += WORKER_INTERVAL_MS * 1000000L;
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
        pthread_cond_timedwait(&g_np.worker_cond, &g_np.worker_mutex, &ts);
        if (!atomic_load(&g_np.worker_running)) break;
        pthread_mutex_unlock(&g_np.worker_mutex);

        int32_t dropped[MAX_USERS];
        int n = 0;
        pthread_mutex_lock(&g_np.clients_mutex);
        for (int i = 0; i < MAX_USERS; i++) {
            if (g_np.sessions[i] && !shadnet_client_is_connected(g_np.sessions[i]->client)) {
                dropped[n++] = g_np.sessions[i]->user_id;
            }
        }
        pthread_mutex_unlock(&g_np.clients_mutex);
        for (int i = 0; i < n; i++) {
            fprintf(stderr, "[NpHandler] user %d: connection dropped; will retry\n", dropped[i]);
            disconnect_user(dropped[i]);
            if (ps4_np_handler_enabled()) {
                pthread_mutex_lock(&g_np.clients_mutex);
                mark_for_reconnect_locked(dropped[i]);
                pthread_mutex_unlock(&g_np.clients_mutex);
            }
        }
        try_reconnect();
        pthread_mutex_lock(&g_np.worker_mutex);
    }
    pthread_mutex_unlock(&g_np.worker_mutex);
    return NULL;
}

static void start_worker(void) {
    bool expected = false;
    if (atomic_compare_exchange_strong(&g_np.worker_running, &expected, true)) {
        if (pthread_create(&g_np.worker, NULL, worker_main, NULL) != 0) atomic_store(&g_np.worker_running, false);
    }
}

void ps4_np_handler_initialize(const Ps4ShadNetConfig *config) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_np.initialized, &expected, true)) return;
    if (config) {
        g_np.config = *config;
    } else {
        ps4_np_handler_load_config(&g_np.config);
    }
    atomic_store(&g_np.session_disabled, false);
    atomic_store(&g_np.appear_offline, g_np.config.appear_offline);
    if (!g_np.config.enabled) {
        fprintf(stderr, "[NpHandler] shadNet disabled: offline mode\n");
        return;
    }
    parse_server_address(g_np.config.server, g_np.host, sizeof(g_np.host), &g_np.port);
    uint32_t server_version = 0;
    const ShadNetProbeResult probe = shadnet_probe(g_np.host, g_np.port, SHADNET_PROBE_TIMEOUT_MS, &server_version);
    if (probe != SHADNET_PROBE_OK) {
        atomic_store(&g_np.session_disabled, true);
        switch (probe) {
        case SHADNET_PROBE_VERSION_MISMATCH:
            fprintf(stderr, "[NpHandler] shadNet server %s:%u protocol version mismatch (server v%u, runtime v%u); online "
                            "features are disabled for this session\n",
                    g_np.host, (unsigned)g_np.port, server_version, SHADNET_PROTOCOL_VERSION);
            break;
        case SHADNET_PROBE_PROTOCOL_ERROR:
            fprintf(stderr, "[NpHandler] shadNet server %s:%u uses an incompatible protocol; online features are disabled "
                            "for this session\n",
                    g_np.host, (unsigned)g_np.port);
            break;
        default:
            fprintf(stderr, "[NpHandler] shadNet server %s:%u is offline; online features are disabled for this session\n",
                    g_np.host, (unsigned)g_np.port);
            break;
        }
        return;
    }
    if (!connect_user_by_id(ORBIS_USER_SERVICE_INITIAL_USER_ID)) {
        fprintf(stderr, "[NpHandler] no users connected to shadNet\n");
        return;
    }
    start_worker();
}

void ps4_np_handler_shutdown(void) {
    bool expected = true;
    if (!atomic_compare_exchange_strong(&g_np.initialized, &expected, false)) return;
    pthread_mutex_lock(&g_np.worker_mutex);
    const bool worker = atomic_exchange(&g_np.worker_running, false);
    pthread_cond_broadcast(&g_np.worker_cond);
    pthread_mutex_unlock(&g_np.worker_mutex);
    pthread_mutex_lock(&g_np.clients_mutex);
    memset(g_np.reconnect, 0, sizeof(g_np.reconnect));
    int32_t ids[MAX_USERS];
    int n = 0;
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_np.sessions[i]) ids[n++] = g_np.sessions[i]->user_id;
    }
    pthread_mutex_unlock(&g_np.clients_mutex);
    for (int i = 0; i < n; i++) disconnect_user(ids[i]);
    if (worker) pthread_join(g_np.worker, NULL);
}

void ps4_np_handler_on_user_logged_in(int32_t user_id) {
    if (connect_user_by_id(user_id)) start_worker();
}

void ps4_np_handler_on_user_logged_out(int32_t user_id) {
    pthread_mutex_lock(&g_np.clients_mutex);
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_np.reconnect[i].active && g_np.reconnect[i].user_id == user_id) g_np.reconnect[i].active = false;
    }
    pthread_mutex_unlock(&g_np.clients_mutex);
    disconnect_user(user_id);
}

void ps4_np_handler_set_appear_offline(bool enable) {
    atomic_store(&g_np.appear_offline, enable);
    pthread_mutex_lock(&g_np.clients_mutex);
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_np.sessions[i]) shadnet_client_set_appear_offline(g_np.sessions[i]->client, enable);
    }
    pthread_mutex_unlock(&g_np.clients_mutex);
}

bool ps4_np_handler_appear_offline(void) {
    return atomic_load(&g_np.appear_offline);
}

//=============================================================================
// Queries
//=============================================================================

bool ps4_np_handler_is_signed_in(int32_t user_id) {
    pthread_mutex_lock(&g_np.clients_mutex);
    const int slot = session_slot(user_id);
    const bool in = slot >= 0 && shadnet_client_is_authenticated(g_np.sessions[slot]->client);
    pthread_mutex_unlock(&g_np.clients_mutex);
    return in;
}

bool ps4_np_handler_any_signed_in(void) {
    pthread_mutex_lock(&g_np.clients_mutex);
    bool any = false;
    for (int i = 0; i < MAX_USERS && !any; i++) {
        any = g_np.sessions[i] && shadnet_client_is_authenticated(g_np.sessions[i]->client);
    }
    pthread_mutex_unlock(&g_np.clients_mutex);
    return any;
}

OrbisNpId ps4_np_handler_np_id(int32_t user_id) {
    OrbisNpId id;
    memset(&id, 0, sizeof(id));
    pthread_mutex_lock(&g_np.clients_mutex);
    const int slot = session_slot(user_id);
    if (slot >= 0) id = g_np.sessions[slot]->np_id;
    pthread_mutex_unlock(&g_np.clients_mutex);
    return id;
}

OrbisNpAccountId ps4_np_handler_account_id(int32_t user_id) {
    pthread_mutex_lock(&g_np.clients_mutex);
    const int slot = session_slot(user_id);
    const OrbisNpAccountId id = slot >= 0 ? shadnet_client_user_id(g_np.sessions[slot]->client) : 0;
    pthread_mutex_unlock(&g_np.clients_mutex);
    return id;
}

uint32_t ps4_np_handler_local_ip(int32_t user_id) {
    pthread_mutex_lock(&g_np.clients_mutex);
    const int slot = session_slot(user_id);
    const uint32_t ip = slot >= 0 ? shadnet_client_addr_local(g_np.sessions[slot]->client) : 0;
    pthread_mutex_unlock(&g_np.clients_mutex);
    return ip;
}

size_t ps4_np_handler_avatar_url(int32_t user_id, char *out, size_t out_size) {
    Session *s = session_acquire(user_id);
    if (!s) {
        if (out_size) out[0] = '\0';
        return 0;
    }
    const size_t n = shadnet_client_avatar_url(s->client, out, out_size);
    session_release(s);
    return n;
}

size_t ps4_np_handler_bearer_token(int32_t user_id, char *out, size_t out_size) {
    Session *s = session_acquire(user_id);
    if (!s) {
        if (out_size) out[0] = '\0';
        return 0;
    }
    const size_t n = shadnet_client_bearer_token(s->client, out, out_size);
    session_release(s);
    return n;
}

int32_t ps4_np_handler_user_by_account_id(uint64_t account_id) {
    pthread_mutex_lock(&g_np.clients_mutex);
    int32_t user = -1;
    for (int i = 0; i < MAX_USERS && user < 0; i++) {
        if (g_np.sessions[i] && shadnet_client_user_id(g_np.sessions[i]->client) == account_id) {
            user = g_np.sessions[i]->user_id;
        }
    }
    pthread_mutex_unlock(&g_np.clients_mutex);
    return user;
}

int32_t ps4_np_handler_user_by_online_id(const OrbisNpOnlineId *online_id) {
    pthread_mutex_lock(&g_np.clients_mutex);
    int32_t user = -1;
    for (int i = 0; i < MAX_USERS && user < 0; i++) {
        if (g_np.sessions[i] &&
            strncmp(g_np.sessions[i]->np_id.handle.data, online_id->data, ORBIS_NP_ONLINEID_MAX_LENGTH) == 0) {
            user = g_np.sessions[i]->user_id;
        }
    }
    pthread_mutex_unlock(&g_np.clients_mutex);
    return user;
}

uint32_t ps4_np_handler_num_friends(int32_t user_id) {
    Session *s = session_acquire(user_id);
    if (!s) return 0;
    const uint32_t n = shadnet_client_num_friends(s->client);
    session_release(s);
    return n;
}

bool ps4_np_handler_friend_npid(int32_t user_id, uint32_t index, char *out, size_t out_size) {
    Session *s = session_acquire(user_id);
    if (!s) return false;
    const bool ok = shadnet_client_friend_npid(s->client, index, out, out_size);
    session_release(s);
    return ok;
}
