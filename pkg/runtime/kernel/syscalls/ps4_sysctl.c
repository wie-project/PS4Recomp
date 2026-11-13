// sysctl(2) for the read-only OIDs the game's bundled libc.prx
// queries: kern.arandom (arc4random seeding), hw.ncpu and hw.pagesize. Any other OID is a
// stop, not a guessed answer.
// Semantics follow FreeBSD 9 (the PS4 kernel base): sys/kern/kern_sysctl.c (sysctl_root,
// sysctl_old_user, userland_sysctl) and sys/kern/kern_mib.c (sysctl_kern_arnd).
#include "ps4_sysctl.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PS4_CTL_MAXNAME 24  // FreeBSD CTL_MAXNAME
#define PS4_ARND_MAX 256    // sysctl_kern_arnd returns at most 256 bytes per call

// sysctl_old_user for a fixed-size value: copies what fits and reports ENOMEM when the
// caller's buffer is too small. A NULL old only asks for the size.
static int sysctl_out(const void *value, uint64_t size, void *old, uint64_t *oldlen) {
  if (!old) {
    if (oldlen) *oldlen = size;
    return 0;
  }
  uint64_t room = oldlen ? *oldlen : 0;
  uint64_t n = room < size ? room : size;
  memcpy(old, value, (size_t)n);
  if (oldlen) *oldlen = n;
  return n == size ? 0 : ENOMEM;
}

int ps4_sysctl(const int32_t *name, uint32_t namelen, void *old, uint64_t *oldlen,
               const void *newp, uint64_t newlen) {
  (void)newlen;
  if (!name || namelen < 2 || namelen > PS4_CTL_MAXNAME) return EINVAL;
  if (namelen != 2) return PS4_SYSCTL_UNSUPPORTED;
  const bool arnd = name[0] == PS4_CTL_KERN && name[1] == PS4_KERN_ARND;
  const bool ncpu = name[0] == PS4_CTL_HW && name[1] == PS4_HW_NCPU;
  const bool pagesize = name[0] == PS4_CTL_HW && name[1] == PS4_HW_PAGESIZE;
  if (!arnd && !ncpu && !pagesize) return PS4_SYSCTL_UNSUPPORTED;
  // All three OIDs are CTLFLAG_RD: sysctl_root refuses a new value before the handler runs.
  if (newp) return EPERM;

  if (arnd) {
    uint64_t len = oldlen ? *oldlen : 0;
    if (len > PS4_ARND_MAX) len = PS4_ARND_MAX;
    if (old) arc4random_buf(old, (size_t)len);
    if (oldlen) *oldlen = len;
    return 0;
  }
  int32_t value;
  if (pagesize) {
    value = PS4_SYSCTL_PAGESIZE;
  } else {
    // Same CPU count as the sysconf(_SC_NPROCESSORS_ONLN) and cpuset_getaffinity shims.
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) return EIO;
    value = (int32_t)n;
  }
  return sysctl_out(&value, sizeof(value), old, oldlen);
}

static void *sysctl_guest_ptr(GuestContext *ctx, uint64_t addr, uint64_t size, bool *fault) {
  if (!addr) return NULL;
  if (addr + size < addr || addr + size > ctx->mem_size) {
    *fault = true;
    return NULL;
  }
  return ctx->mem_base + addr;
}

// int sysctl(const int *name, u_int namelen, void *oldp, size_t *oldlenp,
//            const void *newp, size_t newlen)
void shim_sysctl(GuestContext *ctx) {
  uint32_t namelen = (uint32_t)ctx->rsi;
  bool fault = false;
  const int32_t *name = (const int32_t *)sysctl_guest_ptr(
      ctx, ctx->rdi, (uint64_t)(namelen <= PS4_CTL_MAXNAME ? namelen : 0) * sizeof(int32_t), &fault);
  uint64_t *oldlen = (uint64_t *)sysctl_guest_ptr(ctx, ctx->rcx, sizeof(uint64_t), &fault);
  uint64_t room = oldlen ? *oldlen : 0;
  void *old = sysctl_guest_ptr(ctx, ctx->rdx, room, &fault);
  const void *newp = sysctl_guest_ptr(ctx, ctx->r8, ctx->r9, &fault);
  if (fault) {
    set_guest_errno(ctx, EFAULT);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }
  int r = ps4_sysctl(name, namelen, old, oldlen, newp, ctx->r9);
  if (r == PS4_SYSCTL_UNSUPPORTED) {
    fprintf(stderr, "\nFATAL: sysctl: unsupported MIB");
    for (uint32_t i = 0; i < namelen && i < 8; i++) fprintf(stderr, " %d", name[i]);
    fprintf(stderr, " (namelen %u)\n", namelen);
    recomp_dump_guest_context(ctx);
    fflush(stderr);
    abort();
  }
  if (r != 0) {
    set_guest_errno(ctx, r);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }
  ctx->rax = 0;
  SHIM_RETURN();
}
