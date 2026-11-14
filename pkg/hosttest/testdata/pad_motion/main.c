// SPDX-License-Identifier: GPL-2.0-or-later
//
// Host test of the motion sensor state of ScePadData (ps4_pad_motion.c). Driven by
// pad_motion_test.go: one case per run, named by argv[1]; prints PASS or FAIL lines, exits
// non-zero on failure.
#include "ps4_pad_motion.h"

#include <math.h>
#include <stdio.h>
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

#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))

static PadMotionSample device(PadVec rate, PadVec accel) {
    PadMotionSample s = {1, rate, accel, {0, 0, 0, 1}};
    return s;
}

static PadMotionState fresh(PadMotionSample rest) {
    PadMotionState m;
    memset(&m, 0, sizeof(m));
    m.on = 1;
    pad_motion_reset(&m, &rest, 0);
    return m;
}

// Feeds n samples at 250 Hz (4 ms) from t0; returns the last output and the end time.
static PadMotionOut run(PadMotionState *m, PadMotionSample s, uint64_t *t, int n) {
    PadMotionOut o;
    memset(&o, 0xAB, sizeof(o));
    for (int i = 0; i < n; i++) {
        *t += 4000;
        pad_motion_update(m, &s, *t, &o);
    }
    return o;
}

static double quat_angle_deg(const float q[4]) {
    double s = sqrt((double)q[0] * q[0] + (double)q[1] * q[1] + (double)q[2] * q[2]);
    return 2 * atan2(s, fabs((double)q[3])) * 180 / M_PI;
}

static const PadVec kFlat = {0, 1, 0};

// A pad at rest reports its gravity, zero rate and the identity.
static void case_rest(void) {
    PadMotionState m = fresh(device((PadVec){0, 0, 0}, kFlat));
    uint64_t t = 0;
    PadMotionOut o = run(&m, device((PadVec){0, 0, 0}, kFlat), &t, 100);
    CHECK(o.quat[0] == 0 && o.quat[1] == 0 && o.quat[2] == 0 && o.quat[3] == 1, "quat %f %f %f %f", o.quat[0],
          o.quat[1], o.quat[2], o.quat[3]);
    CHECK(o.accel[0] == 0 && o.accel[1] == 1 && o.accel[2] == 0, "accel %f %f %f", o.accel[0], o.accel[1], o.accel[2]);
    CHECK(o.vel[0] == 0 && o.vel[1] == 0 && o.vel[2] == 0, "vel");
}

// A turn of 90 deg/s about the pad's y (counter-clockwise seen from above) for 1 s gives the
// quaternion of a 90 deg turn about +y, and reports the rate.
static void case_integrate(void) {
    PadMotionState m = fresh(device((PadVec){0, 0, 0}, kFlat));
    uint64_t t = 0;
    PadMotionOut o = run(&m, device((PadVec){0, M_PI / 2, 0}, kFlat), &t, 250);
    const double h = sqrt(0.5);
    CHECK(NEAR(o.quat[0], 0, 1e-5) && NEAR(o.quat[1], h, 1e-5) && NEAR(o.quat[2], 0, 1e-5) && NEAR(o.quat[3], h, 1e-5),
          "quat %f %f %f %f", o.quat[0], o.quat[1], o.quat[2], o.quat[3]);
    CHECK(NEAR(o.vel[1], M_PI / 2, 1e-6), "vel y %f", o.vel[1]);
}

// With the deadband on, a rate under 1 deg/s is reported and integrated as 0; above it is not.
static void case_deadband(void) {
    PadMotionState m = fresh(device((PadVec){0, 0, 0}, kFlat));
    m.deadband = 1;
    uint64_t t = 0;
    PadMotionOut o = run(&m, device((PadVec){0.5 * M_PI / 180, 0, 0}, kFlat), &t, 250);
    CHECK(o.vel[0] == 0 && o.quat[3] == 1, "slow drift not suppressed: vel %f w %f", o.vel[0], o.quat[3]);
    o = run(&m, device((PadVec){2 * M_PI / 180, 0, 0}, kFlat), &t, 250);
    CHECK(NEAR(o.vel[0], 2 * M_PI / 180, 1e-6) && NEAR(quat_angle_deg(o.quat), 2, 1e-3), "vel %f angle %f", o.vel[0],
          quat_angle_deg(o.quat));
}

