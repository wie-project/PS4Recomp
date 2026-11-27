// SPDX-License-Identifier: GPL-2.0-or-later

// The P2P transport: shadPS4 core/net/p2p_transport.cpp and p2p_codec.cpp.
//
// Everything a transport owns (bindings, listeners, connections, queues, scratch buffers) is
// guarded by its mutex; the receive thread takes it for each batch of packets and the timers.
// Connections, listeners, bindings and readiness flags are reference counted under that mutex
// (shadPS4's shared_ptrs); the transport itself has an atomic count, one per socket plus the
// owner's.

#include "ps4_net_p2p_transport.h"
#include "ps4_net_types.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MARKER 0xFF
#define FLAG_VALID 0x80
#define FLAG_BROADCAST 0x40
#define FLAG_DIGEST 0x20 // the communication ID follows
#define KIND_DATAGRAM 3
#define KIND_STREAM 7
#define SIGNALING_VPORT 0xFFFF
#define MAX_REPORTED_BROADCASTS 16 // per transport
#define FRAME_OVERHEAD (6 + 4)     // datagram header and digest, the largest
#define RECEIVE_BUFFER 65536

// ---------------------------------------------------------------------------------------------
// Keyring

typedef struct PeerKey {
    Ps4P2PEndpoint peer;
    Ps4P2PKeyEntry entry;
    int references;
} PeerKey;

static pthread_mutex_t g_keyring_mutex = PTHREAD_MUTEX_INITIALIZER;
static PeerKey *g_peer_keys;
static int g_peer_key_count;
static int g_peer_key_cap;
static bool g_has_wildcard_key;
static uint8_t g_wildcard_key[PS4_P2P_KEY_SIZE];
static bool g_has_default_key;
static uint8_t g_default_key[PS4_P2P_KEY_SIZE];
static bool g_has_communication_id;
static uint8_t g_communication_id[4];

static int find_peer_key(const Ps4P2PEndpoint *peer) {
    for (int i = 0; i < g_peer_key_count; i++) {
        if (ps4_p2p_endpoint_equal(&g_peer_keys[i].peer, peer)) return i;
    }
    return -1;
}

void ps4_p2p_keyring_add_peer_key(const Ps4P2PEndpoint *peer, const Ps4P2PKeyEntry *entry) {
    pthread_mutex_lock(&g_keyring_mutex);
    const int i = find_peer_key(peer);
    if (i >= 0 && memcmp(g_peer_keys[i].entry.key, entry->key, PS4_P2P_KEY_SIZE) == 0) {
        g_peer_keys[i].references++;
    } else if (i >= 0) {
        g_peer_keys[i].entry = *entry;
        g_peer_keys[i].references = 1;
    } else {
        if (g_peer_key_count == g_peer_key_cap) {
            const int cap = g_peer_key_cap ? g_peer_key_cap * 2 : 8;
            PeerKey *grown = (PeerKey *)realloc(g_peer_keys, (size_t)cap * sizeof(PeerKey));
            if (grown) {
                g_peer_keys = grown;
                g_peer_key_cap = cap;
            }
        }
        if (g_peer_key_count < g_peer_key_cap) g_peer_keys[g_peer_key_count++] = (PeerKey){*peer, *entry, 1};
    }
    pthread_mutex_unlock(&g_keyring_mutex);
}

bool ps4_p2p_keyring_release_peer_key(const Ps4P2PEndpoint *peer, const uint8_t key[PS4_P2P_KEY_SIZE], uint8_t id) {
    pthread_mutex_lock(&g_keyring_mutex);
    const int i = find_peer_key(peer);
    const bool found = i >= 0 && memcmp(g_peer_keys[i].entry.key, key, PS4_P2P_KEY_SIZE) == 0 &&
                       g_peer_keys[i].entry.id == id;
    if (found && --g_peer_keys[i].references == 0) g_peer_keys[i] = g_peer_keys[--g_peer_key_count];
    pthread_mutex_unlock(&g_keyring_mutex);
    return found;
}

