// SPDX-License-Identifier: GPL-2.0-or-later

// The process's P2P transport and the key ioctls: shadPS4 libraries/net/net_p2p.cpp and
// net_p2p_ioctl.cpp.

#include "ps4_net_p2p.h"
#include "ps4_net_types.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CHANNEL_SIGNALING = 0, CHANNEL_CONTROL = 1, CHANNEL_MATCHING2 = 2, CHANNEL_COUNT };

#define PORT_FALLBACKS 16
#define RETRY_NS (2LL * 1000000000LL)

static size_t classify_signaling(const uint8_t *packet, size_t len) {
    if (len < 5 || memcmp(packet, "SHAD", 4) != 0) return CHANNEL_SIGNALING;
    switch (packet[4]) {
    case 0x10:
        return CHANNEL_CONTROL;
    case 0x21:
        return CHANNEL_MATCHING2;
    default:
        return CHANNEL_SIGNALING;
    }
}

typedef struct P2PState {
    pthread_mutex_t mutex;
    Ps4P2PTransport *transport;
    uint16_t configured_port; // host order
    uint32_t advertised_addr; // network order
    int64_t retry_after;
    uint16_t last_failure_port;
    int last_failure_error;
} P2PState;

static P2PState g_state = {PTHREAD_MUTEX_INITIALIZER, NULL, PS4_P2P_DEFAULT_UDP_PORT, 0, 0, 0, 0};

// shadPS4's p2p_port setting hook.
static uint16_t setting_port(void) {
    const char *v = getenv("PS4_P2P_PORT");
    if (!v || !*v) return PS4_P2P_DEFAULT_UDP_PORT;
    char *end = NULL;
    const long port = strtol(v, &end, 10);
    if (*end != 0 || port < 0 || port > 0xffff) {
        fprintf(stderr, "[ps4-net] PS4_P2P_PORT %s is not a UDP port, using %d\n", v, PS4_P2P_DEFAULT_UDP_PORT);
        return PS4_P2P_DEFAULT_UDP_PORT;
    }
    return (uint16_t)port;
}

static uint16_t port_to_bind(void) {
    return setting_port();
}

static uint16_t start_locked(uint16_t udp_port, bool allow_fallback) {
    Ps4P2PTransportConfig config;
    ps4_p2p_transport_default_config(&config);
    config.signaling_channels = CHANNEL_COUNT;
    config.classify_signaling = classify_signaling;
    Ps4P2PTransport *transport = NULL;
    Ps4P2PEndpoint bind_address = {htonl(INADDR_ANY), htons(udp_port)};
    int error = ps4_p2p_transport_create(&bind_address, &config, &transport);
    for (int i = 1; !transport && allow_fallback && error == ORBIS_NET_EADDRINUSE && udp_port != 0 &&
                    i <= PORT_FALLBACKS && udp_port + i <= 0xffff;
         i++) {
        bind_address.port = htons((uint16_t)(udp_port + i));
        error = ps4_p2p_transport_create(&bind_address, &config, &transport);
    }
    if (!transport) {
        if (g_state.last_failure_port != udp_port || g_state.last_failure_error != error) {
            g_state.last_failure_port = udp_port;
            g_state.last_failure_error = error;
            fprintf(stderr, "[ps4-net] cannot start P2P on UDP port %u: errno %d\n", udp_port, error);
        }
        return 0;
    }
    g_state.last_failure_port = 0;
    g_state.last_failure_error = 0;
    const uint16_t port = ps4_p2p_transport_bound_port(transport);
    g_state.transport = transport;
    if (udp_port != 0 && port != udp_port) {
        fprintf(stderr,
                "[ps4-net] UDP port %u is in use (another instance?), P2P transport on UDP port %u instead. Set "
                "PS4_P2P_PORT to choose one.\n",
                udp_port, port);
    }
    return port;
}

static Ps4P2PTransport *running_transport(void) {
    pthread_mutex_lock(&g_state.mutex);
    const int64_t now = ps4_net_host_now_ns();
    if (!g_state.transport && now >= g_state.retry_after) {
        if (start_locked(port_to_bind(), true) == 0) g_state.retry_after = now + RETRY_NS;
    }
    Ps4P2PTransport *t = g_state.transport;
    if (t) ps4_p2p_transport_retain(t);
    pthread_mutex_unlock(&g_state.mutex);
    return t;
}

void ps4_net_p2p_configure(uint16_t udp_port, uint32_t advertised_addr) {
    pthread_mutex_lock(&g_state.mutex);
    g_state.configured_port = udp_port;
    g_state.advertised_addr = advertised_addr;
    pthread_mutex_unlock(&g_state.mutex);
}

