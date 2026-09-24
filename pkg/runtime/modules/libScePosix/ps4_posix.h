#ifndef PS4_POSIX_H
#define PS4_POSIX_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// BSD Sockets
void shim_socket(GuestContext *ctx);
void shim_connect(GuestContext *ctx);
void shim_bind(GuestContext *ctx);
void shim_listen(GuestContext *ctx);
void shim_accept(GuestContext *ctx);
void shim_send(GuestContext *ctx);
void shim_recv(GuestContext *ctx);
void shim_sendto(GuestContext *ctx);
void shim_recvfrom(GuestContext *ctx);
void shim_setsockopt(GuestContext *ctx);
void shim_getsockopt(GuestContext *ctx);
void shim_getsockname(GuestContext *ctx);
void shim_getpeername(GuestContext *ctx);
void shim_shutdown(GuestContext *ctx);
void shim_select(GuestContext *ctx);
void shim_inet_pton(GuestContext *ctx);

// POSIX System & Threading
void shim_usleep(GuestContext *ctx);
void shim_getpid(GuestContext *ctx);
void shim_pthread_exit(GuestContext *ctx);
void shim_pthread_key_delete(GuestContext *ctx);
void shim_pthread_rwlock_init(GuestContext *ctx);
void shim_pthread_rwlock_destroy(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_POSIX_H
