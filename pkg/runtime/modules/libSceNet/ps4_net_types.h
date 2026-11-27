// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNet guest types and constants, after shadPS4 (libraries/net/net_types.h, net_error.h).
// Guest pointers inside guest structures are 64-bit guest addresses.

#ifndef PS4_NET_TYPES_H
#define PS4_NET_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// FreeBSD errno values, as the PS4 reports them (sceNetErrnoLoc, ORBIS_NET_ERROR_BASE | errno).
enum {
    ORBIS_NET_EPERM = 1,
    ORBIS_NET_ENOENT = 2,
    ORBIS_NET_EINTR = 4,
    ORBIS_NET_EIO = 5,
    ORBIS_NET_EBADF = 9,
    ORBIS_NET_ENOMEM = 12,
    ORBIS_NET_EACCES = 13,
    ORBIS_NET_EFAULT = 14,
    ORBIS_NET_ENOTBLK = 15,
    ORBIS_NET_EBUSY = 16,
    ORBIS_NET_EEXIST = 17,
    ORBIS_NET_ENODEV = 19,
    ORBIS_NET_EINVAL = 22,
    ORBIS_NET_ENFILE = 23,
    ORBIS_NET_EMFILE = 24,
    ORBIS_NET_ENOSPC = 28,
    ORBIS_NET_EPIPE = 32,
    ORBIS_NET_EAGAIN = 35,
    ORBIS_NET_EWOULDBLOCK = 35,
    ORBIS_NET_EINPROGRESS = 36,
    ORBIS_NET_EALREADY = 37,
    ORBIS_NET_ENOTSOCK = 38,
    ORBIS_NET_EDESTADDRREQ = 39,
    ORBIS_NET_EMSGSIZE = 40,
    ORBIS_NET_EPROTOTYPE = 41,
    ORBIS_NET_ENOPROTOOPT = 42,
    ORBIS_NET_EPROTONOSUPPORT = 43,
    ORBIS_NET_ESOCKTNOSUPPORT = 44,
    ORBIS_NET_EOPNOTSUPP = 45,
    ORBIS_NET_EPFNOSUPPORT = 46,
    ORBIS_NET_EAFNOSUPPORT = 47,
    ORBIS_NET_EADDRINUSE = 48,
    ORBIS_NET_EADDRNOTAVAIL = 49,
    ORBIS_NET_ENETDOWN = 50,
    ORBIS_NET_ENETUNREACH = 51,
    ORBIS_NET_ENETRESET = 52,
    ORBIS_NET_ECONNABORTED = 53,
    ORBIS_NET_ECONNRESET = 54,
    ORBIS_NET_ENOBUFS = 55,
    ORBIS_NET_EISCONN = 56,
    ORBIS_NET_ENOTCONN = 57,
    ORBIS_NET_ESHUTDOWN = 58,
    ORBIS_NET_ETOOMANYREFS = 59,
    ORBIS_NET_ETIMEDOUT = 60,
    ORBIS_NET_ECONNREFUSED = 61,
    ORBIS_NET_ELOOP = 62,
    ORBIS_NET_ENAMETOOLONG = 63,
    ORBIS_NET_EHOSTDOWN = 64,
    ORBIS_NET_EHOSTUNREACH = 65,
    ORBIS_NET_ENOTEMPTY = 66,
    ORBIS_NET_EPROCUNAVAIL = 76,
    ORBIS_NET_ECANCELED = 85,
    ORBIS_NET_EPROTO = 92,
    ORBIS_NET_EINTERNAL = 204, // host errors with no FreeBSD equivalent
};

#define ORBIS_NET_ERROR_BASE ((int32_t)0x80410100)
#define ORBIS_NET_ERROR(e) ((int32_t)(0x80410100u | (uint32_t)(e)))

// Resolver errors (the low byte is what sceNetErrnoLoc reports).
#define ORBIS_NET_ERROR_RESOLVER_EINTERNAL ORBIS_NET_ERROR(220)
#define ORBIS_NET_ERROR_RESOLVER_EBUSY ORBIS_NET_ERROR(221)
#define ORBIS_NET_ERROR_RESOLVER_ENOSPACE ORBIS_NET_ERROR(222)
#define ORBIS_NET_ERROR_RESOLVER_ENODNS ORBIS_NET_ERROR(225)
#define ORBIS_NET_ERROR_RESOLVER_ETIMEDOUT ORBIS_NET_ERROR(226)
#define ORBIS_NET_ERROR_RESOLVER_ENOSUPPORT ORBIS_NET_ERROR(227)
#define ORBIS_NET_ERROR_RESOLVER_ESERVERFAILURE ORBIS_NET_ERROR(229)
#define ORBIS_NET_ERROR_RESOLVER_ENOHOST ORBIS_NET_ERROR(230)
#define ORBIS_NET_ERROR_RESOLVER_ENORECORD ORBIS_NET_ERROR(233)

