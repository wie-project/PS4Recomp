// Present queue and pacing policy (see ps4_present_queue.h).
#include "ps4_present_queue.h"
#include "ps4_vblank_clock.h"

#include <math.h>
#include <string.h>
#include <strings.h>
#include <time.h>

int ps4_present_parse_mode(const char *s, ps4_present_mode *out) {
    // Unset means vsync: games pace their logic by the display, so an unpaced presenter makes
    // them run ahead of their own audio and timers. Uncapped stays available for benchmarking.
    *out = PS4_PRESENT_VSYNC;
    if (!s || !*s || !strcasecmp(s, "vsync")) return 1;
    if (!strcasecmp(s, "uncapped")) {
        *out = PS4_PRESENT_UNCAPPED;
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

// sceVideoOutSetFlipRate value -> vblanks per flip (unknown values as 60 Hz)
static int flip_divider(int guest_flip_rate) {
    return (guest_flip_rate >= 0 && guest_flip_rate <= 2) ? guest_flip_rate + 1 : 1;
}

static double display_hz(ps4_display_info d) {
    return d.hz > 0 ? d.hz : 60.0;
}

bool ps4_display_paces_guest(ps4_display_info d, int guest_flip_rate) {
    if (d.variable) return false;
    const double hz = display_hz(d), guest_hz = (double)PS4_VBLANK_HZ / flip_divider(guest_flip_rate);
    const double n = round(hz / guest_hz);
    return n >= 1 && fabs(hz - n * guest_hz) <= 0.01 * hz;
}

void ps4_flip_pacer_init(ps4_flip_pacer *p, uint64_t epoch_ns) {
    p->epoch_ns = epoch_ns;
    p->last_tick = 0;
    p->has_last = false;
}

ps4_present_plan ps4_flip_pacer_plan(ps4_flip_pacer *p, ps4_present_mode mode, int guest_flip_rate,
                                     int guest_flip_mode, ps4_display_info d, uint64_t now_ns) {
    ps4_present_plan plan = {0, 0.0};
    if (mode != PS4_PRESENT_VSYNC || guest_flip_mode == PS4_FLIP_MODE_HSYNC) return plan;
    const int divider = flip_divider(guest_flip_rate);
    if (ps4_display_paces_guest(d, guest_flip_rate)) {
        p->has_last = false; // the timeline starts over if the window moves to another display
        const double guest = (double)divider / PS4_VBLANK_HZ, refresh = 1.0 / display_hz(d);
        // the display cannot show frames faster than the guest asks for: vsync alone paces them
        if (guest > refresh * 1.05) plan.min_duration = guest - refresh * 0.5;
        return plan;
    }
    uint64_t tick = ps4_vblank_ticks_at(p->epoch_ns, now_ns) + 1; // the next vblank
    if (p->has_last && p->last_tick + (uint64_t)divider > tick) tick = p->last_tick + (uint64_t)divider;
    p->last_tick = tick;
    p->has_last = true;
    plan.not_before_ns = ps4_vblank_tick_ns(p->epoch_ns, tick);
    return plan;
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

// The slot a producer would get now (*replaces: it is a queued frame replaced by mailbox); -1
// when none. Shared by the acquire and the query so the two cannot disagree.
static int acquirable(const ps4_present_queue *q, bool *replaces) {
    *replaces = false;
    if (!q->mailbox && q->credits <= 0) return -1;
    for (int i = 0; i < q->n; i++)
        if (q->state[i] == PS4_PQ_FREE) return i;
    if (!q->mailbox) return -1;
    const int i = oldest_queued(q, true);
    *replaces = i >= 0;
    return i;
}

int ps4_pq_try_acquire_locked(ps4_present_queue *q, int *superseded) {
    if (superseded) *superseded = 0;
    bool replaces;
    const int i = acquirable(q, &replaces);
    if (i < 0) return -1;
    q->state[i] = PS4_PQ_RENDERING;
    if (replaces) {
        q->superseded++;
        if (superseded) *superseded = 1;
    } else if (!q->mailbox) {
        q->credits--;
    }
    return i;
}

bool ps4_pq_can_acquire_locked(const ps4_present_queue *q) {
    bool replaces;
    return acquirable(q, &replaces) >= 0;
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

bool ps4_pq_claim_drawable_locked(ps4_present_queue *q) {
    for (int i = 0; i < q->n; i++)
        if (q->state[i] == PS4_PQ_RENDERING || q->state[i] == PS4_PQ_QUEUED) return false;
    if (!q->mailbox && q->credits > 0) q->credits--;
    return true;
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
