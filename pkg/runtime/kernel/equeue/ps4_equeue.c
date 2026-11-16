#include "ps4_equeue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

#define MAX_EQUEUES 128
#define EQUEUE_CAPACITY 64
#define MAX_TIMERS_PER_EQUEUE 16

typedef struct {
    int in_use;
    int is_hrtimer;
    int one_shot;
    uint64_t ident;
    uint64_t interval_ns;
    uint64_t next_fire_ns;
    void *udata;
} EqueueTimer;

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
    EqueueTimer timers[MAX_TIMERS_PER_EQUEUE];
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
    memset(eq->timers, 0, sizeof(eq->timers));
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
    memset(eq->timers, 0, sizeof(eq->timers));
    eq->in_use = 0;

    pthread_mutex_unlock(&g_eq_table_mutex);
    return 0;
}

void ps4_equeue_destroy(void) {
    pthread_mutex_lock(&g_eq_table_mutex);
    for (int i = 0; i < MAX_EQUEUES; i++) {
        if (g_equeues[i].in_use) {
            pthread_mutex_lock(&g_equeues[i].mutex);
            g_equeues[i].is_deleted = 1;
            pthread_cond_broadcast(&g_equeues[i].cond);
            pthread_mutex_unlock(&g_equeues[i].mutex);
            pthread_mutex_destroy(&g_equeues[i].mutex);
            pthread_cond_destroy(&g_equeues[i].cond);
            g_equeues[i].in_use = 0;
        }
    }
    pthread_mutex_unlock(&g_eq_table_mutex);
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

int ps4_equeue_exists(OrbisKernelEqueue id) {
    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    int ok = (eq && !eq->is_deleted) ? 1 : 0;
    pthread_mutex_unlock(&g_eq_table_mutex);
    return ok;
}

int ps4_equeue_trigger_event(OrbisKernelEqueue id, uint64_t ident, int16_t filter, int64_t data, void *udata) {
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

    int coalesced = 0;
    for (int i = 0; i < eq->count; i++) {
        int idx = (eq->head + i) % EQUEUE_CAPACITY;
        OrbisKernelEvent *ev = &eq->events[idx];
        if (ev->ident == ident && ev->filter == filter) {
            ev->data = data;
            ev->udata = udata;
            ev->fflags++;
            coalesced = 1;
            break;
        }
    }

    if (!coalesced && eq->count < EQUEUE_CAPACITY) {
        OrbisKernelEvent *ev = &eq->events[eq->tail];
        ev->ident = ident;
        ev->filter = filter;
        ev->flags = 0;
        ev->fflags = 1;
        ev->data = data;
        ev->udata = udata;
        eq->tail = (eq->tail + 1) % EQUEUE_CAPACITY;
        eq->count++;
    }
    // Broadcast: several threads may be blocked on the same queue.
    pthread_cond_broadcast(&eq->cond);

    pthread_mutex_unlock(&eq->mutex);
    return 0;
}

static void remove_events_locked(KernelEqueueInternal *eq, uint64_t ident, int16_t filter) {
    int write_idx = 0;
    int original_count = eq->count;
    for (int i = 0; i < original_count; i++) {
        int idx = (eq->head + i) % EQUEUE_CAPACITY;
        if (eq->events[idx].filter == filter && eq->events[idx].ident == ident) {
            continue;
        }
        if (write_idx != i) {
            int target_idx = (eq->head + write_idx) % EQUEUE_CAPACITY;
            eq->events[target_idx] = eq->events[idx];
        }
        write_idx++;
    }
    eq->count = write_idx;
    eq->tail = (eq->head + write_idx) % EQUEUE_CAPACITY;
}

int ps4_equeue_remove_events(OrbisKernelEqueue id, uint64_t ident, int16_t filter) {
    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return -EINVAL;
    }
    pthread_mutex_lock(&eq->mutex);
    pthread_mutex_unlock(&g_eq_table_mutex);
    remove_events_locked(eq, ident, filter);
    pthread_mutex_unlock(&eq->mutex);
    return 0;
}

