#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#include <errno.h>
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ps4_pad.h"

#define MAX_PAD_HANDLES 4

// Motion sensors (scePadSetMotionSensorState and the calls after it).
//
// Facts, from shadPS4 (src/core/libraries/pad/pad.cpp, src/input/controller*.cpp) and the SDL3
// GameController backend it uses on macOS (externals/sdl3/src/joystick/apple/SDL_mfijoystick.m):
// - ScePadData.orientation is a unit quaternion (x, y, z, w). It is integrated from the gyro and
//   is (0, 0, 0, 1) when the pad is opened and after scePadResetOrientation: the pose at that
//   moment becomes the identity.
// - angularVelocity is in rad/s, acceleration in g (gravity included).
// - Axes: x to the right, y out of the top face, z toward the player. A pad lying still and flat
//   reports acceleration (0, +1, 0) and angular velocity 0. (shadPS4 scales SDL's m/s^2 by 0.098,
//   which gives 0.96 g at rest; this runtime reports the physical 1.0 g, as GCMotion gives it.) This is SDL's axis convention, which
//   shadPS4 passes through unchanged.
// - GCMotion -> pad axes (as SDL converts them): rate (x, z, -y), acceleration -(x, y, z).
// - The motion sensor is on after scePadOpen: inFAMOUS Second Son imports none of the motion
//   state calls (only scePadOpen and scePadReadState) and its tutorial on a console still sees the
//   pad held sideways.
// The SDK's exact behaviour of the three state calls is not public (shadPS4 does not implement
// them). Our choices:
// - Motion sensor off: the pad reports orientation (0, 0, 0, 1) and zero acceleration and angular
//   velocity (what shadPS4 reports for a pad without data). Turning it on again starts the
//   orientation at the identity.
// - Angular velocity deadband: rates below PAD_DEADBAND_RAD_S per axis are reported and
//   integrated as 0. The threshold, 1 deg/s, is this runtime's choice (the size of a DualShock 4
//   gyro's zero-rate offset); the console's value is not known.
// - Tilt correction: the orientation's pitch and roll are pulled toward the measured gravity
//   with a time constant of PAD_TILT_TAU_S (a complementary filter); yaw is untouched.
#define PAD_DEADBAND_RAD_S (M_PI / 180.0)
#define PAD_TILT_TAU_S 0.4

typedef struct { double x, y, z; } PadVec;
typedef struct { double x, y, z, w; } PadQuat;

typedef struct {
  int on, tilt, deadband;
  PadQuat orient;     // current pose relative to the pose at enable / reset
  PadVec ref_up;      // measured gravity direction at enable / reset (the reference frame's up)
  PadQuat last_pose;  // virtual pad pose at the last update
  PadVec last_accel;  // acceleration reported at the last update (g)
  uint64_t last_us;
} PadMotionState;

typedef struct {
  int in_use;
  int32_t handle;
  int32_t userId;
  PadMotionState motion;
} PadHandleSlot;

static PadHandleSlot g_pad_slots[MAX_PAD_HANDLES];
static pthread_mutex_t g_pad_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_next_pad_handle = 1;
static int g_pad_inited = 0;

static uint32_t g_key_buttons = 0;
static uint8_t g_key_l2 = 0;
static uint8_t g_key_r2 = 0;
static uint8_t g_key_lx = 128;
static uint8_t g_key_ly = 128;
static uint8_t g_key_rx = 128;
static uint8_t g_key_ry = 128;

static uint64_t get_time_micros(void) {
  static mach_timebase_info_data_t timebase;
  if (timebase.denom == 0) {
    mach_timebase_info(&timebase);
  }
  uint64_t t = mach_absolute_time();
  return (t * timebase.numer) / (timebase.denom * 1000ULL);
}

static const PadQuat kQuatIdentity = {0, 0, 0, 1};

