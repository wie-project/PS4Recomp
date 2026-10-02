#ifndef PS4_KERNEL_SYS_H
#define PS4_KERNEL_SYS_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OrbisKernelTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
} OrbisKernelTimeval;

typedef struct OrbisKernelSwVersion {
    size_t Size;
    char VersionString[0x1C];
    uint32_t Version;
} OrbisKernelSwVersion;

// High-resolution process timers
uint64_t sceKernelGetProcessTimeCounter(void);
uint64_t sceKernelGetProcessTimeCounterFrequency(void);
uint64_t sceKernelGetProcessTime(void);
uint64_t sceKernelGetTscFrequency(void);
uint64_t sceKernelReadTsc(void);
int32_t sceKernelGettimeofday(OrbisKernelTimeval *tv);

// CPU & System Configuration
int32_t sceKernelGetCurrentCpu(void);
int32_t sceKernelGetCpumode(void);
int32_t sceKernelIsNeoMode(void);
int32_t sceKernelHasNeoMode(void);
int32_t sceKernelIsAuthenticNeo(void);
int32_t sceKernelGetSystemSwVersion(OrbisKernelSwVersion *version);
int32_t sceKernelUuidCreate(void *uuid);
int32_t sceKernelSetGPO(uint32_t gpo);
int32_t sceKernelIsProspero(void);

// Guest execution shims
void shim_sceKernelGetProcessTimeCounter(GuestContext *ctx);
void shim_sceKernelGetProcessTimeCounterFrequency(GuestContext *ctx);
void shim_sceKernelGetProcessTime(GuestContext *ctx);
void shim_sceKernelGetTscFrequency(GuestContext *ctx);
void shim_sceKernelReadTsc(GuestContext *ctx);
void shim_sceKernelGettimeofday(GuestContext *ctx);
void shim_sceKernelGetCurrentCpu(GuestContext *ctx);
void shim_sceKernelGetCpumode(GuestContext *ctx);
void shim_sceKernelIsNeoMode(GuestContext *ctx);
void shim_sceKernelHasNeoMode(GuestContext *ctx);
void shim_sceKernelIsAuthenticNeo(GuestContext *ctx);
void shim_sceKernelGetSystemSwVersion(GuestContext *ctx);
void shim_sceKernelClockGettime(GuestContext *ctx);
void shim_sceKernelUuidCreate(GuestContext *ctx);
void shim_sceKernelSetGPO(GuestContext *ctx);
void shim_sceKernelIsProspero(GuestContext *ctx);
void shim_sceKernelConvertUtcToLocaltime(GuestContext *ctx);
void shim_sceKernelConvertLocaltimeToUtc(GuestContext *ctx);
void shim___pthread_cxa_finalize(GuestContext *ctx);
void shim__sceKernelRtldThreadAtexitDecrement(GuestContext *ctx);
void shim__sceKernelRtldThreadAtexitIncrement(GuestContext *ctx);
void shim__sceKernelSetThreadAtexitReport(GuestContext *ctx);
void shim__sceKernelSetThreadAtexitCount(GuestContext *ctx);
void shim__sceKernelSetThreadDtors(GuestContext *ctx);
void shim__sceKernelRtldSetApplicationHeapAPI(GuestContext *ctx);
void shim___elf_phdr_match_addr(GuestContext *ctx);
void shim_signal(GuestContext *ctx);
void shim__is_signal_return(GuestContext *ctx);
void shim_sceDiscMapIsRequestOnHDD(GuestContext *ctx);
void shim_Func_7C980FFB0AA27E7A(GuestContext *ctx);
void shim_sceDiscMapGetPackageSize(GuestContext *ctx);
void shim_Func_8A828CAEE7EDD5E9(GuestContext *ctx);
void shim_Func_E7EBCE96E92F91F8(GuestContext *ctx);
void shim_sceKernelGetSanitizerNewReplaceExternal(GuestContext *ctx);
void shim_sceKernelGetSanitizerMallocReplaceExternal(GuestContext *ctx);
void shim_sceKernelIsAddressSanitizerEnabled(GuestContext *ctx);
void shim_sceKernelInstallExceptionHandler(GuestContext *ctx);
void shim_sceKernelRaiseException(GuestContext *ctx);
void shim_sceKernelDebugRaiseException(GuestContext *ctx);
void shim_sceKernelDebugRaiseExceptionOnReleaseMode(GuestContext *ctx);
void shim_sceKernelPrintBacktraceWithModuleInfo(GuestContext *ctx);
void shim_sceKernelInternalMemoryGetModuleSegmentInfo(GuestContext *ctx);
void shim_sceKernelGetModuleInfoForUnwind(GuestContext *ctx);
void shim_sceKernelGetModuleInfoFromAddr(GuestContext *ctx);
void shim___progname(GuestContext *ctx);
void shim_getargv(GuestContext *ctx);
void shim_getpagesize(GuestContext *ctx);
void shim_sceLibcHeapGetTraceInfo(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_KERNEL_SYS_H