uint16_t ps4_net_p2p_start(uint16_t udp_port) {
    ps4_net_p2p_stop();
    pthread_mutex_lock(&g_state.mutex);
    const uint16_t started = start_locked(udp_port, false);
    g_state.retry_after = 0;
    pthread_mutex_unlock(&g_state.mutex);
    return started;
}

void ps4_net_p2p_stop(void) {
    pthread_mutex_lock(&g_state.mutex);
    Ps4P2PTransport *t = g_state.transport;
    g_state.transport = NULL;
    g_state.retry_after = 0;
    pthread_mutex_unlock(&g_state.mutex);
    // Open P2P sockets keep it alive until they close.
    ps4_p2p_transport_release(t);
}

bool ps4_net_p2p_ensure_transport(void) {
    Ps4P2PTransport *t = running_transport();
    ps4_p2p_transport_release(t);
    return t != NULL;
}

bool ps4_net_p2p_transport_is_ready(void) {
    pthread_mutex_lock(&g_state.mutex);
    const bool ready = g_state.transport != NULL;
    pthread_mutex_unlock(&g_state.mutex);
    return ready;
}

Ps4P2PTransport *ps4_net_p2p_acquire_transport(void) {
    pthread_mutex_lock(&g_state.mutex);
    Ps4P2PTransport *t = g_state.transport;
    if (t) ps4_p2p_transport_retain(t);
    pthread_mutex_unlock(&g_state.mutex);
    return t;
}

uint16_t ps4_net_p2p_configured_port(void) {
    return port_to_bind();
}

uint16_t ps4_net_p2p_bound_port(void) {
    pthread_mutex_lock(&g_state.mutex);
    const uint16_t port = g_state.transport ? ps4_p2p_transport_bound_port(g_state.transport) : 0;
    pthread_mutex_unlock(&g_state.mutex);
    return port;
}

uint32_t ps4_net_p2p_advertised_addr(void) {
    // shadPS4 falls back to its system hook (the UPnP or NP server's view of the public
    // address); without NP there is none.
    pthread_mutex_lock(&g_state.mutex);
    const uint32_t addr = g_state.advertised_addr;
    pthread_mutex_unlock(&g_state.mutex);
    return addr;
}

uint16_t ps4_net_p2p_advertised_port(void) {
    const uint16_t bound = ps4_net_p2p_bound_port();
    return bound != 0 ? bound : ps4_net_p2p_configured_port();
}

static int channel_send(const void *data, uint32_t len, uint32_t dest_addr, uint16_t dest_port) {
    Ps4P2PTransport *t = running_transport();
    if (!t || (!data && len != 0)) {
        ps4_p2p_transport_release(t);
        return -1;
    }
    const Ps4P2PEndpoint to = {dest_addr, dest_port};
    const int e = ps4_p2p_transport_send_signaling(t, (const uint8_t *)data, len, &to);
    ps4_p2p_transport_release(t);
    return e == 0 ? (int)len : -1;
}

static int channel_recv(size_t channel, void *buf, uint32_t len, uint32_t *from_addr, uint16_t *from_port) {
    Ps4P2PTransport *t = running_transport();
    if (!t) return -1;
    size_t n = 0;
    Ps4P2PEndpoint from = {0, 0};
    const bool got = ps4_p2p_transport_recv_signaling(t, channel, (uint8_t *)buf, buf ? len : 0, &n, &from);
    ps4_p2p_transport_release(t);
    if (!got) return -1;
    if (from_addr) *from_addr = from.addr;
    if (from_port) *from_port = from.port;
    return (int)n;
}

int ps4_net_p2p_signaling_send(const void *data, uint32_t len, uint32_t dest_addr, uint16_t dest_port) {
    return channel_send(data, len, dest_addr, dest_port);
}

int ps4_net_p2p_signaling_recv(void *buf, uint32_t len, uint32_t *from_addr, uint16_t *from_port) {
    return channel_recv(CHANNEL_SIGNALING, buf, len, from_addr, from_port);
}

int ps4_net_p2p_control_send(const void *data, uint32_t len, uint32_t dest_addr, uint16_t dest_port) {
    return channel_send(data, len, dest_addr, dest_port);
}

int ps4_net_p2p_control_recv(void *buf, uint32_t len, uint32_t *from_addr, uint16_t *from_port) {
    return channel_recv(CHANNEL_CONTROL, buf, len, from_addr, from_port);
}

int ps4_net_p2p_matching2_send(const void *data, uint32_t len, uint32_t dest_addr, uint16_t dest_port) {
    return channel_send(data, len, dest_addr, dest_port);
}

int ps4_net_p2p_matching2_recv(void *buf, uint32_t len, uint32_t *from_addr, uint16_t *from_port) {
    return channel_recv(CHANNEL_MATCHING2, buf, len, from_addr, from_port);
}

// ---------------------------------------------------------------------------------------------
// Key ioctls (net_p2p_ioctl.cpp). A record is an OrbisNetSockaddrIn (IPv4 only), the 16-byte
// key, the local port, flags and the key id.

