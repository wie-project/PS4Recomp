// SPDX-License-Identifier: GPL-2.0-or-later
//
// Motion sensor state of ScePadData (see ps4_pad_motion.h).
#include "ps4_pad_motion.h"

#include <math.h>

PadQuat pad_quat_mul(PadQuat a, PadQuat b) {
  return (PadQuat){a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                   a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                   a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                   a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

PadQuat pad_quat_conj(PadQuat q) { return (PadQuat){-q.x, -q.y, -q.z, q.w}; }

PadQuat pad_quat_normalize(PadQuat q) {
  double n = sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (n == 0) return kPadQuatIdentity;
  return (PadQuat){q.x / n, q.y / n, q.z / n, q.w / n};
}

PadQuat pad_quat_exp(PadVec r) {
  double a = sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
  if (a < 1e-12) return kPadQuatIdentity;
  double k = sin(a / 2) / a;
  return (PadQuat){r.x * k, r.y * k, r.z * k, cos(a / 2)};
}

PadVec pad_quat_log(PadQuat q) {
  if (q.w < 0) q = (PadQuat){-q.x, -q.y, -q.z, -q.w};
  double s = sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
  if (s < 1e-12) return (PadVec){0, 0, 0};
  double k = 2 * atan2(s, q.w) / s;
  return (PadVec){q.x * k, q.y * k, q.z * k};
}

PadVec pad_quat_rotate_inv(PadQuat q, PadVec v) {
  PadQuat r = pad_quat_mul(pad_quat_mul(pad_quat_conj(q), (PadQuat){v.x, v.y, v.z, 0}), q);
  return (PadVec){r.x, r.y, r.z};
}

PadVec pad_vec_scale(PadVec v, double k) { return (PadVec){v.x * k, v.y * k, v.z * k}; }
double pad_vec_len(PadVec v) { return sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

void pad_motion_reset(PadMotionState *m, const PadMotionSample *s, uint64_t now_us) {
  m->orient = kPadQuatIdentity;
  double n = pad_vec_len(s->accel);
  m->ref_up = (fabs(n - 1) < 0.1) ? pad_vec_scale(s->accel, 1 / n) : (PadVec){0, 1, 0};
  m->last_pose = s->pose;
  m->last_accel = s->accel;
  m->last_us = now_us;
}

static double deadband(double w, int on) { return (on && fabs(w) < PS4_PAD_DEADBAND_RAD_S) ? 0 : w; }

void pad_motion_update(PadMotionState *m, const PadMotionSample *s, uint64_t now_us, PadMotionOut *out) {
  if (!m->on) {
    *out = (PadMotionOut){{0, 0, 0, 1}, {0, 0, 0}, {0, 0, 0}};
    return;
  }
  double dt = now_us > m->last_us ? (double)(now_us - m->last_us) / 1e6 : 0;
  PadVec theta;
  if (s->device) {
    // shadPS4 skips gaps over 1 s instead of integrating a stale rate across them.
    double k = dt <= 1.0 ? dt : 0;
    theta = (PadVec){deadband(s->rate.x, m->deadband) * k, deadband(s->rate.y, m->deadband) * k,
                     deadband(s->rate.z, m->deadband) * k};
  } else {
    // The virtual pad's exact turn since the last update, deadbanded on its mean rate.
    theta = pad_quat_log(pad_quat_mul(pad_quat_conj(m->last_pose), s->pose));
    if (dt > 0) {
      theta = (PadVec){deadband(theta.x / dt, m->deadband) * dt, deadband(theta.y / dt, m->deadband) * dt,
                       deadband(theta.z / dt, m->deadband) * dt};
    }
  }
  m->orient = pad_quat_normalize(pad_quat_mul(m->orient, pad_quat_exp(theta)));
  if (m->tilt && dt > 0) {
    double n = pad_vec_len(s->accel);
    if (fabs(n - 1) < 0.1) {
      PadVec a = pad_vec_scale(s->accel, 1 / n);
      PadVec g = pad_quat_rotate_inv(m->orient, m->ref_up);
      PadVec e = {a.y * g.z - a.z * g.y, a.z * g.x - a.x * g.z, a.x * g.y - a.y * g.x};
      double se = pad_vec_len(e);
      if (se > 1e-12) {
        double ang = atan2(se, a.x * g.x + a.y * g.y + a.z * g.z);
        double gain = dt / PS4_PAD_TILT_TAU_S < 1 ? dt / PS4_PAD_TILT_TAU_S : 1;
        m->orient = pad_quat_normalize(pad_quat_mul(m->orient, pad_quat_exp(pad_vec_scale(e, ang * gain / se))));
      }
    }
  }
  m->last_pose = s->pose;
  m->last_accel = s->accel;
  m->last_us = now_us;
  *out = (PadMotionOut){
      {(float)m->orient.x, (float)m->orient.y, (float)m->orient.z, (float)m->orient.w},
      {(float)s->accel.x, (float)s->accel.y, (float)s->accel.z},
      {(float)deadband(s->rate.x, m->deadband), (float)deadband(s->rate.y, m->deadband),
       (float)deadband(s->rate.z, m->deadband)}};
}

// GameController -> pad axes, as SDL's MFi backend converts them (src/joystick/apple/SDL_mfijoystick.m):
// rate (x, z, -y), acceleration -(x, y, z).
PadMotionSample pad_motion_from_gc(PadVec r, PadVec a) {
  PadMotionSample s = {0};
  s.device = 1;
  s.rate = (PadVec){r.x, r.z, -r.y};
  s.accel = (PadVec){-a.x, -a.y, -a.z};
  s.pose = kPadQuatIdentity;
  return s;
}
