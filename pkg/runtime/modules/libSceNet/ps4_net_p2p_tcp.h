// SPDX-License-Identifier: GPL-2.0-or-later

// TCP over the P2P UDP transport, after shadPS4 (core/net/p2p_tcp.cpp): a small BSD-style TCP
// (handshake, sliding window, NewReno congestion control, retransmission and persist timers,
// TIME-WAIT) whose segments the transport wraps in P2P datagrams.
//
// P2P is IPv4 only (shadPS4 only ever creates an AF_INET transport), so the checksum pseudo
// header carries IPv4 addresses. Times are CLOCK_MONOTONIC nanoseconds (ps4_net_host_now_ns);
// errors are FreeBSD errno values, 0 for none. Not thread safe: the transport serializes calls.

#ifndef PS4_NET_P2P_TCP_H
#define PS4_NET_P2P_TCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// No deadline.
#define PS4_P2P_NEVER INT64_MAX

#define PS4_P2P_TCP_FIN 0x01
#define PS4_P2P_TCP_SYN 0x02
#define PS4_P2P_TCP_RST 0x04
#define PS4_P2P_TCP_PSH 0x08
#define PS4_P2P_TCP_ACK 0x10

// The largest MSS a connection uses, and the largest segment it builds (header with MSS option).
#define PS4_P2P_TCP_MAX_MSS 1460
#define PS4_P2P_TCP_MAX_SEGMENT (24 + PS4_P2P_TCP_MAX_MSS)

static inline bool ps4_p2p_seq_lt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}
static inline bool ps4_p2p_seq_le(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) <= 0;
}
static inline bool ps4_p2p_seq_gt(uint32_t a, uint32_t b) {
    return ps4_p2p_seq_lt(b, a);
}
static inline bool ps4_p2p_seq_ge(uint32_t a, uint32_t b) {
    return ps4_p2p_seq_le(b, a);
}

typedef struct Ps4P2PPseudoHeader {
    uint8_t local[4]; // IPv4, network order
    uint8_t remote[4];
} Ps4P2PPseudoHeader;

typedef struct Ps4P2PTcpSegment {
    uint16_t src_port; // host byte order
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    uint16_t window;
    bool has_mss;
    uint16_t mss;
    const uint8_t *payload;
    size_t payload_len;
} Ps4P2PTcpSegment;

uint16_t ps4_p2p_tcp_checksum(const uint8_t src[4], const uint8_t dst[4], const uint8_t *segment, size_t len);
// pseudo != NULL also checks the checksum (from the peer's point of view).
bool ps4_p2p_tcp_parse(const uint8_t *bytes, size_t len, const Ps4P2PPseudoHeader *pseudo, Ps4P2PTcpSegment *out);
// Builds into out (PS4_P2P_TCP_MAX_SEGMENT bytes, payload_len <= PS4_P2P_TCP_MAX_MSS); returns the size.
size_t ps4_p2p_tcp_build(uint8_t *out, const Ps4P2PPseudoHeader *pseudo, uint16_t src_port, uint16_t dst_port,
                         uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window, bool has_mss, uint16_t mss,
                         const uint8_t *payload, size_t payload_len);
// The RST answering seg (none for a RST); returns its size or 0.
size_t ps4_p2p_tcp_build_reset_for(uint8_t *out, const Ps4P2PPseudoHeader *pseudo, const Ps4P2PTcpSegment *seg);

typedef struct Ps4P2PTcpConfig {
    // PS4 defaults for TCP over UDPP2P.
    uint32_t recv_buffer;
    uint32_t send_buffer;
    uint16_t mss; // fits one P2P datagram after overhead
    int64_t initial_rto_ns;
    int64_t min_rto_ns;
    int64_t max_rto_ns;
    int64_t delayed_ack_ns;
    int64_t time_wait_ns; // shadPS4's 2 s (FreeBSD's 2MSL is 60 s)
    int syn_retries;
    int data_retries;
} Ps4P2PTcpConfig;

extern const Ps4P2PTcpConfig ps4_p2p_tcp_default_config;

typedef enum Ps4P2PTcpState {
    PS4_P2P_TCP_CLOSED,
    PS4_P2P_TCP_SYN_SENT,
    PS4_P2P_TCP_SYN_RECEIVED,
    PS4_P2P_TCP_ESTABLISHED,
    PS4_P2P_TCP_FIN_WAIT1,
    PS4_P2P_TCP_FIN_WAIT2,
    PS4_P2P_TCP_CLOSE_WAIT,
    PS4_P2P_TCP_CLOSING,
    PS4_P2P_TCP_LAST_ACK,
    PS4_P2P_TCP_TIME_WAIT,
} Ps4P2PTcpState;

// Hands a built segment to the transport.
typedef void (*Ps4P2PTcpEmit)(void *ctx, const uint8_t *segment, size_t len);

typedef struct Ps4P2PTcp Ps4P2PTcp;

Ps4P2PTcp *ps4_p2p_tcp_create(const Ps4P2PTcpConfig *config, const Ps4P2PPseudoHeader *pseudo, uint16_t local_port,
                              uint16_t remote_port, uint32_t iss, Ps4P2PTcpEmit emit, void *emit_ctx);
void ps4_p2p_tcp_destroy(Ps4P2PTcp *c);

void ps4_p2p_tcp_connect(Ps4P2PTcp *c, int64_t now);
void ps4_p2p_tcp_accept_syn(Ps4P2PTcp *c, const Ps4P2PTcpSegment *syn, int64_t now);
void ps4_p2p_tcp_on_segment(Ps4P2PTcp *c, const Ps4P2PTcpSegment *seg, int64_t now);
void ps4_p2p_tcp_on_timer(Ps4P2PTcp *c, int64_t now);
// PS4_P2P_NEVER when no timer is armed.
int64_t ps4_p2p_tcp_next_deadline(const Ps4P2PTcp *c);

// Returns bytes queued / read, or -errno (-EWOULDBLOCK when it would block).
int64_t ps4_p2p_tcp_send(Ps4P2PTcp *c, const uint8_t *data, size_t len, int64_t now);
int64_t ps4_p2p_tcp_recv(Ps4P2PTcp *c, uint8_t *out, size_t len, bool peek);
void ps4_p2p_tcp_shutdown(Ps4P2PTcp *c, int64_t now);
void ps4_p2p_tcp_close(Ps4P2PTcp *c, int64_t now);
void ps4_p2p_tcp_abort(Ps4P2PTcp *c);

Ps4P2PTcpState ps4_p2p_tcp_state(const Ps4P2PTcp *c);
int ps4_p2p_tcp_pending_error(const Ps4P2PTcp *c);
bool ps4_p2p_tcp_readable(const Ps4P2PTcp *c);
bool ps4_p2p_tcp_writable(const Ps4P2PTcp *c);
bool ps4_p2p_tcp_received_fin(const Ps4P2PTcp *c);
size_t ps4_p2p_tcp_bytes_to_read(const Ps4P2PTcp *c);
uint32_t ps4_p2p_tcp_retransmissions(const Ps4P2PTcp *c);

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_P2P_TCP_H
