// Present queue and pacing policy (see ps4_present_queue.h).
#include "ps4_present_queue.h"

#include <string.h>
#include <strings.h>
#include <time.h>

int ps4_present_parse_mode(const char *s, ps4_present_mode *out) {
    *out = PS4_PRESENT_UNCAPPED;
    if (!s || !*s || !strcasecmp(s, "uncapped")) return 1;
    if (!strcasecmp(s, "vsync")) {
        *out = PS4_PRESENT_VSYNC;
        return 1;
    }
    if (!strcasecmp(s, "mailbox")) {
        *out = PS4_PRESENT_MAILBOX;
        return 1;
    }
    return 0;
}

const char *ps4_present_mode_name(ps4_present_mode m) {
    switch (m) {
    case PS4_PRESENT_VSYNC: return "vsync";
    case PS4_PRESENT_MAILBOX: return "mailbox";
    default: return "uncapped";
    }
}

double ps4_present_min_duration(ps4_present_mode mode, int guest_flip_rate, int guest_flip_mode, double display_hz) {
    if (mode != PS4_PRESENT_VSYNC || guest_flip_mode == PS4_FLIP_MODE_HSYNC) return 0.0;
    const int divider = (guest_flip_rate >= 0 && guest_flip_rate <= 2) ? guest_flip_rate + 1 : 1;
    const double guest = divider / 60.0;
    const double refresh = 1.0 / (display_hz > 0 ? display_hz : 60.0);
    // the display cannot show frames faster than the guest asks for: vsync alone paces them
    if (guest <= refresh * 1.05) return 0.0;
    return guest - refresh * 0.5;
}

void ps4_pq_init(ps4_present_queue *q, int n, bool mailbox) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->cv, NULL);
    q->n = n < 1 ? 1 : n > PS4_PRESENT_MAX_SLOTS ? PS4_PRESENT_MAX_SLOTS : n;
    q->mailbox = mailbox;
}

static int oldest_queued(const ps4_present_queue *q, bool skip_keep) {
    int best = -1;
    for (int i = 0; i < q->n; i++) {
        if (q->state[i] != PS4_PQ_QUEUED || (skip_keep && q->keep[i])) continue;
        if (best < 0 || q->order[i] < q->order[best]) best = i;
    }
    return best;
}

int ps4_pq_try_acquire_locked(ps4_present_queue *q, int *superseded) {
    if (superseded) *superseded = 0;
    if (!q->mailbox && q->credits <= 0) return -1;
    for (int i = 0; i < q->n; i++) {
        if (q->state[i] == PS4_PQ_FREE) {
            q->state[i] = PS4_PQ_RENDERING;
            if (!q->mailbox) q->credits--;
            return i;
        }
    }
    if (!q->mailbox) return -1;
    const int i = oldest_queued(q, true);
    if (i < 0) return -1;
    q->state[i] = PS4_PQ_RENDERING;
    q->superseded++;
    if (superseded) *superseded = 1;
    return i;
}

bool ps4_pq_submit_locked(ps4_present_queue *q, int slot, bool keep) {
    if (slot < 0 || slot >= q->n || q->state[slot] != PS4_PQ_RENDERING) return false;
    q->state[slot] = PS4_PQ_QUEUED;
    q->keep[slot] = keep;
    q->order[slot] = ++q->next_order;
    return true;
}

bool ps4_pq_cancel_locked(ps4_present_queue *q, int slot) {
    if (slot < 0 || slot >= q->n || q->state[slot] != PS4_PQ_RENDERING) return false;
    q->state[slot] = PS4_PQ_FREE;
    // the presenter still holds the drawable granted for this frame: the next frame may use it
    if (!q->mailbox && q->credits < 1) q->credits++;
    return true;
}

int ps4_pq_try_take_locked(ps4_present_queue *q) {
    const int i = oldest_queued(q, false);
    if (i >= 0) q->state[i] = PS4_PQ_PRESENTING;
    return i;
}

bool ps4_pq_release_locked(ps4_present_queue *q, int slot) {
    if (slot < 0 || slot >= q->n || q->state[slot] != PS4_PQ_PRESENTING) return false;
    q->state[slot] = PS4_PQ_FREE;
    q->keep[slot] = false;
    return true;
}

void ps4_pq_grant_locked(ps4_present_queue *q) {
    if (q->credits < 1) q->credits++;
}

bool ps4_pq_idle_locked(const ps4_present_queue *q) {
    for (int i = 0; i < q->n; i++)
        if (q->state[i] == PS4_PQ_QUEUED || q->state[i] == PS4_PQ_PRESENTING) return false;
    return true;
}

static double mono_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

// Waits on q->cv until `deadline` (monotonic seconds); false once it has passed.
static bool wait_until(ps4_present_queue *q, double deadline) {
    const double left = deadline - mono_s();
    if (left <= 0) return false;
    struct timespec rel;
    rel.tv_sec = (time_t)left;
    rel.tv_nsec = (long)((left - (double)rel.tv_sec) * 1e9);
    pthread_cond_timedwait_relative_np(&q->cv, &q->mu, &rel);
    return true;
}

int ps4_pq_acquire(ps4_present_queue *q, double timeout, int *superseded, double *waited_s) {
    const double t0 = mono_s(), deadline = t0 + timeout;
    pthread_mutex_lock(&q->mu);
    int slot = -1;
    while (!q->stop && (slot = ps4_pq_try_acquire_locked(q, superseded)) < 0)
        if (!wait_until(q, deadline)) break;
    pthread_mutex_unlock(&q->mu);
    if (waited_s) *waited_s = mono_s() - t0;
    return slot;
}

bool ps4_pq_submit(ps4_present_queue *q, int slot, bool keep) {
    pthread_mutex_lock(&q->mu);
    const bool ok = ps4_pq_submit_locked(q, slot, keep);
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
    return ok;
}

bool ps4_pq_cancel(ps4_present_queue *q, int slot) {
    pthread_mutex_lock(&q->mu);
    const bool ok = ps4_pq_cancel_locked(q, slot);
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
    return ok;
}

int ps4_pq_take(ps4_present_queue *q) {
    pthread_mutex_lock(&q->mu);
    int slot = -1;
    while (!q->stop && (slot = ps4_pq_try_take_locked(q)) < 0) pthread_cond_wait(&q->cv, &q->mu);
    pthread_mutex_unlock(&q->mu);
    return slot;
}

bool ps4_pq_release(ps4_present_queue *q, int slot) {
    pthread_mutex_lock(&q->mu);
    const bool ok = ps4_pq_release_locked(q, slot);
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
    return ok;
}

void ps4_pq_grant(ps4_present_queue *q) {
    pthread_mutex_lock(&q->mu);
    ps4_pq_grant_locked(q);
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
}

bool ps4_pq_drain(ps4_present_queue *q, double timeout) {
    const double deadline = mono_s() + timeout;
    pthread_mutex_lock(&q->mu);
    bool idle;
    while (!(idle = ps4_pq_idle_locked(q)))
        if (!wait_until(q, deadline)) break;
    pthread_mutex_unlock(&q->mu);
    return idle;
}

void ps4_pq_stop(ps4_present_queue *q) {
    pthread_mutex_lock(&q->mu);
    q->stop = true;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
}
