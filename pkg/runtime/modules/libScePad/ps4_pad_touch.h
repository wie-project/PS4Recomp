// SPDX-License-Identifier: GPL-2.0-or-later
//
// DualShock 4 touch pad state of ScePadData (scePadReadState) from the host's inputs. Plain C:
// the host tests drive it.
//
// Facts:
// - The DualShock 4 touch pad reports x 0..1919 and y 0..942 (top left 0, 0), two fingers at
//   most; scePadGetControllerInformation gives 1920 x 943 and 44.86 dots/mm (as the gravityrush_pc
//   port reports them; shadPS4 reports 1920 x 942 and a density of 1).
// - Each finger that touches gets a new id 1..127 (after 127 comes 1 again); it keeps the id
//   while it stays down. touchNum counts the fingers down and touch[0] is always used first: when
//   the first finger lifts, the second one moves to touch[0] with its id (shadPS4 pad.cpp and
//   input/controller.cpp, gravityrush_pc src/runtime_pad.c).
// - The 32-bit word after touchNum is the time in microseconds since a finger first touched (0
//   with no finger down); shadPS4 fills it for games built with SDK 3.50 or later.
// - GameController gives a DualShock 4 / DualSense finger as a direction pad (physicalInputProfile
//   GCInputDualShockTouchpadOne / Two): x and y in -1..1 with y up, (0, 0) when no finger is down
//   (SDL's MFi backend, src/joystick/apple/SDL_mfijoystick.m, reads it the same way).
// - Clicking the touch pad takes a finger on it: a click from a source without touch points (the
//   keyboard's click key, a script, a controller without a touch surface) is a finger on the left
//   half. The keyboard's right-half key is a click with a finger on the right half (gravityrush_pc
//   touch_click: x 480 or 1440, y 471).
#ifndef PS4_PAD_TOUCH_H
#define PS4_PAD_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_PAD_MAX_TOUCH_NUM 2
#define PS4_PAD_TOUCH_RES_X 1920
#define PS4_PAD_TOUCH_RES_Y 943
#define PS4_PAD_TOUCH_DENSITY 44.86f
#define PS4_PAD_TOUCH_LEFT_X 480
#define PS4_PAD_TOUCH_RIGHT_X 1440
#define PS4_PAD_TOUCH_HALF_Y 471

typedef struct OrbisPadTouch {
    uint16_t x, y;
    uint8_t finger; // the finger's id
    uint8_t pad[3];
} OrbisPadTouch;

typedef struct OrbisPadTouchData {
    uint8_t fingers; // touchNum
    uint8_t pad1[3];
    uint32_t heldTimeUs; // microseconds since a finger first touched, 0 = none down
    OrbisPadTouch touch[ORBIS_PAD_MAX_TOUCH_NUM];
} OrbisPadTouchData;

_Static_assert(sizeof(OrbisPadTouch) == 8, "ScePadTouch is 8 bytes");
_Static_assert(sizeof(OrbisPadTouchData) == 24, "ScePadTouchData is 24 bytes");

// A GameController touch pad axis value (-1..1) -> touch coordinate. y is up in GameController
// and down on the PS4.
uint16_t ps4_pad_touch_x(float gc_x);
uint16_t ps4_pad_touch_y(float gc_y);

// The host's touch inputs of one read.
typedef struct ps4_pad_touch_input {
    // the controller's fingers (GCInputDualShockTouchpadOne / Two) as GameController gives them:
    // (0, 0) = not down; 0 for a controller without a touch pad
    float pad_x[2], pad_y[2];
    bool key_right; // the keyboard's right-half click key is held
    bool clicked;   // the touch pad button is pressed (by any source)
} ps4_pad_touch_input;

// Touch sources in the order they fill touch[]: the controller's two fingers, the right-half key,
// the finger of a click without other touch points.
enum { PS4_PAD_TOUCH_SRC_PAD0, PS4_PAD_TOUCH_SRC_PAD1, PS4_PAD_TOUCH_SRC_KEY_RIGHT, PS4_PAD_TOUCH_SRC_CLICK,
       PS4_PAD_TOUCH_SOURCES };

// Finger ids across reads.
typedef struct ps4_pad_touch_tracker {
    uint8_t id[PS4_PAD_TOUCH_SOURCES]; // id of each source's finger, 0 = not down
    uint8_t next_id;                   // 1..127
    uint64_t down_since_us;            // when a finger first touched, 0 = none down
} ps4_pad_touch_tracker;

void ps4_pad_touch_init(ps4_pad_touch_tracker *t);
// Fills out from the inputs read at now_us (microseconds, any monotonic clock, > 0).
void ps4_pad_touch_update(ps4_pad_touch_tracker *t, const ps4_pad_touch_input *in, uint64_t now_us,
                          OrbisPadTouchData *out);

#ifdef __cplusplus
}
#endif

#endif // PS4_PAD_TOUCH_H