static inline uint64_t get_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void check_timers_locked(KernelEqueueInternal *eq, uint64_t now_ns) {
    for (int i = 0; i < MAX_TIMERS_PER_EQUEUE; i++) {
        EqueueTimer *t = &eq->timers[i];
        if (!t->in_use) continue;
        if (now_ns >= t->next_fire_ns) {
            int16_t filter = t->is_hrtimer ? ORBIS_KERNEL_EVFILT_HRTIMER : ORBIS_KERNEL_EVFILT_TIMER;
            if (eq->count < EQUEUE_CAPACITY) {
                OrbisKernelEvent *ev = &eq->events[eq->tail];
                ev->ident = t->ident;
                ev->filter = filter;
                ev->flags = 0;
                ev->fflags = 0;
                ev->data = 1;
                ev->udata = t->udata;
                eq->tail = (eq->tail + 1) % EQUEUE_CAPACITY;
                eq->count++;
            }
            if (t->one_shot) {
                t->in_use = 0;
            } else {
                if (t->interval_ns > 0) {
                    t->next_fire_ns += t->interval_ns;
                    if (t->next_fire_ns <= now_ns) {
                        t->next_fire_ns = now_ns + t->interval_ns;
                    }
                } else {
                    t->in_use = 0;
                }
            }
        }
    }
}

static uint64_t get_earliest_timer_deadline_locked(KernelEqueueInternal *eq) {
    uint64_t earliest = 0;
    for (int i = 0; i < MAX_TIMERS_PER_EQUEUE; i++) {
        EqueueTimer *t = &eq->timers[i];
        if (!t->in_use) continue;
        if (earliest == 0 || t->next_fire_ns < earliest) {
            earliest = t->next_fire_ns;
        }
    }
    return earliest;
}

