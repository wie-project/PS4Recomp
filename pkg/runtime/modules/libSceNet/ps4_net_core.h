// SPDX-License-Identifier: GPL-2.0-or-later

// Guest network objects, after shadPS4 (core/net/guest_net.h): sockets, epolls and "external"
// objects (resolvers), in one id space with the kernel's file descriptors.
//
// shadPS4 hands out ids from the kernel's handle table. Our kernel's descriptors are host
// descriptors, so an object's id is a host descriptor too: a socket's own, an epoll's kqueue, an
// external object's wake pipe. The kernel's read/write/close/fcntl/fstat/select route ids this
// table knows to ps4_net_kernel_* (ps4_net.h).
//
// Results are int64_t: the value (>= 0) or -errno (FreeBSD, ORBIS_NET_E*).

#ifndef PS4_NET_CORE_H
#define PS4_NET_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sockets and external objects count towards this limit, epolls do not.
#define PS4_NET_MAX_SOCKETS 128

typedef enum Ps4NetObjectKind {
    PS4_NET_OBJ_NONE = 0,
    PS4_NET_OBJ_SOCKET,
    PS4_NET_OBJ_EPOLL,
    PS4_NET_OBJ_EXTERNAL,
} Ps4NetObjectKind;

Ps4NetObjectKind ps4_net_object_kind(int32_t id);
// An id whose object was closed while a blocked call still used it: its descriptor stays open
// until that call returns, and the kernel must not close it (EBADF).
bool ps4_net_id_reserved(int32_t id);
// Closes a socket or epoll (EBADF for anything else; external objects belong to their owner).
int64_t ps4_net_close_object(int32_t id);
// Makes new_id (a host descriptor dup'ed from old_id) a second id of the same object.
int64_t ps4_net_duplicate_object(int32_t old_id, int32_t new_id);
// Sorted socket ids; returns how many there are (at most cap are written).
int ps4_net_list_sockets(int32_t *out, int cap);

int64_t ps4_net_reserve_external_id(void);
int64_t ps4_net_signal_external(int32_t id, bool hangup);
int64_t ps4_net_release_external_id(int32_t id);

int64_t ps4_net_socket_create(int family, int type, int protocol);
int64_t ps4_net_socket_create_pair(int family, int type, int protocol, int32_t ids[2]);
int64_t ps4_net_socket_bind(int32_t id, const struct sockaddr *addr, socklen_t len);
int64_t ps4_net_socket_listen(int32_t id, int backlog);
int64_t ps4_net_socket_connect(int32_t id, const struct sockaddr *addr, socklen_t len);
int64_t ps4_net_socket_accept(int32_t id, struct sockaddr *addr, socklen_t *len);
int64_t ps4_net_socket_sendto(int32_t id, const void *buf, size_t len, int host_flags, bool dontwait,
                              const struct sockaddr *addr, socklen_t addr_len);
// MSG_WAITALL in host_flags is handled here (the host socket is non-blocking).
int64_t ps4_net_socket_recvfrom(int32_t id, void *buf, size_t len, int host_flags, bool dontwait,
                                struct sockaddr *addr, socklen_t *addr_len);
int64_t ps4_net_socket_shutdown(int32_t id, int how);
int64_t ps4_net_socket_getname(int32_t id, struct sockaddr *addr, socklen_t *len);
int64_t ps4_net_socket_getpeername(int32_t id, struct sockaddr *addr, socklen_t *len);
int64_t ps4_net_socket_set_host_option(int32_t id, int level, int name, const void *value, socklen_t len);
int64_t ps4_net_socket_get_host_option(int32_t id, int level, int name, void *value, socklen_t *len);

typedef struct Ps4NetSocketInfo {
    int type;        // host SOCK_*; P2P sockets report their base type
    bool p2p;
    bool nonblocking;
    bool reuse_addr;
    int64_t rcv_timeout_us;
    int64_t snd_timeout_us;
    int64_t connect_timeout_us;
    int64_t accept_timeout_us;
    bool connected; // also while connecting
    bool listening;
    int pending_error; // FreeBSD errno, read and cleared from SO_ERROR
} Ps4NetSocketInfo;
int64_t ps4_net_socket_get_info(int32_t id, Ps4NetSocketInfo *info);