// Sensor off: identity and zeros whatever the device reports.
static void case_off(void) {
    PadMotionState m = fresh(device((PadVec){0, 0, 0}, kFlat));
    m.on = 0;
    uint64_t t = 0;
    PadMotionOut o = run(&m, device((PadVec){1, 2, 3}, (PadVec){0.5, 0.5, 0.5}), &t, 10);
    CHECK(o.quat[3] == 1 && o.quat[0] == 0 && o.accel[1] == 0 && o.vel[2] == 0, "not neutral");
}

// Tilt correction pulls a drifted pitch back to the measured gravity (time constant 0.4 s) and
// leaves yaw alone; without it the drift stays.
static void case_tilt(void) {
    for (int tilt = 0; tilt <= 1; tilt++) {
        PadMotionState m = fresh(device((PadVec){0, 0, 0}, kFlat));
        m.tilt = tilt;
        uint64_t t = 0;
        // A gyro that wrongly reports 20 deg of pitch and 30 deg of yaw over 0.1 s while the pad
        // stays flat.
        run(&m, device((PadVec){200 * M_PI / 180, 300 * M_PI / 180, 0}, kFlat), &t, 25);
        PadMotionOut o = run(&m, device((PadVec){0, 0, 0}, kFlat), &t, 750); // 3 s still
        PadVec up = pad_quat_rotate_inv((PadQuat){o.quat[0], o.quat[1], o.quat[2], o.quat[3]}, (PadVec){0, 1, 0});
        double tilt_err = acos(up.y > 1 ? 1 : up.y) * 180 / M_PI;
        double total = quat_angle_deg(o.quat);
        if (tilt) {
            CHECK(tilt_err < 0.1, "tilt error left %f deg", tilt_err);
            CHECK(NEAR(total, 30, 1.5), "yaw changed: total %f deg", total);
        } else {
            CHECK(NEAR(tilt_err, 20, 1.5), "tilt error without correction %f deg", tilt_err);
        }
    }
}

// A gap over 1 s between samples is not integrated (a stale rate across a stall).
static void case_gap(void) {
    PadMotionState m = fresh(device((PadVec){0, 0, 0}, kFlat));
    PadMotionOut o;
    PadMotionSample s = device((PadVec){1, 0, 0}, kFlat);
    pad_motion_update(&m, &s, 2000000, &o);
    CHECK(o.quat[3] == 1, "gap integrated: w %f", o.quat[3]);
}

// The reset makes the current pose the identity.
static void case_reset(void) {
    PadMotionState m = fresh(device((PadVec){0, 0, 0}, kFlat));
    uint64_t t = 0;
    run(&m, device((PadVec){1, 0, 0}, kFlat), &t, 100);
    PadMotionSample s = device((PadVec){0, 0, 0}, kFlat);
    pad_motion_reset(&m, &s, t);
    PadMotionOut o = run(&m, s, &t, 1);
    CHECK(o.quat[3] == 1 && o.quat[0] == 0, "not identity after reset");
}

static int vec_near(PadVec v, double x, double y, double z) {
    return NEAR(v.x, x, 1e-9) && NEAR(v.y, y, 1e-9) && NEAR(v.z, z, 1e-9);
}

