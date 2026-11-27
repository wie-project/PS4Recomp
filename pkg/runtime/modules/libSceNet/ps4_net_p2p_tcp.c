// SPDX-License-Identifier: GPL-2.0-or-later

// TCP over the P2P transport: shadPS4 core/net/p2p_tcp.cpp.

#include "ps4_net_p2p_tcp.h"
#include "ps4_net_types.h"

#include <stdlib.h>
#include <string.h>

#define MS(x) ((int64_t)(x) * 1000000LL)

const Ps4P2PTcpConfig ps4_p2p_tcp_default_config = {
    .recv_buffer = 65535,
    .send_buffer = 32 * 1024,
    .mss = 1200,
    .initial_rto_ns = MS(1000),
    .min_rto_ns = MS(200),
    .max_rto_ns = MS(60000),
    .delayed_ack_ns = MS(40),
    .time_wait_ns = MS(2000),
    .syn_retries = 6,
    .data_retries = 12,
};

// ---------------------------------------------------------------------------------------------
// Wire format

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t read32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void write16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void write32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint64_t sum_words(uint64_t sum, const uint8_t *data, size_t size) {
    size_t i = 0;
    for (; i + 1 < size; i += 2) sum += read16(data + i);
    if (i < size) sum += (uint64_t)data[i] << 8;
    return sum;
}

