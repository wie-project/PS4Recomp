#include "ps4_direct_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

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

int sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t length, size_t alignment, int type, off_t *physAddrOut) {
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

    uint64_t vaddr = recomp_vm_alloc(ctx, aligned_len);
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

void shim_sceKernelGetDirectMemorySize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKernelGetDirectMemorySize();
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
