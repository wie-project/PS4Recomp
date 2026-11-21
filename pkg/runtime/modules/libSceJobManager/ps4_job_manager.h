#ifndef PS4_JOB_MANAGER_H
#define PS4_JOB_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Shims for sce::Job::JobManager (libSceJobManager)
void shim__ZN3sce3Job10JobManagerC1Ev(GuestContext *ctx);
void shim__ZN3sce3Job10JobManagerC2Ev(GuestContext *ctx);
void shim__ZN3sce3Job10JobManagerD1Ev(GuestContext *ctx);
void shim__ZN3sce3Job10JobManagerD2Ev(GuestContext *ctx);
void shim__ZN3sce3Job10JobManager27calculateRequiredMemorySizeEPKNS1_21MemorySizeQueryParamsE(GuestContext *ctx);
void shim__ZN3sce3Job10JobManager10initializeEPvmjjjmPKNS1_12ConfigParamsE(GuestContext *ctx);
void shim__ZN3sce3Job10JobManager24setPersistentThreadCountEj(GuestContext *ctx);
void shim__ZN3sce3Job10JobManager27getSequenceFactoryInterfaceENS1_8PriorityE(GuestContext *ctx);
void shim__ZN3sce3Job10JobManager8shutdownEv(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_JOB_MANAGER_H