uint16_t ps4_p2p_tcp_checksum(const uint8_t src[4], const uint8_t dst[4], const uint8_t *segment, size_t len) {
    uint64_t sum = 0;
    sum = sum_words(sum, src, 4);
    sum = sum_words(sum, dst, 4);
    sum += ((uint64_t)len >> 16) + ((uint64_t)len & 0xffff);
    sum += 6; // IPPROTO_TCP
    sum = sum_words(sum, segment, len);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

bool ps4_p2p_tcp_parse(const uint8_t *b, size_t len, const Ps4P2PPseudoHeader *pseudo, Ps4P2PTcpSegment *seg) {
    if (len < 20) return false;
    const size_t header_size = (size_t)(b[12] >> 4) * 4;
    if (header_size < 20 || header_size > len) return false;
    if (pseudo && ps4_p2p_tcp_checksum(pseudo->remote, pseudo->local, b, len) != 0) return false;
    memset(seg, 0, sizeof(*seg));
    seg->src_port = read16(b);
    seg->dst_port = read16(b + 2);
    seg->seq = read32(b + 4);
    seg->ack = read32(b + 8);
    seg->flags = b[13];
    seg->window = read16(b + 14);
    for (size_t i = 20; i < header_size;) {
        const uint8_t kind = b[i];
        if (kind == 0) break;
        if (kind == 1) {
            i++;
            continue;
        }
        if (i + 1 >= header_size) break;
        const uint8_t length = b[i + 1];
        if (length < 2 || i + length > header_size) break;
        if (kind == 2 && length == 4) {
            seg->has_mss = true;
            seg->mss = read16(b + i + 2);
        }
        i += length;
    }
    seg->payload = b + header_size;
    seg->payload_len = len - header_size;
    return true;
}

size_t ps4_p2p_tcp_build(uint8_t *s, const Ps4P2PPseudoHeader *pseudo, uint16_t src_port, uint16_t dst_port,
                         uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window, bool has_mss, uint16_t mss,
                         const uint8_t *payload, size_t payload_len) {
    const size_t header_size = has_mss ? 24 : 20;
    memset(s, 0, header_size);
    write16(s, src_port);
    write16(s + 2, dst_port);
    write32(s + 4, seq);
    write32(s + 8, ack);
    s[12] = (uint8_t)((header_size / 4) << 4);
    s[13] = flags;
    write16(s + 14, window);
    if (has_mss) {
        s[20] = 2;
        s[21] = 4;
        write16(s + 22, mss);
    }
    if (payload_len) memcpy(s + header_size, payload, payload_len);
    const size_t total = header_size + payload_len;
    write16(s + 16, ps4_p2p_tcp_checksum(pseudo->local, pseudo->remote, s, total));
    return total;
}

size_t ps4_p2p_tcp_build_reset_for(uint8_t *out, const Ps4P2PPseudoHeader *pseudo, const Ps4P2PTcpSegment *seg) {
    if (seg->flags & PS4_P2P_TCP_RST) return 0;
    if (seg->flags & PS4_P2P_TCP_ACK) {
        return ps4_p2p_tcp_build(out, pseudo, seg->dst_port, seg->src_port, seg->ack, 0, PS4_P2P_TCP_RST, 0, false, 0,
                                 NULL, 0);
    }
    const uint32_t length = (uint32_t)seg->payload_len + ((seg->flags & PS4_P2P_TCP_SYN) ? 1 : 0) +
                            ((seg->flags & PS4_P2P_TCP_FIN) ? 1 : 0);
    return ps4_p2p_tcp_build(out, pseudo, seg->dst_port, seg->src_port, 0, seg->seq + length,
                             PS4_P2P_TCP_RST | PS4_P2P_TCP_ACK, 0, false, 0, NULL, 0);
}

// ---------------------------------------------------------------------------------------------
// Byte queues: contiguous storage with a moving head.

typedef struct ByteQueue {
    uint8_t *data;
    size_t head;
    size_t len;
    size_t cap;
} ByteQueue;

static bool bq_append(ByteQueue *q, const uint8_t *src, size_t n) {
    if (n == 0) return true;
    if (q->head + q->len + n > q->cap) {
        if (q->len + n <= q->cap) {
            memmove(q->data, q->data + q->head, q->len);
        } else {
            size_t cap = q->cap ? q->cap : 4096;
            while (cap < q->len + n) cap *= 2;
            uint8_t *grown = (uint8_t *)malloc(cap);
            if (!grown) return false;
            if (q->len) memcpy(grown, q->data + q->head, q->len);
            free(q->data);
            q->data = grown;
            q->cap = cap;
        }
        q->head = 0;
    }
    memcpy(q->data + q->head + q->len, src, n);
    q->len += n;
    return true;
}

static const uint8_t *bq_at(const ByteQueue *q, size_t offset) {
    return q->data + q->head + offset;
}

static void bq_pop(ByteQueue *q, size_t n) {
    q->head += n;
    q->len -= n;
    if (q->len == 0) q->head = 0;
}

static void bq_clear(ByteQueue *q) {
    q->head = q->len = 0;
}

static void bq_free(ByteQueue *q) {
    free(q->data);
    memset(q, 0, sizeof(*q));
}

// Out-of-order data, by stream offset, sorted.
typedef struct OooSegment {
    uint64_t offset;
    uint8_t *data;
    size_t len;
} OooSegment;

struct Ps4P2PTcp {
    Ps4P2PTcpConfig config;
    Ps4P2PPseudoHeader pseudo;
    uint16_t local_port;
    uint16_t remote_port;
    Ps4P2PTcpEmit emit;
    void *emit_ctx;

    Ps4P2PTcpState state;
    int error;

    uint32_t iss;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t snd_max;
    uint32_t buf_seq;
    uint32_t snd_wnd;
    uint32_t snd_wl1;
    uint32_t snd_wl2;
    uint16_t snd_mss;
    ByteQueue send_buf;
    bool fin_queued;

    uint32_t cwnd;
    uint32_t ssthresh;
    int dupacks;
    bool in_recovery;
    uint32_t recover;
    int64_t srtt;
    int64_t rttvar;
    int64_t rto;
    bool have_rtt;
    bool rtt_active;
    uint32_t rtt_seq;
    int64_t rtt_start;
    int retries;
    int64_t persist_interval;

    int64_t retransmit_deadline;
    int64_t persist_deadline;
    int64_t delayed_ack_deadline;
    int64_t time_wait_deadline;
    uint32_t irs;
    uint32_t rcv_nxt;
    uint64_t rcv_offset; // stream offset of rcv_nxt
    ByteQueue recv_buf;
    OooSegment *ooo;
    int ooo_count;
    int ooo_cap;
    size_t ooo_bytes;
    bool has_peer_fin_seq;
    uint32_t peer_fin_seq;
    bool fin_received;
    int unacked_segments;
    uint16_t last_advertised_window;

    uint32_t retransmissions;
};

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static int64_t min_i64(int64_t a, int64_t b) {
    return a < b ? a : b;
}

static void ooo_clear(Ps4P2PTcp *c) {
    for (int i = 0; i < c->ooo_count; i++) free(c->ooo[i].data);
    c->ooo_count = 0;
    c->ooo_bytes = 0;
}

Ps4P2PTcp *ps4_p2p_tcp_create(const Ps4P2PTcpConfig *config, const Ps4P2PPseudoHeader *pseudo, uint16_t local_port,
                              uint16_t remote_port, uint32_t iss, Ps4P2PTcpEmit emit, void *emit_ctx) {
    Ps4P2PTcp *c = (Ps4P2PTcp *)calloc(1, sizeof(Ps4P2PTcp));
    if (!c) return NULL;
    c->config = *config;
    if (c->config.mss > PS4_P2P_TCP_MAX_MSS) c->config.mss = PS4_P2P_TCP_MAX_MSS;
    c->pseudo = *pseudo;
    c->local_port = local_port;
    c->remote_port = remote_port;
    c->emit = emit;
    c->emit_ctx = emit_ctx;
    c->state = PS4_P2P_TCP_CLOSED;
    c->iss = iss;
    c->snd_una = c->snd_nxt = c->snd_max = iss;
    c->buf_seq = iss + 1;
    c->snd_mss = 536;
    c->ssthresh = 0x7fffffff;
    c->rto = config->initial_rto_ns;
    c->retransmit_deadline = c->persist_deadline = PS4_P2P_NEVER;
    c->delayed_ack_deadline = c->time_wait_deadline = PS4_P2P_NEVER;
    return c;
}

void ps4_p2p_tcp_destroy(Ps4P2PTcp *c) {
    if (!c) return;
    bq_free(&c->send_buf);
    bq_free(&c->recv_buf);
    ooo_clear(c);
    free(c->ooo);
    free(c);
}

static bool synchronized(const Ps4P2PTcp *c) {
    return c->state != PS4_P2P_TCP_CLOSED && c->state != PS4_P2P_TCP_SYN_SENT && c->state != PS4_P2P_TCP_SYN_RECEIVED;
}

static uint16_t receive_window(const Ps4P2PTcp *c) {
    const size_t used = c->recv_buf.len;
    if (used >= c->config.recv_buffer) return 0;
    const size_t room = c->config.recv_buffer - used;
    return (uint16_t)(room < 65535 ? room : 65535);
}

static uint32_t fin_seq(const Ps4P2PTcp *c) {
    return c->buf_seq + (uint32_t)c->send_buf.len;
}

static bool fin_sent(const Ps4P2PTcp *c) {
    return c->fin_queued && ps4_p2p_seq_gt(c->snd_max, fin_seq(c));
}

static bool fin_acked(const Ps4P2PTcp *c) {
    return c->fin_queued && ps4_p2p_seq_gt(c->snd_una, fin_seq(c));
}

static size_t send_space(const Ps4P2PTcp *c) {
    return c->send_buf.len >= c->config.send_buffer ? 0 : c->config.send_buffer - c->send_buf.len;
}

bool ps4_p2p_tcp_readable(const Ps4P2PTcp *c) {
    return c->recv_buf.len > 0 || c->fin_received || c->error != 0 || c->state == PS4_P2P_TCP_CLOSED;
}

bool ps4_p2p_tcp_writable(const Ps4P2PTcp *c) {
    if (c->error != 0 || c->state == PS4_P2P_TCP_CLOSED) return true; // wake waiters so they see the error
    return (c->state == PS4_P2P_TCP_ESTABLISHED || c->state == PS4_P2P_TCP_CLOSE_WAIT) && !c->fin_queued &&
           send_space(c) > 0;
}

static void emit_segment(Ps4P2PTcp *c, uint32_t seq, uint8_t flags, const uint8_t *payload, size_t len,
                         bool with_mss) {
    const uint16_t window = receive_window(c);
    const uint32_t ack = (flags & PS4_P2P_TCP_ACK) ? c->rcv_nxt : 0;
    uint8_t segment[PS4_P2P_TCP_MAX_SEGMENT];
    const size_t n = ps4_p2p_tcp_build(segment, &c->pseudo, c->local_port, c->remote_port, seq, ack, flags, window,
                                       with_mss, c->config.mss, payload, len);
    if (flags & PS4_P2P_TCP_ACK) {
        c->last_advertised_window = window;
        c->unacked_segments = 0;
        c->delayed_ack_deadline = PS4_P2P_NEVER;
    }
    c->emit(c->emit_ctx, segment, n);
}

static void send_ack(Ps4P2PTcp *c) {
    // Not snd_nxt, which is rewound after a timeout. BSD drops ACKs with an old seq.
    emit_segment(c, c->snd_max, PS4_P2P_TCP_ACK, NULL, 0, false);
}

static void emit_reset(Ps4P2PTcp *c, uint32_t seq) {
    uint8_t segment[PS4_P2P_TCP_MAX_SEGMENT];
    const size_t n = ps4_p2p_tcp_build(segment, &c->pseudo, c->local_port, c->remote_port, seq, 0, PS4_P2P_TCP_RST, 0,
                                       false, 0, NULL, 0);
    c->emit(c->emit_ctx, segment, n);
}

static void send_data(Ps4P2PTcp *c, uint32_t seq, uint32_t len) {
    const bool last = seq + len == fin_seq(c);
    emit_segment(c, seq, PS4_P2P_TCP_ACK | (last ? PS4_P2P_TCP_PSH : 0), bq_at(&c->send_buf, seq - c->buf_seq), len,
                 false);
}

static void arm_retransmit(Ps4P2PTcp *c, int64_t now) {
    if (c->retransmit_deadline == PS4_P2P_NEVER) c->retransmit_deadline = now + c->rto;
}

static void try_send(Ps4P2PTcp *c, int64_t now) {
    switch (c->state) {
    case PS4_P2P_TCP_ESTABLISHED:
    case PS4_P2P_TCP_CLOSE_WAIT:
    case PS4_P2P_TCP_FIN_WAIT1:
    case PS4_P2P_TCP_CLOSING:
    case PS4_P2P_TCP_LAST_ACK:
        break;
    default:
        return;
    }
    for (;;) {
        if (fin_sent(c) && c->snd_nxt == fin_seq(c) + 1) return; // FIN in flight
        const uint32_t offset = c->snd_nxt - c->buf_seq;
        const uint32_t available = offset < c->send_buf.len ? (uint32_t)c->send_buf.len - offset : 0;
        const uint32_t flight = c->snd_nxt - c->snd_una;
        const uint32_t window = min_u32(c->snd_wnd, c->cwnd);
        const uint32_t usable = window > flight ? window - flight : 0;
        const uint32_t len = min_u32(min_u32(available, usable), c->snd_mss);

        if (len == 0) {
            if (available == 0 && c->fin_queued && c->snd_nxt == fin_seq(c)) {
                emit_segment(c, c->snd_nxt, PS4_P2P_TCP_FIN | PS4_P2P_TCP_ACK, NULL, 0, false);
                c->snd_nxt++;
                if (ps4_p2p_seq_gt(c->snd_nxt, c->snd_max)) c->snd_max = c->snd_nxt;
                arm_retransmit(c, now);
            } else if (available > 0 && c->snd_wnd == 0 && c->snd_max == c->snd_una &&
                       c->persist_deadline == PS4_P2P_NEVER) {
                c->persist_interval = c->rto;
                c->persist_deadline = now + c->persist_interval;
            }
            return;
        }

        send_data(c, c->snd_nxt, len);
        if (!c->rtt_active && c->snd_nxt == c->snd_max) {
            c->rtt_active = true; // don't time retransmits
            c->rtt_seq = c->snd_nxt;
            c->rtt_start = now;
        }
        c->snd_nxt += len;
        if (ps4_p2p_seq_gt(c->snd_nxt, c->snd_max)) c->snd_max = c->snd_nxt;
        arm_retransmit(c, now);
    }
}

static void retransmit_first(Ps4P2PTcp *c) {
    c->retransmissions++;
    c->rtt_active = false;
    const uint32_t offset = c->snd_una - c->buf_seq;
    if (offset < c->send_buf.len) {
        send_data(c, c->snd_una, min_u32((uint32_t)c->send_buf.len - offset, c->snd_mss));
    } else if (fin_sent(c)) {
        emit_segment(c, fin_seq(c), PS4_P2P_TCP_FIN | PS4_P2P_TCP_ACK, NULL, 0, false);
    }
}

static void clear_timers(Ps4P2PTcp *c) {
    c->retransmit_deadline = c->persist_deadline = PS4_P2P_NEVER;
    c->delayed_ack_deadline = c->time_wait_deadline = PS4_P2P_NEVER;
}

static void terminate(Ps4P2PTcp *c, int error) {
    c->error = error;
    c->state = PS4_P2P_TCP_CLOSED;
    clear_timers(c);
    bq_clear(&c->send_buf);
    ooo_clear(c);
    // Received data stays readable, like BSD.
}

static void enter_time_wait(Ps4P2PTcp *c, int64_t now) {
    clear_timers(c);
    c->state = PS4_P2P_TCP_TIME_WAIT;
    c->time_wait_deadline = now + c->config.time_wait_ns;
}

static void sample_rtt(Ps4P2PTcp *c, int64_t rtt) {
    if (!c->have_rtt) {
        c->srtt = rtt;
        c->rttvar = rtt / 2;
        c->have_rtt = true;
    } else {
        const int64_t delta = c->srtt > rtt ? c->srtt - rtt : rtt - c->srtt;
        c->rttvar = (3 * c->rttvar + delta) / 4;
        c->srtt = (7 * c->srtt + rtt) / 8;
    }
    int64_t variance = 4 * c->rttvar;
    if (variance < MS(10)) variance = MS(10);
    int64_t rto = c->srtt + variance;
    if (rto < c->config.min_rto_ns) rto = c->config.min_rto_ns;
    if (rto > c->config.max_rto_ns) rto = c->config.max_rto_ns;
    c->rto = rto;
}

static void take_syn(Ps4P2PTcp *c, const Ps4P2PTcpSegment *syn) {
    c->irs = syn->seq;
    c->rcv_nxt = c->irs + 1;
    const uint16_t offered = syn->has_mss ? syn->mss : 536;
    c->snd_mss = offered < c->config.mss ? offered : c->config.mss;
    c->cwnd = min_u32(4u * c->snd_mss, max_u32(2u * c->snd_mss, 4380));
    c->snd_wnd = syn->window;
    c->snd_wl1 = syn->seq;
}

void ps4_p2p_tcp_connect(Ps4P2PTcp *c, int64_t now) {
    c->state = PS4_P2P_TCP_SYN_SENT;
    c->snd_nxt = c->snd_max = c->iss + 1;
    emit_segment(c, c->iss, PS4_P2P_TCP_SYN, NULL, 0, true);
    c->rtt_active = true;
    c->rtt_seq = c->iss;
    c->rtt_start = now;
    arm_retransmit(c, now);
}

void ps4_p2p_tcp_accept_syn(Ps4P2PTcp *c, const Ps4P2PTcpSegment *syn, int64_t now) {
    take_syn(c, syn);
    c->snd_wl2 = c->iss;
    c->state = PS4_P2P_TCP_SYN_RECEIVED;
    c->snd_nxt = c->snd_max = c->iss + 1;
    emit_segment(c, c->iss, PS4_P2P_TCP_SYN | PS4_P2P_TCP_ACK, NULL, 0, true);
    c->rtt_active = true;
    c->rtt_seq = c->iss;
    c->rtt_start = now;
    arm_retransmit(c, now);
}

static void handle_syn_sent(Ps4P2PTcp *c, const Ps4P2PTcpSegment *seg, int64_t now) {
    const bool ack_ok = ps4_p2p_seq_gt(seg->ack, c->iss) && ps4_p2p_seq_le(seg->ack, c->snd_max);
    if ((seg->flags & PS4_P2P_TCP_ACK) && !ack_ok) {
        if (!(seg->flags & PS4_P2P_TCP_RST)) emit_reset(c, seg->ack);
        return;
    }
    if (seg->flags & PS4_P2P_TCP_RST) {
        if (seg->flags & PS4_P2P_TCP_ACK) terminate(c, ORBIS_NET_ECONNREFUSED);
        return;
    }
    if (!(seg->flags & PS4_P2P_TCP_SYN)) return;
    take_syn(c, seg);
    c->snd_wl2 = seg->ack;
    if (seg->flags & PS4_P2P_TCP_ACK) {
        c->snd_una = seg->ack;
        if (c->rtt_active && ps4_p2p_seq_gt(seg->ack, c->rtt_seq)) {
            sample_rtt(c, now - c->rtt_start);
            c->rtt_active = false;
        }
        c->retries = 0;
        c->retransmit_deadline = PS4_P2P_NEVER;
        c->state = PS4_P2P_TCP_ESTABLISHED;
        send_ack(c);
        try_send(c, now);
    } else {
        c->state = PS4_P2P_TCP_SYN_RECEIVED;
        emit_segment(c, c->iss, PS4_P2P_TCP_SYN | PS4_P2P_TCP_ACK, NULL, 0, true);
    }
}

static bool in_window(const Ps4P2PTcp *c, uint32_t s, uint32_t window) {
    return ps4_p2p_seq_le(c->rcv_nxt, s) && ps4_p2p_seq_lt(s, c->rcv_nxt + window);
}

static bool acceptable(const Ps4P2PTcp *c, uint32_t seq, uint32_t len) {
    const uint32_t window = receive_window(c);
    if (len == 0) return window == 0 ? seq == c->rcv_nxt : in_window(c, seq, window);
    if (window == 0) return false;
    return in_window(c, seq, window) || in_window(c, seq + len - 1, window);
}

static void on_new_ack(Ps4P2PTcp *c, uint32_t ack, int64_t now) {
    const uint32_t acked = ack - c->snd_una;
    const uint32_t data_acked =
        ps4_p2p_seq_gt(ack, c->buf_seq) ? min_u32(ack - c->buf_seq, (uint32_t)c->send_buf.len) : 0;
    bq_pop(&c->send_buf, data_acked);
    c->buf_seq += data_acked;
    c->snd_una = ack;
    if (ps4_p2p_seq_lt(c->snd_nxt, c->snd_una)) c->snd_nxt = c->snd_una;

    if (c->rtt_active && ps4_p2p_seq_gt(ack, c->rtt_seq)) {
        sample_rtt(c, now - c->rtt_start);
        c->rtt_active = false;
    }
    c->retries = 0;

    if (c->in_recovery) {
        if (ps4_p2p_seq_ge(ack, c->recover)) {
            c->in_recovery = false;
            c->cwnd = c->ssthresh;
        } else {
            // NewReno partial ACK
            retransmit_first(c);
            c->cwnd = c->cwnd > acked ? c->cwnd - acked + c->snd_mss : c->snd_mss;
        }
    } else if (c->cwnd < c->ssthresh) {
        c->cwnd += min_u32(acked, c->snd_mss); // slow start
    } else {
        c->cwnd += max_u32(1, (uint32_t)c->snd_mss * c->snd_mss / max_u32(c->cwnd, 1));
    }
    c->cwnd = min_u32(c->cwnd, 1u << 30);
    c->dupacks = 0;

    c->retransmit_deadline = c->snd_una != c->snd_max ? now + c->rto : PS4_P2P_NEVER;
}

static void on_duplicate_ack(Ps4P2PTcp *c) {
    if (c->in_recovery) {
        c->cwnd += c->snd_mss;
        return;
    }
    if (++c->dupacks == 3) {
        const uint32_t flight = c->snd_max - c->snd_una;
        c->ssthresh = max_u32(flight / 2, 2u * c->snd_mss);
        c->recover = c->snd_max;
        c->in_recovery = true;
        retransmit_first(c);
        c->cwnd = c->ssthresh + 3u * c->snd_mss;
    }
}

// Returns false when the segment is done with.
static bool process_ack(Ps4P2PTcp *c, const Ps4P2PTcpSegment *seg, int64_t now) {
    if (ps4_p2p_seq_gt(seg->ack, c->snd_max)) {
        send_ack(c); // acks unsent data
        return false;
    }
    if (ps4_p2p_seq_gt(seg->ack, c->snd_una)) {
        on_new_ack(c, seg->ack, now);
    } else if (seg->ack == c->snd_una && seg->payload_len == 0 &&
               !(seg->flags & (PS4_P2P_TCP_SYN | PS4_P2P_TCP_FIN)) && seg->window == c->snd_wnd &&
               c->snd_max != c->snd_una) {
        on_duplicate_ack(c);
    }

    if (ps4_p2p_seq_lt(c->snd_wl1, seg->seq) || (c->snd_wl1 == seg->seq && ps4_p2p_seq_le(c->snd_wl2, seg->ack))) {
        c->snd_wnd = seg->window;
        c->snd_wl1 = seg->seq;
        c->snd_wl2 = seg->ack;
        if (c->snd_wnd > 0) c->persist_deadline = PS4_P2P_NEVER;
    }

    if (fin_acked(c)) {
        switch (c->state) {
        case PS4_P2P_TCP_FIN_WAIT1:
            c->state = PS4_P2P_TCP_FIN_WAIT2;
            break;
        case PS4_P2P_TCP_CLOSING:
            enter_time_wait(c, now);
            return false;
        case PS4_P2P_TCP_LAST_ACK:
            c->state = PS4_P2P_TCP_CLOSED;
            clear_timers(c);
            return false;
        default:
            break;
        }
    }
    return true;
}

static bool ooo_store(Ps4P2PTcp *c, uint64_t offset, const uint8_t *data, size_t len) {
    int i = 0;
    while (i < c->ooo_count && c->ooo[i].offset < offset) i++;
    if (i < c->ooo_count && c->ooo[i].offset == offset) {
        OooSegment *slot = &c->ooo[i];
        if (slot->len < len) {
            uint8_t *copy = (uint8_t *)malloc(len);
            if (!copy) return false;
            memcpy(copy, data, len);
            free(slot->data);
            c->ooo_bytes += len - slot->len;
            slot->data = copy;
            slot->len = len;
        }
        return true;
    }
    if (c->ooo_count == c->ooo_cap) {
        const int cap = c->ooo_cap ? c->ooo_cap * 2 : 8;
        OooSegment *grown = (OooSegment *)realloc(c->ooo, (size_t)cap * sizeof(OooSegment));
        if (!grown) return false;
        c->ooo = grown;
        c->ooo_cap = cap;
    }
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) return false;
    memcpy(copy, data, len);
    memmove(&c->ooo[i + 1], &c->ooo[i], (size_t)(c->ooo_count - i) * sizeof(OooSegment));
    c->ooo[i] = (OooSegment){offset, copy, len};
    c->ooo_count++;
    c->ooo_bytes += len;
    return true;
}

