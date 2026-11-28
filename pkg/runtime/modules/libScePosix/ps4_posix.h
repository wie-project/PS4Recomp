#ifndef PS4_POSIX_H
#define PS4_POSIX_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// select (the socket calls are in libSceNet, ps4_net.h)
void shim_select(GuestContext *ctx);

// POSIX System (the pthread calls are in kernel/threading and kernel/sync)
void shim_getpid(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_POSIX_H
