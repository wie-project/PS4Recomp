// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - the time cadence of PS4_SCREENSHOT_EVERY_SEC (ps4_metal_screen.m). Screenshots by
// flip number cannot pair two runs whose frame rates differ (the intro is paced by time, not
// by frames), so this cadence saves the first flip at or after each multiple of `every` seconds
// of wall time since the first flip. A shot is named by the last multiple its flip has passed,
// so a flip late by several periods (a stall) is named by the moment it shows, and the multiples
// it skipped are not saved. The period is a whole number of seconds, so no two shots share a name.
#ifndef PS4_SCREENSHOT_CADENCE_H
#define PS4_SCREENSHOT_CADENCE_H

#include <math.h>
#include <stdbool.h>

typedef struct {
    long every;   // seconds between shots (> 0)
    double t0;    // the first flip's time
    long next;    // the next multiple of `every` to save
    bool started;
} ps4_shot_cadence;

// The name, in seconds, of a shot taken elapsed_ms milliseconds after the first flip: the last
// multiple of `every` at or before it.
static inline long ps4_shot_cadence_name(long every, long elapsed_ms) {
    return elapsed_ms / (1000 * every) * every;
}

// now: seconds on a monotonic clock. Returns the milliseconds since the first flip (rounded
// down) when this flip is saved, else -1; ps4_shot_cadence_name of it is the shot's name.
static inline long ps4_shot_cadence_due(ps4_shot_cadence *c, double now) {
    if (!c->started) {
        c->started = true;
        c->t0 = now;
        c->next = 0;
    }
    const double elapsed = now - c->t0;
    if (elapsed < (double)(c->next * c->every)) return -1;
    const long ms = (long)floor(elapsed * 1000.0);
    c->next = ps4_shot_cadence_name(c->every, ms) / c->every + 1;
    return ms;
}

#endif // PS4_SCREENSHOT_CADENCE_H
