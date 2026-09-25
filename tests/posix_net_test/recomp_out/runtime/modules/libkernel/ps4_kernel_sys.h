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

#ifdef __cplusplus
}
#endif

#endif // PS4_KERNEL_SYS_H
