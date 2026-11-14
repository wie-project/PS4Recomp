// SPDX-License-Identifier: GPL-2.0-or-later
//
// Motion sensor state of ScePadData (orientation, acceleration, angular velocity) from one sample
// of the pad's sensors. Plain C: the host tests drive it.
//
// Facts, from shadPS4 (src/core/libraries/pad/pad.cpp, src/input/controller*.cpp):
// - ScePadData.orientation is a unit quaternion (x, y, z, w). It is integrated from the gyro and
//   is (0, 0, 0, 1) when the pad is opened and after scePadResetOrientation: the pose at that
//   moment becomes the identity.
// - angularVelocity is in rad/s, acceleration in g (gravity included).
// - Axes: x to the right, y out of the top face, z toward the player. A pad lying still and flat
//   reports acceleration (0, +1, 0) and angular velocity 0. (shadPS4 scales SDL's m/s^2 by 0.098,
//   which gives 0.96 g at rest; this runtime reports the physical 1.0 g.)
// - The motion sensor is on after scePadOpen: inFAMOUS Second Son imports none of the motion
//   state calls (only scePadOpen and scePadReadState) and its tutorial on a console still sees the
//   pad held sideways. Gravity Rush imports scePadSetTiltCorrectionState and
//   scePadSetAngularVelocityDeadbandState but not scePadSetMotionSensorState.
// The SDK's exact behaviour of the three state calls is not public (shadPS4 does not implement
// them). Our choices:
// - Motion sensor off: the pad reports orientation (0, 0, 0, 1) and zero acceleration and angular
//   velocity (what shadPS4 reports for a pad without data). Turning it on again starts the
//   orientation at the identity.
// - Angular velocity deadband: rates below PS4_PAD_DEADBAND_RAD_S per axis are reported and
//   integrated as 0. The threshold, 1 deg/s, is this runtime's choice (the size of a DualShock 4
//   gyro's zero-rate offset); the console's value is not known.
// - Tilt correction: the orientation's pitch and roll are pulled toward the measured gravity
//   with a time constant of PS4_PAD_TILT_TAU_S (a complementary filter); yaw is untouched.
#ifndef PS4_PAD_MOTION_H
#define PS4_PAD_MOTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_PAD_DEADBAND_RAD_S (3.14159265358979323846 / 180.0)
#define PS4_PAD_TILT_TAU_S 0.4

typedef struct { double x, y, z; } PadVec;
typedef struct { double x, y, z, w; } PadQuat;

static const PadQuat kPadQuatIdentity = {0, 0, 0, 1};

PadQuat pad_quat_mul(PadQuat a, PadQuat b);
PadQuat pad_quat_conj(PadQuat q);
PadQuat pad_quat_normalize(PadQuat q);
// Rotation vector (axis * angle, rad) -> quaternion.
PadQuat pad_quat_exp(PadVec r);
// Quaternion -> rotation vector of the shorter rotation.
PadVec pad_quat_log(PadQuat q);
// A vector given in the frame q rotates into, expressed in the frame q rotates from: q^-1 v q.
PadVec pad_quat_rotate_inv(PadQuat q, PadVec v);
PadVec pad_vec_scale(PadVec v, double k);
double pad_vec_len(PadVec v);

typedef struct {
  int on, tilt, deadband;
  PadQuat orient;     // current pose relative to the pose at enable / reset
  PadVec ref_up;      // measured gravity direction at enable / reset (the reference frame's up)
  PadQuat last_pose;  // virtual pad pose at the last update
  PadVec last_accel;  // acceleration reported at the last update (g)
  uint64_t last_us;
} PadMotionState;

// One sample of the pad's motion: body rate (rad/s) and acceleration (g) in pad axes, from a
// connected controller (device) or from the virtual pad (pose: its pose, pad -> world).
typedef struct {
  int device;
  PadVec rate, accel;
  PadQuat pose; // virtual pad pose (used when !device)
} PadMotionSample;

// The three motion fields of ScePadData.
typedef struct {
  float quat[4];  // x, y, z, w
  float accel[3]; // g
  float vel[3];   // rad/s
} PadMotionOut;

// The pose at now_us (sample s) becomes the identity; s's gravity, when it measures 1 g, is the
// reference up of tilt correction.
void pad_motion_reset(PadMotionState *m, const PadMotionSample *s, uint64_t now_us);
// Advances the orientation to now_us with sample s and writes the three motion fields.
void pad_motion_update(PadMotionState *m, const PadMotionSample *s, uint64_t now_us, PadMotionOut *out);

// A GameController GCMotion sample (rotationRate in rad/s, acceleration in g, both in
// GameController's axes) as a device sample in pad axes.
PadMotionSample pad_motion_from_gc(PadVec gc_rate, PadVec gc_accel);

#ifdef __cplusplus
}
#endif

#endif
