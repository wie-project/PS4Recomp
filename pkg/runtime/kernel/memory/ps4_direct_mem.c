#include "ps4_direct_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>

typedef struct DirectMemBlock {
    off_t phys_offset;
    size_t size;
    int is_free;
    uint64_t mapped_guest_vaddr;
    struct DirectMemBlock *next;
    struct DirectMemBlock *prev;
} DirectMemBlock;

static DirectMemBlock *g_direct_blocks = NULL;
static pthread_mutex_t g_direct_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_direct_inited = 0;

int ps4_direct_mem_init(void) {
    pthread_mutex_lock(&g_direct_mutex);
    if (!g_direct_inited) {
        g_direct_blocks = (DirectMemBlock *)calloc(1, sizeof(DirectMemBlock));
        if (!g_direct_blocks) {
            pthread_mutex_unlock(&g_direct_mutex);
            return -1;
        }
        g_direct_blocks->phys_offset = 0;
        g_direct_blocks->size = PS4_DIRECT_MEM_TOTAL_SIZE;
        g_direct_blocks->is_free = 1;
        g_direct_blocks->mapped_guest_vaddr = 0;
        g_direct_blocks->next = NULL;
        g_direct_blocks->prev = NULL;
        g_direct_inited = 1;
    }
    pthread_mutex_unlock(&g_direct_mutex);
    return 0;
}

void ps4_direct_mem_destroy(void) {
    pthread_mutex_lock(&g_direct_mutex);
    DirectMemBlock *curr = g_direct_blocks;
    while (curr) {
        DirectMemBlock *next = curr->next;
        free(curr);
        curr = next;
    }
    g_direct_blocks = NULL;
    g_direct_inited = 0;
    pthread_mutex_unlock(&g_direct_mutex);
}

size_t sceKernelGetDirectMemorySize(void) {
    return PS4_DIRECT_MEM_TOTAL_SIZE;
}

size_t sceKernelAvailableDirectMemorySize(void) {
    ps4_direct_mem_init();
    pthread_mutex_lock(&g_direct_mutex);
    size_t free_total = 0;
    DirectMemBlock *curr = g_direct_blocks;
    while (curr) {
        if (curr->is_free) {
            free_total += curr->size;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&g_direct_mutex);
    return free_total;
}

int sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t length, size_t alignment, int type, off_t *physAddrOut) {
    (void)type;
    if (!physAddrOut || length == 0) return -EINVAL;
    if (alignment == 0) alignment = 4096;
    size_t aligned_len = (length + alignment - 1) & ~(alignment - 1);

    ps4_direct_mem_init();

    pthread_mutex_lock(&g_direct_mutex);
    DirectMemBlock *curr = g_direct_blocks;
    DirectMemBlock *best = NULL;

    while (curr) {
        if (curr->is_free) {
            off_t aligned_start = (curr->phys_offset + alignment - 1) & ~(alignment - 1);
            off_t offset_padding = aligned_start - curr->phys_offset;
            if (curr->size >= aligned_len + offset_padding) {
                if (searchEnd > 0 && aligned_start + aligned_len > (size_t)searchEnd) {
                    curr = curr->next;
                    continue;
                }
                if (aligned_start >= searchStart) {
                    best = curr;
                    break;
                }
            }
        }
        curr = curr->next;
    }

    if (!best) {
        pthread_mutex_unlock(&g_direct_mutex);
        return -ENOMEM;
    }

    off_t alloc_start = (best->phys_offset + alignment - 1) & ~(alignment - 1);
    size_t front_pad = alloc_start - best->phys_offset;

    // Split leading padding if necessary
    if (front_pad > 0) {
        DirectMemBlock *pad_block = (DirectMemBlock *)calloc(1, sizeof(DirectMemBlock));
        pad_block->phys_offset = best->phys_offset;
        pad_block->size = front_pad;
        pad_block->is_free = 1;
        pad_block->prev = best->prev;
        pad_block->next = best;
        if (best->prev) best->prev->next = pad_block;
        else g_direct_blocks = pad_block;
        best->prev = pad_block;
        best->phys_offset = alloc_start;
        best->size -= front_pad;
    }

    // Split trailing remainder
    if (best->size > aligned_len) {
        DirectMemBlock *tail = (DirectMemBlock *)calloc(1, sizeof(DirectMemBlock));
        tail->phys_offset = best->phys_offset + aligned_len;
        tail->size = best->size - aligned_len;
        tail->is_free = 1;
        tail->prev = best;
        tail->next = best->next;
        if (best->next) best->next->prev = tail;
        best->next = tail;
        best->size = aligned_len;
    }

    best->is_free = 0;
    *physAddrOut = best->phys_offset;
    pthread_mutex_unlock(&g_direct_mutex);

    return 0;
}