int sceKernelWaitEqueue(OrbisKernelEqueue id, OrbisKernelEvent *eventsOut, int numEvents, int *outCount, const uint32_t *timeout) {
    if (outCount) {
        *outCount = 0;
    }
    if (numEvents <= 0) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }
    if (!eventsOut) {
        return ORBIS_KERNEL_ERROR_EFAULT;
    }

    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }

    pthread_mutex_lock(&eq->mutex);
    pthread_mutex_unlock(&g_eq_table_mutex);

    if (eq->is_deleted) {
        pthread_mutex_unlock(&eq->mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }

    uint64_t now_ns = get_now_ns();
    check_timers_locked(eq, now_ns);

    if (timeout && *timeout == 0) {
        // Poll mode: retrieve only events already present
        int n = 0;
        while (eq->count > 0 && n < numEvents) {
            eventsOut[n] = eq->events[eq->head];
            eq->head = (eq->head + 1) % EQUEUE_CAPACITY;
            eq->count--;
            n++;
        }
        if (outCount) {
            *outCount = n;
        }
        pthread_mutex_unlock(&eq->mutex);
        if (n == 0) {
            return ORBIS_KERNEL_ERROR_ETIMEDOUT;
        }
        return 0;
    }

    uint64_t user_deadline_ns = 0;
    int has_user_timeout = 0;
    if (timeout) {
        has_user_timeout = 1;
        user_deadline_ns = now_ns + ((uint64_t)*timeout) * 1000ULL;
    }

    while (eq->count == 0 && !eq->is_deleted) {
        now_ns = get_now_ns();
        check_timers_locked(eq, now_ns);
        if (eq->count > 0 || eq->is_deleted) {
            break;
        }

        uint64_t timer_deadline_ns = get_earliest_timer_deadline_locked(eq);
        uint64_t wait_until_ns = 0;

        if (has_user_timeout && timer_deadline_ns > 0) {
            wait_until_ns = (user_deadline_ns < timer_deadline_ns) ? user_deadline_ns : timer_deadline_ns;
        } else if (has_user_timeout) {
            wait_until_ns = user_deadline_ns;
        } else if (timer_deadline_ns > 0) {
            wait_until_ns = timer_deadline_ns;
        }

        if (wait_until_ns > 0) {
            if (now_ns >= wait_until_ns) {
                if (has_user_timeout && now_ns >= user_deadline_ns) {
                    break;
                }
                continue;
            }
            struct timespec ts;
            ts.tv_sec = (time_t)(wait_until_ns / 1000000000ULL);
            ts.tv_nsec = (long)(wait_until_ns % 1000000000ULL);
            pthread_cond_timedwait(&eq->cond, &eq->mutex, &ts);
        } else {
            pthread_cond_wait(&eq->cond, &eq->mutex);
        }

        now_ns = get_now_ns();
        check_timers_locked(eq, now_ns);
        if (has_user_timeout && now_ns >= user_deadline_ns && eq->count == 0) {
            break;
        }
    }

    if (eq->is_deleted) {
        pthread_mutex_unlock(&eq->mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }

    int n = 0;
    while (eq->count > 0 && n < numEvents) {
        eventsOut[n] = eq->events[eq->head];
        eq->head = (eq->head + 1) % EQUEUE_CAPACITY;
        eq->count--;
        n++;
    }

    if (outCount) {
        *outCount = n;
    }

    pthread_mutex_unlock(&eq->mutex);

    if (n == 0) {
        return ORBIS_KERNEL_ERROR_ETIMEDOUT;
    }
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
    const uint32_t *timeout = timeoutGuest ? (const uint32_t *)(ctx->mem_base + timeoutGuest) : NULL;

    int rc = sceKernelWaitEqueue(eq, eventsOut, numEvents, outCount, timeout);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

int sceKernelDeleteUserEvent(OrbisKernelEqueue id, int ident) {
    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return 0x80020009; // ORBIS_KERNEL_ERROR_EBADF
    }

    pthread_mutex_lock(&eq->mutex);
    pthread_mutex_unlock(&g_eq_table_mutex);

    int write_idx = 0;
    int original_count = eq->count;
    for (int i = 0; i < original_count; i++) {
        int idx = (eq->head + i) % EQUEUE_CAPACITY;
        if (eq->events[idx].filter == ORBIS_KERNEL_EVFILT_USER && (int)eq->events[idx].ident == ident) {
            continue;
        }
        if (write_idx != i) {
            int target_idx = (eq->head + write_idx) % EQUEUE_CAPACITY;
            eq->events[target_idx] = eq->events[idx];
        }
        write_idx++;
    }
    eq->count = write_idx;
    eq->tail = (eq->head + write_idx) % EQUEUE_CAPACITY;

    pthread_mutex_unlock(&eq->mutex);
    return 0;
}

uint64_t sceKernelGetEventId(const OrbisKernelEvent *ev) {
    if (!ev) return 0;
    return ev->ident;
}

int sceKernelGetEventFilter(const OrbisKernelEvent *ev) {
    if (!ev) return 0;
    return (int)ev->filter;
}

void shim_sceKernelDeleteUserEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int id = (int)ctx->rsi;
    int rc = sceKernelDeleteUserEvent(eq, id);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelGetEventId(GuestContext *ctx) {
    uint64_t ev_addr = ctx->rdi;
    if (!ev_addr || !ctx->mem_base) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    const OrbisKernelEvent *ev = (const OrbisKernelEvent *)(ctx->mem_base + ev_addr);
    ctx->rax = sceKernelGetEventId(ev);
    SHIM_RETURN();
}

void shim_sceKernelGetEventFilter(GuestContext *ctx) {
    uint64_t ev_addr = ctx->rdi;
    if (!ev_addr || !ctx->mem_base) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    const OrbisKernelEvent *ev = (const OrbisKernelEvent *)(ctx->mem_base + ev_addr);
    ctx->rax = (uint64_t)(int64_t)sceKernelGetEventFilter(ev);
    SHIM_RETURN();
}

int sceKernelAddUserEvent(OrbisKernelEqueue id, int ident) {
    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }
    pthread_mutex_unlock(&g_eq_table_mutex);
    return 0;
}

int sceKernelAddUserEventEdge(OrbisKernelEqueue id, int ident) {
    return sceKernelAddUserEvent(id, ident);
}

