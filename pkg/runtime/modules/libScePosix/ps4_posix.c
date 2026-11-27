#include "ps4_posix.h"
#include "ps4_threading.h"
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