// libkernel error base (sceNetThreadCreate / sceNetThreadJoin report ORBIS_KERNEL_ERROR_UNKNOWN + e).
#define ORBIS_KERNEL_ERROR_UNKNOWN ((int32_t)0x80020000)

enum {
    ORBIS_NET_AF_UNIX = 1,
    ORBIS_NET_AF_INET = 2,
    ORBIS_NET_AF_INET6 = 28,
};

enum {
    ORBIS_NET_SOCK_STREAM = 1,
    ORBIS_NET_SOCK_DGRAM = 2,
    ORBIS_NET_SOCK_RAW = 3,
    ORBIS_NET_SOCK_DGRAM_P2P = 6,
    ORBIS_NET_SOCK_STREAM_P2P = 10,
};

enum {
    ORBIS_NET_IPPROTO_IP = 0,
    ORBIS_NET_IPPROTO_ICMP = 1,
    ORBIS_NET_IPPROTO_IGMP = 2,
    ORBIS_NET_IPPROTO_TCP = 6,
    ORBIS_NET_IPPROTO_UDP = 17,
    ORBIS_NET_IPPROTO_IPV6 = 41,
    ORBIS_NET_SOL_SOCKET = 0xFFFF,
};

enum {
    ORBIS_NET_IP_HDRINCL = 2,
    ORBIS_NET_IP_TOS = 3,
    ORBIS_NET_IP_TTL = 4,
    ORBIS_NET_IP_MULTICAST_IF = 9,
    ORBIS_NET_IP_MULTICAST_TTL = 10,
    ORBIS_NET_IP_MULTICAST_LOOP = 11,
    ORBIS_NET_IP_ADD_MEMBERSHIP = 12,
    ORBIS_NET_IP_DROP_MEMBERSHIP = 13,
    ORBIS_NET_IP_TTLCHK = 30,
    ORBIS_NET_IP_MAXTTL = 31,
    ORBIS_NET_IP_DONTFRAG = 67,
};

enum {
    ORBIS_NET_TCP_NODELAY = 1,
    ORBIS_NET_TCP_MAXSEG = 2,
    ORBIS_NET_TCP_MSS_TO_ADVERTISE = 3,
    ORBIS_NET_TCP_KEEPLISTEN = 128,
};

enum {
    ORBIS_NET_UDP_SND_ON_SUSPEND = 128,
};

#define ORBIS_NET_SOCK_POLICY_NUM_MIN 0
#define ORBIS_NET_SOCK_POLICY_NUM_MAX 15
#define ORBIS_NET_SOCK_POLICY_NA (-1)
#define ORBIS_NET_SOCK_PRIORITY_NUM_USER_MIN 8
#define ORBIS_NET_SOCK_PRIORITY_NUM_USER_MAX 23

// sceNetSocketAbort flags
#define ORBIS_NET_SOCKET_ABORT_FLAG_RCV_PRESERVATION 0x1u
#define ORBIS_NET_SOCKET_ABORT_FLAG_SND_PRESERVATION 0x2u
#define ORBIS_NET_SOCKET_ABORT_FLAG_SND_PRESERVATION_AGAIN 0x4u

enum {
    ORBIS_NET_SO_REUSEADDR = 0x00000004,
    ORBIS_NET_SO_KEEPALIVE = 0x00000008,
    ORBIS_NET_SO_BROADCAST = 0x00000020,
    ORBIS_NET_SO_LINGER = 0x00000080,
    ORBIS_NET_SO_REUSEPORT = 0x00000200,
    ORBIS_NET_SO_ONESBCAST = 0x00010000,
    ORBIS_NET_SO_USECRYPTO = 0x00020000,
    ORBIS_NET_SO_USESIGNATURE = 0x00040000,
    ORBIS_NET_SO_SNDBUF = 0x1001,
    ORBIS_NET_SO_RCVBUF = 0x1002,
    ORBIS_NET_SO_SNDLOWAT = 0x1003,
    ORBIS_NET_SO_RCVLOWAT = 0x1004,
    ORBIS_NET_SO_BSD_SNDTIMEO = 0x1005, // FreeBSD SO_SNDTIMEO (struct timeval)
    ORBIS_NET_SO_BSD_RCVTIMEO = 0x1006, // FreeBSD SO_RCVTIMEO (struct timeval)
    ORBIS_NET_SO_ERROR = 0x1007,
    ORBIS_NET_SO_TYPE = 0x1008,
    ORBIS_NET_SO_SNDTIMEO = 0x1105,
    ORBIS_NET_SO_RCVTIMEO = 0x1106,
    ORBIS_NET_SO_ERROR_EX = 0x1107,
    ORBIS_NET_SO_ACCEPTTIMEO = 0x1108,
    ORBIS_NET_SO_CONNECTTIMEO = 0x1109,
    ORBIS_NET_SO_NBIO = 0x1200,
    ORBIS_NET_SO_POLICY = 0x1201,
    ORBIS_NET_SO_NAME = 0x1202,
    ORBIS_NET_SO_PRIORITY = 0x1203,
};

