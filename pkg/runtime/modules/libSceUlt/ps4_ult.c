#include "ps4_ult.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ULT_MUTEXES 4096
#define MAX_ULT_POOLS   256

typedef struct UltMutexEntry {
    pthread_mutex_t mtx;
    bool in_use;
    uint64_t guest_addr;
} UltMutexEntry;

typedef struct UltPoolEntry {
    bool in_use;
    uint32_t num_resources;
    uint64_t guest_addr;
} UltPoolEntry;

static pthread_mutex_t g_ult_global_lock = PTHREAD_MUTEX_INITIALIZER;
static UltMutexEntry g_ult_mutexes[MAX_ULT_MUTEXES];
static UltPoolEntry g_ult_pools[MAX_ULT_POOLS];
static bool g_ult_initialized = false;

static inline void *guest_to_host(const GuestContext *ctx, uint64_t addr) {
    if (!addr) return NULL;
    if (ctx && ctx->mem_base && addr < ctx->mem_size) {
        return (void *)(ctx->mem_base + addr);
    }
    return (void *)addr;
}

int32_t sceUltInitialize(void *param) {
    (void)param;
    pthread_mutex_lock(&g_ult_global_lock);
    g_ult_initialized = true;
    pthread_mutex_unlock(&g_ult_global_lock);
    return ORBIS_ULT_OK;
}

int32_t sceUltFinalize(void) {
    pthread_mutex_lock(&g_ult_global_lock);
    g_ult_initialized = false;
    pthread_mutex_unlock(&g_ult_global_lock);
    return ORBIS_ULT_OK;
}

int32_t _sceUltMutexOptParamInitialize(SceUltMutexOptParam *opt) {
    if (!opt) {
        return ORBIS_ULT_ERROR_NULL_POINTER;
    }
    memset(opt, 0, sizeof(*opt));
    opt->size = (uint32_t)sizeof(*opt);
    return ORBIS_ULT_OK;
}