int sceKernelTriggerUserEvent(OrbisKernelEqueue id, int ident, void *udata) {
    return ps4_equeue_post_event(id, (uint64_t)ident, ORBIS_KERNEL_EVFILT_USER, 0, udata);
}

void *sceKernelGetEventUserData(const OrbisKernelEvent *ev) {
    return ev ? ev->udata : NULL;
}

int64_t sceKernelGetEventData(const OrbisKernelEvent *ev) {
    return ev ? ev->data : 0;
}

void shim_sceKernelAddUserEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int id = (int)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceKernelAddUserEvent(eq, id);
    SHIM_RETURN();
}

void shim_sceKernelAddUserEventEdge(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int id = (int)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceKernelAddUserEventEdge(eq, id);
    SHIM_RETURN();
}

void shim_sceKernelTriggerUserEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int id = (int)ctx->rsi;
    // udata is an opaque guest value that is copied back verbatim into the guest-visible
    // OrbisKernelEvent by sceKernelWaitEqueue; it must stay a guest address (not host pointer).
    void *udata = (void *)(uintptr_t)ctx->rdx;
    ctx->rax = (uint64_t)(int64_t)sceKernelTriggerUserEvent(eq, id, udata);
    SHIM_RETURN();
}

void shim_sceKernelGetEventUserData(GuestContext *ctx) {
    uint64_t ev_addr = ctx->rdi;
    if (!ev_addr || !ctx->mem_base) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    const OrbisKernelEvent *ev = (const OrbisKernelEvent *)(ctx->mem_base + ev_addr);
    ctx->rax = (uint64_t)(uintptr_t)sceKernelGetEventUserData(ev);
    SHIM_RETURN();
}

void shim_sceKernelGetEventData(GuestContext *ctx) {
    uint64_t ev_addr = ctx->rdi;
    if (!ev_addr || !ctx->mem_base) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    const OrbisKernelEvent *ev = (const OrbisKernelEvent *)(ctx->mem_base + ev_addr);
    ctx->rax = (uint64_t)sceKernelGetEventData(ev);
    SHIM_RETURN();
}

int sceKernelAddTimerEvent(OrbisKernelEqueue id, int ident, uint64_t usec, void *udata) {
    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }
    pthread_mutex_lock(&eq->mutex);
    pthread_mutex_unlock(&g_eq_table_mutex);

    if (eq->is_deleted) {
        pthread_mutex_unlock(&eq->mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }

    uint64_t now_ns = get_now_ns();
    uint64_t interval_ns = usec * 1000ULL;

    EqueueTimer *target = NULL;
    for (int i = 0; i < MAX_TIMERS_PER_EQUEUE; i++) {
        if (eq->timers[i].in_use && eq->timers[i].ident == (uint64_t)ident && !eq->timers[i].is_hrtimer) {
            target = &eq->timers[i];
            break;
        }
    }

    if (!target) {
        for (int i = 0; i < MAX_TIMERS_PER_EQUEUE; i++) {
            if (!eq->timers[i].in_use) {
                target = &eq->timers[i];
                break;
            }
        }
    }

    if (!target) {
        pthread_mutex_unlock(&eq->mutex);
        return ORBIS_KERNEL_ERROR_ENOMEM;
    }

    target->in_use = 1;
    target->is_hrtimer = 0;
    target->one_shot = 0;
    target->ident = (uint64_t)ident;
    target->interval_ns = interval_ns;
    target->next_fire_ns = now_ns + interval_ns;
    target->udata = udata;

    pthread_cond_broadcast(&eq->cond);
    pthread_mutex_unlock(&eq->mutex);
    return 0;
}

int sceKernelDeleteTimerEvent(OrbisKernelEqueue id, int ident) {
    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }
    pthread_mutex_lock(&eq->mutex);
    pthread_mutex_unlock(&g_eq_table_mutex);

    int found = 0;
    for (int i = 0; i < MAX_TIMERS_PER_EQUEUE; i++) {
        if (eq->timers[i].in_use && eq->timers[i].ident == (uint64_t)ident && !eq->timers[i].is_hrtimer) {
            eq->timers[i].in_use = 0;
            found = 1;
            break;
        }
    }

    remove_events_locked(eq, (uint64_t)ident, ORBIS_KERNEL_EVFILT_TIMER);

    pthread_mutex_unlock(&eq->mutex);
    return found ? 0 : ORBIS_KERNEL_ERROR_ENOENT;
}