// GCMotion of a DualSense in poses measured on macOS -> the pad's axes.
static void case_gc_mapping(void) {
    // Flat, buttons up: gravity vector (0, 0, -1) -> the pad's (0, +1, 0).
    PadMotionSample s = pad_motion_from_gc((PadVec){0, 0, 0}, (PadVec){0, 0, -1});
    CHECK(s.device && vec_near(s.accel, 0, 1, 0), "flat: %f %f %f", s.accel.x, s.accel.y, s.accel.z);
    // Standing on its near edge, triggers up, buttons facing the player: the pad's z (toward the
    // player when flat) now points down, so the measured acceleration is -z.
    s = pad_motion_from_gc((PadVec){0, 0, 0}, (PadVec){0, -1, 0});
    CHECK(vec_near(s.accel, 0, 0, -1), "standing: %f %f %f", s.accel.x, s.accel.y, s.accel.z);
    // On its right side (right grip down): gravity along GameController's +x, the pad's -x is up.
    s = pad_motion_from_gc((PadVec){0, 0, 0}, (PadVec){1, 0, 0});
    CHECK(vec_near(s.accel, -1, 0, 0), "right side: %f %f %f", s.accel.x, s.accel.y, s.accel.z);
    // Turning left on the desk (GameController +z) is the pad's +y; tipping the trigger edge up
    // (GameController +x) is the pad's +x (top face toward the player).
    s = pad_motion_from_gc((PadVec){0, 0, 1}, (PadVec){0, 0, -1});
    CHECK(vec_near(s.rate, 0, 1, 0), "yaw: %f %f %f", s.rate.x, s.rate.y, s.rate.z);
    s = pad_motion_from_gc((PadVec){1, 0, 0}, (PadVec){0, 0, -1});
    CHECK(vec_near(s.rate, 1, 0, 0), "pitch: %f %f %f", s.rate.x, s.rate.y, s.rate.z);
    // Rolling right (GameController -y, the right side goes down) is the pad's +z.
    s = pad_motion_from_gc((PadVec){0, -1, 0}, (PadVec){0, 0, -1});
    CHECK(vec_near(s.rate, 0, 0, 1), "roll: %f %f %f", s.rate.x, s.rate.y, s.rate.z);
}

// The gyro and the accelerometer, mapped, agree: standing the pad up (90 deg about its x in 1 s)
// with tilt correction on ends at 90 deg about +x, and the reference up seen from the pad is the
// measured acceleration. A wrong accelerometer mapping would have tilt correction pull the pose
// away.
static void case_gc_consistent(void) {
    PadMotionState m = fresh(pad_motion_from_gc((PadVec){0, 0, 0}, (PadVec){0, 0, -1}));
    m.tilt = 1;
    uint64_t t = 0;
    PadMotionOut o;
    const int n = 250;
    for (int i = 1; i <= n; i++) {
        double th = M_PI / 2 * i / n;
        PadMotionSample s = pad_motion_from_gc((PadVec){M_PI / 2, 0, 0}, (PadVec){0, -sin(th), -cos(th)});
        t += 4000;
        pad_motion_update(&m, &s, t, &o);
    }
    PadMotionSample rest = pad_motion_from_gc((PadVec){0, 0, 0}, (PadVec){0, -1, 0});
    o = run(&m, rest, &t, 500);
    const double h = sqrt(0.5);
    CHECK(NEAR(o.quat[0], h, 1e-3) && NEAR(o.quat[1], 0, 1e-3) && NEAR(o.quat[2], 0, 1e-3) && NEAR(o.quat[3], h, 1e-3),
          "quat %f %f %f %f", o.quat[0], o.quat[1], o.quat[2], o.quat[3]);
    PadVec up = pad_quat_rotate_inv((PadQuat){o.quat[0], o.quat[1], o.quat[2], o.quat[3]}, (PadVec){0, 1, 0});
    CHECK(NEAR(up.x, o.accel[0], 1e-3) && NEAR(up.y, o.accel[1], 1e-3) && NEAR(up.z, o.accel[2], 1e-3),
          "up %f %f %f vs accel %f %f %f", up.x, up.y, up.z, o.accel[0], o.accel[1], o.accel[2]);
}

int main(int argc, char **argv) {
    static const struct { const char *name; void (*fn)(void); } cases[] = {
        {"rest", case_rest},   {"integrate", case_integrate}, {"deadband", case_deadband}, {"off", case_off},
        {"tilt", case_tilt},   {"gap", case_gap},             {"reset", case_reset},
        {"gc-mapping", case_gc_mapping}, {"gc-consistent", case_gc_consistent},
    };
    if (argc != 2) return 2;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            cases[i].fn();
            if (!g_fail) printf("PASS %s\n", argv[1]);
            return g_fail;
        }
    }
    printf("FAIL unknown case %s\n", argv[1]);
    return 2;
}
