// SPDX-License-Identifier: GPL-2.0-or-later
//
// The guest's vblank timeline: the PS4 display engine scans out at 60 Hz whatever the host
// display refreshes at. Vblank k of the process happens at epoch + k / 60 s, computed from k in
// whole nanoseconds (no accumulated rounding, no drift). The VideoOut vblank thread posts its
// events on these ticks, and the presenter paces flips on them when the host display cannot
// (ps4_present_plan in ps4_present_queue.h). Plain C: the host tests drive the arithmetic.
#ifndef PS4_VBLANK_CLOCK_H
#define PS4_VBLANK_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_VBLANK_HZ 60

// Time of vblank k after `epoch_ns`: the first whole nanosecond at or after epoch + k / 60 s.
static inline uint64_t ps4_vblank_tick_ns(uint64_t epoch_ns, uint64_t k) {
    return epoch_ns + (k * 1000000000ull + PS4_VBLANK_HZ - 1) / PS4_VBLANK_HZ;
}

// Vblanks that have happened by `now_ns` (the k of the latest tick at or before it; 0 before the
// epoch). ps4_vblank_tick_ns(e, k) <= now < ps4_vblank_tick_ns(e, k + 1) for now >= e.
static inline uint64_t ps4_vblank_ticks_at(uint64_t epoch_ns, uint64_t now_ns) {
    return now_ns <= epoch_ns ? 0 : (now_ns - epoch_ns) * PS4_VBLANK_HZ / 1000000000ull;
}

// Monotonic host time in nanoseconds (the clock mach_wait_until sleeps on).
uint64_t ps4_vblank_now_ns(void);
// The process's vblank epoch: the host time of the first call (shared by every caller).
uint64_t ps4_vblank_epoch_ns(void);
// Sleeps until ps4_vblank_now_ns() >= t_ns (returns at once when it already is). No spinning.
void ps4_vblank_wait_until_ns(uint64_t t_ns);

#ifdef __cplusplus
}
#endif

#endif // PS4_VBLANK_CLOCK_H