int sceKernelAddHRTimerEvent(OrbisKernelEqueue id, int ident, const SceKernelTimespec *ts, void *udata) {
    if (!ts) {
        return ORBIS_KERNEL_ERROR_EFAULT;
    }
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
        return ORBIS_KERNEL_ERROR_EINVAL;
    }

    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }
    pthread_mutex_lock(&eq->mutex);
    pthread_mutex_unlock(&g_eq_table_mutex);

    if (eq->is_deleted) {
        pthread_mutex_unlock(&eq->mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }

    uint64_t now_ns = get_now_ns();
    uint64_t interval_ns = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;

    EqueueTimer *target = NULL;
    for (int i = 0; i < MAX_TIMERS_PER_EQUEUE; i++) {
        if (eq->timers[i].in_use && eq->timers[i].ident == (uint64_t)ident && eq->timers[i].is_hrtimer) {
            target = &eq->timers[i];
            break;
        }
    }

    if (!target) {
        for (int i = 0; i < MAX_TIMERS_PER_EQUEUE; i++) {
            if (!eq->timers[i].in_use) {
                target = &eq->timers[i];
                break;
            }
        }
    }

    if (!target) {
        pthread_mutex_unlock(&eq->mutex);
        return ORBIS_KERNEL_ERROR_ENOMEM;
    }

    target->in_use = 1;
    target->is_hrtimer = 1;
    target->one_shot = 1;
    target->ident = (uint64_t)ident;
    target->interval_ns = interval_ns;
    target->next_fire_ns = now_ns + interval_ns;
    target->udata = udata;

    pthread_cond_broadcast(&eq->cond);
    pthread_mutex_unlock(&eq->mutex);
    return 0;
}

int sceKernelDeleteHRTimerEvent(OrbisKernelEqueue id, int ident) {
    pthread_mutex_lock(&g_eq_table_mutex);
    KernelEqueueInternal *eq = find_equeue_locked(id);
    if (!eq) {
        pthread_mutex_unlock(&g_eq_table_mutex);
        return ORBIS_KERNEL_ERROR_EBADF;
    }
    pthread_mutex_lock(&eq->mutex);
    pthread_mutex_unlock(&g_eq_table_mutex);

    int found = 0;
    for (int i = 0; i < MAX_TIMERS_PER_EQUEUE; i++) {
        if (eq->timers[i].in_use && eq->timers[i].ident == (uint64_t)ident && eq->timers[i].is_hrtimer) {
            eq->timers[i].in_use = 0;
            found = 1;
            break;
        }
    }

    remove_events_locked(eq, (uint64_t)ident, ORBIS_KERNEL_EVFILT_HRTIMER);

    pthread_mutex_unlock(&eq->mutex);
    return found ? 0 : ORBIS_KERNEL_ERROR_ENOENT;
}

void shim_sceKernelAddTimerEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int id = (int)ctx->rsi;
    uint64_t usec = ctx->rdx;
    void *udata = (void *)(uintptr_t)ctx->rcx;
    int rc = sceKernelAddTimerEvent(eq, id, usec, udata);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelDeleteTimerEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int id = (int)ctx->rsi;
    int rc = sceKernelDeleteTimerEvent(eq, id);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelAddHRTimerEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int id = (int)ctx->rsi;
    uint64_t tsGuest = ctx->rdx;
    void *udata = (void *)(uintptr_t)ctx->rcx;
    const SceKernelTimespec *ts = tsGuest ? (const SceKernelTimespec *)(ctx->mem_base + tsGuest) : NULL;
    int rc = sceKernelAddHRTimerEvent(eq, id, ts, udata);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelDeleteHRTimerEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int id = (int)ctx->rsi;
    int rc = sceKernelDeleteHRTimerEvent(eq, id);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}