void ps4_p2p_keyring_remove_keys_with_id(uint8_t id) {
    pthread_mutex_lock(&g_keyring_mutex);
    for (int i = 0; i < g_peer_key_count;) {
        if (g_peer_keys[i].entry.id == id) {
            g_peer_keys[i] = g_peer_keys[--g_peer_key_count];
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&g_keyring_mutex);
}

static void set_optional_key(bool *has, uint8_t *dst, const uint8_t *key, size_t size) {
    pthread_mutex_lock(&g_keyring_mutex);
    *has = key != NULL;
    if (key) memcpy(dst, key, size);
    pthread_mutex_unlock(&g_keyring_mutex);
}

static bool get_optional_key(const bool *has, const uint8_t *src, uint8_t *out, size_t size) {
    pthread_mutex_lock(&g_keyring_mutex);
    const bool present = *has;
    if (present) memcpy(out, src, size);
    pthread_mutex_unlock(&g_keyring_mutex);
    return present;
}

void ps4_p2p_keyring_set_wildcard_key(const uint8_t *key) {
    set_optional_key(&g_has_wildcard_key, g_wildcard_key, key, PS4_P2P_KEY_SIZE);
}

bool ps4_p2p_keyring_get_wildcard_key(uint8_t out[PS4_P2P_KEY_SIZE]) {
    return get_optional_key(&g_has_wildcard_key, g_wildcard_key, out, PS4_P2P_KEY_SIZE);
}

void ps4_p2p_keyring_set_default_key(const uint8_t *key) {
    set_optional_key(&g_has_default_key, g_default_key, key, PS4_P2P_KEY_SIZE);
}

bool ps4_p2p_keyring_get_default_key(uint8_t out[PS4_P2P_KEY_SIZE]) {
    return get_optional_key(&g_has_default_key, g_default_key, out, PS4_P2P_KEY_SIZE);
}

void ps4_p2p_keyring_set_communication_id(const uint8_t *id) {
    set_optional_key(&g_has_communication_id, g_communication_id, id, 4);
}

bool ps4_p2p_keyring_get_communication_id(uint8_t out[4]) {
    return get_optional_key(&g_has_communication_id, g_communication_id, out, 4);
}

bool ps4_p2p_keyring_find(const Ps4P2PEndpoint *peer, uint8_t out[PS4_P2P_KEY_SIZE]) {
    pthread_mutex_lock(&g_keyring_mutex);
    bool found = true;
    const int i = find_peer_key(peer);
    if (i >= 0) {
        memcpy(out, g_peer_keys[i].entry.key, PS4_P2P_KEY_SIZE);
    } else if (g_has_wildcard_key) {
        memcpy(out, g_wildcard_key, PS4_P2P_KEY_SIZE);
    } else if (g_has_default_key) {
        memcpy(out, g_default_key, PS4_P2P_KEY_SIZE);
    } else {
        found = false;
    }
    pthread_mutex_unlock(&g_keyring_mutex);
    return found;
}

void ps4_p2p_keyring_reset(void) {
    pthread_mutex_lock(&g_keyring_mutex);
    g_peer_key_count = 0;
    g_has_wildcard_key = g_has_default_key = g_has_communication_id = false;
    pthread_mutex_unlock(&g_keyring_mutex);
}

// ---------------------------------------------------------------------------------------------
// Objects

// Readiness flags: [0] readable (data, EOF, error or accept), [1] writable (space or error).
enum { READABLE = 0, WRITABLE = 1 };

typedef struct Readiness {
    Ps4NetWake wake[2];
    bool ready[2];
    int refs;
} Readiness;

typedef struct DgramPacket {
    struct DgramPacket *next;
    Ps4P2PEndpoint from;
    uint16_t src_vport;
    size_t len;
    uint8_t data[];
} DgramPacket;

typedef struct Binding {
    uint16_t vport;
    Readiness *readiness;
    bool has_peer; // set by connect
    Ps4P2PEndpoint peer;
    uint16_t peer_vport;
    DgramPacket *head;
    DgramPacket *tail;
    size_t bytes;
    int refs;
} Binding;

typedef struct Listener Listener;

typedef struct Connection {
    Ps4P2PTransport *transport;
    Ps4P2PEndpoint peer;
    uint16_t peer_vport;
    uint16_t local_vport;
    Ps4P2PProtection protection;
    Ps4P2PTcp *tcp;
    Readiness *readiness;
    Listener *listener;  // during the handshake
    bool pending_accept; // not in the accept queue yet
    bool released;       // no socket owns it
    int refs;
} Connection;

struct Listener {
    uint16_t vport;
    int backlog;
    Ps4P2PProtection protection;
    Readiness *readiness;
    Connection **queue;
    int queue_count;
    int queue_cap;
    int half_open;
    bool closed;
    int refs;
};

typedef struct SigPacket {
    struct SigPacket *next;
    Ps4P2PEndpoint from;
    size_t len;
    uint8_t data[];
} SigPacket;

typedef struct SigQueue {
    SigPacket *head;
    SigPacket *tail;
    size_t count;
} SigQueue;

typedef struct VportUse {
    uint16_t vport;
    int count;
} VportUse;

typedef struct PseudoEntry {
    Ps4P2PEndpoint peer;
    Ps4P2PPseudoHeader header;
} PseudoEntry;

#define PSEUDO_CACHE_MAX 256

struct Ps4P2PTransport {
    atomic_int refs;
    Ps4P2PTransportConfig config;
    int sock;
    uint16_t bound_port;

    pthread_mutex_t mutex;
    Binding **bindings;
    int binding_count;
    int binding_cap;
    Listener **listeners;
    int listener_count;
    int listener_cap;
    Connection **connections;
    int connection_count;
    int connection_cap;
    VportUse *stream_vports;
    int stream_vport_count;
    int stream_vport_cap;
    SigQueue *signaling;
    size_t channel_count;
    uint16_t next_stream_port;    // 49152-65535
    uint16_t next_datagram_vport; // 32768-49999
    uint64_t iss_state;
    double loss_for_testing;
    uint64_t loss_rng;

    PseudoEntry pseudo_cache[PSEUDO_CACHE_MAX];
    int pseudo_count;
    atomic_int reported_broadcasts;

    // Scratch, under the mutex: outgoing frames, decoded payloads, connections due.
    uint8_t *tx;
    size_t tx_cap;
    uint8_t *decoded;
    Connection **due;
    int due_cap;

    pthread_t thread;
    bool thread_started;
    atomic_bool stop;
    Ps4NetWake kick;
    int64_t sleeping_until; // INT64_MIN while the thread is working
    uint8_t *receive_buffer;
};

struct Ps4P2PSocket {
    Ps4P2PTransport *t;
    bool stream;
    Readiness *readiness;
    Ps4P2PProtection protection;
    // Datagrams.
    Binding *binding;
    bool has_peer;
    Ps4P2PEndpoint peer;
    uint16_t peer_vport;
    // Streams.
    uint16_t bound_vport;
    bool owns_vport;
    bool read_shutdown;
    bool closed;
    Listener *listener;
    Connection *connection;
};

static Readiness *readiness_new(void) {
    Readiness *r = (Readiness *)calloc(1, sizeof(Readiness));
    if (!r) return NULL;
    if (!ps4_net_wake_init(&r->wake[0])) {
        free(r);
        return NULL;
    }
    if (!ps4_net_wake_init(&r->wake[1])) {
        ps4_net_wake_destroy(&r->wake[0]);
        free(r);
        return NULL;
    }
    r->refs = 1;
    return r;
}

static void readiness_release(Readiness *r) {
    if (!r || --r->refs > 0) return;
    ps4_net_wake_destroy(&r->wake[0]);
    ps4_net_wake_destroy(&r->wake[1]);
    free(r);
}

// The flag's descriptor is readable while it is set.
static void readiness_set(Readiness *r, int which, bool ready) {
    if (r->ready[which] == ready) return;
    r->ready[which] = ready;
    if (ready) {
        ps4_net_wake_signal(&r->wake[which]);
    } else {
        ps4_net_wake_drain(&r->wake[which]);
    }
}

static void binding_release(Binding *b) {
    if (!b || --b->refs > 0) return;
    for (DgramPacket *p = b->head; p;) {
        DgramPacket *next = p->next;
        free(p);
        p = next;
    }
    readiness_release(b->readiness);
    free(b);
}

static void connection_release(Connection *c);

static void listener_release(Listener *l) {
    if (!l || --l->refs > 0) return;
    for (int i = 0; i < l->queue_count; i++) connection_release(l->queue[i]);
    free(l->queue);
    readiness_release(l->readiness);
    free(l);
}

static void connection_release(Connection *c) {
    if (!c || --c->refs > 0) return;
    ps4_p2p_tcp_destroy(c->tcp);
    readiness_release(c->readiness);
    listener_release(c->listener);
    free(c);
}

// Growable pointer arrays.
static bool grow(void **items, int *cap, int count, size_t size) {
    if (count < *cap) return true;
    const int next = *cap ? *cap * 2 : 8;
    void *grown = realloc(*items, (size_t)next * size);
    if (!grown) return false;
    *items = grown;
    *cap = next;
    return true;
}

static struct sockaddr_in to_sockaddr(const Ps4P2PEndpoint *e) {
    struct sockaddr_in in;
    memset(&in, 0, sizeof(in));
    in.sin_len = sizeof(in);
    in.sin_family = AF_INET;
    in.sin_addr.s_addr = e->addr;
    in.sin_port = e->port;
    return in;
}

// ---------------------------------------------------------------------------------------------
// Codec (FramingCodec)

static bool ensure_tx(Ps4P2PTransport *t, size_t size) {
    if (size <= t->tx_cap) return true;
    size_t cap = t->tx_cap ? t->tx_cap : 2048;
    while (cap < size) cap *= 2;
    uint8_t *grown = (uint8_t *)realloc(t->tx, cap);
    if (!grown) return false;
    t->tx = grown;
    t->tx_cap = cap;
    return true;
}

// Frames [header][digest?][body] into t->tx, protecting the body with to's key when asked
// (sent plain without one, as shadPS4). kind is the base kind; returns the frame size or 0.
static size_t encode(Ps4P2PTransport *t, uint8_t kind, bool datagram_ports, uint16_t src_vport, uint16_t dst_vport,
                     const uint8_t *body, size_t len, const Ps4P2PEndpoint *to, Ps4P2PProtection protection,
                     bool with_digest) {
    if (!ensure_tx(t, FRAME_OVERHEAD + PS4_P2P_PROTECT_OVERHEAD + len)) return 0;
    uint8_t communication_id[4];
    const bool digest = with_digest && ps4_p2p_keyring_get_communication_id(communication_id);
    size_t header = 2;
    if (datagram_ports) {
        t->tx[2] = (uint8_t)(src_vport >> 8);
        t->tx[3] = (uint8_t)src_vport;
        t->tx[4] = (uint8_t)(dst_vport >> 8);
        t->tx[5] = (uint8_t)dst_vport;
        header = 6;
    }
    if (digest) {
        memcpy(t->tx + header, communication_id, 4);
        header += 4;
    }
    uint8_t mode = 0;
    size_t body_len = len;
    uint8_t key[PS4_P2P_KEY_SIZE];
    if ((protection.crypto || protection.signature) && ps4_p2p_keyring_find(to, key) &&
        ps4_p2p_protect(body, len, protection.crypto, protection.signature, key, t->tx + header, &body_len)) {
        mode = (uint8_t)((protection.crypto ? 1 : 0) | (protection.signature ? 2 : 0));
    } else {
        // shadPS4 sends plain without a key (its TODO asks whether to drop instead).
        if (len) memcpy(t->tx + header, body, len);
        body_len = len;
    }
    t->tx[0] = MARKER;
    t->tx[1] = (uint8_t)(FLAG_VALID | (kind + mode) | (digest ? FLAG_DIGEST : 0));
    return header + body_len;
}

typedef enum DecodedKind { DECODED_INVALID, DECODED_STREAM, DECODED_DATAGRAM, DECODED_SIGNALING } DecodedKind;

typedef struct Decoded {
    DecodedKind kind;
    uint16_t src_vport; // datagrams only
    uint16_t dst_vport;
    const uint8_t *payload;
    size_t len;
} Decoded;

static void log_broadcast(const uint8_t *packet, size_t len, const Ps4P2PEndpoint *from) {
    char address[INET_ADDRSTRLEN] = "?";
    struct in_addr in = {from->addr};
    inet_ntop(AF_INET, &in, address, sizeof(address));
    char hex[64 * 3 + 1];
    const size_t shown = len < 64 ? len : 64;
    size_t n = 0;
    for (size_t i = 0; i < shown; i++) n += (size_t)snprintf(hex + n, sizeof(hex) - n, "%s%02x", i ? " " : "", packet[i]);
    hex[n] = 0;
    fprintf(stderr,
            "[ps4-net] P2P broadcast packet received (layout assumed; please report this line): from %s:%u, %zu "
            "bytes: %s%s\n",
            address, ntohs(from->port), len, hex, len > shown ? " ..." : "");
}

static Decoded decode(Ps4P2PTransport *t, const uint8_t *packet, size_t len, const Ps4P2PEndpoint *from) {
    Decoded out = {DECODED_INVALID, 0, 0, NULL, 0};
    if (len < 2 || packet[0] != MARKER || (packet[1] & FLAG_VALID) == 0) return out;
    const uint8_t flags = packet[1];
    const uint8_t kind = flags & 0x0F;
    if (kind < KIND_DATAGRAM || kind > KIND_STREAM + 3) return out;
    const bool stream = kind >= KIND_STREAM;

    size_t offset = 2;
    uint16_t src_vport = 0;
    uint16_t dst_vport = 0;
    if (flags & FLAG_BROADCAST) {
        // The layout is unconfirmed: report a few so it can be checked.
        if (atomic_fetch_add_explicit(&t->reported_broadcasts, 1, memory_order_relaxed) < MAX_REPORTED_BROADCASTS) {
            log_broadcast(packet, len, from);
        }
        if (stream) return out; // no broadcast TCP
        offset = 4;
        if (len < offset) return out;
        dst_vport = (uint16_t)((packet[2] << 8) | packet[3]);
        src_vport = dst_vport;
    } else if (!stream) {
        offset = 6;
        if (len < offset) return out;
        src_vport = (uint16_t)((packet[2] << 8) | packet[3]);
        dst_vport = (uint16_t)((packet[4] << 8) | packet[5]);
    }

    bool has_id = false;
    uint8_t id[4] = {0};
    if (flags & FLAG_DIGEST) {
        if (len < offset + 4) return out;
        memcpy(id, packet + offset, 4);
        has_id = true;
        offset += 4;
    }

    const bool signaling = !stream && dst_vport == SIGNALING_VPORT;
    if (!signaling) {
        uint8_t expected[4];
        if (ps4_p2p_keyring_get_communication_id(expected) && (!has_id || memcmp(id, expected, 4) != 0)) return out;
    }

    const uint8_t mode = (uint8_t)(kind - (stream ? KIND_STREAM : KIND_DATAGRAM));
    const uint8_t *body = packet + offset;
    const size_t body_len = len - offset;
    if (mode != 0) {
        uint8_t key[PS4_P2P_KEY_SIZE];
        size_t plain_len = 0;
        if (!ps4_p2p_keyring_find(from, key) ||
            !ps4_p2p_unprotect(body, body_len, (mode & 1) != 0, (mode & 2) != 0, key, t->decoded, &plain_len)) {
            return out; // no key or a bad signature
        }
        out.payload = t->decoded;
        out.len = plain_len;
    } else {
        out.payload = body;
        out.len = body_len;
    }
    out.kind = stream ? DECODED_STREAM : signaling ? DECODED_SIGNALING : DECODED_DATAGRAM;
    out.src_vport = src_vport;
    out.dst_vport = dst_vport;
    return out;
}

// The checksum pseudo header for a peer: the local address is the one the host routes to it from.
static Ps4P2PPseudoHeader stream_pseudo_header(Ps4P2PTransport *t, const Ps4P2PEndpoint *peer) {
    for (int i = 0; i < t->pseudo_count; i++) {
        if (ps4_p2p_endpoint_equal(&t->pseudo_cache[i].peer, peer)) return t->pseudo_cache[i].header;
    }
    Ps4P2PPseudoHeader pseudo;
    memset(&pseudo, 0, sizeof(pseudo));
    memcpy(pseudo.remote, &peer->addr, 4);
    const int probe = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe >= 0) {
        const struct sockaddr_in to = to_sockaddr(peer);
        struct sockaddr_in local;
        socklen_t local_len = sizeof(local);
        memset(&local, 0, sizeof(local));
        if (connect(probe, (const struct sockaddr *)&to, sizeof(to)) == 0 &&
            getsockname(probe, (struct sockaddr *)&local, &local_len) == 0) {
            memcpy(pseudo.local, &local.sin_addr, 4);
        }
        close(probe);
    }
    if (t->pseudo_count >= PSEUDO_CACHE_MAX) t->pseudo_count = 0;
    t->pseudo_cache[t->pseudo_count++] = (PseudoEntry){*peer, pseudo};
    return pseudo;
}

