// SPDX-License-Identifier: GPL-2.0-or-later
//
// Host test of the guest flip pacing (ps4_flip_pacer_plan, ps4_present_queue.c) and the guest
// vblank timeline (ps4_vblank_clock.h). Driven by videoout_pacing_test.go: one case per run,
// named by argv[1]; prints PASS or FAIL lines, exits non-zero on failure.
//
// The presenter is simulated against a model display with an injected clock (no sleeping): the
// presenter takes a frame (the game always has one ready: the worst case for a cap), asks the
// pacer for a plan, "waits" by moving the clock to the plan's deadline, and the display shows the
// frame as Metal would: a fixed-rate display at its first refresh at or after the frame is ready,
// at least min_duration after the previous frame appeared and one refresh after it (FIFO); an
// adaptive display as soon as the frame is ready, no sooner than its shortest refresh interval
// after the previous frame. A drawable is reused once the frame two later is on screen (three
// drawables), which is the back-pressure of the display.
#include "ps4_present_queue.h"
#include "ps4_vblank_clock.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_fail = 1;                                                                            \
        }                                                                                          \
    } while (0)

static const uint64_t kEpoch = 1000000000000ull; // arbitrary host time of vblank 0
static const double kEncodeS = 0.0007;          // presenter copy until the frame is ready (GPU)
static const double kCpuS = 0.0002;             // presenter CPU time per frame

typedef struct sim_display {
    double hz;       // refresh rate (an adaptive display's highest)
    bool variable;   // adaptive
    double phase_s;  // a fixed display's refresh grid: phase_s + n / hz
} sim_display;

static uint64_t to_ns(double s) {
    return kEpoch + (uint64_t)llround(s * 1e9);
}

static double to_s(uint64_t ns) {
    return (double)(int64_t)(ns - kEpoch) / 1e9;
}

// On-screen time of a frame ready at `ready`, the previous one shown at `prev` (< 0: none).
static double shown_at(const sim_display *d, double ready, double prev, double min_duration) {
    const double refresh = 1.0 / d->hz;
    double t = ready;
    if (prev >= 0) {
        t = fmax(t, prev + min_duration);
        t = fmax(t, prev + refresh * (d->variable ? 1.0 : 0.5)); // fixed: the next grid point after prev
    }
    if (d->variable) return t;
    return d->phase_s + ceil((t - d->phase_s) / refresh - 1e-9) * refresh;
}

typedef struct sim_result {
    int n;
    double *shown; // on-screen times
    uint64_t *not_before;
} sim_result;

// Presents `frames` frames from t = start_s with the given guest flip rate on display d.
static sim_result simulate(const sim_display *d, int flip_rate, int flip_mode, int frames, double start_s) {
    sim_result r = {frames, calloc((size_t)frames, sizeof(double)), calloc((size_t)frames, sizeof(uint64_t))};
    ps4_flip_pacer p;
    ps4_flip_pacer_init(&p, kEpoch);
    const ps4_display_info info = {d->hz, d->variable};
    double now = start_s;
    for (int i = 0; i < frames; i++) {
        if (i >= 2) now = fmax(now, r.shown[i - 2]); // nextDrawable
        const ps4_present_plan plan = ps4_flip_pacer_plan(&p, PS4_PRESENT_VSYNC, flip_rate, flip_mode, info, to_ns(now));
        r.not_before[i] = plan.not_before_ns;
        if (plan.not_before_ns) {
            CHECK(plan.min_duration == 0, "a paced frame also has a minimum duration %f", plan.min_duration);
            if (to_s(plan.not_before_ns) > now) now = to_s(plan.not_before_ns);
        }
        r.shown[i] = shown_at(d, now + kEncodeS, i ? r.shown[i - 1] : -1, plan.min_duration);
        now += kCpuS;
    }
    return r;
}

static void sim_free(sim_result *r) {
    free(r->shown);
    free(r->not_before);
}

