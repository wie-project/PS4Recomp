// SPDX-License-Identifier: GPL-2.0-or-later

// The P2P transport, after shadPS4 (core/net/p2p_transport.cpp, p2p_codec.cpp): one UDP socket
// shared by P2P datagram sockets, P2P TCP streams (ps4_net_p2p_tcp.c) and signaling, with
// virtual ports, the PS4 framing and the keys NP installs through sceNetIoctl.
//
// Wire format (vports big endian):
//   Datagram:  [0xFF][flags|kind][src vport 2][dst vport 2][digest 4?][payload]
//   Broadcast: [0xFF][flags|0x40|kind][port 2][digest 4?][payload], datagrams only (layout
//              assumed by shadPS4, not confirmed)
//   Stream:    [0xFF][flags|kind][digest 4?][TCP segment]
//   Signaling: a plain datagram from and to vport 0xFFFF.
// flags: 0x80 valid, 0x40 broadcast, 0x20 digest. kind: UDP 3-6, TCP 7-10; +1 crypto, +2
// signature. digest: the communication ID (ps4_p2p_derive_communication_id of the value NP
// installs with ioctl 0xd8).
//
// IPv4 only: shadPS4 only creates an AF_INET transport. Errors are FreeBSD errno values.

#ifndef PS4_NET_P2P_TRANSPORT_H
#define PS4_NET_P2P_TRANSPORT_H

#include "ps4_net_host.h"
#include "ps4_net_p2p_crypto.h"
#include "ps4_net_p2p_tcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// An IPv4 UDP endpoint, both fields in network byte order.
typedef struct Ps4P2PEndpoint {
    uint32_t addr;
    uint16_t port;
} Ps4P2PEndpoint;

static inline bool ps4_p2p_endpoint_equal(const Ps4P2PEndpoint *a, const Ps4P2PEndpoint *b) {
    return a->addr == b->addr && a->port == b->port;
}

typedef struct Ps4P2PProtection {
    bool crypto;
    bool signature;
} Ps4P2PProtection;

// ---------------------------------------------------------------------------------------------
// The keyring (one per process, like shadPS4's P2PState keyring)

typedef struct Ps4P2PKeyEntry {
    uint8_t key[PS4_P2P_KEY_SIZE];
    uint16_t local_port; // network byte order
    uint8_t flags;
    uint8_t id; // groups keys for removal
} Ps4P2PKeyEntry;

// Replaces the peer's key; the same key again only adds a reference (NP installs it per user).
void ps4_p2p_keyring_add_peer_key(const Ps4P2PEndpoint *peer, const Ps4P2PKeyEntry *entry);
// Drops a reference. False if that key and id are not installed.
bool ps4_p2p_keyring_release_peer_key(const Ps4P2PEndpoint *peer, const uint8_t key[PS4_P2P_KEY_SIZE], uint8_t id);
void ps4_p2p_keyring_remove_keys_with_id(uint8_t id);
void ps4_p2p_keyring_set_wildcard_key(const uint8_t *key); // NULL clears
bool ps4_p2p_keyring_get_wildcard_key(uint8_t out[PS4_P2P_KEY_SIZE]);
void ps4_p2p_keyring_set_default_key(const uint8_t *key); // NULL clears
bool ps4_p2p_keyring_get_default_key(uint8_t out[PS4_P2P_KEY_SIZE]);
// The peer's key, else the wildcard key, else the default key.
bool ps4_p2p_keyring_find(const Ps4P2PEndpoint *peer, uint8_t out[PS4_P2P_KEY_SIZE]);
void ps4_p2p_keyring_set_communication_id(const uint8_t *id); // 4 bytes; NULL clears
bool ps4_p2p_keyring_get_communication_id(uint8_t out[4]);
// Forgets every key and the communication ID (tests).
void ps4_p2p_keyring_reset(void);

// ---------------------------------------------------------------------------------------------
// Transport

typedef size_t (*Ps4P2PClassifySignaling)(const uint8_t *packet, size_t len);

typedef struct Ps4P2PTransportConfig {
    Ps4P2PTcpConfig tcp;
    size_t datagram_queue_bytes;    // per vport, the PS4's SO_RCVBUF
    size_t signaling_queue_packets; // per channel
    size_t signaling_channels;
    Ps4P2PClassifySignaling classify_signaling; // NULL: everything on channel 0
    int max_backlog;
    // shadPS4 leaves this off: NAT changes the pseudo-header addresses, so peer checksums may
    // not match.
    bool verify_stream_checksum;
} Ps4P2PTransportConfig;