static void pull_out_of_order(Ps4P2PTcp *c) {
    int consumed = 0;
    while (consumed < c->ooo_count) {
        OooSegment *s = &c->ooo[consumed];
        if (s->offset > c->rcv_offset) break;
        const uint64_t end = s->offset + s->len;
        if (end > c->rcv_offset) {
            const size_t skip = (size_t)(c->rcv_offset - s->offset);
            const uint32_t added = (uint32_t)(end - c->rcv_offset);
            if (!bq_append(&c->recv_buf, s->data + skip, added)) break;
            c->rcv_nxt += added;
            c->rcv_offset += added;
        }
        c->ooo_bytes -= s->len;
        free(s->data);
        consumed++;
    }
    if (consumed) {
        memmove(c->ooo, c->ooo + consumed, (size_t)(c->ooo_count - consumed) * sizeof(OooSegment));
        c->ooo_count -= consumed;
    }
}

static void process_data(Ps4P2PTcp *c, const Ps4P2PTcpSegment *seg, int64_t now) {
    if (c->state != PS4_P2P_TCP_ESTABLISHED && c->state != PS4_P2P_TCP_FIN_WAIT1 &&
        c->state != PS4_P2P_TCP_FIN_WAIT2) {
        // The peer already sent FIN: a retransmission.
        if (seg->flags & PS4_P2P_TCP_FIN) send_ack(c);
        return;
    }

    uint32_t seq = seg->seq;
    const uint8_t *data = seg->payload;
    size_t len = seg->payload_len;
    if (seg->flags & PS4_P2P_TCP_FIN) {
        c->has_peer_fin_seq = true;
        c->peer_fin_seq = seg->seq + (uint32_t)seg->payload_len;
    }
    if (ps4_p2p_seq_lt(seq, c->rcv_nxt)) {
        const uint32_t skip = c->rcv_nxt - seq;
        if (skip >= len) {
            len = 0;
        } else {
            data += skip;
            len -= skip;
        }
        seq = c->rcv_nxt;
    }
    const uint32_t window = receive_window(c);
    const uint32_t room = seq - c->rcv_nxt < window ? c->rcv_nxt + window - seq : 0;
    if (len > room) len = room;

    bool ack_now = false;
    if (len > 0) {
        if (seq == c->rcv_nxt) {
            if (bq_append(&c->recv_buf, data, len)) {
                c->rcv_nxt += (uint32_t)len;
                c->rcv_offset += len;
                if (c->ooo_count > 0) {
                    pull_out_of_order(c);
                    ack_now = true; // filled a hole
                }
                c->unacked_segments++;
            }
        } else {
            const uint64_t offset = c->rcv_offset + (seq - c->rcv_nxt);
            if (c->ooo_bytes + len <= c->config.recv_buffer) ooo_store(c, offset, data, len);
            ack_now = true; // a duplicate ACK for fast retransmit
        }
    }

    if (c->has_peer_fin_seq && c->peer_fin_seq == c->rcv_nxt && !c->fin_received) {
        c->rcv_nxt++;
        c->fin_received = true;
        ack_now = true;
        switch (c->state) {
        case PS4_P2P_TCP_ESTABLISHED:
            c->state = PS4_P2P_TCP_CLOSE_WAIT;
            break;
        case PS4_P2P_TCP_FIN_WAIT1:
            if (fin_acked(c)) {
                enter_time_wait(c, now);
            } else {
                c->state = PS4_P2P_TCP_CLOSING;
            }
            break;
        case PS4_P2P_TCP_FIN_WAIT2:
            enter_time_wait(c, now);
            break;
        default:
            break;
        }
    }

    if (ack_now || c->unacked_segments >= 2) {
        send_ack(c);
    } else if (c->unacked_segments > 0 && c->delayed_ack_deadline == PS4_P2P_NEVER) {
        c->delayed_ack_deadline = now + c->config.delayed_ack_ns;
    }
}

