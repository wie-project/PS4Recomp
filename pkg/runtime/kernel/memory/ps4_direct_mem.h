#ifndef PS4_DIRECT_MEM_H
#define PS4_DIRECT_MEM_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Memory totals defined according to PS4 hardware / shadPS4 specifications
#define ORBIS_KERNEL_TOTAL_MEM          (5248ULL * 1024 * 1024)
#define ORBIS_KERNEL_TOTAL_MEM_PRO      (5888ULL * 1024 * 1024)
#define ORBIS_KERNEL_TOTAL_MEM_DEV      (6656ULL * 1024 * 1024)
#define ORBIS_KERNEL_TOTAL_MEM_DEV_PRO  (7936ULL * 1024 * 1024)

#define ORBIS_KERNEL_FLEXIBLE_MEMORY_BASE (64ULL * 1024 * 1024)
#define ORBIS_KERNEL_FLEXIBLE_MEMORY_SIZE (512ULL * 1024 * 1024)

#define GUEST_PAGE_BITS 14
#define GUEST_PAGE_SIZE (1ULL << GUEST_PAGE_BITS) // 16 KB (0x4000)
#define DEFAULT_MAPPING_BASE 0x200000000ULL       // 8 GB

// Physical Memory Type
typedef enum PhysicalMemoryType {
    PHYS_MEM_TYPE_FREE = 0,
    PHYS_MEM_TYPE_ALLOCATED = 1,
    PHYS_MEM_TYPE_MAPPED = 2,
    PHYS_MEM_TYPE_POOLED = 3,
    PHYS_MEM_TYPE_COMMITTED = 4,
    PHYS_MEM_TYPE_FLEXIBLE = 5,
} PhysicalMemoryType;

// Virtual Memory Area Type
typedef enum VMAType {
    VMA_TYPE_FREE = 0,
    VMA_TYPE_RESERVED = 1,
    VMA_TYPE_DIRECT = 2,
    VMA_TYPE_FLEXIBLE = 3,
    VMA_TYPE_POOLED = 4,
    VMA_TYPE_POOL_RESERVED = 5,
    VMA_TYPE_STACK = 6,
    VMA_TYPE_CODE = 7,
    VMA_TYPE_FILE = 8,
    VMA_TYPE_SYSTEM = 9,
} VMAType;

// Memory Protection Flags
typedef enum MemoryProt {
    MEM_PROT_NO_ACCESS = 0,
    MEM_PROT_CPU_READ = 1,
    MEM_PROT_CPU_WRITE = 2,
    MEM_PROT_CPU_READ_WRITE = 3,
    MEM_PROT_CPU_EXEC = 4,
    MEM_PROT_GPU_READ = 16,
    MEM_PROT_GPU_WRITE = 32,
    MEM_PROT_GPU_READ_WRITE = 48,
} MemoryProt;

// Memory Map Flags
typedef enum MemoryMapFlags {
    MEM_MAP_NO_FLAGS = 0,
    MEM_MAP_SHARED = 1,
    MEM_MAP_PRIVATE = 2,
    MEM_MAP_FIXED = 0x10,
    MEM_MAP_NO_OVERWRITE = 0x80,
    MEM_MAP_VOID = 0x100,
    MEM_MAP_STACK = 0x400,
    MEM_MAP_NO_SYNC = 0x800,
    MEM_MAP_ANON = 0x1000,
    MEM_MAP_SYSTEM = 0x2000,
    MEM_MAP_NO_CORE = 0x20000,
    MEM_MAP_NO_COALESCE = 0x400000,
} MemoryMapFlags;

// Matches shadPS4 OrbisVirtualQueryInfo (72 bytes)
typedef struct OrbisKernelVirtualQueryInfo {
    void *start;
    void *end;
    off_t offset;
    int32_t prot;
    int32_t memory_type;
    uint8_t isFlexibleMemory : 1;
    uint8_t isDirectMemory : 1;
    uint8_t isStack : 1;
    uint8_t isPooledMemory : 1;
    uint8_t isCommitted : 1;
    char name[32];
} OrbisKernelVirtualQueryInfo;

// SceKernelBatchMapEntry (32 bytes), the element of sceKernelBatchMap's array.
typedef enum OrbisKernelBatchMapOp {
    ORBIS_KERNEL_BATCH_MAP_DIRECT = 0,
    ORBIS_KERNEL_BATCH_MAP_UNMAP = 1,
    ORBIS_KERNEL_BATCH_MAP_PROTECT = 2,
    ORBIS_KERNEL_BATCH_MAP_FLEXIBLE = 3,
    ORBIS_KERNEL_BATCH_MAP_TYPE_PROTECT = 4
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

// Lifecycle
int ps4_direct_mem_init(void);
void ps4_direct_mem_on_unmap(GuestContext *ctx, uint64_t addr, size_t size);
void ps4_direct_mem_destroy(void);

// Direct Memory
int sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t length, size_t alignment, int type, off_t *physAddrOut);
int sceKernelAllocateMainDirectMemory(size_t length, size_t alignment, int type, off_t *physAddrOut);
size_t sceKernelGetDirectMemorySize(void);
size_t sceKernelAvailableDirectMemorySize(void);
int sceKernelAvailableDirectMemorySizeExact(off_t searchStart, off_t searchEnd, size_t alignment, off_t *physAddrOut, size_t *sizeOut);
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