// ---------------------------------------------------------------------------------------------
// Transport internals (mutex held)

static int send_packet(Ps4P2PTransport *t, const uint8_t *packet, size_t len, const Ps4P2PEndpoint *to) {
    if (len == 0) return ORBIS_NET_ENOBUFS;
    if (t->loss_for_testing > 0) {
        t->loss_rng ^= t->loss_rng << 13;
        t->loss_rng ^= t->loss_rng >> 7;
        t->loss_rng ^= t->loss_rng << 17;
        if ((double)(t->loss_rng >> 11) / (double)(1ull << 53) < t->loss_for_testing) return 0;
    }
    const struct sockaddr_in addr = to_sockaddr(to);
    int64_t sent = 0;
    return ps4_net_host_sendto(t->sock, packet, len, 0, (const struct sockaddr *)&addr, sizeof(addr), &sent);
}

static void emit_stream_segment(void *ctx, const uint8_t *segment, size_t len) {
    Connection *c = (Connection *)ctx;
    Ps4P2PTransport *t = c->transport;
    const size_t n = encode(t, KIND_STREAM, false, 0, 0, segment, len, &c->peer, c->protection, true);
    send_packet(t, t->tx, n, &c->peer);
}

static uint32_t new_iss(Ps4P2PTransport *t) {
    t->iss_state += 0x9e3779b97f4a7c15ull;
    uint64_t z = t->iss_state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;
    const int64_t clock = ps4_net_host_now_ns() / 1000 / 4;
    return (uint32_t)z + (uint32_t)clock;
}