void ps4_p2p_tcp_on_segment(Ps4P2PTcp *c, const Ps4P2PTcpSegment *seg, int64_t now) {
    switch (c->state) {
    case PS4_P2P_TCP_CLOSED:
        return;
    case PS4_P2P_TCP_SYN_SENT:
        handle_syn_sent(c, seg, now);
        return;
    case PS4_P2P_TCP_TIME_WAIT:
        if ((seg->flags & PS4_P2P_TCP_FIN) && !(seg->flags & PS4_P2P_TCP_RST)) {
            send_ack(c);
            c->time_wait_deadline = now + c->config.time_wait_ns;
        }
        return;
    default:
        break;
    }

    // The peer resent its SYN, so our SYN-ACK was lost.
    if (c->state == PS4_P2P_TCP_SYN_RECEIVED &&
        (seg->flags & (PS4_P2P_TCP_SYN | PS4_P2P_TCP_ACK | PS4_P2P_TCP_RST)) == PS4_P2P_TCP_SYN && seg->seq == c->irs) {
        c->retransmissions++;
        emit_segment(c, c->iss, PS4_P2P_TCP_SYN | PS4_P2P_TCP_ACK, NULL, 0, true);
        return;
    }

    const uint32_t length = (uint32_t)seg->payload_len + ((seg->flags & PS4_P2P_TCP_SYN) ? 1 : 0) +
                            ((seg->flags & PS4_P2P_TCP_FIN) ? 1 : 0);
    bool ack_only = false;
    if (!acceptable(c, seg->seq, length)) {
        if (receive_window(c) == 0 && seg->seq == c->rcv_nxt && (seg->flags & PS4_P2P_TCP_ACK) &&
            !(seg->flags & (PS4_P2P_TCP_RST | PS4_P2P_TCP_SYN))) {
            ack_only = true;
        } else {
            if (!(seg->flags & PS4_P2P_TCP_RST)) send_ack(c);
            return;
        }
    }

    if (seg->flags & PS4_P2P_TCP_RST) {
        if (seg->seq == c->rcv_nxt) {
            const bool closing = c->state == PS4_P2P_TCP_CLOSING || c->state == PS4_P2P_TCP_LAST_ACK;
            terminate(c, closing                                       ? 0
                         : c->state == PS4_P2P_TCP_SYN_RECEIVED ? ORBIS_NET_ECONNREFUSED
                                                                : ORBIS_NET_ECONNRESET);
        } else {
            send_ack(c);
        }
        return;
    }
    if (seg->flags & PS4_P2P_TCP_SYN) {
        send_ack(c);
        return;
    }
    if (!(seg->flags & PS4_P2P_TCP_ACK)) return;

    if (c->state == PS4_P2P_TCP_SYN_RECEIVED) {
        if (!(ps4_p2p_seq_gt(seg->ack, c->snd_una) && ps4_p2p_seq_le(seg->ack, c->snd_max))) {
            emit_reset(c, seg->ack);
            return;
        }
        c->state = PS4_P2P_TCP_ESTABLISHED;
        c->snd_wnd = seg->window;
        c->snd_wl1 = seg->seq;
        c->snd_wl2 = seg->ack;
    }

    if (!process_ack(c, seg, now)) return;
    if (ack_only) {
        send_ack(c); // the window is still closed
        return;
    }
    process_data(c, seg, now);
    try_send(c, now);
}

