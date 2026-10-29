#include "ps4_sigset.h"

#include <stdint.h>

#define GUEST_SIGSET_WORDS 4 // FreeBSD: struct __sigset { __uint32_t __bits[4]; }
#define GUEST_EINVAL 22

// sigfillset(set): every signal in the set; -1 with errno EINVAL for NULL.
void shim_sigfillset(GuestContext *ctx) {
  const uint64_t set = ctx->rdi;
  if (!set) {
    set_guest_errno(ctx, GUEST_EINVAL);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }
  uint32_t *bits = (uint32_t *)(ctx->mem_base + set);
  for (int i = 0; i < GUEST_SIGSET_WORDS; i++) bits[i] = 0xffffffffu;
  ctx->rax = 0;
  SHIM_RETURN();
}