static Binding *find_binding(Ps4P2PTransport *t, uint16_t vport) {
    for (int i = 0; i < t->binding_count; i++) {
        if (t->bindings[i]->vport == vport) return t->bindings[i];
    }
    return NULL;
}

static Listener *find_listener(Ps4P2PTransport *t, uint16_t vport) {
    for (int i = 0; i < t->listener_count; i++) {
        if (t->listeners[i]->vport == vport) return t->listeners[i];
    }
    return NULL;
}

static int find_connection(Ps4P2PTransport *t, const Ps4P2PEndpoint *peer, uint16_t peer_vport, uint16_t local_vport) {
    for (int i = 0; i < t->connection_count; i++) {
        const Connection *c = t->connections[i];
        if (ps4_p2p_endpoint_equal(&c->peer, peer) && c->peer_vport == peer_vport && c->local_vport == local_vport) {
            return i;
        }
    }
    return -1;
}

static void remove_connection_at(Ps4P2PTransport *t, int i) {
    Connection *c = t->connections[i];
    t->connections[i] = t->connections[--t->connection_count];
    connection_release(c);
}

static VportUse *find_stream_vport(Ps4P2PTransport *t, uint16_t vport) {
    for (int i = 0; i < t->stream_vport_count; i++) {
        if (t->stream_vports[i].vport == vport) return &t->stream_vports[i];
    }
    return NULL;
}

static uint16_t allocate_vport(Ps4P2PTransport *t, bool stream) {
    const uint16_t first = stream ? 49152 : 32768;
    const uint16_t last = stream ? 65535 : 49999;
    uint16_t *next = stream ? &t->next_stream_port : &t->next_datagram_vport;
    for (int attempt = 0; attempt <= last - first; attempt++) {
        const uint16_t vport = *next;
        *next = *next == last ? first : (uint16_t)(*next + 1);
        const bool used = stream ? (find_stream_vport(t, vport) || find_listener(t, vport)) : find_binding(t, vport) != NULL;
        if (!used) return vport;
    }
    return 0;
}

// A connection held by the connection table (one reference).
static Connection *new_connection(Ps4P2PTransport *t, const Ps4P2PEndpoint *peer, uint16_t peer_vport,
                                  uint16_t local_vport, Ps4P2PProtection protection, Readiness *readiness) {
    if (!grow((void **)&t->connections, &t->connection_cap, t->connection_count, sizeof(Connection *))) return NULL;
    Connection *c = (Connection *)calloc(1, sizeof(Connection));
    if (!c) return NULL;
    c->transport = t;
    c->peer = *peer;
    c->peer_vport = peer_vport;
    c->local_vport = local_vport;
    c->protection = protection;
    c->readiness = readiness;
    c->released = true;
    c->refs = 1;
    const Ps4P2PPseudoHeader pseudo = stream_pseudo_header(t, peer);
    c->tcp = ps4_p2p_tcp_create(&t->config.tcp, &pseudo, local_vport, peer_vport, new_iss(t), emit_stream_segment, c);
    if (!c->tcp) {
        free(c);
        return NULL;
    }
    readiness->refs++;
    t->connections[t->connection_count++] = c;
    return c;
}

static void update(Ps4P2PTransport *t, Connection *c) {
    const Ps4P2PTcpState state = ps4_p2p_tcp_state(c->tcp);
    if (c->pending_accept) {
        Listener *l = c->listener;
        if (!l || l->closed) {
            c->pending_accept = false;
            c->listener = NULL;
            listener_release(l);
            ps4_p2p_tcp_abort(c->tcp); // the listener is gone
        } else if (state != PS4_P2P_TCP_SYN_RECEIVED) {
            c->pending_accept = false;
            l->half_open--;
            c->listener = NULL;
            if (state != PS4_P2P_TCP_CLOSED &&
                grow((void **)&l->queue, &l->queue_cap, l->queue_count, sizeof(Connection *))) {
                c->refs++;
                l->queue[l->queue_count++] = c;
                readiness_set(l->readiness, READABLE, true);
            }
            listener_release(l);
        }
    }
    readiness_set(c->readiness, READABLE, ps4_p2p_tcp_readable(c->tcp));
    readiness_set(c->readiness, WRITABLE, ps4_p2p_tcp_writable(c->tcp));
    if (ps4_p2p_tcp_state(c->tcp) == PS4_P2P_TCP_CLOSED && c->released) {
        for (int i = 0; i < t->connection_count; i++) {
            if (t->connections[i] == c) {
                remove_connection_at(t, i);
                break;
            }
        }
    }
}

static void reschedule(Ps4P2PTransport *t, const Connection *c) {
    const int64_t d = ps4_p2p_tcp_next_deadline(c->tcp);
    if (d != PS4_P2P_NEVER && d < t->sleeping_until) {
        t->sleeping_until = d;
        ps4_net_wake_signal(&t->kick);
    }
}

static void handle_datagram(Ps4P2PTransport *t, const Decoded *d, const Ps4P2PEndpoint *from) {
    Binding *b = find_binding(t, d->dst_vport);
    if (!b) return;
    if (b->has_peer && !(ps4_p2p_endpoint_equal(&b->peer, from) && b->peer_vport == d->src_vport)) {
        return; // not from the connected peer
    }
    if (b->bytes + d->len > t->config.datagram_queue_bytes) return; // queue full, drop like UDP
    DgramPacket *p = (DgramPacket *)malloc(sizeof(DgramPacket) + d->len);
    if (!p) return;
    p->next = NULL;
    p->from = *from;
    p->src_vport = d->src_vport;
    p->len = d->len;
    if (d->len) memcpy(p->data, d->payload, d->len);
    if (b->tail) {
        b->tail->next = p;
    } else {
        b->head = p;
    }
    b->tail = p;
    b->bytes += d->len;
    readiness_set(b->readiness, READABLE, true);
}

static void handle_stream_segment(Ps4P2PTransport *t, const Decoded *d, const Ps4P2PEndpoint *from, int64_t now) {
    Ps4P2PPseudoHeader pseudo;
    bool have_pseudo = false;
    if (t->config.verify_stream_checksum) {
        pseudo = stream_pseudo_header(t, from);
        have_pseudo = true;
    }
    Ps4P2PTcpSegment seg;
    if (!ps4_p2p_tcp_parse(d->payload, d->len, have_pseudo ? &pseudo : NULL, &seg)) return;
    int i = find_connection(t, from, seg.src_port, seg.dst_port);
    if (i >= 0) {
        Connection *c = t->connections[i];
        if (ps4_p2p_tcp_state(c->tcp) != PS4_P2P_TCP_CLOSED) {
            ps4_p2p_tcp_on_segment(c->tcp, &seg, now);
            update(t, c);
            return;
        }
        if (c->released) {
            remove_connection_at(t, i);
            i = -1;
        }
    }

    const bool bare_syn = (seg.flags & (PS4_P2P_TCP_SYN | PS4_P2P_TCP_ACK | PS4_P2P_TCP_RST)) == PS4_P2P_TCP_SYN;
    if (bare_syn && i < 0) {
        Listener *l = find_listener(t, seg.dst_port);
        if (l) {
            if (l->queue_count + l->half_open >= l->backlog) return; // backlog full, the peer retries
            Readiness *readiness = readiness_new();
            if (!readiness) return;
            Connection *c = new_connection(t, from, seg.src_port, seg.dst_port, l->protection, readiness);
            readiness_release(readiness); // the connection holds it
            if (!c) return;
            l->refs++;
            c->listener = l;
            c->pending_accept = true;
            l->half_open++;
            ps4_p2p_tcp_accept_syn(c->tcp, &seg, now);
            update(t, c);
            return;
        }
    }
    // Nobody listening: RST so the peer fails fast.
    if (!have_pseudo) pseudo = stream_pseudo_header(t, from);
    uint8_t rst[PS4_P2P_TCP_MAX_SEGMENT];
    const size_t rst_len = ps4_p2p_tcp_build_reset_for(rst, &pseudo, &seg);
    if (rst_len) {
        const Ps4P2PProtection none = {false, false};
        const size_t n = encode(t, KIND_STREAM, false, 0, 0, rst, rst_len, from, none, true);
        send_packet(t, t->tx, n, from);
    }
}

