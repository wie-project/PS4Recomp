// SPDX-License-Identifier: GPL-2.0-or-later

// Guest <-> host translation: shadPS4 libraries/net/net_translate.cpp (the P2P address forms
// belong to the P2P transport and are not here).

#include "ps4_net_translate.h"
#include "ps4_net_host.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stddef.h>
#include <string.h>
#include <sys/un.h>

int ps4_net_to_host_family(int orbis_family) {
    switch (orbis_family) {
    case ORBIS_NET_AF_UNIX:
        return AF_UNIX;
    case ORBIS_NET_AF_INET:
        return AF_INET;
    case ORBIS_NET_AF_INET6:
        return AF_INET6;
    default:
        return -1;
    }
}

int ps4_net_to_host_socket_kind(int family, int type, int protocol, Ps4NetHostSocketKind *out) {
    if (family == ORBIS_NET_AF_UNIX) {
        if (type != ORBIS_NET_SOCK_STREAM && type != ORBIS_NET_SOCK_DGRAM) return ORBIS_NET_EPROTONOSUPPORT;
        *out = (Ps4NetHostSocketKind){AF_UNIX, type == ORBIS_NET_SOCK_STREAM ? SOCK_STREAM : SOCK_DGRAM, 0, false};
        return 0;
    }
    // The PS4 returns EPROTONOSUPPORT here, not EAFNOSUPPORT.
    if (family != ORBIS_NET_AF_INET) return ORBIS_NET_EPROTONOSUPPORT;
    Ps4NetHostSocketKind kind = {AF_INET, 0, protocol, false};
    switch (type) {
    case ORBIS_NET_SOCK_STREAM:
        kind.type = SOCK_STREAM;
        break;
    case ORBIS_NET_SOCK_DGRAM:
        kind.type = SOCK_DGRAM;
        break;
    case ORBIS_NET_SOCK_RAW:
        kind.type = SOCK_RAW;
        break;
    case ORBIS_NET_SOCK_DGRAM_P2P:
        kind.type = SOCK_DGRAM;
        kind.p2p = true;
        break;
    case ORBIS_NET_SOCK_STREAM_P2P:
        kind.type = SOCK_STREAM;
        kind.p2p = true;
        break;
    default:
        return ORBIS_NET_EPROTONOSUPPORT;
    }
    if (type != ORBIS_NET_SOCK_RAW && protocol != 0) {
        const int natural = kind.p2p ? -1 : kind.type == SOCK_STREAM ? ORBIS_NET_IPPROTO_TCP : ORBIS_NET_IPPROTO_UDP;
        if (protocol != natural) return ORBIS_NET_EPROTOTYPE;
    }
    *out = kind;
    return 0;
}

int ps4_net_to_orbis_socket_type(int host_type, bool p2p) {
    if (host_type == SOCK_STREAM) return p2p ? ORBIS_NET_SOCK_STREAM_P2P : ORBIS_NET_SOCK_STREAM;
    if (host_type == SOCK_DGRAM) return p2p ? ORBIS_NET_SOCK_DGRAM_P2P : ORBIS_NET_SOCK_DGRAM;
    return ORBIS_NET_SOCK_RAW;
}

int ps4_net_to_host_sockaddr(const OrbisNetSockaddr *addr, uint32_t addrlen, struct sockaddr_storage *out,
                             socklen_t *out_len) {
    if (!addr) return ORBIS_NET_EFAULT;
    if (addrlen < 2) return ORBIS_NET_EINVAL;
    memset(out, 0, sizeof(*out));
    switch (addr->sa_family) {
    case ORBIS_NET_AF_INET: {
        if (addrlen < sizeof(OrbisNetSockaddrIn)) return ORBIS_NET_EINVAL;
        OrbisNetSockaddrIn in;
        memcpy(&in, addr, sizeof(in)); // may be unaligned
        struct sockaddr_in *host = (struct sockaddr_in *)out;
        host->sin_len = sizeof(*host);
        host->sin_family = AF_INET;
        host->sin_port = in.sin_port;
        memcpy(&host->sin_addr, &in.sin_addr, sizeof(in.sin_addr));
        *out_len = sizeof(struct sockaddr_in);
        return 0;
    }
    case ORBIS_NET_AF_INET6: {
        if (addrlen < sizeof(OrbisNetSockaddrIn6)) return ORBIS_NET_EINVAL;
        OrbisNetSockaddrIn6 in6;
        memcpy(&in6, addr, sizeof(in6));
        struct sockaddr_in6 *host = (struct sockaddr_in6 *)out;
        host->sin6_len = sizeof(*host);
        host->sin6_family = AF_INET6;
        host->sin6_port = in6.sin6_port;
        host->sin6_flowinfo = in6.sin6_flowinfo;
        memcpy(&host->sin6_addr, in6.sin6_addr, sizeof(in6.sin6_addr));
        host->sin6_scope_id = in6.sin6_scope_id;
        *out_len = sizeof(struct sockaddr_in6);
        return 0;
    }
    case ORBIS_NET_AF_UNIX: {
        const uint32_t size = addrlen < sizeof(OrbisNetSockaddrUn) ? addrlen : (uint32_t)sizeof(OrbisNetSockaddrUn);
        const char *path = (const char *)addr + 2;
        const size_t path_len = strnlen(path, size - 2);
        struct sockaddr_un *host = (struct sockaddr_un *)out;
        if (path_len >= sizeof(host->sun_path)) return ORBIS_NET_ENAMETOOLONG;
        host->sun_family = AF_UNIX;
        memcpy(host->sun_path, path, path_len);
        host->sun_path[path_len] = '\0';
        *out_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1);
        host->sun_len = (uint8_t)*out_len;
        return 0;
    }
    default:
        return ORBIS_NET_EAFNOSUPPORT;
    }
}

