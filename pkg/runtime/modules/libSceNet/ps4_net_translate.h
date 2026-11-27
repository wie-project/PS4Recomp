// SPDX-License-Identifier: GPL-2.0-or-later

// Guest <-> host translation for libSceNet, after shadPS4 (libraries/net/net_translate.h).

#ifndef PS4_NET_TRANSLATE_H
#define PS4_NET_TRANSLATE_H

#include "ps4_net_types.h"
#include "ps4_net_p2p_transport.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

// The host family, or -1.
int ps4_net_to_host_family(int orbis_family);

typedef struct Ps4NetHostSocketKind {
    int family;
    int type;
    int protocol;
    bool p2p;
} Ps4NetHostSocketKind;

// Returns 0 or the guest errno.
int ps4_net_to_host_socket_kind(int family, int type, int protocol, Ps4NetHostSocketKind *out);
int ps4_net_to_orbis_socket_type(int host_type, bool p2p);
// Returns 0 or the guest errno.
int ps4_net_to_host_sockaddr(const OrbisNetSockaddr *addr, uint32_t addrlen, struct sockaddr_storage *out,
                             socklen_t *out_len);
// Writes at most *addrlen bytes to out (when not NULL) and sets *addrlen to the full length.
void ps4_net_to_orbis_sockaddr(const struct sockaddr *addr, socklen_t len, OrbisNetSockaddr *out, uint32_t *addrlen);

typedef struct Ps4NetHostMsgFlags {
    int host;
    bool dontwait;
    bool peek;
    bool peeklen;
    bool waitall;
    bool crypto;
    bool signature;
    int unsupported;
} Ps4NetHostMsgFlags;
Ps4NetHostMsgFlags ps4_net_to_host_msg_flags(int orbis_flags);

typedef enum Ps4NetOptionValue {
    PS4_NET_OPT_INT,
    PS4_NET_OPT_LINGER,
    PS4_NET_OPT_RAW,
} Ps4NetOptionValue;

typedef struct Ps4NetHostOption {
    int level;
    int name;
    Ps4NetOptionValue value;
} Ps4NetHostOption;

// Returns false when the option has no host equivalent.
bool ps4_net_to_host_option(int level, int name, Ps4NetHostOption *out);
bool ps4_net_is_reserved_port(uint16_t port);
// P2P vports at or above 32768 belong to the system (and 5353).
bool ps4_net_is_reserved_p2p_vport(uint16_t vport);

// P2P addresses (shadPS4 ToP2PAddress / ToOrbisP2PAddress). A DGRAM_P2P address is the peer's
// UDP port in sin_port and the vport in sin_vport; a STREAM_P2P address is the TCP port in
// sin_port and the UDP port in sin_vport (0 = PS4_NET_DEFAULT_P2P_UDP_PORT). port is the vport
// or TCP port, host order. Returns 0 or the guest errno.
#define PS4_NET_DEFAULT_P2P_UDP_PORT 3658
int ps4_net_to_p2p_address(const OrbisNetSockaddr *addr, uint32_t addrlen, bool stream, Ps4P2PEndpoint *endpoint,
                           uint16_t *port);
void ps4_net_to_orbis_p2p_address(const Ps4P2PEndpoint *endpoint, uint16_t port, bool stream, OrbisNetSockaddr *out,
                                  uint32_t *addrlen);
int32_t ps4_net_default_send_buffer(int host_type);    // TCP 32768, UDP and RAW 9216
int32_t ps4_net_default_receive_buffer(int host_type); // TCP 65536, UDP and RAW 40960
uint32_t ps4_net_to_host_epoll_events(uint32_t orbis_events);
uint32_t ps4_net_to_orbis_epoll_events(uint32_t host_events);

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_TRANSLATE_H
