#ifndef PS4_DIRECT_MEM_H
#define PS4_DIRECT_MEM_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_DIRECT_MEM_TOTAL_SIZE (32ULL * 1024ULL * 1024ULL * 1024ULL) // 32 GB unified Direct Memory

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

// SceKernelBatchMapEntry (32 bytes), the element of sceKernelBatchMap's array. `start` holds a
// guest address and is written back by the map operations.
typedef enum OrbisKernelBatchMapOp {
    ORBIS_KERNEL_BATCH_MAP_DIRECT = 0,        // sceKernelMapDirectMemory(start, length, protection, offset)
    ORBIS_KERNEL_BATCH_MAP_UNMAP = 1,         // sceKernelMunmap(start, length)
    ORBIS_KERNEL_BATCH_MAP_PROTECT = 2,       // sceKernelMprotect(start, length, protection)
    ORBIS_KERNEL_BATCH_MAP_FLEXIBLE = 3,      // sceKernelMapFlexibleMemory(start, length, protection)
    ORBIS_KERNEL_BATCH_MAP_TYPE_PROTECT = 4   // sceKernelMtypeprotect(start, length, type, protection)
} OrbisKernelBatchMapOp;

typedef struct OrbisKernelBatchMapEntry {
    uint64_t start;
    uint64_t offset;
    uint64_t length;
    int8_t protection;
    int8_t type;
    int16_t reserved;
    int32_t operation;
} OrbisKernelBatchMapEntry;

typedef struct OrbisDirectMemoryQueryInfo {
    uint64_t start;
    uint64_t end;
    int32_t memoryType;
} OrbisDirectMemoryQueryInfo;

int ps4_direct_mem_init(void);
void ps4_direct_mem_on_unmap(GuestContext *ctx, uint64_t addr, size_t size);
void ps4_direct_mem_destroy(void);

// Direct Memory
int sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t length, size_t alignment, int type, off_t *physAddrOut);
int sceKernelAllocateMainDirectMemory(size_t length, size_t alignment, int type, off_t *physAddrOut);
size_t sceKernelGetDirectMemorySize(void);
size_t sceKernelAvailableDirectMemorySize(void);
int sceKernelMapDirectMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags, off_t physAddr, size_t alignment);
int sceKernelReleaseDirectMemory(off_t physAddr, size_t length);
int sceKernelCheckedReleaseDirectMemory(off_t physAddr, size_t length);
int sceKernelDirectMemoryQuery(off_t offset, int flags, OrbisDirectMemoryQueryInfo *info, size_t infoSize);

// Flexible Memory & VMM
int sceKernelMapFlexibleMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags);
int sceKernelMapNamedFlexibleMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags, const char *name);
int sceKernelReserveVirtualRange(GuestContext *ctx, void **addrInOut, size_t length, int flags, size_t alignment);
int sceKernelSetVirtualRangeName(GuestContext *ctx, const void *addr, size_t len, const char *name);
int sceKernelMprotect(GuestContext *ctx, const void *addr, size_t size, int prot);
int sceKernelMtypeprotect(GuestContext *ctx, const void *addr, size_t size, int mtype, int prot);
int sceKernelConfiguredFlexibleMemorySize(GuestContext *ctx, uint64_t *sizeOut);
size_t sceKernelAvailableFlexibleMemorySize(GuestContext *ctx);
int sceKernelBatchMap(GuestContext *ctx, OrbisKernelBatchMapEntry *entries, int numEntries, int *numEntriesOut);
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
void shim_sceKernelCheckedReleaseDirectMemory(GuestContext *ctx);
void shim_sceKernelDirectMemoryQuery(GuestContext *ctx);
void shim_sceKernelMapDirectMemory(GuestContext *ctx);
void shim_sceKernelGetDirectMemorySize(GuestContext *ctx);
void shim_sceKernelAvailableDirectMemorySize(GuestContext *ctx);
void shim_sceKernelMapFlexibleMemory(GuestContext *ctx);
void shim_sceKernelMapNamedFlexibleMemory(GuestContext *ctx);
void shim_sceKernelReserveVirtualRange(GuestContext *ctx);
void shim_sceKernelSetVirtualRangeName(GuestContext *ctx);
void shim_sceKernelMprotect(GuestContext *ctx);
void shim_sceKernelMtypeprotect(GuestContext *ctx);
void shim_sceKernelBatchMap(GuestContext *ctx);
void shim_sceKernelConfiguredFlexibleMemorySize(GuestContext *ctx);
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