static void handle_packet(Ps4P2PTransport *t, const uint8_t *packet, size_t len, const Ps4P2PEndpoint *from,
                          int64_t now) {
    const Decoded d = decode(t, packet, len, from);
    switch (d.kind) {
    case DECODED_STREAM:
        handle_stream_segment(t, &d, from, now);
        break;
    case DECODED_DATAGRAM:
        handle_datagram(t, &d, from);
        break;
    case DECODED_SIGNALING: {
        const size_t channel = t->config.classify_signaling ? t->config.classify_signaling(d.payload, d.len) : 0;
        if (channel >= t->channel_count) break;
        SigQueue *q = &t->signaling[channel];
        if (q->count >= t->config.signaling_queue_packets) break;
        SigPacket *p = (SigPacket *)malloc(sizeof(SigPacket) + d.len);
        if (!p) break;
        p->next = NULL;
        p->from = *from;
        p->len = d.len;
        if (d.len) memcpy(p->data, d.payload, d.len);
        if (q->tail) {
            q->tail->next = p;
        } else {
            q->head = p;
        }
        q->tail = p;
        q->count++;
        break;
    }
    case DECODED_INVALID:
        break;
    }
}

static int64_t earliest_deadline(const Ps4P2PTransport *t) {
    int64_t earliest = PS4_P2P_NEVER;
    for (int i = 0; i < t->connection_count; i++) {
        const int64_t d = ps4_p2p_tcp_next_deadline(t->connections[i]->tcp);
        if (d < earliest) earliest = d;
    }
    return earliest;
}

static void run_timers(Ps4P2PTransport *t, int64_t now) {
    int due = 0;
    for (int i = 0; i < t->connection_count; i++) {
        Connection *c = t->connections[i];
        const int64_t d = ps4_p2p_tcp_next_deadline(c->tcp);
        if (d == PS4_P2P_NEVER || d > now) continue;
        if (!grow((void **)&t->due, &t->due_cap, due, sizeof(Connection *))) break;
        c->refs++;
        t->due[due++] = c;
    }
    for (int i = 0; i < due; i++) {
        Connection *c = t->due[i];
        ps4_p2p_tcp_on_timer(c->tcp, now);
        update(t, c);
        connection_release(c);
    }
}