enum {
    ORBIS_NET_MSG_PEEK = 0x00000002,
    ORBIS_NET_MSG_WAITALL = 0x00000040,
    ORBIS_NET_MSG_DONTWAIT = 0x00000080,
    ORBIS_NET_MSG_USECRYPTO = 0x00100000,
    ORBIS_NET_MSG_USESIGNATURE = 0x00200000,
    ORBIS_NET_MSG_PEEKLEN = 0x00400000 | 0x00000002,
};

enum {
    ORBIS_NET_SHUT_RD = 0,
    ORBIS_NET_SHUT_WR = 1,
    ORBIS_NET_SHUT_RDWR = 2,
};

enum {
    ORBIS_NET_EPOLL_CTL_ADD = 1,
    ORBIS_NET_EPOLL_CTL_MOD = 2,
    ORBIS_NET_EPOLL_CTL_DEL = 3,
};

enum {
    ORBIS_NET_EPOLLIN = 0x1,
    ORBIS_NET_EPOLLOUT = 0x2,
    ORBIS_NET_EPOLLERR = 0x8,
    ORBIS_NET_EPOLLHUP = 0x10,
    ORBIS_NET_EPOLLDESCID = 0x10000,
};

#define ORBIS_NET_EPOLL_ABORT_FLAG_PRESERVATION 0x1u

typedef struct OrbisNetSockaddr {
    uint8_t sa_len;
    uint8_t sa_family;
    char sa_data[14];
} OrbisNetSockaddr;

typedef struct OrbisNetSockaddrIn {
    uint8_t sin_len;
    uint8_t sin_family;
    uint16_t sin_port;  // network byte order
    uint32_t sin_addr;  // network byte order
    uint16_t sin_vport; // P2P virtual port, network byte order
    char sin_zero[6];
} OrbisNetSockaddrIn;

typedef struct OrbisNetSockaddrIn6 {
    uint8_t sin6_len;
    uint8_t sin6_family;
    uint16_t sin6_port; // network byte order
    uint32_t sin6_flowinfo;
    uint8_t sin6_addr[16];
    uint32_t sin6_scope_id;
} OrbisNetSockaddrIn6;

typedef struct OrbisNetSockaddrUn {
    uint8_t sun_len;
    uint8_t sun_family;
    char sun_path[104];
} OrbisNetSockaddrUn;

typedef struct OrbisNetLinger {
    int32_t l_onoff;
    int32_t l_linger;
} OrbisNetLinger;

typedef struct OrbisNetEpollEvent {
    uint32_t events;
    uint32_t pad;
    uint64_t ident;
    uint64_t data;
} OrbisNetEpollEvent;

typedef struct OrbisNetInAddr {
    uint32_t inaddr_addr; // network byte order
} OrbisNetInAddr;

#define ORBIS_NET_INADDR_BROADCAST 0xffffffffu
#define ORBIS_NET_ETHER_ADDRSTRLEN 18

typedef struct OrbisNetUdpSndOnSuspend {
    int32_t onoff;
    uint32_t pad0;
    uint64_t addr; // OrbisNetSockaddr *
    uint32_t addrlen;
    uint32_t pad1;
    uint64_t data; // void *
    uint32_t datalen;
    uint32_t pad2;
} OrbisNetUdpSndOnSuspend;
#define ORBIS_NET_UDP_SND_ON_SUSPEND_DATALEN_MAX 512u

typedef struct OrbisNetMemoryPoolStats {
    uint64_t pool_size;
    uint64_t max_inuse_size;
    uint64_t current_inuse_size;
    int32_t reserved;
    int32_t pad;
} OrbisNetMemoryPoolStats;

typedef struct OrbisNetStatisticsInfo {
    int32_t kernel_mem_free_size;
    int32_t kernel_mem_free_min;
    int32_t packet_count;
    int32_t packet_qos_count;
    int32_t libnet_mem_free_size;
    int32_t libnet_mem_free_min;
} OrbisNetStatisticsInfo;

typedef struct OrbisNetIovec {
    uint64_t iov_base; // void *
    uint64_t iov_len;
} OrbisNetIovec;

typedef struct OrbisNetMsghdr {
    uint64_t msg_name; // void *
    uint32_t msg_namelen;
    uint32_t pad0;
    uint64_t msg_iov; // OrbisNetIovec *
    int32_t msg_iovlen;
    uint32_t pad1;
    uint64_t msg_control; // void *
    uint32_t msg_controllen;
    int32_t msg_flags;
} OrbisNetMsghdr;