void ps4_p2p_transport_default_config(Ps4P2PTransportConfig *config);

typedef struct Ps4P2PTransport Ps4P2PTransport;

// Binds the UDP socket (exclusively) and starts the receive thread. Returns 0 or an errno.
int ps4_p2p_transport_create(const Ps4P2PEndpoint *bind_address, const Ps4P2PTransportConfig *config,
                             Ps4P2PTransport **out);
void ps4_p2p_transport_retain(Ps4P2PTransport *t);
// The last reference stops the thread and closes the socket.
void ps4_p2p_transport_release(Ps4P2PTransport *t);
uint16_t ps4_p2p_transport_bound_port(const Ps4P2PTransport *t); // host order

int ps4_p2p_transport_send_signaling(Ps4P2PTransport *t, const uint8_t *data, size_t len, const Ps4P2PEndpoint *to);
// Copies at most cap bytes of the oldest packet on the channel; false when there is none.
bool ps4_p2p_transport_recv_signaling(Ps4P2PTransport *t, size_t channel, uint8_t *buf, size_t cap, size_t *len,
                                      Ps4P2PEndpoint *from);
// Drops this fraction of outgoing packets (tests).
void ps4_p2p_transport_set_outgoing_loss(Ps4P2PTransport *t, double probability);

// ---------------------------------------------------------------------------------------------
// P2P sockets: a datagram socket or a stream (listener or connection)

typedef struct Ps4P2PSocket Ps4P2PSocket;

// NULL when out of memory or wake handles.
Ps4P2PSocket *ps4_p2p_socket_create(Ps4P2PTransport *t, bool stream);
// Closes the socket (a stream still finishes gracefully inside the transport) and frees it.
void ps4_p2p_socket_destroy(Ps4P2PSocket *s);

bool ps4_p2p_socket_is_stream(const Ps4P2PSocket *s);
// Pollable descriptors that become readable on any change of readability / writability.
int ps4_p2p_socket_readable_fd(const Ps4P2PSocket *s);
int ps4_p2p_socket_writable_fd(const Ps4P2PSocket *s);

void ps4_p2p_socket_set_protection(Ps4P2PSocket *s, Ps4P2PProtection protection);
Ps4P2PProtection ps4_p2p_socket_get_protection(const Ps4P2PSocket *s);
int ps4_p2p_socket_bind(Ps4P2PSocket *s, uint16_t vport); // 0 = any free vport
int ps4_p2p_socket_listen(Ps4P2PSocket *s, int backlog);
// Datagram: sets the peer. Stream: starts the handshake (EINPROGRESS).
int ps4_p2p_socket_connect(Ps4P2PSocket *s, const Ps4P2PEndpoint *peer, uint16_t peer_vport);
int ps4_p2p_socket_connect_result(const Ps4P2PSocket *s); // EINPROGRESS while connecting
// NULL with *error set (EWOULDBLOCK when nothing is queued).
Ps4P2PSocket *ps4_p2p_socket_accept(Ps4P2PSocket *s, Ps4P2PEndpoint *peer, uint16_t *peer_vport, int *error);
// to == NULL sends to the connected peer; extra is per-message protection (datagrams).
int64_t ps4_p2p_socket_send(Ps4P2PSocket *s, const uint8_t *data, size_t len, const Ps4P2PEndpoint *to,
                            uint16_t to_vport, Ps4P2PProtection extra);
int64_t ps4_p2p_socket_recv(Ps4P2PSocket *s, uint8_t *out, size_t len, bool peek, Ps4P2PEndpoint *from,
                            uint16_t *from_vport);
int ps4_p2p_socket_shutdown(Ps4P2PSocket *s, int how); // 0 read, 1 write, 2 both
void ps4_p2p_socket_abort(Ps4P2PSocket *s);
uint32_t ps4_p2p_socket_events(const Ps4P2PSocket *s); // PS4_NET_EV_*
uint16_t ps4_p2p_socket_bound_vport(const Ps4P2PSocket *s);
uint16_t ps4_p2p_socket_transport_port(const Ps4P2PSocket *s);
bool ps4_p2p_socket_get_peer(const Ps4P2PSocket *s, Ps4P2PEndpoint *peer, uint16_t *peer_vport);
// The stream's TCP state, or -1 without a connection.
int ps4_p2p_socket_tcp_state(const Ps4P2PSocket *s);

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_P2P_TRANSPORT_H
