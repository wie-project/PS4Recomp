#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#include <errno.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void ps4_pad_handle_key(unsigned short keyCode, int isDown) {
  pthread_mutex_lock(&g_pad_mutex);

  uint32_t btn = 0;
  switch (keyCode) {
  case 126: // Up Arrow
  case 13:  // W
    btn = ORBIS_PAD_BUTTON_UP;
    break;
  case 125: // Down Arrow
  case 1:   // S
    btn = ORBIS_PAD_BUTTON_DOWN;
    break;
  case 123: // Left Arrow
  case 0:   // A
    btn = ORBIS_PAD_BUTTON_LEFT;
    break;
  case 124: // Right Arrow
  case 2:   // D
    btn = ORBIS_PAD_BUTTON_RIGHT;
    break;
  case 34: // I
  case 16: // Y
    btn = ORBIS_PAD_BUTTON_TRIANGLE;
    break;
  case 37: // L
  case 31: // O
    btn = ORBIS_PAD_BUTTON_CIRCLE;
    break;
  case 49: // Space
  case 40: // K
  case 36: // Return
    btn = ORBIS_PAD_BUTTON_CROSS;
    break;
  case 38: // J
  case 32: // U
    btn = ORBIS_PAD_BUTTON_SQUARE;
    break;
  case 12: // Q
    btn = ORBIS_PAD_BUTTON_L1;
    break;
  case 14: // E
    btn = ORBIS_PAD_BUTTON_R1;
    break;
  case 18: // 1
    btn = ORBIS_PAD_BUTTON_L2;
    g_key_l2 = isDown ? 255 : 0;
    break;
  case 20: // 3
    btn = ORBIS_PAD_BUTTON_R2;
    g_key_r2 = isDown ? 255 : 0;
    break;
  case 6: // Z
    btn = ORBIS_PAD_BUTTON_L3;
    break;
  case 8: // C
    btn = ORBIS_PAD_BUTTON_R3;
    break;
  case 48: // Tab
    btn = ORBIS_PAD_BUTTON_OPTIONS;
    break;
  case 17: // T
  case 35: // P
    btn = ORBIS_PAD_BUTTON_TOUCH_PAD;
    break;
  default:
    break;
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

  uint32_t buttons = g_key_buttons;
  uint8_t lx = g_key_lx;
  uint8_t ly = g_key_ly;
  uint8_t rx = g_key_rx;
  uint8_t ry = g_key_ry;
  uint8_t l2 = g_key_l2;
  uint8_t r2 = g_key_r2;

  @autoreleasepool {
    GCController *controller = nil;
    NSArray<GCController *> *controllers = [GCController controllers];
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
