// No-op host shims for system services that have no effect on a desktop
// host (store icons, abort/terminate calls). Each returns
// SCE_OK so the guest continues past initialization.
#include "recomp_runtime.h"
#include <stdio.h>

#define NOOP_SHIM(name)                                                        \
  void shim_##name(GuestContext *ctx) {                                        \
    static int warned;                                                         \
    if (!warned) {                                                             \
      warned = 1;                                                              \
      fprintf(stderr, "[ps4-stub] %s -> 0 (no-op)\n", #name);                  \
    }                                                                          \
    ctx->rax = 0;                                                              \
    SHIM_RETURN();                                                             \
  }

NOOP_SHIM(sceKernelSync)
NOOP_SHIM(sceRemoteplayTerminate)
NOOP_SHIM(sceNpWebApiAbortRequest)
NOOP_SHIM(sceNpSnsFacebookAbortRequest)
NOOP_SHIM(sceNetSocketAbort)

// sce::Toolkit::NP::V2::Core::Request::InitParams::InitParams() (NID: vbh-MWtjMh4)
// C++ constructor: zeroes the structure instance and returns this (rdi).
void shim_sceNpToolkit2InitParams(GuestContext *ctx) {
  if (ctx->rdi && ctx->mem_base) {
    memset((void *)(ctx->mem_base + ctx->rdi), 0, 128);
  }
  ctx->rax = ctx->rdi;
  SHIM_RETURN();
}

NOOP_SHIM(sceNpToolkit2Generic)