static void copy_out(const void *full, uint32_t full_len, OrbisNetSockaddr *out, uint32_t *addrlen) {
    if (out) memcpy(out, full, *addrlen < full_len ? *addrlen : full_len);
    *addrlen = full_len;
}

void ps4_net_to_orbis_sockaddr(const struct sockaddr *addr, socklen_t len, OrbisNetSockaddr *out, uint32_t *addrlen) {
    if (addr->sa_family == AF_INET && len >= (socklen_t)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *host = (const struct sockaddr_in *)addr;
        OrbisNetSockaddrIn in;
        memset(&in, 0, sizeof(in));
        in.sin_len = sizeof(in);
        in.sin_family = ORBIS_NET_AF_INET;
        in.sin_port = host->sin_port;
        memcpy(&in.sin_addr, &host->sin_addr, sizeof(in.sin_addr));
        copy_out(&in, sizeof(in), out, addrlen);
    } else if (addr->sa_family == AF_INET6 && len >= (socklen_t)sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *host = (const struct sockaddr_in6 *)addr;
        OrbisNetSockaddrIn6 in6;
        memset(&in6, 0, sizeof(in6));
        in6.sin6_len = sizeof(in6);
        in6.sin6_family = ORBIS_NET_AF_INET6;
        in6.sin6_port = host->sin6_port;
        in6.sin6_flowinfo = host->sin6_flowinfo;
        memcpy(in6.sin6_addr, &host->sin6_addr, sizeof(in6.sin6_addr));
        in6.sin6_scope_id = host->sin6_scope_id;
        copy_out(&in6, sizeof(in6), out, addrlen);
    } else if (addr->sa_family == AF_UNIX) {
        // Unnamed sockets get an empty path.
        const struct sockaddr_un *host = (const struct sockaddr_un *)addr;
        OrbisNetSockaddrUn un;
        memset(&un, 0, sizeof(un));
        un.sun_len = sizeof(un);
        un.sun_family = ORBIS_NET_AF_UNIX;
        const socklen_t offset = (socklen_t)offsetof(struct sockaddr_un, sun_path);
        if (len > offset) {
            size_t max = (size_t)(len - offset);
            if (max > sizeof(un.sun_path) - 1) max = sizeof(un.sun_path) - 1;
            memcpy(un.sun_path, host->sun_path, strnlen(host->sun_path, max));
        }
        copy_out(&un, sizeof(un), out, addrlen);
    } else {
        *addrlen = 0;
    }
}

Ps4NetHostMsgFlags ps4_net_to_host_msg_flags(int orbis_flags) {
    const int no_signal = 0x20000; // FreeBSD MSG_NOSIGNAL, always implied
    Ps4NetHostMsgFlags out;
    memset(&out, 0, sizeof(out));
    int rest = orbis_flags;
    if ((rest & ORBIS_NET_MSG_PEEKLEN) == ORBIS_NET_MSG_PEEKLEN) {
        out.peeklen = true;
        rest &= ~(ORBIS_NET_MSG_PEEKLEN & ~ORBIS_NET_MSG_PEEK);
    }
    if (rest & ORBIS_NET_MSG_PEEK) {
        out.host |= MSG_PEEK;
        out.peek = true;
        rest &= ~ORBIS_NET_MSG_PEEK;
    }
    if (rest & ORBIS_NET_MSG_WAITALL) {
        out.host |= MSG_WAITALL; // the host socket is non-blocking, the core loops
        out.waitall = true;
        rest &= ~ORBIS_NET_MSG_WAITALL;
    }
    if (rest & ORBIS_NET_MSG_DONTWAIT) {
        out.dontwait = true;
        rest &= ~ORBIS_NET_MSG_DONTWAIT;
    }
    if (rest & ORBIS_NET_MSG_USECRYPTO) {
        out.crypto = true;
        rest &= ~ORBIS_NET_MSG_USECRYPTO;
    }
    if (rest & ORBIS_NET_MSG_USESIGNATURE) {
        out.signature = true;
        rest &= ~ORBIS_NET_MSG_USESIGNATURE;
    }
    rest &= ~no_signal;
    out.unsupported = rest; // e.g. MSG_OOB
    return out;
}

