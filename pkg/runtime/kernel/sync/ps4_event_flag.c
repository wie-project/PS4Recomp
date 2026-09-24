#include "ps4_event_flag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#define MAX_EVENT_FLAGS 1024
#define EVF_MAGIC 0x45564631 // 'EVF1'

typedef struct {
    uint32_t magic;
    int in_use;
    uint32_t attr;
    uint64_t pattern;
    char name[32];
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} RecompInternalEventFlag;

static RecompInternalEventFlag g_evf_table[MAX_EVENT_FLAGS];
static pthread_mutex_t g_evf_table_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_evf_inited = 0;

void ps4_event_flag_init(void) {
    pthread_mutex_lock(&g_evf_table_lock);
    if (!g_evf_inited) {
        memset(g_evf_table, 0, sizeof(g_evf_table));
        g_evf_inited = 1;
    }
    pthread_mutex_unlock(&g_evf_table_lock);
}

void ps4_event_flag_destroy(void) {
    pthread_mutex_lock(&g_evf_table_lock);
    for (int i = 1; i < MAX_EVENT_FLAGS; i++) {
        if (g_evf_table[i].in_use) {
            pthread_mutex_destroy(&g_evf_table[i].mutex);
            pthread_cond_destroy(&g_evf_table[i].cond);
            g_evf_table[i].in_use = 0;
            g_evf_table[i].magic = 0;
        }
    }
    g_evf_inited = 0;
    pthread_mutex_unlock(&g_evf_table_lock);
}

static int alloc_evf_slot(void) {
    pthread_mutex_lock(&g_evf_table_lock);
    for (int i = 1; i < MAX_EVENT_FLAGS; i++) {
        if (!g_evf_table[i].in_use) {
            g_evf_table[i].in_use = 1;
            pthread_mutex_unlock(&g_evf_table_lock);
            return i;
        }
    }
    pthread_mutex_unlock(&g_evf_table_lock);
    return -1;
}

static RecompInternalEventFlag *get_evf(OrbisKernelEventFlag id) {
    if (id <= 0 || id >= MAX_EVENT_FLAGS) {
        return NULL;
    }
    RecompInternalEventFlag *f = &g_evf_table[id];
    if (!f->in_use || f->magic != EVF_MAGIC) {
        return NULL;
    }
    return f;
}

static inline int check_evf_condition(uint64_t currPattern, uint64_t bitPattern, uint32_t waitMode) {
    if (waitMode & SCE_KERNEL_EVF_WAITMODE_OR) {
        return (currPattern & bitPattern) != 0;
    }
    // Default is AND
    return (currPattern & bitPattern) == bitPattern;
}

int32_t sceKernelCreateEventFlag(OrbisKernelEventFlag *idOut, const char *name, uint32_t attr, uint64_t initPattern, const OrbisKernelEventFlagOptParam *opt) {
    (void)opt;
    if (!idOut) return (int32_t)SCE_KERNEL_ERROR_EINVAL;
    ps4_event_flag_init();

    int slot = alloc_evf_slot();
    if (slot < 0) {
        return (int32_t)0x80020018; // SCE_KERNEL_ERROR_ENFILE
    }

    RecompInternalEventFlag *f = &g_evf_table[slot];
    f->magic = EVF_MAGIC;
    f->attr = attr;
    f->pattern = initPattern;
    if (name) {
        strncpy(f->name, name, sizeof(f->name) - 1);
        f->name[sizeof(f->name) - 1] = '\0';
    } else {
        f->name[0] = '\0';
    }
    pthread_mutex_init(&f->mutex, NULL);
    pthread_cond_init(&f->cond, NULL);

    *idOut = (OrbisKernelEventFlag)slot;
    return 0;
}

int32_t sceKernelDeleteEventFlag(OrbisKernelEventFlag id) {
    RecompInternalEventFlag *f = get_evf(id);
    if (!f) return (int32_t)SCE_KERNEL_ERROR_EINVAL;

    pthread_mutex_lock(&f->mutex);
    f->magic = 0;
    pthread_cond_broadcast(&f->cond);
    pthread_mutex_unlock(&f->mutex);

    pthread_mutex_lock(&g_evf_table_lock);
    pthread_mutex_destroy(&f->mutex);
    pthread_cond_destroy(&f->cond);
    f->in_use = 0;
    pthread_mutex_unlock(&g_evf_table_lock);

    return 0;
}

int32_t sceKernelSetEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern) {
    RecompInternalEventFlag *f = get_evf(id);
    if (!f) return (int32_t)SCE_KERNEL_ERROR_EINVAL;

    pthread_mutex_lock(&f->mutex);
    f->pattern |= bitPattern;
    pthread_cond_broadcast(&f->cond);
    pthread_mutex_unlock(&f->mutex);

    return 0;
}

int32_t sceKernelClearEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern) {
    RecompInternalEventFlag *f = get_evf(id);
    if (!f) return (int32_t)SCE_KERNEL_ERROR_EINVAL;

    pthread_mutex_lock(&f->mutex);
    f->pattern &= bitPattern;
    pthread_mutex_unlock(&f->mutex);

    return 0;
}

int32_t sceKernelPollEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern, uint32_t waitMode, uint64_t *resultPattern) {
    RecompInternalEventFlag *f = get_evf(id);
    if (!f) return (int32_t)SCE_KERNEL_ERROR_EINVAL;

    pthread_mutex_lock(&f->mutex);
    if (check_evf_condition(f->pattern, bitPattern, waitMode)) {
        if (resultPattern) {
            *resultPattern = f->pattern;
        }
        if (waitMode & SCE_KERNEL_EVF_WAITMODE_CLEAR_ALL) {
            f->pattern = 0;
        } else if (waitMode & SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT) {
            f->pattern &= ~bitPattern;
        }
        pthread_mutex_unlock(&f->mutex);
        return 0;
    }
    pthread_mutex_unlock(&f->mutex);
    return (int32_t)SCE_KERNEL_ERROR_EBUSY;
}

