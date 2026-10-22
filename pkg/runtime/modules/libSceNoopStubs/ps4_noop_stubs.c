// No-op host shims for system services that have no effect on a desktop
// host (crash handlers, store icons, abort/terminate calls). Each returns
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

NOOP_SHIM(sceCoredumpRegisterCoredumpHandler)
NOOP_SHIM(sceCoredumpWriteUserData)

// Uncharted 4 uses two more coredump calls, several call sites (0x1689462, 0x1689b0d ...), all in
// the function at 0x16886b0. Nothing in the recompiled image calls that function directly; its
// address is only taken, which is the coredump handler passed to
// sceCoredumpRegisterCoredumpHandler. That registration is a no-op above and no coredump is
// ever generated on the host, so the handler never runs.
//  * sceCoredumpAttachMemoryRegionAsUserFile adds a memory region to a dump being written:
//    class N, there is no dump. Returns SCE_OK.
//  * sceCoredumpGetStopInfoGpu reads the GPU state at the time of the crash and is only valid
//    inside the handler: class E, ORBIS_COREDUMP_ERROR_NOT_IN_COREDUMP_HANDLER (0x81180003,
//    value from shadPS4's coredump_error.h), which is what the console answers outside a dump.
NOOP_SHIM(sceCoredumpAttachMemoryRegionAsUserFile)

void shim_sceCoredumpGetStopInfoGpu(GuestContext *ctx) {
  ctx->rax = (uint64_t)(int64_t)(int32_t)0x81180003;
  SHIM_RETURN();
}

NOOP_SHIM(sceKernelSync)
NOOP_SHIM(sceRemoteplayTerminate)
NOOP_SHIM(sceNpCommerceShowPsStoreIcon)
NOOP_SHIM(sceNpCommerceHidePsStoreIcon)
NOOP_SHIM(sceNpCommerceDialogUpdateStatus)
NOOP_SHIM(sceNpSetContentRestriction)
NOOP_SHIM(sceNpNotifyPlusFeature)
NOOP_SHIM(sceNpAbortRequest)
NOOP_SHIM(sceNpAuthAbortRequest)
NOOP_SHIM(sceNpWebApiAbortRequest)
NOOP_SHIM(sceNpSnsFacebookAbortRequest)
NOOP_SHIM(sceHttpAbortWaitRequest)
NOOP_SHIM(sceHttpUnsetEpoll)
NOOP_SHIM(sceNetSocketAbort)