static int count_in(const sim_result *r, double t0, double t1) {
    int n = 0;
    for (int i = 0; i < r->n; i++) n += r->shown[i] >= t0 && r->shown[i] < t1;
    return n;
}

// Every interval between shown frames (after the first `skip`) is a whole number of display
// refreshes in [lo, hi]; returns false and reports otherwise.
static bool intervals_in_refreshes(const sim_result *r, const sim_display *d, int skip, int lo, int hi) {
    const double refresh = 1.0 / d->hz;
    for (int i = skip + 1; i < r->n; i++) {
        const double dt = r->shown[i] - r->shown[i - 1];
        const double k = round(dt / refresh);
        if (fabs(dt - k * refresh) > 1e-6 || k < lo || k > hi) {
            CHECK(false, "frame %d shown %.3f ms after the previous one (%.0f Hz display, want %d..%d refreshes)", i,
                  dt * 1e3, d->hz, lo, hi);
            return false;
        }
    }
    return true;
}

// The guest's flip rate over 10 s of steady state, frames per second.
static double steady_rate(const sim_result *r) {
    return count_in(r, 2.0, 12.0) / 10.0;
}

static void check_rate(const sim_result *r, double want_hz, const char *what) {
    const double got = steady_rate(r);
    CHECK(fabs(got - want_hz) <= 0.1 + 1e-9, "%s: %.1f frames/s, want %.0f", what, got, want_hz);
}

// --- cases ------------------------------------------------------------------------------------

static void case_vblank_timeline(void) {
    // tick k is at the ceiling of k / 60 s: whole nanoseconds without accumulated rounding
    CHECK(ps4_vblank_tick_ns(kEpoch, 0) == kEpoch, "tick 0");
    CHECK(ps4_vblank_tick_ns(kEpoch, 1) == kEpoch + 16666667ull, "tick 1 = %llu",
          (unsigned long long)(ps4_vblank_tick_ns(kEpoch, 1) - kEpoch));
    CHECK(ps4_vblank_tick_ns(kEpoch, 3) == kEpoch + 50000000ull, "tick 3");
    CHECK(ps4_vblank_tick_ns(kEpoch, 60) == kEpoch + 1000000000ull, "tick 60 = 1 s");
    // a day of vblanks lands on the exact second: no drift
    CHECK(ps4_vblank_tick_ns(kEpoch, 60ull * 86400) == kEpoch + 86400ull * 1000000000ull, "a day of ticks");
    CHECK(ps4_vblank_ticks_at(kEpoch, kEpoch - 5) == 0, "before the epoch");
    // ticks_at is the inverse: tick(k) <= t < tick(k + 1)
    uint64_t x = 0x9e3779b97f4a7c15ull;
    for (int i = 0; i < 200000; i++) {
        x ^= x << 13, x ^= x >> 7, x ^= x << 17;
        const uint64_t t = kEpoch + x % (3600ull * 1000000000ull * 24 * 30);
        const uint64_t k = ps4_vblank_ticks_at(kEpoch, t);
        if (!(ps4_vblank_tick_ns(kEpoch, k) <= t && t < ps4_vblank_tick_ns(kEpoch, k + 1))) {
            CHECK(false, "t=%llu: k=%llu out of [tick(k), tick(k+1))", (unsigned long long)(t - kEpoch),
                  (unsigned long long)k);
            break;
        }
    }
    for (uint64_t k = 1; k < 5000; k++) {
        const uint64_t t = ps4_vblank_tick_ns(kEpoch, k);
        if (ps4_vblank_ticks_at(kEpoch, t) != k || ps4_vblank_ticks_at(kEpoch, t - 1) != k - 1) {
            CHECK(false, "tick %llu boundary", (unsigned long long)k);
            break;
        }
    }
}

