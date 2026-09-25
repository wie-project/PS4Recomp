#include "ps4_posix.h"
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
#include <pthread.h>

static inline void *guest_to_host(GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

void shim_socket(GuestContext *ctx) {
    int domain = (int)ctx->rdi;
    int type = (int)ctx->rsi;
    int protocol = (int)ctx->rdx;
    int ret = socket(domain, type, protocol);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_connect(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    const struct sockaddr *addr = (const struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t addrlen = (socklen_t)ctx->rdx;
    int ret = connect(sockfd, addr, addrlen);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_bind(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    const struct sockaddr *addr = (const struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t addrlen = (socklen_t)ctx->rdx;
    int ret = bind(sockfd, addr, addrlen);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_listen(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    int backlog = (int)ctx->rsi;
    int ret = listen(sockfd, backlog);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_accept(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    struct sockaddr *addr = (struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t *addrlen = (socklen_t *)guest_to_host(ctx, ctx->rdx);
    int ret = accept(sockfd, addr, addrlen);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_send(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    const void *buf = guest_to_host(ctx, ctx->rsi);
    size_t len = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;
    ssize_t ret = send(sockfd, buf, len, flags);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_recv(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    void *buf = guest_to_host(ctx, ctx->rsi);
    size_t len = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;
    ssize_t ret = recv(sockfd, buf, len, flags);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_sendto(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    const void *buf = guest_to_host(ctx, ctx->rsi);
    size_t len = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;
    const struct sockaddr *dest_addr = (const struct sockaddr *)guest_to_host(ctx, ctx->r8);
    socklen_t addrlen = (socklen_t)ctx->r9;
    ssize_t ret = sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_recvfrom(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    void *buf = guest_to_host(ctx, ctx->rsi);
    size_t len = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;
    struct sockaddr *src_addr = (struct sockaddr *)guest_to_host(ctx, ctx->r8);
    socklen_t *addrlen = (socklen_t *)guest_to_host(ctx, ctx->r9);
    ssize_t ret = recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_setsockopt(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    int level = (int)ctx->rsi;
    int optname = (int)ctx->rdx;
    const void *optval = guest_to_host(ctx, ctx->rcx);
    socklen_t optlen = (socklen_t)ctx->r8;
    int ret = setsockopt(sockfd, level, optname, optval, optlen);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_getsockopt(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    int level = (int)ctx->rsi;
    int optname = (int)ctx->rdx;
    void *optval = guest_to_host(ctx, ctx->rcx);
    socklen_t *optlen = (socklen_t *)guest_to_host(ctx, ctx->r8);
    int ret = getsockopt(sockfd, level, optname, optval, optlen);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_getsockname(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    struct sockaddr *addr = (struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t *addrlen = (socklen_t *)guest_to_host(ctx, ctx->rdx);
    int ret = getsockname(sockfd, addr, addrlen);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_getpeername(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    struct sockaddr *addr = (struct sockaddr *)guest_to_host(ctx, ctx->rsi);
    socklen_t *addrlen = (socklen_t *)guest_to_host(ctx, ctx->rdx);
    int ret = getpeername(sockfd, addr, addrlen);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_shutdown(GuestContext *ctx) {
    int sockfd = (int)ctx->rdi;
    int how = (int)ctx->rsi;
    int ret = shutdown(sockfd, how);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_select(GuestContext *ctx) {
    int nfds = (int)ctx->rdi;
    fd_set *readfds = (fd_set *)guest_to_host(ctx, ctx->rsi);
    fd_set *writefds = (fd_set *)guest_to_host(ctx, ctx->rdx);
    fd_set *exceptfds = (fd_set *)guest_to_host(ctx, ctx->rcx);
    struct timeval *timeout = (struct timeval *)guest_to_host(ctx, ctx->r8);
    int ret = select(nfds, readfds, writefds, exceptfds, timeout);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = (uint64_t)ret;
    }
    SHIM_RETURN();
}

void shim_inet_pton(GuestContext *ctx) {
    int af = (int)ctx->rdi;
    const char *src = (const char *)guest_to_host(ctx, ctx->rsi);
    void *dst = guest_to_host(ctx, ctx->rdx);
    int ret = inet_pton(af, src, dst);
    if (ret <= 0) {
        set_guest_errno(ctx, errno);
    }
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
}

void shim_usleep(GuestContext *ctx) {
    useconds_t usec = (useconds_t)ctx->rdi;
    int ret = usleep(usec);
    if (ret < 0) {
        set_guest_errno(ctx, errno);
        ctx->rax = (uint64_t)-1;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_getpid(GuestContext *ctx) {
    ctx->rax = (uint64_t)getpid();
    SHIM_RETURN();
}

void shim_pthread_exit(GuestContext *ctx) {
    (void)ctx;
    pthread_exit(NULL);
}

void shim_pthread_rename_np(GuestContext *ctx) {
    // pthread_rename_np(pthread_t thread, const char *name)
    // On macOS: pthread_setname_np(const char *name) for self, or nothing if external.
    const char *name = (const char *)guest_to_host(ctx, ctx->rsi);
    if (name) {
        pthread_setname_np(name);
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_pthread_create_name_np(GuestContext *ctx) {
    // pthread_create_name_np(thread, attr, start_routine, arg, name)
    // Maps directly to shim_pthread_create which sets up context and spawns thread
    shim_pthread_create(ctx);
}