int sceKernelAllocateMainDirectMemory(size_t length, size_t alignment, int type, off_t *physAddrOut) {
    return sceKernelAllocateDirectMemory(0, -1, length, alignment, type, physAddrOut);
}

int sceKernelMapDirectMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags, off_t physAddr, size_t alignment) {
    if (!ctx || !addrInOut || length == 0) return -EINVAL;
    if (alignment == 0) alignment = 4096;
    size_t aligned_len = (length + alignment - 1) & ~(alignment - 1);

    ps4_direct_mem_init();

    pthread_mutex_lock(&g_direct_mutex);
    DirectMemBlock *curr = g_direct_blocks;
    DirectMemBlock *found = NULL;
    while (curr) {
        if (!curr->is_free && curr->phys_offset == physAddr) {
            found = curr;
            break;
        }
        curr = curr->next;
    }

    if (!found) {
        pthread_mutex_unlock(&g_direct_mutex);
        return -EINVAL;
    }

    uint64_t vaddr = (uint64_t)-1;
    // Check if fixed address was requested
    if (*addrInOut != NULL) {
        uint64_t req_vaddr = (uint64_t)*addrInOut;
        vaddr = recomp_vm_alloc_fixed(ctx, req_vaddr, aligned_len, prot, flags, "direct_mem");
    }

    // If no fixed address requested or fixed mapping failed, allocate dynamically
    if (vaddr == (uint64_t)-1) {
        vaddr = recomp_vm_alloc_named(ctx, aligned_len, prot, flags, "direct_mem");
    }

    if (vaddr == (uint64_t)-1) {
        pthread_mutex_unlock(&g_direct_mutex);
        return -ENOMEM;
    }

    found->mapped_guest_vaddr = vaddr;
    *addrInOut = (void *)vaddr;

    pthread_mutex_unlock(&g_direct_mutex);
    return 0;
}

int sceKernelReleaseDirectMemory(off_t physAddr, size_t length) {
    (void)length;
    ps4_direct_mem_init();

    pthread_mutex_lock(&g_direct_mutex);
    DirectMemBlock *curr = g_direct_blocks;
    while (curr) {
        if (!curr->is_free && curr->phys_offset == physAddr) {
            curr->is_free = 1;
            curr->mapped_guest_vaddr = 0;

            // Merge with next if free
            if (curr->next && curr->next->is_free) {
                DirectMemBlock *next = curr->next;
                curr->size += next->size;
                curr->next = next->next;
                if (next->next) next->next->prev = curr;
                free(next);
            }
            // Merge with prev if free
            if (curr->prev && curr->prev->is_free) {
                DirectMemBlock *prev = curr->prev;
                prev->size += curr->size;
                prev->next = curr->next;
                if (curr->next) curr->next->prev = prev;
                free(curr);
            }
            pthread_mutex_unlock(&g_direct_mutex);
            return 0;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&g_direct_mutex);
    return -EINVAL;
}

int sceKernelMapFlexibleMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags) {
    if (!ctx || !addrInOut || length == 0) return -EINVAL;
    size_t aligned_len = (length + 4095ULL) & ~4095ULL;

    uint64_t vaddr = (uint64_t)-1;
    if (*addrInOut != NULL) {
        uint64_t req_vaddr = (uint64_t)*addrInOut;
        vaddr = recomp_vm_alloc_fixed(ctx, req_vaddr, aligned_len, prot, flags, "flexible_mem");
    }

    if (vaddr == (uint64_t)-1) {
        vaddr = recomp_vm_alloc_named(ctx, aligned_len, prot, flags, "flexible_mem");
    }

    if (vaddr == (uint64_t)-1) {
        return -ENOMEM;
    }

    *addrInOut = (void *)vaddr;
    return 0;
}

