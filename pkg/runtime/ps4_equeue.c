#include "ps4_equeue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

#define MAX_EQUEUES 128
#define EQUEUE_CAPACITY 64

typedef struct {
    int32_t id;
    int in_use;
    int is_deleted;
    char name[64];
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    OrbisKernelEvent events[EQUEUE_CAPACITY];
    int head;
    int tail;
    int count;
} KernelEqueueInternal;

static KernelEqueueInternal g_equeues[MAX_EQUEUES];
static pthread_mutex_t g_eq_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_next_eq_id = 1;

static KernelEqueueInternal *find_equeue_locked(OrbisKernelEqueue id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < MAX_EQUEUES; i++) {
        if (g_equeues[i].in_use && g_equeues[i].id == id) {
            return &g_equeues[i];
        }
    }
    return NULL;
}

int sceKernelCreateEqueue(OrbisKernelEqueue *eqOut, const char *name) {
    if (!eqOut) return -EINVAL;

    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = NULL;
    for (int i = 0; i < MAX_EQUEUES; i++) {
        if (!g_equeues[i].in_use) {
            eq = &g_equeues[i];
            break;
        }
    }

    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return -ENOMEM;
    }

    eq->id = g_next_eq_id++;
    eq->in_use = 1;
    eq->is_deleted = 0;
    eq->head = 0;
    eq->tail = 0;
    eq->count = 0;
    if (name) {
        strncpy(eq->name, name, sizeof(eq->name) - 1);
        eq->name[sizeof(eq->name) - 1] = '\0';
    } else {
        eq->name[0] = '\0';
    }

    pthread_mutex_init(&eq->mutex, NULL);
    pthread_cond_init(&eq->cond, NULL);

    *eqOut = eq->id;
    pthread_mutex_unlock(&g_eq_table_mutex);
    return 0;
}

int sceKernelDeleteEqueue(OrbisKernelEqueue id) {
    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&eq->mutex);
    eq->is_deleted = 1;
    pthread_cond_broadcast(&eq->cond);
    pthread_mutex_unlock(&eq->mutex);

    pthread_mutex_destroy(&eq->mutex);
    pthread_cond_destroy(&eq->cond);
    eq->in_use = 0;

    pthread_mutex_unlock(&g_eq_table_mutex);
    return 0;
}

int ps4_equeue_post_event(OrbisKernelEqueue id, uint64_t ident, int16_t filter, int64_t data, void *udata) {
    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&eq->mutex);
    pthread_mutex_unlock(&g_eq_table_mutex);

    if (eq->is_deleted) {
        pthread_mutex_unlock(&eq->mutex);
        return -EINVAL;
    }

    if (eq->count < EQUEUE_CAPACITY) {
        eq->events[eq->tail].ident = ident;
        eq->events[eq->tail].filter = filter;
        eq->events[eq->tail].flags = 0;
        eq->events[eq->tail].fflags = 0;
        eq->events[eq->tail].data = data;
        eq->events[eq->tail].udata = udata;
        eq->tail = (eq->tail + 1) % EQUEUE_CAPACITY;
        eq->count++;
        pthread_cond_signal(&eq->cond);
    }

    pthread_mutex_unlock(&eq->mutex);
    return 0;
}

int sceKernelWaitEqueue(OrbisKernelEqueue id, OrbisKernelEvent *eventsOut, int numEvents, int *outCount, const SceKernelTimeval *timeout) {
    if (numEvents <= 0) return -EINVAL;

    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&eq->mutex);
    pthread_mutex_unlock(&g_eq_table_mutex);

    struct timespec deadline = {0};
    if (timeout) {
        struct timeval now;
        gettimeofday(&now, NULL);
        deadline.tv_sec = now.tv_sec + timeout->tv_sec;
        deadline.tv_nsec = (now.tv_usec + timeout->tv_usec) * 1000;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec += deadline.tv_nsec / 1000000000L;
            deadline.tv_nsec %= 1000000000L;
        }
    }

    while (eq->count == 0 && !eq->is_deleted) {
        if (timeout) {
            int wait_rc = pthread_cond_timedwait(&eq->cond, &eq->mutex, &deadline);
            if (wait_rc == ETIMEDOUT) {
                break;
            }
        } else {
            pthread_cond_wait(&eq->cond, &eq->mutex);
        }
    }

    if (eq->is_deleted) {
        pthread_mutex_unlock(&eq->mutex);
        return -EINVAL;
    }

    int n = 0;
    while (eq->count > 0 && n < numEvents) {
        if (eventsOut) {
            eventsOut[n] = eq->events[eq->head];
        }
        eq->head = (eq->head + 1) % EQUEUE_CAPACITY;
        eq->count--;
        n++;
    }

    if (outCount) {
        *outCount = n;
    }

    pthread_mutex_unlock(&eq->mutex);
    return 0;
}

// Shims for guest execution
void shim_sceKernelCreateEqueue(GuestContext *ctx) {
    uint64_t eqOutGuest = ctx->rdi;
    uint64_t nameGuest = ctx->rsi;
    const char *name = nameGuest ? (const char *)(ctx->mem_base + nameGuest) : NULL;

    OrbisKernelEqueue eq = 0;
    int rc = sceKernelCreateEqueue(&eq, name);
    if (rc == 0 && eqOutGuest != 0) {
        *(int32_t *)(ctx->mem_base + eqOutGuest) = eq;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelDeleteEqueue(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceKernelDeleteEqueue(eq);
    SHIM_RETURN();
}

void shim_sceKernelWaitEqueue(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    uint64_t eventsOutGuest = ctx->rsi;
    int numEvents = (int)ctx->rdx;
    uint64_t outCountGuest = ctx->rcx;
    uint64_t timeoutGuest = ctx->r8;

    OrbisKernelEvent *eventsOut = eventsOutGuest ? (OrbisKernelEvent *)(ctx->mem_base + eventsOutGuest) : NULL;
    int *outCount = outCountGuest ? (int *)(ctx->mem_base + outCountGuest) : NULL;
    const SceKernelTimeval *timeout = timeoutGuest ? (const SceKernelTimeval *)(ctx->mem_base + timeoutGuest) : NULL;

    int rc = sceKernelWaitEqueue(eq, eventsOut, numEvents, outCount, timeout);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}