#define ADDRESS_SIZE sizeof(OrbisNetSockaddrIn)
#define RECORD_SIZE (ADDRESS_SIZE + 20)
#define KEY_OFFSET ADDRESS_SIZE
#define LOCAL_PORT_OFFSET (RECORD_SIZE - 4)
#define FLAGS_OFFSET (RECORD_SIZE - 2)
#define ID_OFFSET (RECORD_SIZE - 1)
#define DEFAULT_KEY_PORT 3658 // the loopback port that also gets the default key

static Ps4P2PKeyEntry read_entry(const uint8_t *bytes) {
    Ps4P2PKeyEntry entry;
    memcpy(entry.key, bytes + KEY_OFFSET, PS4_P2P_KEY_SIZE);
    memcpy(&entry.local_port, bytes + LOCAL_PORT_OFFSET, sizeof(entry.local_port));
    entry.flags = bytes[FLAGS_OFFSET];
    entry.id = bytes[ID_OFFSET];
    return entry;
}

static bool read_peer(const uint8_t *bytes, Ps4P2PEndpoint *peer) {
    OrbisNetSockaddrIn in;
    memcpy(&in, bytes, sizeof(in));
    if (in.sin_family != ORBIS_NET_AF_INET) return false;
    peer->addr = in.sin_addr;
    peer->port = in.sin_port;
    return true;
}

static void write_key(uint8_t *bytes, bool present, const uint8_t *key) {
    if (present) {
        memcpy(bytes + KEY_OFFSET, key, PS4_P2P_KEY_SIZE);
    } else {
        memset(bytes + KEY_OFFSET, 0, PS4_P2P_KEY_SIZE);
    }
}

int ps4_net_p2p_key_ioctl(uint16_t np_port, uint64_t cmd, void *data) {
    const uint32_t control = (uint32_t)cmd;
    if ((control & 0xff00) != 0x5000) return ORBIS_NET_EINVAL;
    if (control == 0x200050c8) return 0; // enable (no-op)
    if ((control & 0xff) == 0xd9) {
        ps4_p2p_keyring_set_communication_id(NULL);
        return 0;
    }
    if (!data) return ORBIS_NET_EFAULT;
    uint8_t *bytes = (uint8_t *)data;
    if (control == 0x802450c8 || control == 0x803450c8) {
        ps4_p2p_keyring_remove_keys_with_id(bytes[ID_OFFSET]);
        return 0;
    }
    // Key values are never logged.
    switch (control & 0xff) {
    case 0xc9: {
        const Ps4P2PKeyEntry entry = read_entry(bytes);
        ps4_p2p_keyring_set_default_key(entry.key);
        const Ps4P2PEndpoint loopback = {htonl(INADDR_LOOPBACK), htons(DEFAULT_KEY_PORT)};
        ps4_p2p_keyring_add_peer_key(&loopback, &entry);
        return 0;
    }
    case 0xca: {
        uint8_t key[PS4_P2P_KEY_SIZE];
        write_key(bytes, ps4_p2p_keyring_get_default_key(key), key);
        return 0;
    }
    case 0xcb: {
        Ps4P2PEndpoint peer;
        if (!read_peer(bytes, &peer)) return ORBIS_NET_EINVAL;
        Ps4P2PKeyEntry entry = read_entry(bytes);
        if (entry.local_port == 0) entry.local_port = np_port;
        ps4_p2p_keyring_add_peer_key(&peer, &entry);
        return 0;
    }
    case 0xcc: {
        Ps4P2PEndpoint peer;
        if (!read_peer(bytes, &peer)) return ORBIS_NET_EINVAL;
        const Ps4P2PKeyEntry entry = read_entry(bytes);
        return ps4_p2p_keyring_release_peer_key(&peer, entry.key, entry.id) ? 0 : ORBIS_NET_ENOENT;
    }
    case 0xcd: {
        const Ps4P2PKeyEntry entry = read_entry(bytes);
        ps4_p2p_keyring_set_wildcard_key(entry.key);
        return 0;
    }
    case 0xce: {
        uint8_t key[PS4_P2P_KEY_SIZE];
        write_key(bytes, ps4_p2p_keyring_get_wildcard_key(key), key);
        return 0;
    }
    case 0xcf:
    case 0xd0:
        memcpy(bytes, &np_port, sizeof(np_port));
        return 0;
    case 0xd8: {
        uint8_t id[4];
        ps4_p2p_derive_communication_id(bytes, id);
        ps4_p2p_keyring_set_communication_id(id);
        return 0;
    }
    default:
        fprintf(stderr, "[ps4-net] P2P ioctl 0x%x: unknown command\n", control);
        return ORBIS_NET_EINVAL;
    }
}
