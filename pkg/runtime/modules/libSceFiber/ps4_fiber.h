#ifndef PS4_FIBER_H
#define PS4_FIBER_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_FIBER_ERROR_NULL 0x80590001
#define ORBIS_FIBER_ERROR_ALIGNMENT 0x80590002
#define ORBIS_FIBER_ERROR_RANGE 0x80590003
#define ORBIS_FIBER_ERROR_INVALID 0x80590004
#define ORBIS_FIBER_ERROR_PERMISSION 0x80590005
#define ORBIS_FIBER_ERROR_STATE 0x80590006

#define ORBIS_FIBER_CONTEXT_MINIMUM_SIZE 512
#define ORBIS_FIBER_MAX_NAME_LENGTH 31
#define ORBIS_FIBER_OBJECT_SIZE 256
#define ORBIS_FIBER_OPT_PARAM_SIZE 128
#define ORBIS_FIBER_INFO_SIZE 128

// Host-side fiber runtime.
//
// Recompiled guest code runs as nested host C calls, so a suspended fiber
// owns live host stack frames. Each fiber therefore has its own host stack
// and its own GuestContext. By default fibers are coroutines: they run on the
// host thread of the guest thread that runs or switches to them, and a switch
// is a user-mode stack switch (callee-saved registers only). With
// PS4_FIBER_THREADS=1 each fiber instead runs on a dedicated host thread and
// switching passes a baton so exactly one endpoint per guest thread executes
// at a time (every switch is then a cross-thread wake-up, ~10 us).
// On every switch-in the fiber adopts the identity of the guest thread it
// runs on (FS base, thread id, TLS keys), which matches Orbis semantics where
// fibers may migrate between threads and share the running thread's TLS.

void shim_sceFiberOptParamInitialize(GuestContext *ctx);
void shim_sceFiberInitializeImpl(GuestContext *ctx);
void shim_sceFiberInitializeWithInternalOptionImpl(GuestContext *ctx);
void shim_sceFiberFinalize(GuestContext *ctx);
void shim_sceFiberRun(GuestContext *ctx);
void shim_sceFiberSwitch(GuestContext *ctx);
void shim_sceFiberGetSelf(GuestContext *ctx);
void shim_sceFiberReturnToThread(GuestContext *ctx);
void shim_sceFiberGetInfo(GuestContext *ctx);
void shim_sceFiberRename(GuestContext *ctx);
void shim_sceFiberStartContextSizeCheck(GuestContext *ctx);
void shim_sceFiberStopContextSizeCheck(GuestContext *ctx);
void shim_sceFiberAttachContextAndRun(GuestContext *ctx);
void shim_sceFiberAttachContextAndSwitch(GuestContext *ctx);

// The host thread that runs what a guest thread executes now, given the guest thread's own
// GuestContext or that of a fiber it runs: the fiber's host thread in thread mode, else the
// guest thread's. False when no thread that ran fibers has this context. Async-signal-safe.
bool ps4_fiber_endpoint_host_thread(const GuestContext *ctx, pthread_t *out);
// PS4_FIBER_THREADS=1: fibers run on their own host threads.
bool ps4_fiber_threads_mode(void);

// Prints fiber transfer statistics accumulated over the last `frames` frames.
void ps4_fiber_print_stats(int frames);

#ifdef __cplusplus
}
#endif

#endif // PS4_FIBER_H
