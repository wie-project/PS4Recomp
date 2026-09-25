#include "ps4_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/event.h>
#include <pthread.h>
#include <netdb.h>

#define ORBIS_OK 0
#define ORBIS_FAIL -1

static inline void *guest_to_host(GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

// Convert system errno to Orbis Net / POSIX errno
static int host_errno_to_orbis(int err) {
    return err; // macOS and FreeBSD share 1:1 BSD errno values for standard networking
}

static inline void set_net_errno(GuestContext *ctx, int err) {
    int oerr = host_errno_to_orbis(err);
    set_guest_errno(ctx, oerr);
}

void shim_sceNetInit(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceNetTerm(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceNetErrnoLoc(GuestContext *ctx) {
    if (ctx && ctx->fs_base) {
        ctx->rax = ctx->fs_base + 0x100ULL;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_sceNetSocket(GuestContext *ctx) {
    const char *name = (const char *)guest_to_host(ctx, ctx->rdi);
    (void)name;
    int domain = (int)ctx->rsi;
    int type = (int)ctx->rdx;
    int protocol = (int)ctx->rcx;

    int ret = socket(domain, type, protocol);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_sceNetSocketClose(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    int ret = close(s);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

void shim_sceNetBind(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    const struct sockaddr *addr = (const struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t addrlen = (socklen_t)ctx->rdx;

    int ret = bind(s, addr, addrlen);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

void shim_sceNetListen(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    int backlog = (int)ctx->rsi;

    int ret = listen(s, backlog);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

void shim_sceNetAccept(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    struct sockaddr *addr = (struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t *addrlen = (socklen_t *)guest_to_host(ctx, ctx->rdx);

    int ret = accept(s, addr, addrlen);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_sceNetConnect(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    const struct sockaddr *addr = (const struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t addrlen = (socklen_t)ctx->rdx;

    int ret = connect(s, addr, addrlen);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

void shim_sceNetSend(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    const void *buf = guest_to_host(ctx, ctx->rsi);
    size_t len = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;

    ssize_t ret = send(s, buf, len, flags);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_sceNetRecv(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    void *buf = guest_to_host(ctx, ctx->rsi);
    size_t len = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;

    ssize_t ret = recv(s, buf, len, flags);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_sceNetSendto(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    const void *buf = guest_to_host(ctx, ctx->rsi);
    size_t len = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;
    const struct sockaddr *dest_addr = (const struct sockaddr *)guest_to_host(ctx, ctx->r8);
    socklen_t addrlen = (socklen_t)ctx->r9;

    ssize_t ret = sendto(s, buf, len, flags, dest_addr, addrlen);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_sceNetRecvfrom(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    void *buf = guest_to_host(ctx, ctx->rsi);
    size_t len = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;
    struct sockaddr *src_addr = (struct sockaddr *)guest_to_host(ctx, ctx->r8);
    socklen_t *addrlen = (socklen_t *)guest_to_host(ctx, ctx->r9);

    ssize_t ret = recvfrom(s, buf, len, flags, src_addr, addrlen);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_sceNetSetsockopt(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    int level = (int)ctx->rsi;
    int optname = (int)ctx->rdx;
    const void *optval = guest_to_host(ctx, ctx->rcx);
    socklen_t optlen = (socklen_t)ctx->r8;

    int ret = setsockopt(s, level, optname, optval, optlen);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

void shim_sceNetGetsockopt(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    int level = (int)ctx->rsi;
    int optname = (int)ctx->rdx;
    void *optval = guest_to_host(ctx, ctx->rcx);
    socklen_t *optlen = (socklen_t *)guest_to_host(ctx, ctx->r8);

    int ret = getsockopt(s, level, optname, optval, optlen);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

void shim_sceNetGetsockname(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    struct sockaddr *addr = (struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t *addrlen = (socklen_t *)guest_to_host(ctx, ctx->rdx);

    int ret = getsockname(s, addr, addrlen);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

void shim_sceNetGetpeername(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    struct sockaddr *addr = (struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t *addrlen = (socklen_t *)guest_to_host(ctx, ctx->rdx);

    int ret = getpeername(s, addr, addrlen);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

void shim_sceNetShutdown(GuestContext *ctx) {
    int s = (int)ctx->rdi;
    int how = (int)ctx->rsi;

    int ret = shutdown(s, how);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

// Endian conversions
void shim_sceNetHtons(GuestContext *ctx) {
    uint16_t val = (uint16_t)ctx->rdi;
    ctx->rax = (uint64_t)htons(val);
    SHIM_RETURN();
}

void shim_sceNetHtonl(GuestContext *ctx) {
    uint32_t val = (uint32_t)ctx->rdi;
    ctx->rax = (uint64_t)htonl(val);
    SHIM_RETURN();
}

void shim_sceNetHtonll(GuestContext *ctx) {
    uint64_t val = ctx->rdi;
    // htonll / ntohll: swap if host is little-endian
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    ctx->rax = __builtin_bswap64(val);
    #else
    ctx->rax = val;
    #endif
    SHIM_RETURN();
}

void shim_sceNetNtohs(GuestContext *ctx) {
    uint16_t val = (uint16_t)ctx->rdi;
    ctx->rax = (uint64_t)ntohs(val);
    SHIM_RETURN();
}

void shim_sceNetNtohl(GuestContext *ctx) {
    uint32_t val = (uint32_t)ctx->rdi;
    ctx->rax = (uint64_t)ntohl(val);
    SHIM_RETURN();
}

void shim_sceNetNtohll(GuestContext *ctx) {
    shim_sceNetHtonll(ctx);
}

void shim_sceNetInetPton(GuestContext *ctx) {
    int af = (int)ctx->rdi;
    const char *src = (const char *)guest_to_host(ctx, ctx->rsi);
    void *dst = guest_to_host(ctx, ctx->rdx);

    int ret = inet_pton(af, src, dst);
    if (ret <= 0) {
        set_net_errno(ctx, errno);
    }
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
}

void shim_sceNetInetNtop(GuestContext *ctx) {
    int af = (int)ctx->rdi;
    const void *src = guest_to_host(ctx, ctx->rsi);
    char *dst = (char *)guest_to_host(ctx, ctx->rdx);
    socklen_t size = (socklen_t)ctx->rcx;

    const char *ret = inet_ntop(af, src, dst, size);
    if (!ret) {
        set_net_errno(ctx, errno);
        ctx->rax = 0;
    } else {
        ctx->rax = ctx->rdx;
    }
    SHIM_RETURN();
}

// Memory Pool
static int g_next_pool_id = 1;

void shim_sceNetPoolCreate(GuestContext *ctx) {
    const char *name = (const char *)guest_to_host(ctx, ctx->rdi);
    int size = (int)ctx->rsi;
    int flags = (int)ctx->rdx;
    (void)name; (void)size; (void)flags;

    ctx->rax = (uint64_t)(g_next_pool_id++);
    SHIM_RETURN();
}

void shim_sceNetPoolDestroy(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

// DNS Resolver
static int g_next_resolver_id = 1;

void shim_sceNetResolverCreate(GuestContext *ctx) {
    const char *name = (const char *)guest_to_host(ctx, ctx->rdi);
    int memid = (int)ctx->rsi;
    int flags = (int)ctx->rdx;
    (void)name; (void)memid; (void)flags;

    ctx->rax = (uint64_t)(g_next_resolver_id++);
    SHIM_RETURN();
}

void shim_sceNetResolverDestroy(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceNetResolverStartNtoa(GuestContext *ctx) {
    // int32_t sceNetResolverStartNtoa(rid, hostname, addr, timeout, retry, flags)
    const char *hostname = (const char *)guest_to_host(ctx, ctx->rsi);
    uint32_t *addr_out = (uint32_t *)guest_to_host(ctx, ctx->rdx);

    if (!hostname || !addr_out) {
        set_net_errno(ctx, EINVAL);
        ctx->rax = (uint64_t)-1;
        SHIM_RETURN();
    }

    struct hostent *he = gethostbyname(hostname);
    if (he && he->h_addr_list && he->h_addr_list[0]) {
        memcpy(addr_out, he->h_addr_list[0], sizeof(uint32_t));
        ctx->rax = ORBIS_OK;
    } else {
        set_net_errno(ctx, ENOENT);
        ctx->rax = (uint64_t)-1;
    }
    SHIM_RETURN();
}

void shim_sceNetResolverStartAton(GuestContext *ctx) {
    // Reverse DNS
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceNetResolverStartNtoaMultipleRecords(GuestContext *ctx) {
    shim_sceNetResolverStartNtoa(ctx);
}

// Epoll (Emulated via kqueue on macOS)
void shim_sceNetEpollCreate(GuestContext *ctx) {
    const char *name = (const char *)guest_to_host(ctx, ctx->rdi);
    int flags = (int)ctx->rsi;
    (void)name; (void)flags;

    int kq = kqueue();
    if (kq < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)kq;
    }
    SHIM_RETURN();
}

void shim_sceNetEpollDestroy(GuestContext *ctx) {
    int epfd = (int)ctx->rdi;
    int ret = close(epfd);
    if (ret < 0) {
        set_net_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = ORBIS_OK;
    }
    SHIM_RETURN();
}

void shim_sceNetEpollControl(GuestContext *ctx) {
    // sceNetEpollControl(epfd, op, fd, event)
    int epfd = (int)ctx->rdi;
    int op = (int)ctx->rsi;
    int fd = (int)ctx->rdx;
    uint64_t ev_ptr = ctx->rcx;
    (void)epfd; (void)op; (void)fd; (void)ev_ptr;

    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceNetEpollWait(GuestContext *ctx) {
    // sceNetEpollWait(epfd, events, maxevents, timeout)
    int epfd = (int)ctx->rdi;
    (void)epfd;
    ctx->rax = 0; // 0 events triggered
    SHIM_RETURN();
}

void shim_sceNetGetMacAddress(GuestContext *ctx) {
    unsigned char *mac = (unsigned char *)guest_to_host(ctx, ctx->rdi);
    int len = (int)ctx->rsi;
    if (mac && len >= 6) {
        // Return a valid fake PS4 NIC MAC address (e.g., 00:D9:D1:xx:xx:xx)
        mac[0] = 0x00;
        mac[1] = 0xD9;
        mac[2] = 0xD1;
        mac[3] = 0x12;
        mac[4] = 0x34;
        mac[5] = 0x56;
        ctx->rax = ORBIS_OK;
    } else {
        ctx->rax = (uint64_t)-1;
    }
    SHIM_RETURN();
}

void shim_sceNetGetSockInfo(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}