int64_t ps4_p2p_tcp_next_deadline(const Ps4P2PTcp *c) {
    return min_i64(min_i64(c->retransmit_deadline, c->persist_deadline),
                   min_i64(c->delayed_ack_deadline, c->time_wait_deadline));
}

static void on_retransmit_timeout(Ps4P2PTcp *c, int64_t now) {
    const bool handshake = c->state == PS4_P2P_TCP_SYN_SENT || c->state == PS4_P2P_TCP_SYN_RECEIVED;
    if (++c->retries > (handshake ? c->config.syn_retries : c->config.data_retries)) {
        if (synchronized(c)) emit_reset(c, c->snd_max);
        terminate(c, ORBIS_NET_ETIMEDOUT);
        return;
    }
    c->retransmissions++;
    c->rtt_active = false;
    c->rto = min_i64(c->rto * 2, c->config.max_rto_ns);

    if (c->state == PS4_P2P_TCP_SYN_SENT) {
        emit_segment(c, c->iss, PS4_P2P_TCP_SYN, NULL, 0, true);
    } else if (c->state == PS4_P2P_TCP_SYN_RECEIVED) {
        emit_segment(c, c->iss, PS4_P2P_TCP_SYN | PS4_P2P_TCP_ACK, NULL, 0, true);
    } else {
        const uint32_t flight = c->snd_max - c->snd_una;
        c->ssthresh = max_u32(flight / 2, 2u * c->snd_mss);
        c->cwnd = c->snd_mss;
        c->in_recovery = false;
        c->dupacks = 0;
        c->snd_nxt = c->snd_una;
        try_send(c, now);
    }
    c->retransmit_deadline = now + c->rto;
}

