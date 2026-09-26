#ifndef PS4_DIRECT_MEM_H
#define PS4_DIRECT_MEM_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_DIRECT_MEM_TOTAL_SIZE (6ULL * 1024ULL * 1024ULL * 1024ULL) // 6 GB unified Direct Memory

typedef struct OrbisKernelVirtualQueryInfo {
    void *start;
    void *end;
    off_t offset;
    int32_t prot;
    int32_t flags;
    unsigned isFlexibleMemory : 1;
    unsigned isDirectMemory : 1;
    unsigned isStack : 1;
    unsigned isPooledMemory : 1;
    unsigned isCommitted : 1;
    char name[32];
} OrbisKernelVirtualQueryInfo;

int ps4_direct_mem_init(void);
void ps4_direct_mem_destroy(void);

// Direct Memory
int sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t length, size_t alignment, int type, off_t *physAddrOut);
int sceKernelAllocateMainDirectMemory(size_t length, size_t alignment, int type, off_t *physAddrOut);
size_t sceKernelGetDirectMemorySize(void);
size_t sceKernelAvailableDirectMemorySize(void);
int sceKernelMapDirectMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags, off_t physAddr, size_t alignment);
int sceKernelReleaseDirectMemory(off_t physAddr, size_t length);

// Flexible Memory & VMM
int sceKernelMapFlexibleMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags);
size_t sceKernelAvailableFlexibleMemorySize(GuestContext *ctx);
int sceKernelVirtualQuery(GuestContext *ctx, const void *addr, int flags, OrbisKernelVirtualQueryInfo *info, size_t infoSize);
int sceKernelQueryMemoryProtection(GuestContext *ctx, const void *addr, void **start, void **end, int *prot);

// Memory Pools
int sceKernelMemoryPoolReserve(GuestContext *ctx, void *addrIn, size_t len, size_t alignment, int flags, void **addrOut);
int sceKernelMemoryPoolExpand(GuestContext *ctx, off_t searchStart, off_t searchEnd, size_t len, size_t alignment, off_t *physAddrOut);
int sceKernelMemoryPoolCommit(GuestContext *ctx, void *addr, size_t len, int type, int prot, int flags);
int sceKernelMemoryPoolDecommit(GuestContext *ctx, void *addr, size_t len, int flags);

// Guest execution shims
void shim_sceKernelAllocateDirectMemory(GuestContext *ctx);
void shim_sceKernelAllocateMainDirectMemory(GuestContext *ctx);
void shim_sceKernelReleaseDirectMemory(GuestContext *ctx);
void shim_sceKernelMapDirectMemory(GuestContext *ctx);
void shim_sceKernelGetDirectMemorySize(GuestContext *ctx);
void shim_sceKernelAvailableDirectMemorySize(GuestContext *ctx);
void shim_sceKernelMapFlexibleMemory(GuestContext *ctx);
void shim_sceKernelAvailableFlexibleMemorySize(GuestContext *ctx);
void shim_sceKernelVirtualQuery(GuestContext *ctx);
void shim_sceKernelQueryMemoryProtection(GuestContext *ctx);
void shim_sceKernelMemoryPoolReserve(GuestContext *ctx);
void shim_sceKernelMemoryPoolExpand(GuestContext *ctx);
void shim_sceKernelMemoryPoolCommit(GuestContext *ctx);
void shim_sceKernelMemoryPoolDecommit(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_DIRECT_MEM_H