static void case_paces_guest(void) {
    static const struct {
        double hz;
        bool variable;
        int rate;
        bool want;
    } t[] = {
        {60, false, 0, true},     {59.94, false, 0, true},  {120, false, 0, true},  {119.88, false, 0, true},
        {240, false, 0, true},    {0, false, 0, true},      {72, false, 0, false},  {75, false, 0, false},
        {90, false, 0, false},    {100, false, 0, false},   {144, false, 0, false}, {165, false, 0, false},
        {120, true, 0, false},    {60, false, 1, true},     {90, false, 1, true},   {144, false, 1, false},
        {72, false, 1, false},    {100, false, 2, true},    {72, false, 2, false},  {144, false, 2, false},
        {30, false, 0, false},
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        const ps4_display_info d = {t[i].hz, t[i].variable};
        CHECK(ps4_display_paces_guest(d, t[i].rate) == t[i].want, "%.2f Hz%s, flip rate %d: want %d", t[i].hz,
              t[i].variable ? " adaptive" : "", t[i].rate, t[i].want);
    }
}

// A display that paces the guest keeps the pre-existing exact cadence: every frame the same
// number of refreshes, no CPU wait.
static void exact_display(double hz, int flip_rate, int refreshes) {
    const sim_display d = {hz, false, 0.37 / hz};
    sim_result r = simulate(&d, flip_rate, PS4_FLIP_MODE_VSYNC, 1500, 0.123);
    for (int i = 0; i < r.n; i++)
        if (r.not_before[i]) {
            CHECK(false, "%.2f Hz, flip rate %d: frame %d waited on the CPU", hz, flip_rate, i);
            break;
        }
    intervals_in_refreshes(&r, &d, 3, refreshes, refreshes);
    sim_free(&r);
}

static void case_60hz(void) {
    exact_display(60, 0, 1);
    exact_display(59.94, 0, 1);
    sim_display d = {60, false, 0.37 / 60};
    sim_result r = simulate(&d, 0, PS4_FLIP_MODE_VSYNC, 1500, 0.123);
    check_rate(&r, 60, "60 Hz");
    sim_free(&r);
}

static void case_120hz(void) {
    exact_display(120, 0, 2);
    exact_display(119.88, 0, 2);
    exact_display(240, 0, 4);
    const sim_display d = {120, false, 0.37 / 120};
    sim_result r = simulate(&d, 0, PS4_FLIP_MODE_VSYNC, 1500, 0.123);
    check_rate(&r, 60, "120 Hz");
    sim_free(&r);
}

// A display that cannot pace the guest: the guest still flips at its rate; each frame is shown
// for lo or hi refreshes (the unavoidable cadence of a non-multiple rate), never faster.
static void paced_display(double hz, bool variable, int flip_rate, double want_hz, int lo, int hi) {
    for (int ph = 0; ph < 4; ph++) {
        const sim_display d = {hz, variable, (0.11 + 0.23 * ph) / hz};
        sim_result r = simulate(&d, flip_rate, PS4_FLIP_MODE_VSYNC, (int)(want_hz * 14), 0.05 + 0.0031 * ph);
        char what[64];
        snprintf(what, sizeof(what), "%.0f Hz%s, flip rate %d", hz, variable ? " adaptive" : "", flip_rate);
        check_rate(&r, want_hz, what);
        if (!variable) intervals_in_refreshes(&r, &d, 3, lo, hi);
        // the flips themselves are on consecutive flip-rate vblanks of the guest timeline
        const uint64_t step = (uint64_t)(flip_rate + 1);
        for (int i = 4; i < r.n; i++) {
            const uint64_t a = ps4_vblank_ticks_at(kEpoch, r.not_before[i - 1]);
            const uint64_t b = ps4_vblank_ticks_at(kEpoch, r.not_before[i]);
            if (r.not_before[i] != ps4_vblank_tick_ns(kEpoch, b) || b - a != step) {
                CHECK(false, "%s: frame %d flips at vblank %llu after %llu", what, i, (unsigned long long)b,
                      (unsigned long long)a);
                break;
            }
        }
        sim_free(&r);
    }
}

