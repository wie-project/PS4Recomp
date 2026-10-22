#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#include <errno.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ps4_pad.h"

#define MAX_PAD_HANDLES 4

typedef struct {
  int in_use;
  int32_t handle;
  int32_t userId;
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
__attribute__((weak)) volatile int g_ps4_screenshot_request = 0;
__attribute__((weak)) volatile int g_ps4_gnm_dump_request = 0;
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

  int found = 0;
  for (int i = 0; i < MAX_PAD_HANDLES; i++) {
    if (g_pad_slots[i].in_use && g_pad_slots[i].handle == handle) {
      found = 1;
      break;
    }
  }

  if (!found) {
    pthread_mutex_unlock(&g_pad_mutex);
    return -EINVAL;
  }

  memset(data, 0, sizeof(OrbisPadData));

  extern volatile uint64_t g_ps4_videoout_flip_count;
  uint32_t buttons = g_key_buttons;
  // Debug clock for AUTOPRESS/SCRIPT: wall time since the first pad read, or with
  // PS4_PAD_CLOCK=frames, submitted video flips converted to ms at 30 fps (so scripts
  // stay in sync with the game when the host runs slowly).
  static int pad_clock_frames = -1;
  if (pad_clock_frames < 0) {
    const char *c = getenv("PS4_PAD_CLOCK");
    pad_clock_frames = (c && strcmp(c, "frames") == 0) ? 1 : 0;
  }
  {
    // Debug aid for headless runs: PS4_PAD_AUTOPRESS=<hex button mask> holds
    // those buttons for 300 ms every 4 s (e.g. 0x4000 = Cross).
    static int autopress_init;
    static uint32_t autopress_mask;
    static struct timespec autopress_t0;
    static long autopress_until = -1;
    if (!autopress_init) {
      autopress_init = 1;
      const char *env = getenv("PS4_PAD_AUTOPRESS");
      if (env) autopress_mask = (uint32_t)strtoul(env, NULL, 16);
      // PS4_PAD_AUTOPRESS_UNTIL=<ms>: stop auto-pressing after this time (lets a
      // PS4_PAD_SCRIPT drive menus afterwards without stray presses).
      const char *until = getenv("PS4_PAD_AUTOPRESS_UNTIL");
      if (until) autopress_until = strtol(until, NULL, 10);
      clock_gettime(CLOCK_MONOTONIC, &autopress_t0);
    }
    if (autopress_mask) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      long ms = (now.tv_sec - autopress_t0.tv_sec) * 1000 + (now.tv_nsec - autopress_t0.tv_nsec) / 1000000;
      if (pad_clock_frames) ms = (long)(g_ps4_videoout_flip_count * 1000 / 30);
      if (ms % 4000 < 300 && (autopress_until < 0 || ms < autopress_until)) buttons |= autopress_mask;
    }
  }
  uint8_t lx = g_key_lx;
  uint8_t ly = g_key_ly;
  {
    // Debug aid: PS4_PAD_SCRIPT="<t_ms>:<hex mask>[:<dur_ms>[:<lx>:<ly>]],..." presses the
    // buttons (and optionally sets the left stick) at absolute times since the first pad
    // read, e.g. "60000:8:200,61000:4000" = Options at 60 s, Cross at 61 s.
    // The default hold is 200 ms. Menus auto-repeat a held direction, so use about 100 ms for one
    // menu step (see PS4_PAD_CTL above). PS4_PAD_AUTOPRESS keeps pressing for the whole run unless
    // PS4_PAD_AUTOPRESS_UNTIL ends it, and its presses then confirm whatever a menu has selected.
    typedef struct { long t, dur; uint32_t mask; int lx, ly; } PadStep;
    static PadStep steps[256];
    static int nsteps = -1;
    static struct timespec t0;
    if (nsteps < 0) {
      nsteps = 0;
      clock_gettime(CLOCK_MONOTONIC, &t0);
      const char *env = getenv("PS4_PAD_SCRIPT");
      while (env && *env && nsteps < 256) {
        PadStep st = {0, 200, 0, -1, -1};
        char *end;
        st.t = strtol(env, &end, 10);
        if (*end == ':') st.mask = (uint32_t)strtoul(end + 1, &end, 16);
        if (*end == ':') st.dur = strtol(end + 1, &end, 10);
        if (*end == ':') st.lx = (int)strtol(end + 1, &end, 10);
        if (*end == ':') st.ly = (int)strtol(end + 1, &end, 10);
        steps[nsteps++] = st;
        env = strchr(end, ',');
        if (env) env++;
      }
    }
    if (nsteps > 0) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      long ms = (now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000;
      if (pad_clock_frames) ms = (long)(g_ps4_videoout_flip_count * 1000 / 30);
      // marks the start of the scripted part of a run (the walking benchmark measures after it)
      static int announced = 0;
      if (!announced && ms >= steps[0].t) {
        announced = 1;
        fprintf(stderr, "[pad] script started at %ld ms\n", ms);
      }
      for (int i = 0; i < nsteps; i++) {
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
  }

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

int32_t scePadSetMotionSensorState(int32_t handle, int32_t bEnable) {
  (void)handle;
  (void)bEnable;
  return 0;
}

int32_t scePadSetTiltCorrectionState(int32_t handle, int32_t bEnable) {
  (void)handle;
  (void)bEnable;
  return 0;
}

int32_t scePadSetAngularVelocityDeadbandState(int32_t handle, int32_t bEnable) {
  (void)handle;
  (void)bEnable;
  return 0;
}

int32_t scePadResetLightBar(int32_t handle) {
  (void)handle;
  return 0;
}

int32_t scePadResetOrientation(int32_t handle) {
  (void)handle;
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
