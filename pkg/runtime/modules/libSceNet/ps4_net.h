// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNet after shadPS4 (libraries/net/net.cpp, net_kernel.cpp, net_resolver.cpp,
// net_translate.cpp; core/net/guest_net.cpp, host_net.cpp).
//
// Functions return what the guest sees: a value >= 0, or ORBIS_NET_ERROR_BASE | errno with the
// errno in the thread's sceNetErrnoLoc slot and the kernel errno (shadPS4's kernel errno hook).
// Pointer arguments are host pointers into guest memory.
//
// Not ported yet: the P2P transport (shadPS4 core/net/p2p_*, libraries/net/net_p2p*): a
// SOCK_DGRAM_P2P / SOCK_STREAM_P2P socket fails with ENETDOWN, as shadPS4's P2PSocketCreate does
// without a transport. The libSceNet data exports (in6addr_any & co., shadPS4 LIB_OBJ) need data
// symbol support in the loader and are not provided.

#ifndef PS4_NET_H
#define PS4_NET_H

#include "recomp_runtime.h"
#include "ps4_net_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t ps4_sceNetSocket(GuestContext *ctx, const char *name, int32_t family, int32_t type, int32_t protocol);
int32_t ps4_sceNetSocketClose(GuestContext *ctx, int32_t s);
int32_t ps4_sceNetSocketAbort(GuestContext *ctx, int32_t s, int32_t flags);
int32_t ps4_sceNetBind(GuestContext *ctx, int32_t s, const OrbisNetSockaddr *addr, uint32_t addrlen);
int32_t ps4_sceNetListen(GuestContext *ctx, int32_t s, int32_t backlog);
int32_t ps4_sceNetAccept(GuestContext *ctx, int32_t s, OrbisNetSockaddr *addr, uint32_t *paddrlen);
int32_t ps4_sceNetConnect(GuestContext *ctx, int32_t s, const OrbisNetSockaddr *addr, uint32_t addrlen);
int32_t ps4_sceNetSendto(GuestContext *ctx, int32_t s, const void *buf, uint64_t len, int32_t flags,
                         const OrbisNetSockaddr *addr, uint32_t addrlen);
int32_t ps4_sceNetRecvfrom(GuestContext *ctx, int32_t s, void *buf, uint64_t len, int32_t flags,
                           OrbisNetSockaddr *addr, uint32_t *paddrlen);
int32_t ps4_sceNetSendmsg(GuestContext *ctx, int32_t s, const OrbisNetMsghdr *msg, int32_t flags);
int32_t ps4_sceNetRecvmsg(GuestContext *ctx, int32_t s, OrbisNetMsghdr *msg, int32_t flags);
int32_t ps4_sceNetShutdown(GuestContext *ctx, int32_t s, int32_t how);
int32_t ps4_sceNetGetsockname(GuestContext *ctx, int32_t s, OrbisNetSockaddr *addr, uint32_t *paddrlen);
int32_t ps4_sceNetGetpeername(GuestContext *ctx, int32_t s, OrbisNetSockaddr *addr, uint32_t *paddrlen);
int32_t ps4_sceNetSetsockopt(GuestContext *ctx, int32_t s, int32_t level, int32_t optname, const void *optval,
                             uint32_t optlen);
int32_t ps4_sceNetGetsockopt(GuestContext *ctx, int32_t s, int32_t level, int32_t optname, void *optval,
                             uint32_t *optlen);
int32_t ps4_sceNetIoctl(GuestContext *ctx, int32_t s, uint64_t cmd, void *data);
int32_t ps4_sceNetGetSockInfo(GuestContext *ctx, int32_t s, OrbisNetSockInfo *info, int32_t n, int32_t flags);

int32_t ps4_sceNetEpollCreate(GuestContext *ctx, const char *name, int32_t flags);
int32_t ps4_sceNetEpollControl(GuestContext *ctx, int32_t eid, int32_t op, int32_t id, const OrbisNetEpollEvent *event);
int32_t ps4_sceNetEpollWait(GuestContext *ctx, int32_t eid, OrbisNetEpollEvent *events, int32_t maxevents,
                            int32_t timeout_us);
int32_t ps4_sceNetEpollAbort(GuestContext *ctx, int32_t eid, int32_t flags);
int32_t ps4_sceNetEpollDestroy(GuestContext *ctx, int32_t eid);

int32_t ps4_sceNetPoolCreate(GuestContext *ctx, const char *name, int32_t size, int32_t flags);
int32_t ps4_sceNetPoolDestroy(GuestContext *ctx, int32_t pool_id);
int32_t ps4_sceNetResolverCreate(GuestContext *ctx, const char *name, int32_t poolid, int32_t flags);
int32_t ps4_sceNetResolverDestroy(GuestContext *ctx, int32_t rid);
int32_t ps4_sceNetResolverAbort(GuestContext *ctx, int32_t rid, int32_t flags);
int32_t ps4_sceNetResolverGetError(GuestContext *ctx, int32_t rid, int32_t *status);
int32_t ps4_sceNetResolverStartNtoa(GuestContext *ctx, int32_t rid, const char *hostname, OrbisNetInAddr *addr,
                                    int32_t flags);
int32_t ps4_sceNetResolverStartAton(GuestContext *ctx, int32_t rid, const OrbisNetInAddr *addr, char *hostname,
                                    int32_t len, int32_t flags);
int32_t ps4_sceNetResolverStartNtoaMultipleRecordsEx(GuestContext *ctx, int32_t rid, const char *hostname,
                                                     OrbisNetResolverInfo *info, int32_t flags);

// Kernel integration (shadPS4 net_kernel.cpp). ps4_net_kernel_owns(fd) tells whether the kernel
// must route fd here: a net object, or the id of one closed while a blocked call still uses it.
bool ps4_net_kernel_owns(int32_t fd);
// POSIX results (-1 with the kernel errno set); the net errno is left as it was.
int32_t ps4_net_kernel_close(GuestContext *ctx, int32_t fd);
int64_t ps4_net_kernel_read(GuestContext *ctx, int32_t fd, void *buf, uint64_t nbytes);
int64_t ps4_net_kernel_write(GuestContext *ctx, int32_t fd, const void *buf, uint64_t nbytes);
int32_t ps4_net_kernel_fcntl(GuestContext *ctx, int32_t fd, int32_t cmd, int64_t arg);
int32_t ps4_net_kernel_duplicate(GuestContext *ctx, int32_t old_fd, int32_t new_fd);

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_H