static void *transport_thread(void *arg) {
    Ps4P2PTransport *t = (Ps4P2PTransport *)arg;
    while (!atomic_load(&t->stop)) {
        pthread_mutex_lock(&t->mutex);
        const int64_t deadline = earliest_deadline(t);
        t->sleeping_until = deadline;
        pthread_mutex_unlock(&t->mutex);
        const Ps4NetWaitResult r = ps4_net_host_wait_one(t->sock, PS4_NET_READABLE, &t->kick, deadline);
        if (atomic_load(&t->stop)) break;
        if (r == PS4_NET_WAIT_WOKEN) ps4_net_wake_drain(&t->kick);

        pthread_mutex_lock(&t->mutex);
        t->sleeping_until = INT64_MIN;
        const int64_t now = ps4_net_host_now_ns();
        for (int i = 0; i < 1024; i++) { // don't starve the timers
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            memset(&from, 0, sizeof(from));
            int64_t n = 0;
            if (ps4_net_host_recvfrom(t->sock, t->receive_buffer, RECEIVE_BUFFER, 0, (struct sockaddr *)&from,
                                      &from_len, &n) != 0) {
                break;
            }
            if (from.sin_family != AF_INET) continue;
            const Ps4P2PEndpoint sender = {from.sin_addr.s_addr, from.sin_port};
            handle_packet(t, t->receive_buffer, (size_t)n, &sender, now);
        }
        run_timers(t, now);
        pthread_mutex_unlock(&t->mutex);
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------------
// Transport

void ps4_p2p_transport_default_config(Ps4P2PTransportConfig *config) {
    memset(config, 0, sizeof(*config));
    config->tcp = ps4_p2p_tcp_default_config;
    config->datagram_queue_bytes = 40 * 1024;
    config->signaling_queue_packets = 256;
    config->signaling_channels = 1;
    config->max_backlog = 64;
}

static void transport_free(Ps4P2PTransport *t) {
    for (int i = 0; i < t->connection_count; i++) connection_release(t->connections[i]);
    for (int i = 0; i < t->listener_count; i++) listener_release(t->listeners[i]);
    for (int i = 0; i < t->binding_count; i++) binding_release(t->bindings[i]);
    for (size_t c = 0; c < t->channel_count; c++) {
        for (SigPacket *p = t->signaling[c].head; p;) {
            SigPacket *next = p->next;
            free(p);
            p = next;
        }
    }
    free(t->connections);
    free(t->listeners);
    free(t->bindings);
    free(t->stream_vports);
    free(t->signaling);
    free(t->tx);
    free(t->decoded);
    free(t->due);
    free(t->receive_buffer);
    if (t->sock >= 0) close(t->sock);
    ps4_net_wake_destroy(&t->kick);
    pthread_mutex_destroy(&t->mutex);
    free(t);
}

int ps4_p2p_transport_create(const Ps4P2PEndpoint *bind_address, const Ps4P2PTransportConfig *config,
                             Ps4P2PTransport **out) {
    *out = NULL;
    Ps4P2PTransport *t = (Ps4P2PTransport *)calloc(1, sizeof(Ps4P2PTransport));
    if (!t) return ORBIS_NET_ENOMEM;
    t->sock = -1;
    atomic_init(&t->refs, 1);
    t->config = *config;
    t->channel_count = config->signaling_channels ? config->signaling_channels : 1;
    t->next_stream_port = 49152;
    t->next_datagram_vport = 32768;
    t->iss_state = (uint64_t)ps4_net_host_now_ns() ^ (uint64_t)(uintptr_t)t;
    t->loss_rng = 0x9e3779b97f4a7c15ull;
    pthread_mutex_init(&t->mutex, NULL);
    if (!ps4_net_wake_init(&t->kick)) {
        pthread_mutex_destroy(&t->mutex);
        free(t);
        return ORBIS_NET_ENOBUFS;
    }
    t->signaling = (SigQueue *)calloc(t->channel_count, sizeof(SigQueue));
    t->receive_buffer = (uint8_t *)malloc(RECEIVE_BUFFER);
    t->decoded = (uint8_t *)malloc(RECEIVE_BUFFER);
    if (!t->signaling || !t->receive_buffer || !t->decoded) {
        transport_free(t);
        return ORBIS_NET_ENOMEM;
    }
    int e = ps4_net_host_create_socket(AF_INET, SOCK_DGRAM, 0, &t->sock);
    if (e != 0) {
        t->sock = -1;
        transport_free(t);
        return e;
    }
    // An exclusive port: nobody else should get the port we advertise.
    const struct sockaddr_in addr = to_sockaddr(bind_address);
    e = ps4_net_host_prepare_bind(t->sock, false, false);
    if (e == 0 && bind(t->sock, (const struct sockaddr *)&addr, sizeof(addr)) != 0) e = ps4_net_host_last_error();
    if (e != 0) {
        transport_free(t);
        return e;
    }
    const int buffer = 1 << 20; // absorb bursts
    setsockopt(t->sock, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
    struct sockaddr_in actual;
    socklen_t actual_len = sizeof(actual);
    memset(&actual, 0, sizeof(actual));
    getsockname(t->sock, (struct sockaddr *)&actual, &actual_len);
    t->bound_port = ntohs(actual.sin_port);
    if (pthread_create(&t->thread, NULL, transport_thread, t) != 0) {
        transport_free(t);
        return ORBIS_NET_EAGAIN;
    }
    t->thread_started = true;
    *out = t;
    return 0;
}

void ps4_p2p_transport_retain(Ps4P2PTransport *t) {
    atomic_fetch_add_explicit(&t->refs, 1, memory_order_relaxed);
}

void ps4_p2p_transport_release(Ps4P2PTransport *t) {
    if (!t || atomic_fetch_sub_explicit(&t->refs, 1, memory_order_acq_rel) != 1) return;
    atomic_store(&t->stop, true);
    ps4_net_wake_signal(&t->kick);
    if (t->thread_started) pthread_join(t->thread, NULL);
    transport_free(t); // the thread is stopped by now
}

uint16_t ps4_p2p_transport_bound_port(const Ps4P2PTransport *t) {
    return t->bound_port;
}

int ps4_p2p_transport_send_signaling(Ps4P2PTransport *t, const uint8_t *data, size_t len, const Ps4P2PEndpoint *to) {
    pthread_mutex_lock(&t->mutex);
    const Ps4P2PProtection none = {false, false};
    const size_t n = encode(t, KIND_DATAGRAM, true, SIGNALING_VPORT, SIGNALING_VPORT, data, len, to, none, false);
    const int e = send_packet(t, t->tx, n, to);
    pthread_mutex_unlock(&t->mutex);
    return e;
}

bool ps4_p2p_transport_recv_signaling(Ps4P2PTransport *t, size_t channel, uint8_t *buf, size_t cap, size_t *len,
                                      Ps4P2PEndpoint *from) {
    pthread_mutex_lock(&t->mutex);
    SigPacket *p = channel < t->channel_count ? t->signaling[channel].head : NULL;
    if (p) {
        SigQueue *q = &t->signaling[channel];
        q->head = p->next;
        if (!q->head) q->tail = NULL;
        q->count--;
    }
    pthread_mutex_unlock(&t->mutex);
    if (!p) return false;
    const size_t n = p->len < cap ? p->len : cap;
    if (buf && n) memcpy(buf, p->data, n);
    *len = n;
    if (from) *from = p->from;
    free(p);
    return true;
}

void ps4_p2p_transport_set_outgoing_loss(Ps4P2PTransport *t, double probability) {
    pthread_mutex_lock(&t->mutex);
    t->loss_for_testing = probability;
    pthread_mutex_unlock(&t->mutex);
}

// ---------------------------------------------------------------------------------------------
// Sockets

static Ps4P2PSocket *socket_alloc(Ps4P2PTransport *t, bool stream, Readiness *readiness) {
    Ps4P2PSocket *s = (Ps4P2PSocket *)calloc(1, sizeof(Ps4P2PSocket));
    if (!s) return NULL;
    ps4_p2p_transport_retain(t);
    s->t = t;
    s->stream = stream;
    s->readiness = readiness;
    return s;
}

Ps4P2PSocket *ps4_p2p_socket_create(Ps4P2PTransport *t, bool stream) {
    Readiness *readiness = readiness_new();
    if (!readiness) return NULL;
    if (!stream) readiness_set(readiness, WRITABLE, true); // UDP is always writable
    Ps4P2PSocket *s = socket_alloc(t, stream, readiness);
    if (!s) readiness_release(readiness);
    return s;
}

bool ps4_p2p_socket_is_stream(const Ps4P2PSocket *s) {
    return s->stream;
}

int ps4_p2p_socket_readable_fd(const Ps4P2PSocket *s) {
    return s->readiness->wake[READABLE].read_fd;
}

int ps4_p2p_socket_writable_fd(const Ps4P2PSocket *s) {
    return s->readiness->wake[WRITABLE].read_fd;
}

void ps4_p2p_socket_set_protection(Ps4P2PSocket *s, Ps4P2PProtection protection) {
    pthread_mutex_lock(&s->t->mutex);
    s->protection = protection;
    pthread_mutex_unlock(&s->t->mutex);
}

Ps4P2PProtection ps4_p2p_socket_get_protection(const Ps4P2PSocket *s) {
    pthread_mutex_lock(&s->t->mutex);
    const Ps4P2PProtection p = s->protection;
    pthread_mutex_unlock(&s->t->mutex);
    return p;
}

static int datagram_bind_locked(Ps4P2PSocket *s, uint16_t vport) {
    Ps4P2PTransport *t = s->t;
    if (s->binding) return ORBIS_NET_EINVAL;
    if (vport == 0 && (vport = allocate_vport(t, false)) == 0) return ORBIS_NET_EADDRNOTAVAIL;
    if (find_binding(t, vport)) return ORBIS_NET_EADDRINUSE;
    if (!grow((void **)&t->bindings, &t->binding_cap, t->binding_count, sizeof(Binding *))) return ORBIS_NET_ENOMEM;
    Binding *b = (Binding *)calloc(1, sizeof(Binding));
    if (!b) return ORBIS_NET_ENOMEM;
    b->vport = vport;
    b->readiness = s->readiness;
    s->readiness->refs++;
    b->has_peer = s->has_peer;
    b->peer = s->peer;
    b->peer_vport = s->peer_vport;
    b->refs = 2; // the table and the socket
    t->bindings[t->binding_count++] = b;
    s->binding = b;
    return 0;
}

static int stream_bind_locked(Ps4P2PSocket *s, uint16_t vport) {
    Ps4P2PTransport *t = s->t;
    if (s->bound_vport != 0 || s->closed) return ORBIS_NET_EINVAL;
    if (vport == 0 && (vport = allocate_vport(t, true)) == 0) return ORBIS_NET_EADDRNOTAVAIL;
    if (find_stream_vport(t, vport)) return ORBIS_NET_EADDRINUSE;
    if (!grow((void **)&t->stream_vports, &t->stream_vport_cap, t->stream_vport_count, sizeof(VportUse))) {
        return ORBIS_NET_ENOMEM;
    }
    t->stream_vports[t->stream_vport_count++] = (VportUse){vport, 1};
    s->bound_vport = vport;
    s->owns_vport = true;
    return 0;
}

int ps4_p2p_socket_bind(Ps4P2PSocket *s, uint16_t vport) {
    pthread_mutex_lock(&s->t->mutex);
    const int e = s->stream ? stream_bind_locked(s, vport) : datagram_bind_locked(s, vport);
    pthread_mutex_unlock(&s->t->mutex);
    return e;
}

int ps4_p2p_socket_listen(Ps4P2PSocket *s, int backlog) {
    if (!s->stream) return ORBIS_NET_EOPNOTSUPP;
    Ps4P2PTransport *t = s->t;
    pthread_mutex_lock(&t->mutex);
    int e = 0;
    if (s->connection || s->closed) {
        e = ORBIS_NET_EINVAL;
    } else if (s->bound_vport == 0 && stream_bind_locked(s, 0) != 0) {
        e = ORBIS_NET_EADDRNOTAVAIL;
    } else {
        const int clamped = backlog < 1 ? 1 : backlog > t->config.max_backlog ? t->config.max_backlog : backlog;
        if (s->listener) {
            s->listener->backlog = clamped;
        } else if (!grow((void **)&t->listeners, &t->listener_cap, t->listener_count, sizeof(Listener *))) {
            e = ORBIS_NET_ENOMEM;
        } else {
            Listener *l = (Listener *)calloc(1, sizeof(Listener));
            if (!l) {
                e = ORBIS_NET_ENOMEM;
            } else {
                l->vport = s->bound_vport;
                l->backlog = clamped;
                l->protection = s->protection;
                l->readiness = s->readiness;
                s->readiness->refs++;
                l->refs = 2; // the table and the socket
                t->listeners[t->listener_count++] = l;
                s->listener = l;
            }
        }
    }
    pthread_mutex_unlock(&t->mutex);
    return e;
}

int ps4_p2p_socket_connect(Ps4P2PSocket *s, const Ps4P2PEndpoint *peer, uint16_t peer_vport) {
    Ps4P2PTransport *t = s->t;
    pthread_mutex_lock(&t->mutex);
    int e;
    if (!s->stream) {
        // SetPeer.
        if (peer_vport == 0) {
            e = ORBIS_NET_EADDRNOTAVAIL;
        } else {
            s->has_peer = true;
            s->peer = *peer;
            s->peer_vport = peer_vport;
            if (s->binding) {
                s->binding->has_peer = true;
                s->binding->peer = *peer;
                s->binding->peer_vport = peer_vport;
            }
            e = 0;
        }
    } else if (s->listener || s->closed) {
        e = ORBIS_NET_EINVAL;
    } else if (s->connection) {
        const Ps4P2PTcpState state = ps4_p2p_tcp_state(s->connection->tcp);
        e = state == PS4_P2P_TCP_SYN_SENT || state == PS4_P2P_TCP_SYN_RECEIVED ? ORBIS_NET_EALREADY
                                                                                : ORBIS_NET_EISCONN;
    } else if (peer_vport == 0) {
        e = ORBIS_NET_EADDRNOTAVAIL;
    } else if (s->bound_vport == 0 && stream_bind_locked(s, 0) != 0) {
        e = ORBIS_NET_EADDRNOTAVAIL;
    } else if (find_connection(t, peer, peer_vport, s->bound_vport) >= 0) {
        e = ORBIS_NET_EADDRINUSE; // e.g. an old connection in TIME-WAIT
    } else {
        Connection *c = new_connection(t, peer, peer_vport, s->bound_vport, s->protection, s->readiness);
        if (!c) {
            e = ORBIS_NET_ENOMEM;
        } else {
            c->refs++; // the socket's
            c->released = false;
            s->connection = c;
            ps4_p2p_tcp_connect(c->tcp, ps4_net_host_now_ns());
            update(t, c);
            reschedule(t, c);
            e = ORBIS_NET_EINPROGRESS;
        }
    }
    pthread_mutex_unlock(&t->mutex);
    return e;
}

int ps4_p2p_socket_connect_result(const Ps4P2PSocket *s) {
    pthread_mutex_lock(&s->t->mutex);
    int e;
    if (!s->connection) {
        e = ORBIS_NET_ENOTCONN;
    } else if ((e = ps4_p2p_tcp_pending_error(s->connection->tcp)) == 0) {
        switch (ps4_p2p_tcp_state(s->connection->tcp)) {
        case PS4_P2P_TCP_SYN_SENT:
        case PS4_P2P_TCP_SYN_RECEIVED:
            e = ORBIS_NET_EINPROGRESS;
            break;
        case PS4_P2P_TCP_CLOSED:
            e = ORBIS_NET_ENOTCONN;
            break;
        default:
            e = 0;
            break;
        }
    }
    pthread_mutex_unlock(&s->t->mutex);
    return e;
}

Ps4P2PSocket *ps4_p2p_socket_accept(Ps4P2PSocket *s, Ps4P2PEndpoint *peer, uint16_t *peer_vport, int *error) {
    Ps4P2PTransport *t = s->t;
    pthread_mutex_lock(&t->mutex);
    Ps4P2PSocket *accepted = NULL;
    Listener *l = s->listener;
    if (!s->stream) {
        *error = ORBIS_NET_EOPNOTSUPP;
    } else if (!l) {
        *error = ORBIS_NET_EINVAL;
    } else if (l->queue_count == 0) {
        *error = ORBIS_NET_EWOULDBLOCK;
    } else if (!(accepted = socket_alloc(t, true, l->queue[0]->readiness))) {
        *error = ORBIS_NET_ENOMEM;
    } else {
        Connection *c = l->queue[0]; // its queue reference passes to the socket
        memmove(l->queue, l->queue + 1, (size_t)(l->queue_count - 1) * sizeof(Connection *));
        l->queue_count--;
        if (l->queue_count == 0) readiness_set(s->readiness, READABLE, false);
        c->released = false;
        c->readiness->refs++;
        accepted->protection = c->protection;
        accepted->bound_vport = c->local_vport; // the listener owns the vport
        accepted->connection = c;
        if (peer) *peer = c->peer;
        if (peer_vport) *peer_vport = c->peer_vport;
        update(t, c);
        *error = 0;
    }
    pthread_mutex_unlock(&t->mutex);
    return accepted;
}

int64_t ps4_p2p_socket_send(Ps4P2PSocket *s, const uint8_t *data, size_t len, const Ps4P2PEndpoint *to,
                            uint16_t to_vport, Ps4P2PProtection extra) {
    Ps4P2PTransport *t = s->t;
    pthread_mutex_lock(&t->mutex);
    int64_t r;
    if (s->stream) {
        if (!s->connection) {
            r = -ORBIS_NET_ENOTCONN;
        } else {
            r = ps4_p2p_tcp_send(s->connection->tcp, data, len, ps4_net_host_now_ns());
            update(t, s->connection);
            reschedule(t, s->connection);
        }
    } else if (!s->binding && datagram_bind_locked(s, 0) != 0) { // implicit bind
        r = -ORBIS_NET_EADDRNOTAVAIL;
    } else {
        if (!to) {
            to = s->has_peer ? &s->peer : NULL;
            to_vport = s->peer_vport;
        }
        if (!to) {
            r = -ORBIS_NET_EDESTADDRREQ;
        } else if (to_vport == 0) {
            r = -ORBIS_NET_EADDRNOTAVAIL;
        } else {
            const Ps4P2PProtection protection = {s->protection.crypto || extra.crypto,
                                                 s->protection.signature || extra.signature};
            const size_t n =
                encode(t, KIND_DATAGRAM, true, s->binding->vport, to_vport, data, len, to, protection, true);
            const int e = send_packet(t, t->tx, n, to);
            // The PS4 drops instead of returning ENOBUFS.
            r = e == 0 || e == ORBIS_NET_EWOULDBLOCK || e == ORBIS_NET_ENOBUFS ? (int64_t)len : -e;
        }
    }
    pthread_mutex_unlock(&t->mutex);
    return r;
}

int64_t ps4_p2p_socket_recv(Ps4P2PSocket *s, uint8_t *out, size_t len, bool peek, Ps4P2PEndpoint *from,
                            uint16_t *from_vport) {
    Ps4P2PTransport *t = s->t;
    pthread_mutex_lock(&t->mutex);
    int64_t r;
    if (s->stream) {
        if (!s->connection) {
            r = -ORBIS_NET_ENOTCONN;
        } else if (s->read_shutdown) {
            r = 0;
        } else {
            r = ps4_p2p_tcp_recv(s->connection->tcp, out, len, peek);
            if (r >= 0) {
                if (from) *from = s->connection->peer;
                if (from_vport) *from_vport = s->connection->peer_vport;
            }
            update(t, s->connection);
        }
    } else if (!s->binding || !s->binding->head) {
        r = -ORBIS_NET_EWOULDBLOCK;
    } else {
        Binding *b = s->binding;
        DgramPacket *p = b->head;
        const size_t n = len < p->len ? len : p->len; // the rest is discarded
        if (n) memcpy(out, p->data, n);
        if (from) *from = p->from;
        if (from_vport) *from_vport = p->src_vport;
        if (!peek) {
            b->bytes -= p->len;
            b->head = p->next;
            if (!b->head) {
                b->tail = NULL;
                readiness_set(s->readiness, READABLE, false);
            }
            free(p);
        }
        r = (int64_t)n;
    }
    pthread_mutex_unlock(&t->mutex);
    return r;
}

int ps4_p2p_socket_shutdown(Ps4P2PSocket *s, int how) {
    if (!s->stream) return 0;
    Ps4P2PTransport *t = s->t;
    pthread_mutex_lock(&t->mutex);
    int e = 0;
    if (!s->connection) {
        e = ORBIS_NET_ENOTCONN;
    } else if (how < 0 || how > 2) {
        e = ORBIS_NET_EINVAL;
    } else {
        if (how != 1) {
            s->read_shutdown = true;
            readiness_set(s->readiness, READABLE, true); // reads return EOF now
        }
        if (how != 0) {
            ps4_p2p_tcp_shutdown(s->connection->tcp, ps4_net_host_now_ns());
            update(t, s->connection);
            reschedule(t, s->connection);
        }
    }
    pthread_mutex_unlock(&t->mutex);
    return e;
}

static void datagram_close_locked(Ps4P2PSocket *s) {
    Ps4P2PTransport *t = s->t;
    Binding *b = s->binding;
    if (!b) return;
    s->binding = NULL;
    for (int i = 0; i < t->binding_count; i++) {
        if (t->bindings[i] == b) {
            t->bindings[i] = t->bindings[--t->binding_count];
            b->refs--; // the table's; the socket's is still held
            break;
        }
    }
    binding_release(b);
}

static void stream_close_locked(Ps4P2PSocket *s) {
    Ps4P2PTransport *t = s->t;
    if (s->closed) return;
    s->closed = true;
    if (s->listener) {
        Listener *l = s->listener;
        l->closed = true;
        for (int i = 0; i < t->listener_count; i++) {
            if (t->listeners[i] == l) {
                t->listeners[i] = t->listeners[--t->listener_count];
                listener_release(l); // the table's
                break;
            }
        }
        for (int i = 0; i < l->queue_count; i++) {
            Connection *pending = l->queue[i];
            ps4_p2p_tcp_abort(pending->tcp); // reset unaccepted ones, like BSD
            update(t, pending);
            connection_release(pending);
        }
        l->queue_count = 0;
        s->listener = NULL;
        listener_release(l);
    }
    if (s->connection) {
        Connection *c = s->connection;
        c->released = true;
        ps4_p2p_tcp_close(c->tcp, ps4_net_host_now_ns()); // FIN, or RST with unread data
        update(t, c);
        reschedule(t, c);
        s->connection = NULL;
        connection_release(c);
    }
    if (s->owns_vport) {
        VportUse *use = find_stream_vport(t, s->bound_vport);
        if (use && --use->count <= 0) *use = t->stream_vports[--t->stream_vport_count];
        s->owns_vport = false;
    }
}

void ps4_p2p_socket_destroy(Ps4P2PSocket *s) {
    if (!s) return;
    Ps4P2PTransport *t = s->t;
    pthread_mutex_lock(&t->mutex);
    if (s->stream) {
        stream_close_locked(s);
    } else {
        datagram_close_locked(s);
    }
    readiness_release(s->readiness);
    pthread_mutex_unlock(&t->mutex);
    free(s);
    ps4_p2p_transport_release(t);
}

void ps4_p2p_socket_abort(Ps4P2PSocket *s) {
    pthread_mutex_lock(&s->t->mutex);
    if (s->connection) {
        ps4_p2p_tcp_abort(s->connection->tcp);
        update(s->t, s->connection);
    }
    pthread_mutex_unlock(&s->t->mutex);
}

uint32_t ps4_p2p_socket_events(const Ps4P2PSocket *s) {
    pthread_mutex_lock(&s->t->mutex);
    uint32_t events = 0;
    if (!s->stream) {
        events = PS4_NET_EV_OUT | (s->binding && s->binding->head ? PS4_NET_EV_IN : 0u);
    } else if (s->listener) {
        events = s->listener->queue_count ? PS4_NET_EV_IN : 0u;
    } else if (s->connection) {
        const Ps4P2PTcp *tcp = s->connection->tcp;
        if (ps4_p2p_tcp_readable(tcp) || s->read_shutdown) events |= PS4_NET_EV_IN;
        if (ps4_p2p_tcp_writable(tcp)) events |= PS4_NET_EV_OUT;
        if (ps4_p2p_tcp_pending_error(tcp) != 0) events |= PS4_NET_EV_ERR;
        const Ps4P2PTcpState state = ps4_p2p_tcp_state(tcp);
        // shadPS4's TODO: a peer FIN alone is RDHUP on Linux, not HUP.
        if (state == PS4_P2P_TCP_CLOSED || state == PS4_P2P_TCP_TIME_WAIT || ps4_p2p_tcp_received_fin(tcp)) {
            events |= PS4_NET_EV_HUP;
        }
    }
    pthread_mutex_unlock(&s->t->mutex);
    return events;
}

uint16_t ps4_p2p_socket_bound_vport(const Ps4P2PSocket *s) {
    pthread_mutex_lock(&s->t->mutex);
    const uint16_t vport = s->stream ? s->bound_vport : s->binding ? s->binding->vport : 0;
    pthread_mutex_unlock(&s->t->mutex);
    return vport;
}

uint16_t ps4_p2p_socket_transport_port(const Ps4P2PSocket *s) {
    return s->t->bound_port;
}

bool ps4_p2p_socket_get_peer(const Ps4P2PSocket *s, Ps4P2PEndpoint *peer, uint16_t *peer_vport) {
    pthread_mutex_lock(&s->t->mutex);
    bool connected = false;
    if (s->stream && s->connection) {
        *peer = s->connection->peer;
        *peer_vport = s->connection->peer_vport;
        connected = true;
    } else if (!s->stream && s->has_peer) {
        *peer = s->peer;
        *peer_vport = s->peer_vport;
        connected = true;
    }
    pthread_mutex_unlock(&s->t->mutex);
    return connected;
}

int ps4_p2p_socket_tcp_state(const Ps4P2PSocket *s) {
    pthread_mutex_lock(&s->t->mutex);
    const int state = s->stream && s->connection ? (int)ps4_p2p_tcp_state(s->connection->tcp) : -1;
    pthread_mutex_unlock(&s->t->mutex);
    return state;
}