int32_t _sceUltMutexCreate(void *mutex, const char *name, const SceUltMutexOptParam *opt, void *workArea) {
    (void)name;
    (void)opt;
    (void)workArea;
    if (!mutex) {
        return ORBIS_ULT_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&g_ult_global_lock);
    int slot = -1;
    for (int i = 0; i < MAX_ULT_MUTEXES; i++) {
        if (!g_ult_mutexes[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_ult_global_lock);
        return ORBIS_ULT_ERROR_OUT_OF_MEMORY;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_ult_mutexes[slot].mtx, &attr);
    pthread_mutexattr_destroy(&attr);

    g_ult_mutexes[slot].in_use = true;
    g_ult_mutexes[slot].guest_addr = (uint64_t)(uintptr_t)mutex;

    // Store handle (1-based index) in caller's memory
    *(uint32_t *)mutex = (uint32_t)(slot + 1);

    pthread_mutex_unlock(&g_ult_global_lock);
    return ORBIS_ULT_OK;
}

static UltMutexEntry *find_mutex_entry(void *mutex) {
    if (!mutex) return NULL;
    uint32_t handle = *(uint32_t *)mutex;
    if (handle >= 1 && handle <= MAX_ULT_MUTEXES) {
        int idx = (int)handle - 1;
        if (g_ult_mutexes[idx].in_use) {
            return &g_ult_mutexes[idx];
        }
    }
    // Fallback: search by guest address
    uint64_t addr = (uint64_t)(uintptr_t)mutex;
    for (int i = 0; i < MAX_ULT_MUTEXES; i++) {
        if (g_ult_mutexes[i].in_use && g_ult_mutexes[i].guest_addr == addr) {
            return &g_ult_mutexes[i];
        }
    }
    return NULL;
}

int32_t sceUltMutexLock(void *mutex) {
    if (!mutex) {
        return ORBIS_ULT_ERROR_NULL_POINTER;
    }
    pthread_mutex_lock(&g_ult_global_lock);
    UltMutexEntry *entry = find_mutex_entry(mutex);
    pthread_mutex_unlock(&g_ult_global_lock);

    if (!entry) {
        return ORBIS_ULT_ERROR_INVALID_ARG;
    }
    pthread_mutex_lock(&entry->mtx);
    return ORBIS_ULT_OK;
}

int32_t sceUltMutexUnlock(void *mutex) {
    if (!mutex) {
        return ORBIS_ULT_ERROR_NULL_POINTER;
    }
    pthread_mutex_lock(&g_ult_global_lock);
    UltMutexEntry *entry = find_mutex_entry(mutex);
    pthread_mutex_unlock(&g_ult_global_lock);

    if (!entry) {
        return ORBIS_ULT_ERROR_INVALID_ARG;
    }
    pthread_mutex_unlock(&entry->mtx);
    return ORBIS_ULT_OK;
}

int32_t sceUltMutexDestroy(void *mutex) {
    if (!mutex) {
        return ORBIS_ULT_ERROR_NULL_POINTER;
    }
    pthread_mutex_lock(&g_ult_global_lock);
    UltMutexEntry *entry = find_mutex_entry(mutex);
    if (!entry) {
        pthread_mutex_unlock(&g_ult_global_lock);
        return ORBIS_ULT_ERROR_INVALID_ARG;
    }
    pthread_mutex_destroy(&entry->mtx);
    entry->in_use = false;
    entry->guest_addr = 0;
    *(uint32_t *)mutex = 0;
    pthread_mutex_unlock(&g_ult_global_lock);
    return ORBIS_ULT_OK;
}

size_t sceUltWaitingQueueResourcePoolGetWorkAreaSize(uint32_t numResources) {
    return (size_t)(numResources * 64 + 256);
}

int32_t _sceUltWaitingQueueResourcePoolCreate(void *pool, const char *name, uint32_t numResources, void *workArea, const void *opt) {
    (void)name;
    (void)workArea;
    (void)opt;
    if (!pool) {
        return ORBIS_ULT_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&g_ult_global_lock);
    int slot = -1;
    for (int i = 0; i < MAX_ULT_POOLS; i++) {
        if (!g_ult_pools[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_ult_global_lock);
        return ORBIS_ULT_ERROR_OUT_OF_MEMORY;
    }

    g_ult_pools[slot].in_use = true;
    g_ult_pools[slot].num_resources = numResources;
    g_ult_pools[slot].guest_addr = (uint64_t)(uintptr_t)pool;
    *(uint32_t *)pool = (uint32_t)(slot + 1);

    pthread_mutex_unlock(&g_ult_global_lock);
    return ORBIS_ULT_OK;
}

int32_t sceUltWaitingQueueResourcePoolDestroy(void *pool) {
    if (!pool) {
        return ORBIS_ULT_ERROR_NULL_POINTER;
    }
    pthread_mutex_lock(&g_ult_global_lock);
    uint32_t handle = *(uint32_t *)pool;
    if (handle >= 1 && handle <= MAX_ULT_POOLS) {
        int idx = (int)handle - 1;
        if (g_ult_pools[idx].in_use) {
            g_ult_pools[idx].in_use = false;
            g_ult_pools[idx].guest_addr = 0;
            *(uint32_t *)pool = 0;
            pthread_mutex_unlock(&g_ult_global_lock);
            return ORBIS_ULT_OK;
        }
    }
    pthread_mutex_unlock(&g_ult_global_lock);
    return ORBIS_ULT_ERROR_INVALID_ARG;
}

// Shims
void shim_sceUltInitialize(GuestContext *ctx) {
    void *param = guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceUltInitialize(param);
    SHIM_RETURN();
}

void shim_sceUltFinalize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUltFinalize();
    SHIM_RETURN();
}

void shim__sceUltMutexOptParamInitialize(GuestContext *ctx) {
    SceUltMutexOptParam *opt = (SceUltMutexOptParam *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)_sceUltMutexOptParamInitialize(opt);
    SHIM_RETURN();
}

void shim__sceUltMutexCreate(GuestContext *ctx) {
    void *mutex = guest_to_host(ctx, ctx->rdi);
    const char *name = (const char *)guest_to_host(ctx, ctx->rsi);
    const SceUltMutexOptParam *opt = (const SceUltMutexOptParam *)guest_to_host(ctx, ctx->rdx);
    void *workArea = guest_to_host(ctx, ctx->rcx);
    ctx->rax = (uint64_t)(int64_t)_sceUltMutexCreate(mutex, name, opt, workArea);
    SHIM_RETURN();
}

void shim_sceUltMutexLock(GuestContext *ctx) {
    void *mutex = guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceUltMutexLock(mutex);
    SHIM_RETURN();
}

void shim_sceUltMutexUnlock(GuestContext *ctx) {
    void *mutex = guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceUltMutexUnlock(mutex);
    SHIM_RETURN();
}

void shim_sceUltMutexDestroy(GuestContext *ctx) {
    void *mutex = guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceUltMutexDestroy(mutex);
    SHIM_RETURN();
}

void shim_sceUltWaitingQueueResourcePoolGetWorkAreaSize(GuestContext *ctx) {
    uint32_t numResources = (uint32_t)ctx->rdi;
    ctx->rax = (uint64_t)sceUltWaitingQueueResourcePoolGetWorkAreaSize(numResources);
    SHIM_RETURN();
}

void shim__sceUltWaitingQueueResourcePoolCreate(GuestContext *ctx) {
    void *pool = guest_to_host(ctx, ctx->rdi);
    const char *name = (const char *)guest_to_host(ctx, ctx->rsi);
    uint32_t numResources = (uint32_t)ctx->rdx;
    void *workArea = guest_to_host(ctx, ctx->rcx);
    const void *opt = guest_to_host(ctx, ctx->r8);
    ctx->rax = (uint64_t)(int64_t)_sceUltWaitingQueueResourcePoolCreate(pool, name, numResources, workArea, opt);
    SHIM_RETURN();
}

void shim_sceUltWaitingQueueResourcePoolDestroy(GuestContext *ctx) {
    void *pool = guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceUltWaitingQueueResourcePoolDestroy(pool);
    SHIM_RETURN();
}