void ps4_p2p_tcp_on_timer(Ps4P2PTcp *c, int64_t now) {
    if (c->time_wait_deadline != PS4_P2P_NEVER && now >= c->time_wait_deadline) {
        c->state = PS4_P2P_TCP_CLOSED;
        clear_timers(c);
        return;
    }
    if (c->delayed_ack_deadline != PS4_P2P_NEVER && now >= c->delayed_ack_deadline) send_ack(c);
    if (c->retransmit_deadline != PS4_P2P_NEVER && now >= c->retransmit_deadline) {
        c->retransmit_deadline = PS4_P2P_NEVER;
        on_retransmit_timeout(c, now);
    }
    if (c->persist_deadline != PS4_P2P_NEVER && now >= c->persist_deadline) {
        emit_segment(c, c->snd_una - 1, PS4_P2P_TCP_ACK, NULL, 0, false);
        c->persist_interval = min_i64(c->persist_interval * 2, c->config.max_rto_ns);
        c->persist_deadline = now + c->persist_interval;
    }
}

int64_t ps4_p2p_tcp_send(Ps4P2PTcp *c, const uint8_t *data, size_t len, int64_t now) {
    if (c->error != 0) return -c->error;
    switch (c->state) {
    case PS4_P2P_TCP_SYN_SENT:
    case PS4_P2P_TCP_SYN_RECEIVED:
        return -ORBIS_NET_EWOULDBLOCK;
    case PS4_P2P_TCP_ESTABLISHED:
    case PS4_P2P_TCP_CLOSE_WAIT:
        break;
    default:
        return c->fin_queued ? -ORBIS_NET_EPIPE : -ORBIS_NET_ENOTCONN;
    }
    if (c->fin_queued) return -ORBIS_NET_EPIPE;
    const size_t space = send_space(c);
    const size_t n = space < len ? space : len;
    if (n == 0) return len == 0 ? 0 : -ORBIS_NET_EWOULDBLOCK;
    if (!bq_append(&c->send_buf, data, n)) return -ORBIS_NET_ENOBUFS;
    try_send(c, now);
    return (int64_t)n;
}