enum {
    ORBIS_NET_RESOLVER_ASYNC = 0x1,
    ORBIS_NET_RESOLVER_START_NTOA_DISABLE_IPADDRESS = 0x10000,
};
#define ORBIS_NET_RESOLVER_ABORT_FLAG_NTOA_PRESERVATION 0x1u
#define ORBIS_NET_RESOLVER_ABORT_FLAG_ATON_PRESERVATION 0x2u
#define ORBIS_NET_RESOLVER_HOSTNAME_LEN_MAX 255

typedef struct OrbisNetResolverAddr {
    union {
        OrbisNetInAddr addr;
        uint8_t addr6[16];
    } u;
    uint32_t af;
    uint32_t pad[3];
} OrbisNetResolverAddr;

typedef struct OrbisNetResolverInfo {
    OrbisNetResolverAddr addrs[10];
    uint32_t records;
    uint32_t recordsv4;
    uint32_t pad[14];
} OrbisNetResolverInfo;

#define ORBIS_NET_DEBUG_NAME_LEN_MAX 31

enum {
    ORBIS_NET_SOCKINFO_STATE_OPENED = 2,
    ORBIS_NET_SOCKINFO_STATE_ESTABLISHED = 6,
};

enum {
    ORBIS_NET_SOCKINFO_F_SELF = 0x00000001,
    ORBIS_NET_SOCKINFO_F_NONBLOCK = 0x00200000,
};

typedef struct OrbisNetSockInfo {
    char name[ORBIS_NET_DEBUG_NAME_LEN_MAX + 1];
    int32_t pid;
    int32_t s;
    int8_t socket_type;
    int8_t policy;
    int8_t priority;
    int8_t reserved8;
    int32_t recv_queue_length;
    int32_t send_queue_length;
    OrbisNetInAddr local_adr;
    OrbisNetInAddr remote_adr;
    uint16_t local_port; // network byte order
    uint16_t remote_port;
    uint16_t local_vport;
    uint16_t remote_vport;
    int32_t state;
    int32_t flags;
    int32_t tx_bps;
    int32_t rx_bps;
    int32_t max_tx_bps;
    int32_t max_rx_bps;
    int32_t tx_vbps;
    int32_t rx_vbps;
    int32_t recv_buffer_size;
    int32_t send_buffer_size;
    int32_t reserved6[8];
    int32_t tx_drops;
    int32_t rx_drops;
    int32_t tx_wait;
    int32_t reserved[2];
} OrbisNetSockInfo;

// Shared with libSceNetCtl (ps4_netctl.h).
#ifndef PS4_ORBIS_NET_ETHER_ADDR_DEFINED
#define PS4_ORBIS_NET_ETHER_ADDR_DEFINED
typedef struct OrbisNetEtherAddr {
    uint8_t data[6];
} OrbisNetEtherAddr;
#endif

// sceNetThreadCreate parameter.
typedef struct OrbisNetThreadParam {
    uint64_t entry; // void (*)(void *arg)
    uint64_t arg;
    uint64_t reserved;
    uint32_t flags; // 1: the thread exits through pthread_exit after the entry returns
    uint32_t pad;
} OrbisNetThreadParam;

#ifdef __cplusplus
#define PS4_NET_STATIC_ASSERT static_assert
#else
#define PS4_NET_STATIC_ASSERT _Static_assert
#endif
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetSockaddr) == 16, "OrbisNetSockaddr");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetSockaddrIn) == 16, "OrbisNetSockaddrIn");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetSockaddrIn6) == 28, "OrbisNetSockaddrIn6");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetSockaddrUn) == 106, "OrbisNetSockaddrUn");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetEpollEvent) == 24, "OrbisNetEpollEvent");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetMsghdr) == 48, "OrbisNetMsghdr");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetResolverInfo) == 384, "OrbisNetResolverInfo");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetSockInfo) == 160, "OrbisNetSockInfo");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetUdpSndOnSuspend) == 40, "OrbisNetUdpSndOnSuspend");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetMemoryPoolStats) == 32, "OrbisNetMemoryPoolStats");
PS4_NET_STATIC_ASSERT(sizeof(OrbisNetStatisticsInfo) == 24, "OrbisNetStatisticsInfo");
PS4_NET_STATIC_ASSERT(offsetof(OrbisNetMsghdr, msg_iov) == 16, "OrbisNetMsghdr.msg_iov");
PS4_NET_STATIC_ASSERT(offsetof(OrbisNetMsghdr, msg_control) == 32, "OrbisNetMsghdr.msg_control");
PS4_NET_STATIC_ASSERT(offsetof(OrbisNetUdpSndOnSuspend, data) == 24, "OrbisNetUdpSndOnSuspend.data");

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_TYPES_H
