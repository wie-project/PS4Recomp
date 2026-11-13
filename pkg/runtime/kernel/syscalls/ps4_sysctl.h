#ifndef PS4_SYSCTL_H
#define PS4_SYSCTL_H
#include <stdint.h>
#include "recomp_runtime.h"

// FreeBSD MIB names the guest reads (sys/sysctl.h).
#define PS4_CTL_KERN 1
#define PS4_KERN_ARND 37
#define PS4_CTL_HW 6
#define PS4_HW_NCPU 3
#define PS4_HW_PAGESIZE 7

// PS4 page size: 16 KiB (shadPS4 posix_getpagesize).
#define PS4_SYSCTL_PAGESIZE 16384

// ps4_sysctl result for an OID this runtime does not model; the shim stops on it.
#define PS4_SYSCTL_UNSUPPORTED (-1)

// Host side of sysctl(2) for the OIDs above. Returns 0 on success, a FreeBSD errno value
// (EINVAL, ENOMEM, EPERM, EIO) on failure, or PS4_SYSCTL_UNSUPPORTED. *oldlen follows
// FreeBSD: on entry the size of old (ignored when old is NULL), on return the size of the
// value (for kern.arandom, the number of random bytes produced).
int ps4_sysctl(const int32_t *name, uint32_t namelen, void *old, uint64_t *oldlen,
               const void *newp, uint64_t newlen);

void shim_sysctl(GuestContext *ctx);

#endif