int sceKernelConfiguredFlexibleMemorySize(GuestContext *ctx, uint64_t *sizeOut) {
    (void)ctx;
    if (!sizeOut) return -EINVAL;
    *sizeOut = 512ULL * 1024 * 1024; // 512MB default flexible memory size (ORBIS_KERNEL_FLEXIBLE_MEMORY_SIZE)
    return 0;
}

size_t sceKernelAvailableFlexibleMemorySize(GuestContext *ctx) {
    if (!ctx) return 0;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    pthread_mutex_lock(&proc->vm_mutex);
    size_t free_total = 0;
    GuestVMExtent *curr = proc->vm_extents;
    while (curr) {
        if (curr->is_free) {
            free_total += curr->size;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&proc->vm_mutex);
    return free_total;
}

int sceKernelVirtualQuery(GuestContext *ctx, const void *addr, int flags, OrbisKernelVirtualQueryInfo *info, size_t infoSize) {
    (void)flags;
    if (!ctx || !info || infoSize < sizeof(OrbisKernelVirtualQueryInfo)) {
        return -EINVAL;
    }
    uint64_t vaddr = (uint64_t)addr;
    GuestVMExtent *extent = recomp_vm_find(ctx, vaddr);
    if (!extent) {
        return -EFAULT;
    }

    memset(info, 0, infoSize);
    info->start = (void *)extent->addr;
    info->end = (void *)(extent->addr + extent->size);
    info->prot = extent->prot ? extent->prot : (PROT_READ | PROT_WRITE);
    info->flags = extent->flags;
    info->isCommitted = extent->is_free ? 0 : 1;
    info->isFlexibleMemory = (strstr(extent->name, "flexible") != NULL) ? 1 : 0;
    info->isDirectMemory = (strstr(extent->name, "direct") != NULL) ? 1 : 0;
    info->isPooledMemory = (strstr(extent->name, "pool") != NULL) ? 1 : 0;
    strncpy(info->name, extent->name, sizeof(info->name) - 1);

    return 0;
}

int sceKernelQueryMemoryProtection(GuestContext *ctx, const void *addr, void **start, void **end, int *prot) {
    if (!ctx) return -EINVAL;
    uint64_t vaddr = (uint64_t)addr;
    GuestVMExtent *extent = recomp_vm_find(ctx, vaddr);
    if (!extent) {
        return -EFAULT;
    }
    if (start) *start = (void *)extent->addr;
    if (end) *end = (void *)(extent->addr + extent->size);
    if (prot) *prot = extent->prot ? extent->prot : (PROT_READ | PROT_WRITE);
    return 0;
}

// Memory Pool implementation
int sceKernelMemoryPoolReserve(GuestContext *ctx, void *addrIn, size_t len, size_t alignment, int flags, void **addrOut) {
    if (!ctx || !addrOut || len == 0) return -EINVAL;
    if (alignment == 0) alignment = 2ULL * 1024 * 1024; // 2MB default alignment
    size_t aligned_size = (len + alignment - 1) & ~(alignment - 1);

    uint64_t vaddr = (uint64_t)-1;
    if (addrIn != NULL) {
        uint64_t req_vaddr = (uint64_t)addrIn;
        vaddr = recomp_vm_alloc_fixed(ctx, req_vaddr, aligned_size, PROT_READ | PROT_WRITE, flags, "pool_reserved");
        if (vaddr == (uint64_t)-1) {
            // In case the guest requested a fixed virtual reservation that is outside
            // the initial linear window, accept the requested virtual base.
            vaddr = req_vaddr;
        }
    } else {
        vaddr = recomp_vm_alloc_named(ctx, aligned_size, PROT_READ | PROT_WRITE, flags, "pool_reserved");
    }

    if (vaddr == (uint64_t)-1) {
        return -ENOMEM;
    }

    *addrOut = (void *)vaddr;
    return 0; // ORBIS_OK
}

int sceKernelMemoryPoolExpand(GuestContext *ctx, off_t searchStart, off_t searchEnd, size_t len, size_t alignment, off_t *physAddrOut) {
    (void)ctx;
    if (len == 0 || !physAddrOut) return -EINVAL;
    if (alignment == 0) alignment = 64ULL * 1024; // 64KB default alignment
    return sceKernelAllocateDirectMemory(searchStart, searchEnd, len, alignment, 3 /* POOLED */, physAddrOut);
}

int sceKernelMemoryPoolCommit(GuestContext *ctx, void *addr, size_t len, int type, int prot, int flags) {
    (void)ctx;
    (void)type;
    (void)prot;
    (void)flags;
    if (!addr || len == 0) return -EINVAL;
    return 0; // ORBIS_OK
}

int sceKernelMemoryPoolDecommit(GuestContext *ctx, void *addr, size_t len, int flags) {
    (void)flags;
    if (!ctx || !addr || len == 0) return -EINVAL;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    uint64_t vaddr = (uint64_t)addr;
    if (proc->mem_base && vaddr + len <= proc->mem_size) {
#if defined(__APPLE__) || defined(__linux__)
        madvise(proc->mem_base + vaddr, len, MADV_DONTNEED);
#endif
    }
    return 0; // ORBIS_OK
}

// Shims for guest execution
void shim_sceKernelAllocateDirectMemory(GuestContext *ctx) {
    off_t searchStart = (off_t)ctx->rdi;
    off_t searchEnd = (off_t)ctx->rsi;
    size_t length = (size_t)ctx->rdx;
    size_t alignment = (size_t)ctx->rcx;
    int type = (int)ctx->r8;
    uint64_t physAddrOutGuest = ctx->r9;

    off_t physAddr = 0;
    int rc = sceKernelAllocateDirectMemory(searchStart, searchEnd, length, alignment, type, &physAddr);
    if (rc == 0 && physAddrOutGuest != 0) {
        *(off_t *)(ctx->mem_base + physAddrOutGuest) = physAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelAllocateMainDirectMemory(GuestContext *ctx) {
    size_t length = (size_t)ctx->rdi;
    size_t alignment = (size_t)ctx->rsi;
    int type = (int)ctx->rdx;
    uint64_t physAddrOutGuest = ctx->rcx;

    off_t physAddr = 0;
    int rc = sceKernelAllocateMainDirectMemory(length, alignment, type, &physAddr);
    if (rc == 0 && physAddrOutGuest != 0) {
        *(off_t *)(ctx->mem_base + physAddrOutGuest) = physAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelGetDirectMemorySize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKernelGetDirectMemorySize();
    SHIM_RETURN();
}

void shim_sceKernelAvailableDirectMemorySize(GuestContext *ctx) {
    uint64_t startOutGuest = ctx->rcx;
    uint64_t sizeOutGuest = ctx->r8;

    if (startOutGuest != 0 || sizeOutGuest != 0) {
        // PS4 SDK convention:
        // int sceKernelAvailableDirectMemorySize(off_t searchStart, off_t searchEnd, int flags, off_t *startOut, size_t *sizeOut)
        // Returns 0 on success, writes free block start and size.
        off_t searchStart = (off_t)ctx->rdi;
        off_t searchEnd = (off_t)ctx->rsi;
        (void)searchEnd;

        ps4_direct_mem_init();
        pthread_mutex_lock(&g_direct_mutex);
        size_t free_total = 0;
        off_t best_start = searchStart;
        DirectMemBlock *curr = g_direct_blocks;
        while (curr) {
            if (curr->is_free) {
                free_total += curr->size;
                if (best_start == 0) best_start = curr->phys_offset;
            }
            curr = curr->next;
        }
        pthread_mutex_unlock(&g_direct_mutex);

        if (startOutGuest && startOutGuest < ctx->mem_size) {
            *(off_t *)(ctx->mem_base + startOutGuest) = best_start;
        }
        if (sizeOutGuest && sizeOutGuest < ctx->mem_size) {
            *(size_t *)(ctx->mem_base + sizeOutGuest) = free_total;
        }
        ctx->rax = 0; // SCE_OK
    } else {
        ctx->rax = (uint64_t)sceKernelAvailableDirectMemorySize();
    }
    SHIM_RETURN();
}

void shim_sceKernelMapDirectMemory(GuestContext *ctx) {
    uint64_t addrInOutGuest = ctx->rdi;
    size_t length = (size_t)ctx->rsi;
    int prot = (int)ctx->rdx;
    int flags = (int)ctx->rcx;
    off_t physAddr = (off_t)ctx->r8;
    size_t alignment = (size_t)ctx->r9;

    void *mappedAddr = NULL;
    if (addrInOutGuest != 0) {
        mappedAddr = (void *)*(uint64_t *)(ctx->mem_base + addrInOutGuest);
    }
    int rc = sceKernelMapDirectMemory(ctx, &mappedAddr, length, prot, flags, physAddr, alignment);
    if (rc == 0 && addrInOutGuest != 0) {
        *(uint64_t *)(ctx->mem_base + addrInOutGuest) = (uint64_t)mappedAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelReleaseDirectMemory(GuestContext *ctx) {
    off_t physAddr = (off_t)ctx->rdi;
    size_t length = (size_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceKernelReleaseDirectMemory(physAddr, length);
    SHIM_RETURN();
}

void shim_sceKernelMapFlexibleMemory(GuestContext *ctx) {
    uint64_t addrInOutGuest = ctx->rdi;
    size_t length = (size_t)ctx->rsi;
    int prot = (int)ctx->rdx;
    int flags = (int)ctx->rcx;

    void *mappedAddr = NULL;
    if (addrInOutGuest != 0) {
        mappedAddr = (void *)*(uint64_t *)(ctx->mem_base + addrInOutGuest);
    }
    int rc = sceKernelMapFlexibleMemory(ctx, &mappedAddr, length, prot, flags);
    if (rc == 0 && addrInOutGuest != 0) {
        *(uint64_t *)(ctx->mem_base + addrInOutGuest) = (uint64_t)mappedAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelConfiguredFlexibleMemorySize(GuestContext *ctx) {
    uint64_t sizeOutGuest = ctx->rdi;
    if (sizeOutGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)-EINVAL;
    } else if (ctx->mem_base && sizeOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + sizeOutGuest) = 512ULL * 1024 * 1024;
        ctx->rax = 0;
    } else {
        ctx->rax = (uint64_t)(int64_t)-EFAULT;
    }
    SHIM_RETURN();
}

void shim_sceKernelAvailableFlexibleMemorySize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKernelAvailableFlexibleMemorySize(ctx);
    SHIM_RETURN();
}

void shim_sceKernelVirtualQuery(GuestContext *ctx) {
    uint64_t addr = ctx->rdi;
    int flags = (int)ctx->rsi;
    uint64_t infoGuest = ctx->rdx;
    size_t infoSize = (size_t)ctx->rcx;

    if (!infoGuest) {
        ctx->rax = (uint64_t)(int64_t)-EINVAL;
        SHIM_RETURN();
    }

    OrbisKernelVirtualQueryInfo info = {0};
    int rc = sceKernelVirtualQuery(ctx, (const void *)addr, flags, &info, sizeof(info));
    if (rc == 0) {
        size_t copy_sz = infoSize < sizeof(info) ? infoSize : sizeof(info);
        memcpy(ctx->mem_base + infoGuest, &info, copy_sz);
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelQueryMemoryProtection(GuestContext *ctx) {
    uint64_t addr = ctx->rdi;
    uint64_t startGuest = ctx->rsi;
    uint64_t endGuest = ctx->rdx;
    uint64_t protGuest = ctx->rcx;

    void *start = NULL;
    void *end = NULL;
    int prot = 0;
    int rc = sceKernelQueryMemoryProtection(ctx, (const void *)addr, &start, &end, &prot);
    if (rc == 0) {
        if (startGuest) *(uint64_t *)(ctx->mem_base + startGuest) = (uint64_t)start;
        if (endGuest) *(uint64_t *)(ctx->mem_base + endGuest) = (uint64_t)end;
        if (protGuest) *(int *)(ctx->mem_base + protGuest) = prot;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMemoryPoolReserve(GuestContext *ctx) {
    // PS4 ABI: sceKernelMemoryPoolReserve(void* addr_in, u64 len, u64 alignment, s32 flags, void** addr_out)
    // RDI: addr_in, RSI: len, RDX: alignment, RCX: flags, R8: addr_out
    void *addr_in = (void *)ctx->rdi;
    size_t len = (size_t)ctx->rsi;
    size_t alignment = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;
    uint64_t addrOutGuest = ctx->r8;

    // Handle compatibility with old 3-arg stub (addr_out in RDI) if R8 == 0
    if (addrOutGuest == 0 && ctx->rdi != 0 && ctx->rsi != 0 && ctx->rdx != 0 && ctx->rcx == 0) {
        addrOutGuest = ctx->rdi;
        addr_in = NULL;
        flags = (int)ctx->rdx;
    }

    void *addrOut = NULL;
    int rc = sceKernelMemoryPoolReserve(ctx, addr_in, len, alignment, flags, &addrOut);
    if (rc == 0 && addrOutGuest != 0 && addrOutGuest < ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + addrOutGuest) = (uint64_t)addrOut;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMemoryPoolExpand(GuestContext *ctx) {
    // PS4 ABI: sceKernelMemoryPoolExpand(u64 searchStart, u64 searchEnd, u64 len, u64 alignment, u64* physAddrOut)
    // RDI: searchStart, RSI: searchEnd, RDX: len, RCX: alignment, R8: physAddrOut
    off_t searchStart = (off_t)ctx->rdi;
    off_t searchEnd = (off_t)ctx->rsi;
    size_t len = (size_t)ctx->rdx;
    size_t alignment = (size_t)ctx->rcx;
    uint64_t physAddrOutGuest = ctx->r8;

    off_t physAddr = 0;
    int rc = sceKernelMemoryPoolExpand(ctx, searchStart, searchEnd, len, alignment, &physAddr);
    if (rc == 0 && physAddrOutGuest != 0 && physAddrOutGuest < ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + physAddrOutGuest) = (uint64_t)physAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMemoryPoolCommit(GuestContext *ctx) {
    // PS4 ABI: sceKernelMemoryPoolCommit(void* addr, u64 len, s32 type, s32 prot, s32 flags)
    // RDI: addr, RSI: len, RDX: type, RCX: prot, R8: flags
    void *addr = (void *)ctx->rdi;
    size_t len = (size_t)ctx->rsi;
    int type = (int)ctx->rdx;
    int prot = (int)ctx->rcx;
    int flags = (int)ctx->r8;
    int rc = sceKernelMemoryPoolCommit(ctx, addr, len, type, prot, flags);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMemoryPoolDecommit(GuestContext *ctx) {
    // PS4 ABI: sceKernelMemoryPoolDecommit(void* addr, u64 len, s32 flags)
    // RDI: addr, RSI: len, RDX: flags
    void *addr = (void *)ctx->rdi;
    size_t len = (size_t)ctx->rsi;
    int flags = (int)ctx->rdx;
    int rc = sceKernelMemoryPoolDecommit(ctx, addr, len, flags);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}
