// libkernel: sceKernelGetModuleList, sceKernelGetModuleInfo,
// sceKernelReleaseFlexibleMemory.
#include "ps4_sysmodule.h"
#include "recomp_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define KMOD_ERROR_ENOMEM ((int32_t)0x8002000c)
#define KMOD_ERROR_EFAULT ((int32_t)0x8002000e)
#define KMOD_ERROR_EINVAL ((int32_t)0x80020016)
#define KMOD_ERROR_ESRCH  ((int32_t)0x80020003)

#define KMOD_MAX 80

typedef struct KmodSegmentInfo {
    uint64_t address;
    uint32_t size;
    int32_t prot;
} KmodSegmentInfo;

// OrbisKernelModuleInfo (shadPS4 layout, 352 bytes)
typedef struct KmodModuleInfo {
    uint64_t st_size;
    char name[256];
    KmodSegmentInfo segments[4];
    uint32_t num_segments;
    uint8_t fingerprint[20];
} KmodModuleInfo;

// int sceKernelGetModuleList(SceKernelModule *array, size_t numArray, size_t *actualNum)
void shim_sceKernelGetModuleList(GuestContext *ctx) {
    uint64_t arr_g = ctx->rdi;
    uint64_t num = ctx->rsi;
    uint64_t out_g = ctx->rdx;
    if (!out_g || !ctx->mem_base || (num && !arr_g)) {
        ctx->rax = (uint64_t)(int64_t)KMOD_ERROR_EFAULT;
        SHIM_RETURN();
    }
    RecompModuleSnapshot *mods = (RecompModuleSnapshot *)calloc(KMOD_MAX, sizeof(*mods));
    if (!mods) {
        ctx->rax = (uint64_t)(int64_t)KMOD_ERROR_ENOMEM;
        SHIM_RETURN();
    }
    int n = recomp_module_snapshot(mods, KMOD_MAX);
    *(uint64_t *)(ctx->mem_base + out_g) = (uint64_t)n;
    int32_t rc = 0;
    if ((uint64_t)n > num) {
        rc = KMOD_ERROR_ENOMEM;
    } else {
        int32_t *arr = (int32_t *)(ctx->mem_base + arr_g);
        for (int i = 0; i < n; i++) arr[i] = mods[i].handle;
    }
    static int logged;
    if (!logged) {
        logged = 1;
        fprintf(stderr, "[ps4-kernel] sceKernelGetModuleList: %d module(s)\n", n);
    }
    free(mods);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// int sceKernelGetModuleInfo(SceKernelModule handle, SceKernelModuleInfo *info)
void shim_sceKernelGetModuleInfo(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t info_g = ctx->rsi;
    if (!info_g || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)KMOD_ERROR_EFAULT;
        SHIM_RETURN();
    }
    RecompModuleSnapshot snap;
    if (recomp_module_info_by_handle(handle, &snap) != 0) {
        ctx->rax = (uint64_t)(int64_t)KMOD_ERROR_ESRCH;
        SHIM_RETURN();
    }
    KmodModuleInfo *info = (KmodModuleInfo *)(ctx->mem_base + info_g);
    memset(info, 0, sizeof(*info));
    info->st_size = sizeof(*info);
    snprintf(info->name, sizeof(info->name), "%s", snap.name);
    RecompModuleSegment segs[4];
    info->num_segments = recomp_module_snapshot_segments(&snap, segs);
    for (uint32_t i = 0; i < info->num_segments; i++) {
        info->segments[i].address = segs[i].addr;
        info->segments[i].size = (uint32_t)segs[i].size;
        info->segments[i].prot = segs[i].prot;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceKernelReleaseFlexibleMemory(void *addr, size_t len)
// Returns the pages to the system: unmap through the guest VM; if the range is
// only part of a larger mapping, discard the backing pages instead.
void shim_sceKernelReleaseFlexibleMemory(GuestContext *ctx) {
    uint64_t addr = ctx->rdi;
    uint64_t len = ctx->rsi;
    if (len == 0 || (addr & 0x3fffULL) || (len & 0x3fffULL) || addr >= ctx->mem_size ||
        len > ctx->mem_size - addr) {
        ctx->rax = (uint64_t)(int64_t)KMOD_ERROR_EINVAL;
        SHIM_RETURN();
    }
    if (recomp_vm_free(ctx, addr, (size_t)len) != 0) {
        uint64_t page = (uint64_t)getpagesize();
        uint64_t start = (addr + page - 1) & ~(page - 1);
        uint64_t end = (addr + len) & ~(page - 1);
        if (end > start) {
            madvise(ctx->mem_base + start, (size_t)(end - start), MADV_FREE);
        }
    }
    static int logged;
    if (!logged) {
        logged = 1;
        fprintf(stderr, "[ps4-kernel] sceKernelReleaseFlexibleMemory(0x%llx, 0x%llx)\n",
                (unsigned long long)addr, (unsigned long long)len);
    }
    ctx->rax = 0;
    SHIM_RETURN();
}
