#ifndef PS4_DIRECT_MEM_H
#define PS4_DIRECT_MEM_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_DIRECT_MEM_TOTAL_SIZE (4ULL * 1024ULL * 1024ULL * 1024ULL) // 4 GB

int ps4_direct_mem_init(void);
void ps4_direct_mem_destroy(void);
int sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t length, size_t alignment, int type, off_t *physAddrOut);
size_t sceKernelGetDirectMemorySize(void);
int sceKernelMapDirectMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags, off_t physAddr, size_t alignment);
int sceKernelReleaseDirectMemory(off_t physAddr, size_t length);

void shim_sceKernelAllocateDirectMemory(GuestContext *ctx);
void shim_sceKernelGetDirectMemorySize(GuestContext *ctx);
void shim_sceKernelMapDirectMemory(GuestContext *ctx);
void shim_sceKernelReleaseDirectMemory(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_DIRECT_MEM_H