static PadQuat quat_mul(PadQuat a, PadQuat b) {
  return (PadQuat){a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                   a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                   a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                   a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

static PadQuat quat_conj(PadQuat q) { return (PadQuat){-q.x, -q.y, -q.z, q.w}; }

static PadQuat quat_normalize(PadQuat q) {
  double n = sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (n == 0) return kQuatIdentity;
  return (PadQuat){q.x / n, q.y / n, q.z / n, q.w / n};
}

// Rotation vector (axis * angle, rad) -> quaternion.
static PadQuat quat_exp(PadVec r) {
  double a = sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
  if (a < 1e-12) return kQuatIdentity;
  double k = sin(a / 2) / a;
  return (PadQuat){r.x * k, r.y * k, r.z * k, cos(a / 2)};
}

// Quaternion -> rotation vector of the shorter rotation.
static PadVec quat_log(PadQuat q) {
  if (q.w < 0) q = (PadQuat){-q.x, -q.y, -q.z, -q.w};
  double s = sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
  if (s < 1e-12) return (PadVec){0, 0, 0};
  double k = 2 * atan2(s, q.w) / s;
  return (PadVec){q.x * k, q.y * k, q.z * k};
}

// A vector given in the frame q rotates into, expressed in the frame q rotates from: q^-1 v q.
static PadVec quat_rotate_inv(PadQuat q, PadVec v) {
  PadQuat r = quat_mul(quat_mul(quat_conj(q), (PadQuat){v.x, v.y, v.z, 0}), q);
  return (PadVec){r.x, r.y, r.z};
}

static PadVec vec_scale(PadVec v, double k) { return (PadVec){v.x * k, v.y * k, v.z * k}; }
static double vec_len(PadVec v) { return sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// The virtual pad of PS4_PAD_SCRIPT tilt steps and the sideways key: a pose (pad -> world, world
// y up) that turns at a constant body rate from one pose to the next.
typedef struct {
  uint64_t start_us, dur_us;
  PadQuat from, to;
  PadVec rate; // body frame, rad/s, while turning
} PadTurn;
static PadTurn g_turn = {0, 0, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0}};

static PadQuat turn_pose(uint64_t t_us) {
  if (t_us <= g_turn.start_us) return g_turn.from;
  if (t_us >= g_turn.start_us + g_turn.dur_us) return g_turn.to;
  return quat_mul(g_turn.from, quat_exp(vec_scale(g_turn.rate, (double)(t_us - g_turn.start_us) / 1e6)));
}

static PadVec turn_rate(uint64_t t_us) {
  if (t_us > g_turn.start_us && t_us < g_turn.start_us + g_turn.dur_us) return g_turn.rate;
  return (PadVec){0, 0, 0};
}

// Pose of a pad, from lying flat: rolled about z (positive raises the right side), then
// pitched about its own x (positive tilts the top face toward the player), then yawed about its
// own y (the top face's normal; positive turns counter-clockwise seen from above). Held sideways
// (roll 90), pitch turns the pad left and right and yaw turns it up and down.
static PadQuat pose_from_angles(double roll_deg, double pitch_deg, double yaw_deg) {
  PadQuat roll = quat_exp((PadVec){0, 0, roll_deg * M_PI / 180});
  PadQuat pitch = quat_exp((PadVec){pitch_deg * M_PI / 180, 0, 0});
  PadQuat yaw = quat_exp((PadVec){0, yaw_deg * M_PI / 180, 0});
  return quat_mul(quat_mul(roll, pitch), yaw);
}

static void start_turn(uint64_t t_us, PadQuat target, uint64_t dur_us) {
  PadQuat from = turn_pose(t_us);
  g_turn.start_us = t_us;
  g_turn.from = from;
  g_turn.to = target;
  g_turn.dur_us = dur_us;
  PadVec r = quat_log(quat_mul(quat_conj(from), target));
  g_turn.rate = dur_us ? vec_scale(r, 1e6 / (double)dur_us) : (PadVec){0, 0, 0};
  if (!dur_us) g_turn.from = target;
}

// Clock of the debug scripts and the virtual pad (call with g_pad_mutex held): microseconds since
// the first pad read (0 before it, so scePadOpen and the motion calls before that read do not move
// the origin of existing scripts), or with PS4_PAD_CLOCK=frames, submitted video flips at 30 fps.
extern volatile uint64_t g_ps4_videoout_flip_count;
static int g_pad_clock_frames = -1;
static uint64_t g_pad_clock_t0;
static int g_pad_clock_started;

static void pad_clock_start(void) {
  if (!g_pad_clock_started) {
    g_pad_clock_started = 1;
    g_pad_clock_t0 = get_time_micros();
  }
}

static uint64_t pad_script_clock_us(void) {
  if (g_pad_clock_frames < 0) {
    const char *c = getenv("PS4_PAD_CLOCK");
    g_pad_clock_frames = (c && strcmp(c, "frames") == 0) ? 1 : 0;
  }
  if (g_pad_clock_frames) return g_ps4_videoout_flip_count * 1000000ULL / 30;
  if (!g_pad_clock_started) return 0;
  const uint64_t now = get_time_micros();
  const uint64_t t0 = g_pad_clock_t0;
  return now - t0;
}

// One sample of the pad's motion: body rate (rad/s) and acceleration (g) in pad axes. A connected
// motion-capable controller wins; otherwise the virtual pad (resting flat unless a script or the
// sideways key turns it).
typedef struct {
  int device;
  PadVec rate, accel;
  PadQuat pose; // virtual pad pose (used when !device)
} PadMotionSample;

static void motion_reset(PadMotionState *m, const PadMotionSample *s, uint64_t now_us) {
  m->orient = kQuatIdentity;
  double n = vec_len(s->accel);
  m->ref_up = (fabs(n - 1) < 0.1) ? vec_scale(s->accel, 1 / n) : (PadVec){0, 1, 0};
  m->last_pose = s->pose;
  m->last_accel = s->accel;
  m->last_us = now_us;
}

static double deadband(double w, int on) { return (on && fabs(w) < PAD_DEADBAND_RAD_S) ? 0 : w; }

// Advances one handle's orientation to now_us and writes the three motion fields.
static void motion_update(PadMotionState *m, const PadMotionSample *s, uint64_t now_us, OrbisPadData *d) {
  if (!m->on) {
    d->quat = (vec_float4){0, 0, 0, 1};
    d->vel = (vec_float3){0, 0, 0};
    d->acell = (vec_float3){0, 0, 0};
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
    theta = quat_log(quat_mul(quat_conj(m->last_pose), s->pose));
    if (dt > 0) {
      theta = (PadVec){deadband(theta.x / dt, m->deadband) * dt, deadband(theta.y / dt, m->deadband) * dt,
                       deadband(theta.z / dt, m->deadband) * dt};
    }
  }
  m->orient = quat_normalize(quat_mul(m->orient, quat_exp(theta)));
  if (m->tilt && dt > 0) {
    double n = vec_len(s->accel);
    if (fabs(n - 1) < 0.1) {
      PadVec a = vec_scale(s->accel, 1 / n);
      PadVec g = quat_rotate_inv(m->orient, m->ref_up);
      PadVec e = {a.y * g.z - a.z * g.y, a.z * g.x - a.x * g.z, a.x * g.y - a.y * g.x};
      double se = vec_len(e);
      if (se > 1e-12) {
        double ang = atan2(se, a.x * g.x + a.y * g.y + a.z * g.z);
        double gain = dt / PAD_TILT_TAU_S < 1 ? dt / PAD_TILT_TAU_S : 1;
        m->orient = quat_normalize(quat_mul(m->orient, quat_exp(vec_scale(e, ang * gain / se))));
      }
    }
  }
  m->last_pose = s->pose;
  m->last_accel = s->accel;
  m->last_us = now_us;
  d->quat = (vec_float4){(float)m->orient.x, (float)m->orient.y, (float)m->orient.z, (float)m->orient.w};
  d->vel = (vec_float3){(float)deadband(s->rate.x, m->deadband), (float)deadband(s->rate.y, m->deadband),
                        (float)deadband(s->rate.z, m->deadband)};
  d->acell = (vec_float3){(float)s->accel.x, (float)s->accel.y, (float)s->accel.z};
}

// A shake of the virtual pad (PS4_PAD_SCRIPT shake steps, the N key): a vertical linear
// acceleration of amp_g * sin(2 pi hz t) on top of gravity, as when a pad is rattled up and down.
typedef struct {
  uint64_t start_us, end_us; // end UINT64_MAX: until the key is released
  double amp_g, hz;
} PadShake;
static PadShake g_shake;

static void start_shake(uint64_t t_us, uint64_t dur_us, double amp_g, double hz) {
  g_shake.start_us = t_us;
  g_shake.end_us = dur_us == UINT64_MAX ? UINT64_MAX : t_us + dur_us;
  g_shake.amp_g = amp_g;
  g_shake.hz = hz;
}

// Virtual pad sample at t_us: rate of its turn, and gravity plus the shake's acceleration seen in
// the pad's axes (a turn at a constant rate about the pad's centre adds none).
static PadMotionSample virtual_sample(uint64_t t_us) {
  PadMotionSample s;
  s.device = 0;
  s.pose = turn_pose(t_us);
  s.rate = turn_rate(t_us);
  double up = 1;
  if (t_us >= g_shake.start_us && t_us < g_shake.end_us) {
    up += g_shake.amp_g * sin(2 * M_PI * g_shake.hz * (double)(t_us - g_shake.start_us) / 1e6);
  }
  s.accel = quat_rotate_inv(s.pose, (PadVec){0, up, 0});
  return s;
}

static PadHandleSlot *find_slot(int32_t handle) {
  for (int i = 0; i < MAX_PAD_HANDLES; i++) {
    if (g_pad_slots[i].in_use && g_pad_slots[i].handle == handle) return &g_pad_slots[i];
  }
  return NULL;
}

int32_t scePadInit(void) {
  pthread_mutex_lock(&g_pad_mutex);
  if (!g_pad_inited) {
    g_pad_inited = 1;
    memset(g_pad_slots, 0, sizeof(g_pad_slots));
    @autoreleasepool {
      [GCController startWirelessControllerDiscoveryWithCompletionHandler:nil];
    }
  }
  pthread_mutex_unlock(&g_pad_mutex);
  return 0;
}

int32_t scePadOpen(int32_t userID, int32_t type, int32_t index, void *param) {
  (void)type;
  (void)index;
  (void)param;

  pthread_mutex_lock(&g_pad_mutex);
  if (!g_pad_inited) {
    g_pad_inited = 1;
    memset(g_pad_slots, 0, sizeof(g_pad_slots));
  }

  PadHandleSlot *slot = NULL;
  for (int i = 0; i < MAX_PAD_HANDLES; i++) {
    if (!g_pad_slots[i].in_use) {
      slot = &g_pad_slots[i];
      break;
    }
  }

  if (!slot) {
    pthread_mutex_unlock(&g_pad_mutex);
    return -ENOMEM;
  }

  slot->handle = g_next_pad_handle++;
  slot->userId = userID;
  slot->in_use = 1;
  // A new handle starts with the motion sensor on, and tilt correction and deadband off. The
  // orientation starts at the identity (as after scePadResetOrientation).
  memset(&slot->motion, 0, sizeof(slot->motion));
  slot->motion.on = 1;
  const uint64_t now = pad_script_clock_us();
  PadMotionSample rest = virtual_sample(now);
  motion_reset(&slot->motion, &rest, now);
  int32_t assignedHandle = slot->handle;

  pthread_mutex_unlock(&g_pad_mutex);
  return assignedHandle;
}

int32_t scePadClose(int32_t handle) {
  if (handle <= 0)
    return -EINVAL;

  pthread_mutex_lock(&g_pad_mutex);
  for (int i = 0; i < MAX_PAD_HANDLES; i++) {
    if (g_pad_slots[i].in_use && g_pad_slots[i].handle == handle) {
      g_pad_slots[i].in_use = 0;
      pthread_mutex_unlock(&g_pad_mutex);
      return 0;
    }
  }
  pthread_mutex_unlock(&g_pad_mutex);
  return -EINVAL;
}

static int g_key_up = 0;
static int g_key_down = 0;
static int g_key_left = 0;
static int g_key_right = 0;
static int g_key_rx_up = 0;
static int g_key_rx_down = 0;
static int g_key_rx_left = 0;
static int g_key_rx_right = 0;
static int g_key_sideways = 0;
static int g_key_shake = 0;

void ps4_pad_handle_key(unsigned short keyCode, int isDown) {
  pthread_mutex_lock(&g_pad_mutex);

  uint32_t btn = 0;
  switch (keyCode) {
  // --- Directional / Left Stick ---
  case 126: // Up Arrow
  case 13:  // W
    g_key_up = isDown ? 1 : 0;
    break;
  case 125: // Down Arrow
  case 1:   // S
    g_key_down = isDown ? 1 : 0;
    break;
  case 123: // Left Arrow
  case 0:   // A
    g_key_left = isDown ? 1 : 0;
    break;
  case 124: // Right Arrow
  case 2:   // D
    g_key_right = isDown ? 1 : 0;
    break;

  // --- Right Stick ---
  case 91:  // Numpad 8
  case 116: // Page Up
    g_key_rx_up = isDown ? 1 : 0;
    break;
  case 84:  // Numpad 2
  case 87:  // Numpad 5
  case 121: // Page Down
    g_key_rx_down = isDown ? 1 : 0;
    break;
  case 86:  // Numpad 4
  case 115: // Home
    g_key_rx_left = isDown ? 1 : 0;
    break;
  case 88:  // Numpad 6
  case 119: // End
    g_key_rx_right = isDown ? 1 : 0;
    break;

  // --- Face Buttons ---
  // Cross (X) - Bottom: Space, Return, K, Z
  case 49: // Space
  case 36: // Return
  case 40: // K
  case 6:  // Z
    btn = ORBIS_PAD_BUTTON_CROSS;
    break;
  // Circle (O) - Right: L, O, X
  case 37: // L
  case 31: // O
  case 7:  // X
    btn = ORBIS_PAD_BUTTON_CIRCLE;
    break;
  // Square (□) - Left: J, U, C
  case 38: // J
  case 32: // U
  case 8:  // C
    btn = ORBIS_PAD_BUTTON_SQUARE;
    break;
  // Triangle (△) - Top: I, Y, V
  case 34: // I
  case 16: // Y
  case 9:  // V
    btn = ORBIS_PAD_BUTTON_TRIANGLE;
    break;

  // --- Shoulder Buttons & Triggers ---
  case 12: // Q
    btn = ORBIS_PAD_BUTTON_L1;
    break;
  case 14: // E
    btn = ORBIS_PAD_BUTTON_R1;
    break;
  case 18: // 1
  case 56: // Left Shift
    btn = ORBIS_PAD_BUTTON_L2;
    g_key_l2 = isDown ? 255 : 0;
    break;
  case 20: // 3
  case 19: // 2
  case 59: // Left Control
  case 60: // Right Shift
    btn = ORBIS_PAD_BUTTON_R2;
    g_key_r2 = isDown ? 255 : 0;
    break;

  // --- Stick Clicks ---
  case 3:  // F
    btn = ORBIS_PAD_BUTTON_L3;
    break;
  case 5:  // G
  case 11: // B
    btn = ORBIS_PAD_BUTTON_R3;
    break;

  // --- Menu / Options / Restart ---
  case 15: // R (Restart in SDL2, Options)
  case 48: // Tab
  case 53: // Escape
  case 35: // P (Pause)
  case 46: // M (Menu)
    btn = ORBIS_PAD_BUTTON_OPTIONS;
    break;

  // --- Motion: H holds the pad sideways (rolled 90 degrees, right side up) ---
  case 4: // H
    if (isDown != g_key_sideways) {
      g_key_sideways = isDown ? 1 : 0;
      start_turn(pad_script_clock_us(), pose_from_angles(isDown ? 90 : 0, 0, 0), 300000);
    }
    break;

  // --- Motion: N rattles the pad (1 g at 6 Hz, up and down) while held ---
  case 45: // N
    if (isDown != g_key_shake) {
      g_key_shake = isDown ? 1 : 0;
      const uint64_t now = pad_script_clock_us();
      if (isDown) start_shake(now, UINT64_MAX, 1.0, 6.0);
      else g_shake.end_us = now;
    }
    break;

  // --- Touchpad Click ---
  case 17: // T
  case 50: // ` (Backtick)
    btn = ORBIS_PAD_BUTTON_TOUCH_PAD;
    break;

  default:
    break;
  }

  // Update analog left stick axes based on directional keys
  if (g_key_left && !g_key_right) {
    g_key_lx = 0;
  } else if (g_key_right && !g_key_left) {
    g_key_lx = 255;
  } else {
    g_key_lx = 128;
  }

  if (g_key_up && !g_key_down) {
    g_key_ly = 0;
  } else if (g_key_down && !g_key_up) {
    g_key_ly = 255;
  } else {
    g_key_ly = 128;
  }

  // Update analog right stick axes
  if (g_key_rx_left && !g_key_rx_right) {
    g_key_rx = 0;
  } else if (g_key_rx_right && !g_key_rx_left) {
    g_key_rx = 255;
  } else {
    g_key_rx = 128;
  }

  if (g_key_rx_up && !g_key_rx_down) {
    g_key_ry = 0;
  } else if (g_key_rx_down && !g_key_rx_up) {
    g_key_ry = 255;
  } else {
    g_key_ry = 128;
  }

  // Update digital D-pad button states
  if (g_key_up) {
    g_key_buttons |= ORBIS_PAD_BUTTON_UP;
  } else {
    g_key_buttons &= ~ORBIS_PAD_BUTTON_UP;
  }
  if (g_key_down) {
    g_key_buttons |= ORBIS_PAD_BUTTON_DOWN;
  } else {
    g_key_buttons &= ~ORBIS_PAD_BUTTON_DOWN;
  }
  if (g_key_left) {
    g_key_buttons |= ORBIS_PAD_BUTTON_LEFT;
  } else {
    g_key_buttons &= ~ORBIS_PAD_BUTTON_LEFT;
  }
  if (g_key_right) {
    g_key_buttons |= ORBIS_PAD_BUTTON_RIGHT;
  } else {
    g_key_buttons &= ~ORBIS_PAD_BUTTON_RIGHT;
  }

  if (btn != 0) {
    if (isDown) {
      g_key_buttons |= btn;
    } else {
      g_key_buttons &= ~btn;
    }
  }

  pthread_mutex_unlock(&g_pad_mutex);
}

// Debug aid for driving headless runs interactively: PS4_PAD_CTL=<file> is polled
// for appended lines while the game runs. Commands are executed one after another, in file
// order (a "shot" after a "w" is taken when the wait has ended):
//   p <hexmask> <dur_ms> [<lx> <ly> [<rx> <ry>]]  hold buttons/sticks for dur_ms
//   w <ms>                                       wait (neutral pad)
//   shot                                         save a screenshot of the next frame
//                                                (needs PS4_SCREENSHOT_DIR)
//   dumprt / dumpdraws                           write the GPU render targets of the next
//                                                frame / the color target after each of its
//                                                draws (needs PS4_SCREENSHOT_DIR)
//   trace                                        log the GPU work of the next frame
//   close / terminate                            quit through the window close button / Cmd+Q
// Stick values are 0..255 (128 = center); -1 keeps the stick untouched.
// Menus auto-repeat a held direction: Uncharted 4's menus move twice for a 300 ms hold and
// once for 100 ms, so taps should be short.
extern volatile int g_ps4_screenshot_request;
extern volatile int g_ps4_gnm_dump_request;
typedef enum {
  PAD_CTL_HOLD, PAD_CTL_SHOT, PAD_CTL_DUMPRT, PAD_CTL_DUMPDRAWS, PAD_CTL_TRACE, PAD_CTL_CLOSE,
  PAD_CTL_TERMINATE
} PadCtlKind;
typedef struct { PadCtlKind kind; long dur; uint32_t mask; int lx, ly, rx, ry; } PadCtlCmd;

static void ps4_pad_control_action(PadCtlKind kind) {
  switch (kind) {
  case PAD_CTL_SHOT: g_ps4_screenshot_request = 1; break;
  case PAD_CTL_DUMPRT: g_ps4_gnm_dump_request |= 1; break;
  case PAD_CTL_DUMPDRAWS: g_ps4_gnm_dump_request |= 2; break;
  case PAD_CTL_TRACE: g_ps4_gnm_dump_request |= 4; break;
  case PAD_CTL_CLOSE: {
    extern void ps4_metal_screen_request_close(void);
    ps4_metal_screen_request_close();
    break;
  }
  case PAD_CTL_TERMINATE: {
    extern void ps4_metal_screen_request_terminate(void);
    ps4_metal_screen_request_terminate();
    break;
  }
  case PAD_CTL_HOLD: break;
  }
}

static void ps4_pad_control_file(uint32_t *buttons, uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry) {
  static int init;
  static FILE *f;
  static char line[256];
  static size_t line_len;
  static PadCtlCmd queue[512];
  static int q_head, q_tail;
  static int active;
  static PadCtlCmd cur;
  static uint64_t cur_end_us;
  if (!init) {
    init = 1;
    const char *path = getenv("PS4_PAD_CTL");
    if (path) {
      f = fopen(path, "a+");
      if (f) fseek(f, 0, SEEK_END);
      else fprintf(stderr, "[pad] cannot open control file %s\n", path);
    }
  }
  if (!f) return;
  int c;
  clearerr(f);
  while ((c = fgetc(f)) != EOF) {
    if (c != '\n') {
      if (line_len + 1 < sizeof(line)) line[line_len++] = (char)c;
      continue;
    }
    line[line_len] = 0;
    line_len = 0;
    PadCtlCmd cmd = {PAD_CTL_HOLD, 0, 0, -1, -1, -1, -1};
    if (strncmp(line, "shot", 4) == 0) {
      cmd.kind = PAD_CTL_SHOT;
    } else if (strncmp(line, "dumprt", 6) == 0) {
      cmd.kind = PAD_CTL_DUMPRT;
    } else if (strncmp(line, "dumpdraws", 9) == 0) {
      cmd.kind = PAD_CTL_DUMPDRAWS;
    } else if (strncmp(line, "trace", 5) == 0) {
      cmd.kind = PAD_CTL_TRACE;
    } else if (strncmp(line, "close", 5) == 0) {
      cmd.kind = PAD_CTL_CLOSE;
    } else if (strncmp(line, "terminate", 9) == 0) {
      cmd.kind = PAD_CTL_TERMINATE;
    } else if (line[0] == 'w') {
      cmd.dur = strtol(line + 1, NULL, 10);
    } else if (line[0] == 'p') {
      char *e = line + 1;
      cmd.mask = (uint32_t)strtoul(e, &e, 16);
      cmd.dur = strtol(e, &e, 10);
      int v[4] = {-1, -1, -1, -1};
      for (int k = 0; k < 4; k++) {
        char *e2;
        long x = strtol(e, &e2, 10);
        if (e2 == e) break;
        v[k] = (int)x;
        e = e2;
      }
      cmd.lx = v[0]; cmd.ly = v[1]; cmd.rx = v[2]; cmd.ry = v[3];
    } else {
      continue;
    }
    if ((q_tail + 1) % 512 != q_head) {
      queue[q_tail] = cmd;
      q_tail = (q_tail + 1) % 512;
    } else {
      fprintf(stderr, "[pad] control queue full, dropped: %s\n", line);
    }
  }
  const uint64_t now = get_time_micros();
  if (active && now >= cur_end_us) active = 0;
  // Actions take no time: run every action that is due, up to the next hold or wait.
  while (!active && q_head != q_tail) {
    cur = queue[q_head];
    q_head = (q_head + 1) % 512;
    if (cur.kind != PAD_CTL_HOLD) {
      ps4_pad_control_action(cur.kind);
      continue;
    }
    cur_end_us = now + (uint64_t)(cur.dur > 0 ? cur.dur : 0) * 1000;
    active = 1;
  }
  if (!active) return;
  *buttons |= cur.mask;
  if (cur.lx >= 0) *lx = (uint8_t)cur.lx;
  if (cur.ly >= 0) *ly = (uint8_t)cur.ly;
  if (cur.rx >= 0) *rx = (uint8_t)cur.rx;
  if (cur.ry >= 0) *ry = (uint8_t)cur.ry;
}

int32_t scePadReadState(int32_t handle, OrbisPadData *data) {
  if (!data)
    return -EINVAL;

  pthread_mutex_lock(&g_pad_mutex);

  PadHandleSlot *slot = find_slot(handle);
  if (!slot) {
    pthread_mutex_unlock(&g_pad_mutex);
    return -EINVAL;
  }

  memset(data, 0, sizeof(OrbisPadData));

  uint32_t buttons = g_key_buttons;
  // Debug clock for AUTOPRESS/SCRIPT: wall time since the first pad read, or with
  // PS4_PAD_CLOCK=frames, submitted video flips converted to ms at 30 fps (so scripts
  // stay in sync with the game when the host runs slowly).
  pad_clock_start();
  const uint64_t script_us = pad_script_clock_us();
  {
    // Debug aid for headless runs: PS4_PAD_AUTOPRESS=<hex button mask> holds
    // those buttons for 300 ms every 4 s (e.g. 0x4000 = Cross).
    static int autopress_init;
    static uint32_t autopress_mask;
    static long autopress_until = -1;
    if (!autopress_init) {
      autopress_init = 1;
      const char *env = getenv("PS4_PAD_AUTOPRESS");
      if (env) autopress_mask = (uint32_t)strtoul(env, NULL, 16);
      // PS4_PAD_AUTOPRESS_UNTIL=<ms>: stop auto-pressing after this time (lets a
      // PS4_PAD_SCRIPT drive menus afterwards without stray presses).
      const char *until = getenv("PS4_PAD_AUTOPRESS_UNTIL");
      if (until) autopress_until = strtol(until, NULL, 10);
    }
    if (autopress_mask) {
      long ms = (long)(script_us / 1000);
      if (ms % 4000 < 300 && (autopress_until < 0 || ms < autopress_until)) buttons |= autopress_mask;
    }
  }
  uint8_t lx = g_key_lx;
  uint8_t ly = g_key_ly;
  {
    // Debug aid: PS4_PAD_SCRIPT="<step>,<step>,..." with steps at absolute times since the first
    // pad read:
    //   <t_ms>:<hex mask>[:<dur_ms>[:<lx>:<ly>]]       press the buttons (and optionally set the
    //                                                   left stick) for dur_ms (default 200)
    //   <t_ms>:shake:<amp_g>[:<dur_ms>[:<hz>]]          rattle the virtual pad up and down:
    //                                                   amp_g * sin(2 pi hz t) of vertical
    //                                                   acceleration for dur_ms (default 1000, 6 Hz)
    //   <t_ms>:tilt:<roll>[:<pitch>[:<yaw>[:<turn_ms>]]]  turn the virtual pad's motion sensors,
    //                                                   from wherever it is, to this pose (degrees,
    //                                                   see pose_from_angles) over turn_ms
    //                                                   (default 500; 0 = at once) and hold it
    // e.g. "60000:8:200,61000:4000" = Options at 60 s, Cross at 61 s, and
    // "90000:tilt:90,120000:tilt:0" = held sideways (right side up) from 90 s to 120 s.
    // A connected motion-capable controller takes precedence over the virtual pad.
    // Menus auto-repeat a held direction, so use about 100 ms for one menu step (see PS4_PAD_CTL
    // above). PS4_PAD_AUTOPRESS keeps pressing for the whole run unless PS4_PAD_AUTOPRESS_UNTIL
    // ends it, and its presses then confirm whatever a menu has selected.
    typedef struct { long t, dur; uint32_t mask; int lx, ly; int tilt, shake, done; double roll, pitch, yaw, amp, hz; } PadStep;
    static PadStep steps[256];
    static int nsteps = -1;
    if (nsteps < 0) {
      nsteps = 0;
      const char *env = getenv("PS4_PAD_SCRIPT");
      while (env && *env && nsteps < 256) {
        PadStep st = {0, 200, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0};
        char *end;
        st.t = strtol(env, &end, 10);
        if (strncmp(end, ":shake:", 7) == 0) {
          st.shake = 1;
          st.dur = 1000;
          st.hz = 6;
          st.amp = strtod(end + 7, &end);
          if (*end == ':') st.dur = strtol(end + 1, &end, 10);
          if (*end == ':') st.hz = strtod(end + 1, &end);
        } else if (strncmp(end, ":tilt:", 6) == 0) {
          st.tilt = 1;
          st.dur = 500;
          st.roll = strtod(end + 6, &end);
          if (*end == ':') st.pitch = strtod(end + 1, &end);
          if (*end == ':') st.yaw = strtod(end + 1, &end);
          if (*end == ':') st.dur = strtol(end + 1, &end, 10);
        } else {
          if (*end == ':') st.mask = (uint32_t)strtoul(end + 1, &end, 16);
          if (*end == ':') st.dur = strtol(end + 1, &end, 10);
          if (*end == ':') st.lx = (int)strtol(end + 1, &end, 10);
          if (*end == ':') st.ly = (int)strtol(end + 1, &end, 10);
        }
        steps[nsteps++] = st;
        env = strchr(end, ',');
        if (env) env++;
      }
    }
    if (nsteps > 0) {
      long ms = (long)(script_us / 1000);
      // marks the start of the scripted part of a run (the walking benchmark measures after it)
      static int announced = 0;
      if (!announced && ms >= steps[0].t) {
        announced = 1;
        fprintf(stderr, "[pad] script started at %ld ms\n", ms);
      }
      for (int i = 0; i < nsteps; i++) {
        if (steps[i].shake) {
          if (!steps[i].done && ms >= steps[i].t) {
            steps[i].done = 1;
            start_shake((uint64_t)steps[i].t * 1000, (uint64_t)steps[i].dur * 1000, steps[i].amp, steps[i].hz);
          }
          continue;
        }
        if (steps[i].tilt) {
          // Started at its own time, so the pose does not depend on when the pad is read.
          if (!steps[i].done && ms >= steps[i].t) {
            steps[i].done = 1;
            start_turn((uint64_t)steps[i].t * 1000, pose_from_angles(steps[i].roll, steps[i].pitch, steps[i].yaw),
                       (uint64_t)steps[i].dur * 1000);
          }
          continue;
        }
        if (ms >= steps[i].t && ms < steps[i].t + steps[i].dur) {
          buttons |= steps[i].mask;
          if (steps[i].lx >= 0) lx = (uint8_t)steps[i].lx;
          if (steps[i].ly >= 0) ly = (uint8_t)steps[i].ly;
        }
      }
    }
  }
  uint8_t rx = g_key_rx;
  uint8_t ry = g_key_ry;
  uint8_t l2 = g_key_l2;
  uint8_t r2 = g_key_r2;
  ps4_pad_control_file(&buttons, &lx, &ly, &rx, &ry);

  PadMotionSample motion = virtual_sample(script_us);
  @autoreleasepool {
    GCController *controller = nil;
    // Automated background runs (PS4_BACKGROUND_WINDOW) take input only from the scripts above:
    // they must neither react to the user's gamepad nor have it override scripted sticks.
    static int background = -1;
    if (background < 0) background = getenv("PS4_BACKGROUND_WINDOW") != NULL;
    NSArray<GCController *> *controllers = background ? @[] : [GCController controllers];
    if (controllers.count > 0) {
      controller = controllers.firstObject;
    }

    if (controller && controller.extendedGamepad) {
      GCExtendedGamepad *gp = controller.extendedGamepad;

      if (gp.dpad.up.isPressed)
        buttons |= ORBIS_PAD_BUTTON_UP;
      if (gp.dpad.down.isPressed)
        buttons |= ORBIS_PAD_BUTTON_DOWN;
      if (gp.dpad.left.isPressed)
        buttons |= ORBIS_PAD_BUTTON_LEFT;
      if (gp.dpad.right.isPressed)
        buttons |= ORBIS_PAD_BUTTON_RIGHT;

      if (gp.buttonA.isPressed)
        buttons |= ORBIS_PAD_BUTTON_CROSS;
      if (gp.buttonB.isPressed)
        buttons |= ORBIS_PAD_BUTTON_CIRCLE;
      if (gp.buttonX.isPressed)
        buttons |= ORBIS_PAD_BUTTON_SQUARE;
      if (gp.buttonY.isPressed)
        buttons |= ORBIS_PAD_BUTTON_TRIANGLE;

      if (gp.leftShoulder.isPressed)
        buttons |= ORBIS_PAD_BUTTON_L1;
      if (gp.rightShoulder.isPressed)
        buttons |= ORBIS_PAD_BUTTON_R1;

      float l2Val = gp.leftTrigger.value;
      if (l2Val > 0.05f) {
        buttons |= ORBIS_PAD_BUTTON_L2;
        uint8_t val = (uint8_t)(l2Val * 255.0f);
        if (val > l2)
          l2 = val;
      }

      float r2Val = gp.rightTrigger.value;
      if (r2Val > 0.05f) {
        buttons |= ORBIS_PAD_BUTTON_R2;
        uint8_t val = (uint8_t)(r2Val * 255.0f);
        if (val > r2)
          r2 = val;
      }

      if (gp.leftThumbstickButton && gp.leftThumbstickButton.isPressed) {
        buttons |= ORBIS_PAD_BUTTON_L3;
      }
      if (gp.rightThumbstickButton && gp.rightThumbstickButton.isPressed) {
        buttons |= ORBIS_PAD_BUTTON_R3;
      }

      if (gp.buttonMenu.isPressed ||
          (gp.buttonOptions && gp.buttonOptions.isPressed)) {
        buttons |= ORBIS_PAD_BUTTON_OPTIONS;
      }

      float stickX = gp.leftThumbstick.xAxis.value;
      float stickY = gp.leftThumbstick.yAxis.value;
      lx = (uint8_t)((stickX + 1.0f) * 127.5f);
      ly = (uint8_t)((-stickY + 1.0f) * 127.5f);

      float rstickX = gp.rightThumbstick.xAxis.value;
      float rstickY = gp.rightThumbstick.yAxis.value;
      rx = (uint8_t)((rstickX + 1.0f) * 127.5f);
      ry = (uint8_t)((-rstickY + 1.0f) * 127.5f);
    }
    GCMotion *gm = controller ? controller.motion : nil;
    if (gm && slot->motion.on) {
      // Turn the controller's sensors on once per controller (a controller that refuses stays
      // off, and the virtual pad is used).
      static GCController *sensors_requested;
      if (sensors_requested != controller) {
        sensors_requested = controller;
        gm.sensorsActive = YES;
      }
      if (gm.sensorsActive && gm.hasRotationRate && gm.hasGravityAndUserAcceleration) {
        GCRotationRate r = gm.rotationRate;
        GCAcceleration a = gm.acceleration;
        motion.device = 1;
        motion.rate = (PadVec){r.x, r.z, -r.y};
        motion.accel = (PadVec){-a.x, -a.y, -a.z};
      }
    }
  }
  motion_update(&slot->motion, &motion, script_us, data);

  // The L2/R2 bits of a pad come from its analog triggers: a scripted or auto press of the bit
  // is a fully pulled trigger.
  if ((buttons & ORBIS_PAD_BUTTON_L2) && l2 == 0) l2 = 255;
  if ((buttons & ORBIS_PAD_BUTTON_R2) && r2 == 0) r2 = 255;
  data->buttons = buttons;
  data->leftStick.x = lx;
  data->leftStick.y = ly;
  data->rightStick.x = rx;
  data->rightStick.y = ry;
  data->analogButtons.l2 = l2;
  data->analogButtons.r2 = r2;
  data->connected = 1;
  data->count = 1;
  data->timestamp = get_time_micros();

  pthread_mutex_unlock(&g_pad_mutex);
  return 0;
}

int32_t scePadRead(int32_t handle, OrbisPadData *data, int32_t count) {
  if (!data || count <= 0)
    return -EINVAL;
  int32_t ret = scePadReadState(handle, data);
  if (ret != 0)
    return ret;
  return 1;
}

// Guest ABI Shims
void shim_scePadInit(GuestContext *ctx) {
  int32_t ret = scePadInit();
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

void shim_scePadOpen(GuestContext *ctx) {
  int32_t userId = (int32_t)ctx->rdi;
  int32_t type = (int32_t)ctx->rsi;
  int32_t index = (int32_t)ctx->rdx;
  uint64_t paramGuest = ctx->rcx;
  void *param = paramGuest ? (void *)(ctx->mem_base + paramGuest) : NULL;

  int32_t ret = scePadOpen(userId, type, index, param);
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

void shim_scePadClose(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  int32_t ret = scePadClose(handle);
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

void shim_scePadReadState(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  uint64_t dataGuest = ctx->rsi;
  OrbisPadData *data =
      dataGuest ? (OrbisPadData *)(ctx->mem_base + dataGuest) : NULL;

  int32_t ret = scePadReadState(handle, data);
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

void shim_scePadRead(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  uint64_t dataGuest = ctx->rsi;
  int32_t count = (int32_t)ctx->rdx;
  OrbisPadData *data =
      dataGuest ? (OrbisPadData *)(ctx->mem_base + dataGuest) : NULL;

  int32_t ret = scePadRead(handle, data, count);
  ctx->rax = (uint64_t)(int64_t)ret;
  SHIM_RETURN();
}

int32_t scePadGetHandle(int32_t userID, int32_t type, int32_t index) {
  pthread_mutex_lock(&g_pad_mutex);
  for (int i = 0; i < MAX_PAD_HANDLES; i++) {
    if (g_pad_slots[i].in_use && g_pad_slots[i].userId == userID) {
      int32_t h = g_pad_slots[i].handle;
      pthread_mutex_unlock(&g_pad_mutex);
      return h;
    }
  }
  pthread_mutex_unlock(&g_pad_mutex);
  return scePadOpen(userID, type, index, NULL);
}

void shim_scePadGetHandle(GuestContext *ctx) {
  int32_t userID = (int32_t)ctx->rdi;
  int32_t type = (int32_t)ctx->rsi;
  int32_t index = (int32_t)ctx->rdx;
  ctx->rax = (uint64_t)(int64_t)scePadGetHandle(userID, type, index);
  SHIM_RETURN();
}

// The three motion state calls and scePadResetOrientation act on one handle (see the motion
// notes at the top). Each logs a change of state once.
typedef enum { PAD_MOTION_SENSOR, PAD_MOTION_TILT, PAD_MOTION_DEADBAND } PadMotionSetting;

static int32_t pad_set_motion_state(int32_t handle, PadMotionSetting what, int32_t bEnable) {
  static const char *const names[] = {"scePadSetMotionSensorState", "scePadSetTiltCorrectionState",
                                      "scePadSetAngularVelocityDeadbandState"};
  pthread_mutex_lock(&g_pad_mutex);
  PadHandleSlot *slot = find_slot(handle);
  if (!slot) {
    pthread_mutex_unlock(&g_pad_mutex);
    return ORBIS_PAD_ERROR_INVALID_HANDLE;
  }
  PadMotionState *m = &slot->motion;
  int on = bEnable != 0;
  int *field = what == PAD_MOTION_SENSOR ? &m->on : what == PAD_MOTION_TILT ? &m->tilt : &m->deadband;
  if (*field != on) {
    fprintf(stderr, "[pad] %s(handle %d, %d)\n", names[what], handle, on);
    if (what == PAD_MOTION_SENSOR && on) {
      const uint64_t now = pad_script_clock_us();
      PadMotionSample s = virtual_sample(now);
      motion_reset(m, &s, now);
    }
  }
  *field = on;
  pthread_mutex_unlock(&g_pad_mutex);
  return 0;
}

int32_t scePadSetMotionSensorState(int32_t handle, int32_t bEnable) {
  return pad_set_motion_state(handle, PAD_MOTION_SENSOR, bEnable);
}

int32_t scePadSetTiltCorrectionState(int32_t handle, int32_t bEnable) {
  return pad_set_motion_state(handle, PAD_MOTION_TILT, bEnable);
}

int32_t scePadSetAngularVelocityDeadbandState(int32_t handle, int32_t bEnable) {
  return pad_set_motion_state(handle, PAD_MOTION_DEADBAND, bEnable);
}

int32_t scePadResetLightBar(int32_t handle) {
  (void)handle;
  return 0;
}

int32_t scePadResetOrientation(int32_t handle) {
  pthread_mutex_lock(&g_pad_mutex);
  PadHandleSlot *slot = find_slot(handle);
  if (!slot) {
    pthread_mutex_unlock(&g_pad_mutex);
    return ORBIS_PAD_ERROR_INVALID_HANDLE;
  }
  // The current pose becomes the identity. The gravity of the virtual pad (or, with a device,
  // the one last reported) defines the new reference up.
  const uint64_t now = pad_script_clock_us();
  PadMotionSample s = virtual_sample(now);
  if (slot->motion.on) {
    s.accel = slot->motion.last_accel;
  }
  motion_reset(&slot->motion, &s, now);
  pthread_mutex_unlock(&g_pad_mutex);
  return 0;
}

int32_t scePadSetLightBar(int32_t handle, const OrbisPadColor *pParam) {
  (void)handle;
  (void)pParam;
  return 0;
}

int32_t scePadSetVibration(int32_t handle, const OrbisPadVibrationParam *pParam) {
  (void)handle;
  (void)pParam;
  return 0;
}

int32_t scePadDeviceClassGetExtendedInformation(int32_t handle, void *pExtInfo) {
  (void)handle;
  if (pExtInfo) {
    memset(pExtInfo, 0, 64);
  }
  return 0;
}

int32_t scePadDeviceClassParseData(int32_t handle, const OrbisPadData *pData, void *pDeviceClassData) {
  (void)handle;
  (void)pData;
  if (pDeviceClassData) {
    memset(pDeviceClassData, 0, 64);
  }
  return 0;
}

int32_t scePadEnableSpecificDeviceClass(int32_t deviceClass) {
  (void)deviceClass;
  return 0;
}

void shim_scePadSetVibration(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  const OrbisPadVibrationParam *pParam = ctx->rsi ? (const OrbisPadVibrationParam *)(ctx->mem_base + ctx->rsi) : NULL;
  ctx->rax = (uint64_t)(int64_t)scePadSetVibration(handle, pParam);
  SHIM_RETURN();
}

// One connected local DualShock 4 per opened handle. The touch pad and dead-zone values are the
// ones shadPS4 reports (src/core/libraries/pad/pad.cpp, GPL-2.0-or-later).
int32_t scePadGetControllerInformation(int32_t handle, OrbisPadControllerInformation *info) {
  if (!info)
    return ORBIS_PAD_ERROR_INVALID_ARG;
  pthread_mutex_lock(&g_pad_mutex);
  int found = 0;
  for (int i = 0; i < MAX_PAD_HANDLES; i++) {
    if (g_pad_slots[i].in_use && g_pad_slots[i].handle == handle) {
      found = 1;
      break;
    }
  }
  pthread_mutex_unlock(&g_pad_mutex);
  if (!found)
    return ORBIS_PAD_ERROR_INVALID_HANDLE;
  memset(info, 0, sizeof(*info));
  info->touchPadPixelDensity = 1.0f;
  info->touchPadResolutionX = 1920;
  info->touchPadResolutionY = 950;
  info->deadZoneLeft = 1;
  info->deadZoneRight = 1;
  info->connectionType = ORBIS_PAD_CONNECTION_TYPE_LOCAL;
  info->connectedCount = 1; // matches OrbisPadData.count
  info->connected = 1;
  info->deviceClass = ORBIS_PAD_DEVICE_CLASS_STANDARD;
  return 0;
}

void shim_scePadGetControllerInformation(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  uint64_t info_addr = ctx->rsi;
  OrbisPadControllerInformation *info =
      info_addr ? (OrbisPadControllerInformation *)(ctx->mem_base + info_addr) : NULL;
  ctx->rax = (uint64_t)(int64_t)scePadGetControllerInformation(handle, info);
  SHIM_RETURN();
}

void shim_scePadSetLightBar(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  const OrbisPadColor *pParam = ctx->rsi ? (const OrbisPadColor *)(ctx->mem_base + ctx->rsi) : NULL;
  ctx->rax = (uint64_t)(int64_t)scePadSetLightBar(handle, pParam);
  SHIM_RETURN();
}

void shim_scePadResetOrientation(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  ctx->rax = (uint64_t)(int64_t)scePadResetOrientation(handle);
  SHIM_RETURN();
}

void shim_scePadResetLightBar(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  ctx->rax = (uint64_t)(int64_t)scePadResetLightBar(handle);
  SHIM_RETURN();
}

void shim_scePadSetMotionSensorState(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  int32_t bEnable = (int32_t)ctx->rsi;
  ctx->rax = (uint64_t)(int64_t)scePadSetMotionSensorState(handle, bEnable);
  SHIM_RETURN();
}

void shim_scePadSetTiltCorrectionState(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  int32_t bEnable = (int32_t)ctx->rsi;
  ctx->rax = (uint64_t)(int64_t)scePadSetTiltCorrectionState(handle, bEnable);
  SHIM_RETURN();
}

void shim_scePadSetAngularVelocityDeadbandState(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  int32_t bEnable = (int32_t)ctx->rsi;
  ctx->rax = (uint64_t)(int64_t)scePadSetAngularVelocityDeadbandState(handle, bEnable);
  SHIM_RETURN();
}

void shim_scePadDeviceClassGetExtendedInformation(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  uint64_t info_addr = ctx->rsi;
  void *info = info_addr ? (void *)(ctx->mem_base + info_addr) : NULL;
  ctx->rax = (uint64_t)(int64_t)scePadDeviceClassGetExtendedInformation(handle, info);
  SHIM_RETURN();
}

void shim_scePadDeviceClassParseData(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  const OrbisPadData *pData = ctx->rsi ? (const OrbisPadData *)(ctx->mem_base + ctx->rsi) : NULL;
  void *outData = ctx->rdx ? (void *)(ctx->mem_base + ctx->rdx) : NULL;
  ctx->rax = (uint64_t)(int64_t)scePadDeviceClassParseData(handle, pData, outData);
  SHIM_RETURN();
}

void shim_scePadEnableSpecificDeviceClass(GuestContext *ctx) {
  int32_t deviceClass = (int32_t)ctx->rdi;
  ctx->rax = (uint64_t)(int64_t)scePadEnableSpecificDeviceClass(deviceClass);
  SHIM_RETURN();
}
