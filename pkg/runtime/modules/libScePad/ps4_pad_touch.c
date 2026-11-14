// SPDX-License-Identifier: GPL-2.0-or-later
// Touch pad state of ScePadData (see ps4_pad_touch.h).
#include "ps4_pad_touch.h"

#include <string.h>

// 0..1 -> 0..max, rounded and clamped
static uint16_t touch_axis(float v, int max) {
    if (!(v > 0.0f)) return 0; // also NaN
    if (v >= 1.0f) return (uint16_t)max;
    return (uint16_t)(v * (float)max + 0.5f);
}

uint16_t ps4_pad_touch_x(float gc_x) {
    return touch_axis((gc_x + 1.0f) * 0.5f, PS4_PAD_TOUCH_RES_X - 1);
}

uint16_t ps4_pad_touch_y(float gc_y) {
    return touch_axis((1.0f - gc_y) * 0.5f, PS4_PAD_TOUCH_RES_Y - 1);
}

void ps4_pad_touch_init(ps4_pad_touch_tracker *t) {
    memset(t, 0, sizeof(*t));
    t->next_id = 1;
}

void ps4_pad_touch_update(ps4_pad_touch_tracker *t, const ps4_pad_touch_input *in, uint64_t now_us,
                          OrbisPadTouchData *out) {
    bool down[PS4_PAD_TOUCH_SOURCES];
    uint16_t x[PS4_PAD_TOUCH_SOURCES], y[PS4_PAD_TOUCH_SOURCES];
    for (int i = 0; i < 2; i++) {
        down[PS4_PAD_TOUCH_SRC_PAD0 + i] = in->pad_x[i] != 0.0f || in->pad_y[i] != 0.0f;
        x[PS4_PAD_TOUCH_SRC_PAD0 + i] = ps4_pad_touch_x(in->pad_x[i]);
        y[PS4_PAD_TOUCH_SRC_PAD0 + i] = ps4_pad_touch_y(in->pad_y[i]);
    }
    down[PS4_PAD_TOUCH_SRC_KEY_RIGHT] = in->key_right;
    x[PS4_PAD_TOUCH_SRC_KEY_RIGHT] = PS4_PAD_TOUCH_RIGHT_X;
    y[PS4_PAD_TOUCH_SRC_KEY_RIGHT] = PS4_PAD_TOUCH_HALF_Y;
    // a click needs a finger on the pad
    down[PS4_PAD_TOUCH_SRC_CLICK] =
        in->clicked && !down[PS4_PAD_TOUCH_SRC_PAD0] && !down[PS4_PAD_TOUCH_SRC_PAD1] && !in->key_right;
    x[PS4_PAD_TOUCH_SRC_CLICK] = PS4_PAD_TOUCH_LEFT_X;
    y[PS4_PAD_TOUCH_SRC_CLICK] = PS4_PAD_TOUCH_HALF_Y;

    memset(out, 0, sizeof(*out));
    for (int s = 0; s < PS4_PAD_TOUCH_SOURCES; s++) {
        // a third finger is not on the pad for the game (it gets an id once it is)
        if (!down[s] || out->fingers == ORBIS_PAD_MAX_TOUCH_NUM) {
            t->id[s] = 0;
            continue;
        }
        if (!t->id[s]) {
            t->id[s] = t->next_id;
            t->next_id = t->next_id == 127 ? 1 : t->next_id + 1;
        }
        OrbisPadTouch *p = &out->touch[out->fingers++];
        p->x = x[s];
        p->y = y[s];
        p->finger = t->id[s];
    }
    if (!out->fingers) t->down_since_us = 0;
    else if (!t->down_since_us) t->down_since_us = now_us;
    if (t->down_since_us) {
        const uint64_t held = now_us - t->down_since_us;
        out->heldTimeUs = held > UINT32_MAX ? UINT32_MAX : (uint32_t)held;
    }
}
