// SPDX-License-Identifier: GPL-2.0-or-later

// The process's P2P transport and its keys, after shadPS4 (libraries/net/net_p2p.cpp,
// net_p2p_ioctl.cpp).
//
// The transport starts with the first P2P socket, on UDP port PS4_P2P_PORT (shadPS4's p2p_port
// setting: default 3658, the PS4's, 0 = any free port), trying the next 16 ports when it is
// taken; after a failure it retries at most every 2 s. Signaling traffic (vport 0xFFFF) is
// split into the signaling, control ("SHAD" 0x10) and Matching2 ("SHAD" 0x21) channels for NP.
// shadPS4 also asks the router for a UPnP port mapping when NP Matching2 runs; that belongs to
// the NP libraries.

#ifndef PS4_NET_P2P_H
#define PS4_NET_P2P_H

#include "ps4_net_p2p_transport.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_P2P_DEFAULT_UDP_PORT 3658

// advertised_addr: network order, 0 if unknown.
void ps4_net_p2p_configure(uint16_t udp_port, uint32_t advertised_addr);
// (Re)starts on exactly udp_port (no fallback). Returns the bound port, 0 on failure.
uint16_t ps4_net_p2p_start(uint16_t udp_port);
void ps4_net_p2p_stop(void);
// Starts the transport if needed (with the port fallback).
bool ps4_net_p2p_ensure_transport(void);
bool ps4_net_p2p_transport_is_ready(void);
// A reference to the running transport (release it), or NULL.
Ps4P2PTransport *ps4_net_p2p_acquire_transport(void);
uint16_t ps4_net_p2p_configured_port(void); // host order
uint16_t ps4_net_p2p_bound_port(void);      // host order, 0 when not running
uint32_t ps4_net_p2p_advertised_addr(void); // network order, 0 if unknown
uint16_t ps4_net_p2p_advertised_port(void);

// The NP channels. Ports and addresses in network order; -1 on failure.
int ps4_net_p2p_signaling_send(const void *data, uint32_t len, uint32_t dest_addr, uint16_t dest_port);
int ps4_net_p2p_signaling_recv(void *buf, uint32_t len, uint32_t *from_addr, uint16_t *from_port);
int ps4_net_p2p_control_send(const void *data, uint32_t len, uint32_t dest_addr, uint16_t dest_port);
int ps4_net_p2p_control_recv(void *buf, uint32_t len, uint32_t *from_addr, uint16_t *from_port);
int ps4_net_p2p_matching2_send(const void *data, uint32_t len, uint32_t dest_addr, uint16_t dest_port);
int ps4_net_p2p_matching2_recv(void *buf, uint32_t len, uint32_t *from_addr, uint16_t *from_port);

// The P2P key ioctls (sceNetIoctl on a P2P socket). np_port is in network byte order. Returns 0
// or a FreeBSD errno.
int ps4_net_p2p_key_ioctl(uint16_t np_port, uint64_t cmd, void *data);

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_P2P_H
