#ifndef PS4_SIGSET_H
#define PS4_SIGSET_H
#include "recomp_runtime.h"
// FreeBSD sigset_t operations on the guest's four-word set.
void shim_sigfillset(GuestContext *ctx);
#endif