static void case_72hz(void) {
    paced_display(72, false, 0, 60, 1, 2);
}

static void case_144hz(void) {
    paced_display(144, false, 0, 60, 2, 3);
    paced_display(75, false, 0, 60, 1, 2);
    paced_display(165, false, 0, 60, 2, 3);
}

// ProMotion: an adaptive display shows a frame when it is ready, so every frame comes exactly one
// guest vblank after the previous one.
static void case_promotion(void) {
    paced_display(120, true, 0, 60, 0, 0);
    const sim_display d = {120, true, 0};
    sim_result r = simulate(&d, 0, PS4_FLIP_MODE_VSYNC, 1000, 0.2);
    for (int i = 4; i < r.n; i++) {
        const double dt = r.shown[i] - r.shown[i - 1];
        if (fabs(dt - 1.0 / 60) > 2e-9) {
            CHECK(false, "adaptive 120 Hz: frame %d shown %.6f ms after the previous one", i, dt * 1e3);
            break;
        }
    }
    sim_free(&r);
    // flip rate 30 on ProMotion
    paced_display(120, true, 1, 30, 0, 0);
}

static void case_flip_rate_30(void) {
    paced_display(72, false, 1, 30, 2, 3);
    paced_display(144, false, 1, 30, 4, 5);
    exact_display(60, 1, 2);
    exact_display(120, 1, 4);
    exact_display(90, 1, 3);
    // 20 Hz
    paced_display(72, false, 2, 20, 3, 4);
    exact_display(60, 2, 3);
}

// No drift: over an hour at 72 Hz the guest shows exactly 60 frames a second.
static void case_no_drift(void) {
    const sim_display d = {72, false, 0.5 / 72};
    const int frames = 60 * 3600 + 120;
    sim_result r = simulate(&d, 0, PS4_FLIP_MODE_VSYNC, frames, 0.01);
    const int n = count_in(&r, 1.0, 3601.0);
    CHECK(n >= 60 * 3600 - 1 && n <= 60 * 3600 + 1, "an hour at 72 Hz: %d frames, want %d", n, 60 * 3600);
    const uint64_t last = ps4_vblank_ticks_at(kEpoch, r.not_before[frames - 1]);
    CHECK(r.not_before[frames - 1] == ps4_vblank_tick_ns(kEpoch, last), "the last deadline is not a vblank tick");
    sim_free(&r);
}

// A late frame flips on the next vblank; later frames do not burst to catch up.
static void case_late_frame(void) {
    ps4_flip_pacer p;
    ps4_flip_pacer_init(&p, kEpoch);
    const ps4_display_info d = {144, false};
    uint64_t now = ps4_vblank_tick_ns(kEpoch, 10) + 100000; // 0.1 ms after vblank 10
    ps4_present_plan a = ps4_flip_pacer_plan(&p, PS4_PRESENT_VSYNC, 0, PS4_FLIP_MODE_VSYNC, d, now);
    CHECK(a.not_before_ns == ps4_vblank_tick_ns(kEpoch, 11), "first frame: the next vblank");
    // the next frame is taken at once: one vblank later, not the same one
    ps4_present_plan b = ps4_flip_pacer_plan(&p, PS4_PRESENT_VSYNC, 0, PS4_FLIP_MODE_VSYNC, d, a.not_before_ns + 200000);
    CHECK(b.not_before_ns == ps4_vblank_tick_ns(kEpoch, 12), "second frame: vblank 12");
    // the game stalls for 50 ms: the frame flips on the vblank after it shows up
    now = ps4_vblank_tick_ns(kEpoch, 12) + 50000000;
    ps4_present_plan c = ps4_flip_pacer_plan(&p, PS4_PRESENT_VSYNC, 0, PS4_FLIP_MODE_VSYNC, d, now);
    CHECK(c.not_before_ns == ps4_vblank_tick_ns(kEpoch, ps4_vblank_ticks_at(kEpoch, now) + 1), "late frame: next vblank");
    CHECK(c.not_before_ns > now, "late frame: deadline in the future");
    ps4_present_plan e = ps4_flip_pacer_plan(&p, PS4_PRESENT_VSYNC, 0, PS4_FLIP_MODE_VSYNC, d, c.not_before_ns + 1000);
    CHECK(e.not_before_ns - c.not_before_ns >= 16666666, "after a late frame: no burst (%llu ns apart)",
          (unsigned long long)(e.not_before_ns - c.not_before_ns));
    // flip rate 30: two vblanks after the previous flip even if the frame is ready earlier
    ps4_present_plan f = ps4_flip_pacer_plan(&p, PS4_PRESENT_VSYNC, 1, PS4_FLIP_MODE_VSYNC, d, e.not_before_ns + 1000);
    CHECK(f.not_before_ns == ps4_vblank_tick_ns(kEpoch, ps4_vblank_ticks_at(kEpoch, e.not_before_ns) + 2),
          "flip rate 30: two vblanks after the previous flip");
}