static void maybe_send_window_update(Ps4P2PTcp *c) {
    if (!synchronized(c) || c->fin_received) return;
    // Only once the window opened enough, to avoid silly window syndrome.
    const uint32_t threshold = min_u32(c->config.recv_buffer / 2, 2u * c->snd_mss);
    if ((int32_t)receive_window(c) - (int32_t)c->last_advertised_window >= (int32_t)threshold) send_ack(c);
}

int64_t ps4_p2p_tcp_recv(Ps4P2PTcp *c, uint8_t *out, size_t len, bool peek) {
    if (c->recv_buf.len > 0) {
        const size_t n = len < c->recv_buf.len ? len : c->recv_buf.len;
        memcpy(out, bq_at(&c->recv_buf, 0), n);
        if (!peek) {
            bq_pop(&c->recv_buf, n);
            maybe_send_window_update(c);
        }
        return (int64_t)n;
    }
    if (c->fin_received) return 0; // EOF
    if (c->error != 0) return -c->error;
    if (c->state == PS4_P2P_TCP_CLOSED) return -ORBIS_NET_ENOTCONN;
    return -ORBIS_NET_EWOULDBLOCK;
}

void ps4_p2p_tcp_shutdown(Ps4P2PTcp *c, int64_t now) {
    if (c->fin_queued) return;
    switch (c->state) {
    case PS4_P2P_TCP_SYN_SENT:
        terminate(c, 0);
        return;
    case PS4_P2P_TCP_SYN_RECEIVED:
        ps4_p2p_tcp_abort(c); // not accepted yet
        return;
    case PS4_P2P_TCP_ESTABLISHED:
        c->state = PS4_P2P_TCP_FIN_WAIT1;
        break;
    case PS4_P2P_TCP_CLOSE_WAIT:
        c->state = PS4_P2P_TCP_LAST_ACK;
        break;
    default:
        return;
    }
    c->fin_queued = true;
    try_send(c, now);
}