bool ps4_net_to_host_option(int level, int name, Ps4NetHostOption *out) {
    switch (level) {
    case ORBIS_NET_SOL_SOCKET:
        switch (name) {
        case ORBIS_NET_SO_KEEPALIVE:
            *out = (Ps4NetHostOption){SOL_SOCKET, SO_KEEPALIVE, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_SO_BROADCAST:
            *out = (Ps4NetHostOption){SOL_SOCKET, SO_BROADCAST, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_SO_SNDBUF:
            *out = (Ps4NetHostOption){SOL_SOCKET, SO_SNDBUF, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_SO_RCVBUF:
            *out = (Ps4NetHostOption){SOL_SOCKET, SO_RCVBUF, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_SO_SNDLOWAT:
            *out = (Ps4NetHostOption){SOL_SOCKET, SO_SNDLOWAT, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_SO_RCVLOWAT:
            *out = (Ps4NetHostOption){SOL_SOCKET, SO_RCVLOWAT, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_SO_LINGER:
            // Darwin's SO_LINGER counts clock ticks; SO_LINGER_SEC is the BSD one.
            *out = (Ps4NetHostOption){SOL_SOCKET, SO_LINGER_SEC, PS4_NET_OPT_LINGER};
            return true;
        default:
            return false;
        }
    case ORBIS_NET_IPPROTO_IP:
        switch (name) {
        case ORBIS_NET_IP_HDRINCL:
            *out = (Ps4NetHostOption){IPPROTO_IP, IP_HDRINCL, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_IP_TOS:
            *out = (Ps4NetHostOption){IPPROTO_IP, IP_TOS, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_IP_TTL:
            *out = (Ps4NetHostOption){IPPROTO_IP, IP_TTL, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_IP_MULTICAST_IF:
            *out = (Ps4NetHostOption){IPPROTO_IP, IP_MULTICAST_IF, PS4_NET_OPT_RAW};
            return true;
        case ORBIS_NET_IP_MULTICAST_TTL:
            *out = (Ps4NetHostOption){IPPROTO_IP, IP_MULTICAST_TTL, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_IP_MULTICAST_LOOP:
            *out = (Ps4NetHostOption){IPPROTO_IP, IP_MULTICAST_LOOP, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_IP_ADD_MEMBERSHIP:
            *out = (Ps4NetHostOption){IPPROTO_IP, IP_ADD_MEMBERSHIP, PS4_NET_OPT_RAW};
            return true;
        case ORBIS_NET_IP_DROP_MEMBERSHIP:
            *out = (Ps4NetHostOption){IPPROTO_IP, IP_DROP_MEMBERSHIP, PS4_NET_OPT_RAW};
            return true;
        default:
            return false;
        }
    case ORBIS_NET_IPPROTO_TCP:
        switch (name) {
        case ORBIS_NET_TCP_NODELAY:
            *out = (Ps4NetHostOption){IPPROTO_TCP, TCP_NODELAY, PS4_NET_OPT_INT};
            return true;
        case ORBIS_NET_TCP_MAXSEG:
            *out = (Ps4NetHostOption){IPPROTO_TCP, TCP_MAXSEG, PS4_NET_OPT_INT};
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

bool ps4_net_is_reserved_port(uint16_t port) {
    return (port >= 1 && port <= 1023) || port == 5353 || (port >= 8540 && port <= 8579) ||
           (port >= 9293 && port <= 9310) || port >= 40000;
}

int32_t ps4_net_default_send_buffer(int host_type) {
    return host_type == SOCK_STREAM ? 32768 : 9216;
}

int32_t ps4_net_default_receive_buffer(int host_type) {
    return host_type == SOCK_STREAM ? 65536 : 40 * 1024;
}

uint32_t ps4_net_to_host_epoll_events(uint32_t orbis_events) {
    uint32_t out = 0;
    out |= (orbis_events & ORBIS_NET_EPOLLIN) ? PS4_NET_EV_IN : 0u;
    out |= (orbis_events & ORBIS_NET_EPOLLOUT) ? PS4_NET_EV_OUT : 0u;
    out |= (orbis_events & ORBIS_NET_EPOLLERR) ? PS4_NET_EV_ERR : 0u;
    out |= (orbis_events & ORBIS_NET_EPOLLHUP) ? PS4_NET_EV_HUP : 0u;
    return out;
}

uint32_t ps4_net_to_orbis_epoll_events(uint32_t host_events) {
    uint32_t out = 0;
    out |= (host_events & PS4_NET_EV_IN) ? ORBIS_NET_EPOLLIN : 0u;
    out |= (host_events & PS4_NET_EV_OUT) ? ORBIS_NET_EPOLLOUT : 0u;
    out |= (host_events & PS4_NET_EV_ERR) ? ORBIS_NET_EPOLLERR : 0u;
    out |= (host_events & PS4_NET_EV_HUP) ? ORBIS_NET_EPOLLHUP : 0u;
    out |= (host_events & PS4_NET_EV_DESCID) ? ORBIS_NET_EPOLLDESCID : 0u;
    return out;
}