// Immediate flips, uncapped and mailbox present at the next refresh: no wait, no minimum.
static void case_unpaced(void) {
    ps4_flip_pacer p;
    ps4_flip_pacer_init(&p, kEpoch);
    const ps4_display_info disp[] = {{72, false}, {144, false}, {120, false}, {60, false}, {120, true}};
    for (size_t i = 0; i < sizeof(disp) / sizeof(disp[0]); i++) {
        const uint64_t now = kEpoch + 123456789;
        ps4_present_plan a = ps4_flip_pacer_plan(&p, PS4_PRESENT_VSYNC, 0, PS4_FLIP_MODE_HSYNC, disp[i], now);
        ps4_present_plan b = ps4_flip_pacer_plan(&p, PS4_PRESENT_UNCAPPED, 0, PS4_FLIP_MODE_VSYNC, disp[i], now);
        ps4_present_plan c = ps4_flip_pacer_plan(&p, PS4_PRESENT_MAILBOX, 1, PS4_FLIP_MODE_VSYNC, disp[i], now);
        CHECK(!a.not_before_ns && a.min_duration == 0, "hsync flip paced (%.0f Hz)", disp[i].hz);
        CHECK(!b.not_before_ns && b.min_duration == 0, "uncapped paced (%.0f Hz)", disp[i].hz);
        CHECK(!c.not_before_ns && c.min_duration == 0, "mailbox paced (%.0f Hz)", disp[i].hz);
    }
}

// The real clock: the wait sleeps at least until the deadline (a lower bound only: not flaky).
static void case_wait_until(void) {
    const uint64_t e = ps4_vblank_epoch_ns();
    CHECK(e && ps4_vblank_epoch_ns() == e, "the epoch is set once");
    const uint64_t t0 = ps4_vblank_now_ns();
    ps4_vblank_wait_until_ns(t0 + 2000000);
    CHECK(ps4_vblank_now_ns() >= t0 + 2000000, "woke before the deadline");
    ps4_vblank_wait_until_ns(t0); // already passed: returns at once
}

static const struct {
    const char *name;
    void (*fn)(void);
} kCases[] = {
    {"vblank-timeline", case_vblank_timeline},
    {"paces-guest", case_paces_guest},
    {"60hz", case_60hz},
    {"120hz", case_120hz},
    {"72hz", case_72hz},
    {"144hz", case_144hz},
    {"promotion", case_promotion},
    {"flip-rate-30", case_flip_rate_30},
    {"no-drift", case_no_drift},
    {"late-frame", case_late_frame},
    {"unpaced", case_unpaced},
    {"wait-until", case_wait_until},
};

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        if (strcmp(argv[1], kCases[i].name) != 0) continue;
        kCases[i].fn();
        if (!g_fail) printf("PASS %s\n", argv[1]);
        return g_fail;
    }
    fprintf(stderr, "unknown case %s\n", argv[1]);
    return 2;
}