// Bounded by the option rule table (ps4_net.c), plus the P2P linger seconds.
#define PS4_NET_MAX_STORED_OPTIONS 40
typedef struct Ps4NetStoredOption {
    int32_t level;
    int32_t name;
    int32_t value;
} Ps4NetStoredOption;

typedef struct Ps4NetSocketAttributes {
    char name[32]; // SO_NAME
    int32_t snd_buf; // 0 = PS4 default
    int32_t rcv_buf;
    int32_t policy;   // 0-15
    int32_t priority; // 16 until set (shadPS4's value)
    // Options kept for getsockopt only, keyed by (level, name).
    Ps4NetStoredOption stored[PS4_NET_MAX_STORED_OPTIONS];
    int stored_count;
    // A STREAM_P2P socket bound to a non-zero TCP port can listen, but connect gives EPROTO.
    bool p2p_tcp_port_bound;
} Ps4NetSocketAttributes;

// Copies the attributes out.
int64_t ps4_net_socket_get_attributes(int32_t id, Ps4NetSocketAttributes *out);
// Calls update(attributes, arg) with the attributes locked.
int64_t ps4_net_socket_update_attributes(int32_t id, void (*update)(Ps4NetSocketAttributes *, void *), void *arg);
// Stored option helpers (attributes locked by the caller, through update).
bool ps4_net_attr_find(const Ps4NetSocketAttributes *a, int32_t level, int32_t name, int32_t *value);
void ps4_net_attr_store(Ps4NetSocketAttributes *a, int32_t level, int32_t name, int32_t value);

// ip_mreq joins (8 bytes) issued before bind, retried after it.
int64_t ps4_net_socket_add_pending_membership(int32_t id, const uint8_t request[8]);
// Removes a pending join; returns 1 when it was pending.
int64_t ps4_net_socket_drop_pending_membership(int32_t id, const uint8_t request[8]);
// Applies and forgets the pending joins.
void ps4_net_socket_apply_pending_memberships(int32_t id);

int64_t ps4_net_socket_set_nonblocking(int32_t id, bool enable);
int64_t ps4_net_socket_bytes_readable(int32_t id);
int64_t ps4_net_socket_set_reuse_addr(int32_t id, bool enable);
int64_t ps4_net_socket_set_reuse_port(int32_t id, bool enable);
int64_t ps4_net_socket_set_recv_timeout(int32_t id, int64_t us);
int64_t ps4_net_socket_set_send_timeout(int32_t id, int64_t us);
int64_t ps4_net_socket_set_connect_timeout(int32_t id, int64_t us);
int64_t ps4_net_socket_set_accept_timeout(int32_t id, int64_t us);

#define PS4_NET_ABORT_PRESERVE_RECV 0x1u
#define PS4_NET_ABORT_PRESERVE_SEND 0x2u
#define PS4_NET_ABORT_PRESERVE_SEND_AGAIN 0x4u
int64_t ps4_net_socket_abort(int32_t id, uint32_t flags);
int64_t ps4_net_socket_close(int32_t id);

typedef enum Ps4NetEpollOp {
    PS4_NET_EPOLL_ADD,
    PS4_NET_EPOLL_MODIFY,
    PS4_NET_EPOLL_DELETE,
} Ps4NetEpollOp;

typedef struct Ps4NetGuestEpollEvent {
    uint32_t events; // PS4_NET_EV_*
    int32_t ident;   // object id
    uint64_t data;
} Ps4NetGuestEpollEvent;

int64_t ps4_net_epoll_create(void);
int64_t ps4_net_epoll_control(int32_t eid, Ps4NetEpollOp op, int32_t id, uint32_t events, uint64_t data);
// timeout_us < 0 waits forever.
int64_t ps4_net_epoll_wait(int32_t eid, Ps4NetGuestEpollEvent *out, int cap, int64_t timeout_us);
#define PS4_NET_EPOLL_ABORT_PRESERVE 0x1u
int64_t ps4_net_epoll_abort(int32_t eid, uint32_t flags);
int64_t ps4_net_epoll_destroy(int32_t eid);

typedef struct Ps4NetSelectEntry {
    int32_t id;
    uint32_t interest; // PS4_NET_EV_IN / PS4_NET_EV_OUT
    uint32_t ready;
} Ps4NetSelectEntry;
// Returns the number of ready (id, direction) pairs. timeout_us < 0 waits forever.
int64_t ps4_net_socket_select(Ps4NetSelectEntry *entries, int count, int64_t timeout_us);

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_CORE_H
