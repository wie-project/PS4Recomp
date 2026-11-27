#ifndef PS4_POSIX_H
#define PS4_POSIX_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// select (the socket calls are in libSceNet, ps4_net.h)
void shim_select(GuestContext *ctx);

// POSIX System & Threading
void shim_usleep(GuestContext *ctx);
void shim_getpid(GuestContext *ctx);
void shim_pthread_exit(GuestContext *ctx);
void shim_pthread_key_delete(GuestContext *ctx);
void shim_pthread_rwlock_init(GuestContext *ctx);
void shim_pthread_rwlock_destroy(GuestContext *ctx);
void shim_pthread_rename_np(GuestContext *ctx);
void shim_pthread_create_name_np(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_POSIX_H
