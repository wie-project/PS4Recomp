// SPDX-License-Identifier: GPL-2.0-or-later

// The host side of the guest network stack, after shadPS4 (core/net/host_net.h, the macOS
// backend: poll + pipe for single waits, kqueue for epolls).
//
// Host sockets are always non-blocking; the guest layer (ps4_net_core.c) waits for them itself so
// that sceNetSocketAbort and close can interrupt a blocking call. Errors are FreeBSD errno values
// (ORBIS_NET_E*), translated from the host's.

#ifndef PS4_NET_HOST_H
#define PS4_NET_HOST_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// A deadline in CLOCK_MONOTONIC nanoseconds; PS4_NET_NO_DEADLINE waits forever.
#define PS4_NET_NO_DEADLINE INT64_MAX
int64_t ps4_net_host_now_ns(void);
// BSD SO_RCVTIMEO semantics: 0 or less waits forever.
int64_t ps4_net_host_deadline_from_timeout(int64_t timeout_us);

// The FreeBSD errno for a host errno (ORBIS_NET_EINTERNAL when there is none).
int ps4_net_host_translate(int host_errno);
int ps4_net_host_last_error(void);

// Socket creation and configuration. Each returns 0 or a FreeBSD errno.
int ps4_net_host_create_socket(int family, int type, int protocol, int *out);
int ps4_net_host_create_socket_pair(int family, int type, int protocol, int out[2]);
bool ps4_net_host_configure(int s);
int ps4_net_host_prepare_bind(int s, bool reuse_addr, bool reuse_port);
// Call before closing a stream socket: sorts out SO_LINGER for the guest's blocking mode.
void ps4_net_host_prepare_close(int s, bool guest_blocking);
// Returns EINPROGRESS, never EWOULDBLOCK, while connecting.
int ps4_net_host_connect(int s, const struct sockaddr *addr, socklen_t len);
// Reads and clears SO_ERROR.
int ps4_net_host_pending_error(int s);
// Bytes ready to read (FIONREAD) in *out.
int ps4_net_host_bytes_readable(int s, int64_t *out);
int ps4_net_host_accept(int s, struct sockaddr *addr, socklen_t *len, int *out);
// addr == NULL uses send()/recv(). On success *out is the byte count.
int ps4_net_host_sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *addr,
                        socklen_t addr_len, int64_t *out);
int ps4_net_host_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *addr,
                          socklen_t *addr_len, int64_t *out);

// A pollable object another thread can signal to interrupt a wait. It stays signalled until
// drained.
typedef struct Ps4NetWake {
    int read_fd;
    int write_fd;
} Ps4NetWake;
bool ps4_net_wake_init(Ps4NetWake *w);
void ps4_net_wake_destroy(Ps4NetWake *w);
void ps4_net_wake_signal(const Ps4NetWake *w);
void ps4_net_wake_drain(const Ps4NetWake *w);

enum {
    PS4_NET_READABLE = 1u << 0,
    PS4_NET_WRITABLE = 1u << 1,
};

typedef enum Ps4NetWaitResult {
    PS4_NET_WAIT_READY,     // includes error/hangup, retry the operation
    PS4_NET_WAIT_TIMED_OUT,
    PS4_NET_WAIT_WOKEN,     // abort or close
    PS4_NET_WAIT_FAILED,    // see ps4_net_host_last_error()
} Ps4NetWaitResult;

// Waits on a single socket, for a blocking guest call.
Ps4NetWaitResult ps4_net_host_wait_one(int s, uint32_t interest, const Ps4NetWake *wake, int64_t deadline_ns);

// Host kqueue backing one guest epoll. Always level-triggered.
enum {
    PS4_NET_EV_IN = 1u << 0,
    PS4_NET_EV_OUT = 1u << 1,
    PS4_NET_EV_ERR = 1u << 2,
    PS4_NET_EV_HUP = 1u << 3,
    // Guest only: a non-socket object such as a resolver has finished.
    PS4_NET_EV_DESCID = 1u << 4,
};

// The tag of the wake handle, never a caller tag.
#define PS4_NET_RESERVED_TAG UINT64_MAX
#define PS4_NET_HOST_EPOLL_MAX_EVENTS 64

typedef struct Ps4NetHostEvent {
    uint64_t tag;
    uint32_t events;
} Ps4NetHostEvent;

typedef struct Ps4NetHostEpoll {
    int kq;
    Ps4NetWake wake;
} Ps4NetHostEpoll;

bool ps4_net_host_epoll_init(Ps4NetHostEpoll *ep);
void ps4_net_host_epoll_destroy(Ps4NetHostEpoll *ep);
// Add and modify are the same for kqueue; the guest layer checks EEXIST/ENOENT.
int ps4_net_host_epoll_set(Ps4NetHostEpoll *ep, int s, uint64_t tag, uint32_t events);
int ps4_net_host_epoll_remove(Ps4NetHostEpoll *ep, int s);
// Waits for at most cap (<= 64) events. *woken tells the wake handle fired (not drained).
int ps4_net_host_epoll_wait(Ps4NetHostEpoll *ep, Ps4NetHostEvent *out, int cap, int64_t deadline_ns, int *count,
                            bool *woken);

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_HOST_H