void ps4_p2p_tcp_close(Ps4P2PTcp *c, int64_t now) {
    if (c->state == PS4_P2P_TCP_SYN_SENT || (synchronized(c) && c->recv_buf.len > 0)) {
        ps4_p2p_tcp_abort(c); // BSD resets on close with unread data
        return;
    }
    ps4_p2p_tcp_shutdown(c, now);
}

void ps4_p2p_tcp_abort(Ps4P2PTcp *c) {
    if (c->state != PS4_P2P_TCP_CLOSED && c->state != PS4_P2P_TCP_SYN_SENT && c->state != PS4_P2P_TCP_TIME_WAIT) {
        emit_reset(c, c->snd_max);
    }
    terminate(c, 0);
}

Ps4P2PTcpState ps4_p2p_tcp_state(const Ps4P2PTcp *c) {
    return c->state;
}

int ps4_p2p_tcp_pending_error(const Ps4P2PTcp *c) {
    return c->error;
}

bool ps4_p2p_tcp_received_fin(const Ps4P2PTcp *c) {
    return c->fin_received;
}

size_t ps4_p2p_tcp_bytes_to_read(const Ps4P2PTcp *c) {
    return c->recv_buf.len;
}

uint32_t ps4_p2p_tcp_retransmissions(const Ps4P2PTcp *c) {
    return c->retransmissions;
}
