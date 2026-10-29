// Present statistics (see ps4_present_stats.h).
#include "ps4_present_stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ps4_present_stats_init(ps4_present_stats *s) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->mu, NULL);
}

void ps4_present_stats_on_presented(ps4_present_stats *s, double t) {
    pthread_mutex_lock(&s->mu);
    s->n_window++;
    if (t <= 0) {
        s->dropped++;
        s->total_dropped++;
    } else {
        s->total_presented++;
        // Handlers of successive drawables run in presentation order; a time that goes backwards
        // cannot be measured against the previous frame and only restarts the chain.
        if (s->last_presented_s > 0 && t > s->last_presented_s && s->n_interval < PS4_PRESENT_STATS_WINDOW)
            s->interval_ms[s->n_interval++] = (float)((t - s->last_presented_s) * 1000.0);
        s->last_presented_s = t;
    }
    pthread_mutex_unlock(&s->mu);
}

void ps4_present_stats_on_superseded(ps4_present_stats *s) {
    pthread_mutex_lock(&s->mu);
    s->superseded++;
    s->total_superseded++;
    pthread_mutex_unlock(&s->mu);
}

void ps4_present_stats_on_held(ps4_present_stats *s) {
    pthread_mutex_lock(&s->mu);
    s->held++;
    s->total_held++;
    pthread_mutex_unlock(&s->mu);
}

void ps4_present_stats_on_held_dropped(ps4_present_stats *s) {
    pthread_mutex_lock(&s->mu);
    s->held_dropped++;
    s->total_held_dropped++;
    pthread_mutex_unlock(&s->mu);
}

void ps4_present_stats_on_acquire(ps4_present_stats *s, double wait_ms) {
    pthread_mutex_lock(&s->mu);
    s->acquire_n++;
    s->acquire_ms_sum += wait_ms;
    if (wait_ms > s->acquire_ms_max) s->acquire_ms_max = wait_ms;
    if (wait_ms > 4.0) s->acquire_over_4ms++;
    if (wait_ms > 16.0) s->acquire_over_16ms++;
    pthread_mutex_unlock(&s->mu);
}

void ps4_present_stats_on_backpressure(ps4_present_stats *s, double wait_ms) {
    pthread_mutex_lock(&s->mu);
    s->backpressure_n++;
    s->backpressure_ms_sum += wait_ms;
    if (wait_ms > s->backpressure_ms_max) s->backpressure_ms_max = wait_ms;
    pthread_mutex_unlock(&s->mu);
}

void ps4_present_stats_on_submit(ps4_present_stats *s, double now_s) {
    pthread_mutex_lock(&s->mu);
    if (s->last_submit_s > 0 && (now_s - s->last_submit_s) * 1000.0 < PS4_PRESENT_BURST_MS) s->bursts++;
    s->last_submit_s = now_s;
    s->submits++;
    pthread_mutex_unlock(&s->mu);
}

// 120 Hz refreshes: 1 (8.3), 1 late, 2 (16.7), 3 (25), 4 (33.3), then long stalls
const float ps4_present_hist_upper_ms[PS4_PRESENT_HIST_BUCKETS - 1] = {10.0f, 14.0f, 20.0f, 27.0f, 33.4f, 50.0f, 100.0f};

static int cmp_float(const void *a, const void *b) {
    const float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

void ps4_present_summarize(float *ms, int n, ps4_present_summary *o) {
    memset(o, 0, sizeof(*o));
    if (n <= 0) return;
    qsort(ms, (size_t)n, sizeof(float), cmp_float);
    o->frames = n;
#define PCT(q) ms[(int)((q) * n) < n ? (int)((q) * n) : n - 1]
    o->p50 = PCT(0.50);
    o->p95 = PCT(0.95);
    o->p99 = PCT(0.99);
#undef PCT
    o->max = ms[n - 1];
    for (int i = 0; i < n; i++) {
        o->over33 += ms[i] > 33.4f;
        o->over50 += ms[i] > 50.0f;
        o->over100 += ms[i] > 100.0f;
        o->under6 += ms[i] < 6.0f;
        int b = 0;
        while (b < PS4_PRESENT_HIST_BUCKETS - 1 && ms[i] > ps4_present_hist_upper_ms[b]) b++;
        o->hist[b]++;
    }
}

int ps4_present_stats_report(ps4_present_stats *s, char *buf, size_t size) {
    pthread_mutex_lock(&s->mu);
    if (s->n_window < PS4_PRESENT_STATS_WINDOW) {
        pthread_mutex_unlock(&s->mu);
        return 0;
    }
    ps4_present_summary sum;
    ps4_present_summarize(s->interval_ms, s->n_interval, &sum);
    snprintf(buf, size,
             "displayed %d frames: interval p50 %.1f p95 %.1f p99 %.1f max %.1f ms; >33ms %d, >50ms %d, >100ms %d, "
             "<6ms %d; dropped %u, superseded %u, held %u, held-dropped %u; nextDrawable avg %.2f max %.1f ms (>4ms %u, >16ms %u); "
             "backpressure avg %.2f max %.1f ms; submits %u, bursts <%.0fms %u",
             sum.frames, sum.p50, sum.p95, sum.p99, sum.max, sum.over33, sum.over50, sum.over100, sum.under6,
             s->dropped, s->superseded, s->held, s->held_dropped, s->acquire_n ? s->acquire_ms_sum / s->acquire_n : 0.0, s->acquire_ms_max,
             s->acquire_over_4ms, s->acquire_over_16ms,
             s->backpressure_n ? s->backpressure_ms_sum / s->backpressure_n : 0.0, s->backpressure_ms_max, s->submits,
             PS4_PRESENT_BURST_MS, s->bursts);
    size_t len = strlen(buf);
    for (int b = 0; b < PS4_PRESENT_HIST_BUCKETS && len < size; b++) {
        const int w = b < PS4_PRESENT_HIST_BUCKETS - 1
                          ? snprintf(buf + len, size - len, "%s<=%g %d", b ? " " : "; hist ", ps4_present_hist_upper_ms[b], sum.hist[b])
                          : snprintf(buf + len, size - len, " >%g %d", ps4_present_hist_upper_ms[b - 1], sum.hist[b]);
        if (w < 0) break;
        len += (size_t)w;
    }
    // next window: the chain of presented times continues across windows
    s->n_interval = 0;
    s->n_window = 0;
    s->dropped = s->superseded = s->held = s->held_dropped = 0;
    s->acquire_ms_sum = s->acquire_ms_max = 0;
    s->acquire_n = s->acquire_over_4ms = s->acquire_over_16ms = 0;
    s->backpressure_ms_sum = s->backpressure_ms_max = 0;
    s->backpressure_n = 0;
    s->submits = s->bursts = 0;
    pthread_mutex_unlock(&s->mu);
    return 1;
}
