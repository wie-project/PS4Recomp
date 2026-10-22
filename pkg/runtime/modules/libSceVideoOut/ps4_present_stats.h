// Statistics of the Metal presenter (ps4_metal_screen.m): when frames really reached the
// display (presented handlers), how long the presenter waited for a drawable, and how the
// presents were spaced when they were submitted. Plain C so the host tests can drive it; every
// function takes the internal lock, so callers may be on any thread (Metal runs presented
// handlers on its own threads).
#ifndef PS4_PRESENT_STATS_H
#define PS4_PRESENT_STATS_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Frames per report window (presented or dropped).
#define PS4_PRESENT_STATS_WINDOW 600
// Submissions closer together than this count as a burst ("present storm" towards WindowServer).
#define PS4_PRESENT_BURST_MS 2.0

typedef struct ps4_present_stats {
    pthread_mutex_t mu;
    // display side: intervals between successive presentedTime values of shown frames
    float interval_ms[PS4_PRESENT_STATS_WINDOW];
    int n_interval;
    int n_window;              // frames that ended in this window (shown or dropped)
    double last_presented_s;   // presentedTime of the previous shown frame, 0 = none yet
    uint32_t dropped;          // drawables that never reached the screen (presentedTime 0)
    uint32_t superseded;       // frames replaced by a newer one before they got a drawable
    // presenter side: time blocked in -[CAMetalLayer nextDrawable]
    double acquire_ms_sum, acquire_ms_max;
    uint32_t acquire_n, acquire_over_4ms, acquire_over_16ms;
    // producer side: time the frame's producer waited for a free present slot
    double backpressure_ms_sum, backpressure_ms_max;
    uint32_t backpressure_n;
    // submissions
    double last_submit_s;
    uint32_t submits, bursts;
    // lifetime totals
    uint64_t total_presented, total_dropped, total_superseded;
} ps4_present_stats;

void ps4_present_stats_init(ps4_present_stats *s);

// A drawable's presented handler ran: presented_time_s is its presentedTime (0 = dropped).
void ps4_present_stats_on_presented(ps4_present_stats *s, double presented_time_s);
// A frame was dropped by the presenter itself because a newer one replaced it.
void ps4_present_stats_on_superseded(ps4_present_stats *s);
// The presenter got a drawable after waiting wait_ms.
void ps4_present_stats_on_acquire(ps4_present_stats *s, double wait_ms);
// The producer waited wait_ms for a free present slot.
void ps4_present_stats_on_backpressure(ps4_present_stats *s, double wait_ms);
// A present was submitted at now_s (any monotonic clock in seconds).
void ps4_present_stats_on_submit(ps4_present_stats *s, double now_s);

// Summary of one window of the display-side intervals.
typedef struct ps4_present_summary {
    int frames;     // intervals in the window
    float p50, p95, p99, max;
    int over33, over50, over100;
    int under6;     // intervals shorter than 6 ms: frames shown faster than a 120 Hz display can
} ps4_present_summary;

// Percentiles of `n` intervals (sorted in place). Index q*n, clamped: the same convention as the
// renderer's frame-time line.
void ps4_present_summarize(float *ms, int n, ps4_present_summary *out);

// When a window is complete, writes one report line into buf (no newline), starts the next
// window and returns 1; otherwise returns 0.
int ps4_present_stats_report(ps4_present_stats *s, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif // PS4_PRESENT_STATS_H