int32_t sceKernelWaitEventFlag(OrbisKernelEventFlag id, uint64_t bitPattern, uint32_t waitMode, uint64_t *resultPattern, OrbisKernelUseconds *timeout) {
    RecompInternalEventFlag *f = get_evf(id);
    if (!f) return (int32_t)SCE_KERNEL_ERROR_EINVAL;

    pthread_mutex_lock(&f->mutex);

    struct timespec ts;
    int has_timeout = 0;
    if (timeout) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        uint64_t usec = *timeout;
        ts.tv_sec = now.tv_sec + (time_t)(usec / 1000000ULL);
        ts.tv_nsec = now.tv_nsec + (long)((usec % 1000000ULL) * 1000ULL);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        has_timeout = 1;
    }

    while (f->magic == EVF_MAGIC && !check_evf_condition(f->pattern, bitPattern, waitMode)) {
        if (has_timeout) {
            int ret = pthread_cond_timedwait(&f->cond, &f->mutex, &ts);
            if (ret == ETIMEDOUT) {
                if (resultPattern) {
                    *resultPattern = f->pattern;
                }
                pthread_mutex_unlock(&f->mutex);
                return (int32_t)SCE_KERNEL_ERROR_ETIMEDOUT;
            }
        } else {
            pthread_cond_wait(&f->cond, &f->mutex);
        }
    }

    if (f->magic != EVF_MAGIC) {
        pthread_mutex_unlock(&f->mutex);
        return (int32_t)SCE_KERNEL_ERROR_EINVAL;
    }

    if (resultPattern) {
        *resultPattern = f->pattern;
    }
    if (waitMode & SCE_KERNEL_EVF_WAITMODE_CLEAR_ALL) {
        f->pattern = 0;
    } else if (waitMode & SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT) {
        f->pattern &= ~bitPattern;
    }

    pthread_mutex_unlock(&f->mutex);
    return 0;
}

// Guest shims
void shim_sceKernelCreateEventFlag(GuestContext *ctx) {
    uint64_t idOutGuest = ctx->rdi;
    uint64_t nameGuest = ctx->rsi;
    uint32_t attr = (uint32_t)ctx->rdx;
    uint64_t initPattern = ctx->rcx;
    uint64_t optGuest = ctx->r8;

    const char *name = nameGuest ? (const char *)(ctx->mem_base + nameGuest) : NULL;
    const OrbisKernelEventFlagOptParam *opt = optGuest ? (const OrbisKernelEventFlagOptParam *)(ctx->mem_base + optGuest) : NULL;

    OrbisKernelEventFlag id = 0;
    int32_t rc = sceKernelCreateEventFlag(&id, name, attr, initPattern, opt);
    if (rc == 0 && idOutGuest) {
        *(int32_t *)(ctx->mem_base + idOutGuest) = id;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelDeleteEventFlag(GuestContext *ctx) {
    OrbisKernelEventFlag id = (OrbisKernelEventFlag)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceKernelDeleteEventFlag(id);
    SHIM_RETURN();
}

void shim_sceKernelSetEventFlag(GuestContext *ctx) {
    OrbisKernelEventFlag id = (OrbisKernelEventFlag)ctx->rdi;
    uint64_t bitPattern = ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceKernelSetEventFlag(id, bitPattern);
    SHIM_RETURN();
}

void shim_sceKernelClearEventFlag(GuestContext *ctx) {
    OrbisKernelEventFlag id = (OrbisKernelEventFlag)ctx->rdi;
    uint64_t bitPattern = ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceKernelClearEventFlag(id, bitPattern);
    SHIM_RETURN();
}

void shim_sceKernelPollEventFlag(GuestContext *ctx) {
    OrbisKernelEventFlag id = (OrbisKernelEventFlag)ctx->rdi;
    uint64_t bitPattern = ctx->rsi;
    uint32_t waitMode = (uint32_t)ctx->rdx;
    uint64_t resGuest = ctx->rcx;

    uint64_t resultPattern = 0;
    int32_t rc = sceKernelPollEventFlag(id, bitPattern, waitMode, &resultPattern);
    if (resGuest) {
        *(uint64_t *)(ctx->mem_base + resGuest) = resultPattern;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelWaitEventFlag(GuestContext *ctx) {
    OrbisKernelEventFlag id = (OrbisKernelEventFlag)ctx->rdi;
    uint64_t bitPattern = ctx->rsi;
    uint32_t waitMode = (uint32_t)ctx->rdx;
    uint64_t resGuest = ctx->rcx;
    uint64_t timeoutGuest = ctx->r8;

    uint64_t resultPattern = 0;
    OrbisKernelUseconds *pTimeout = NULL;
    OrbisKernelUseconds timeoutVal = 0;
    if (timeoutGuest) {
        timeoutVal = *(OrbisKernelUseconds *)(ctx->mem_base + timeoutGuest);
        pTimeout = &timeoutVal;
    }

    int32_t rc = sceKernelWaitEventFlag(id, bitPattern, waitMode, &resultPattern, pTimeout);
    if (resGuest) {
        *(uint64_t *)(ctx->mem_base + resGuest) = resultPattern;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}
