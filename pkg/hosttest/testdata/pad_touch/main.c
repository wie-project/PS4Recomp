// SPDX-License-Identifier: GPL-2.0-or-later
//
// Host test of the touch pad state of ScePadData (ps4_pad_touch.c). Driven by pad_touch_test.go:
// one case per run, named by argv[1]; prints PASS or FAIL lines, exits non-zero on failure.
#include "ps4_pad_touch.h"

#include <stddef.h>
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

static OrbisPadTouchData read_touch(ps4_pad_touch_tracker *t, ps4_pad_touch_input in, uint64_t now_us) {
    OrbisPadTouchData d;
    memset(&d, 0xAB, sizeof(d)); // every byte must be written
    ps4_pad_touch_update(t, &in, now_us, &d);
    return d;
}

static ps4_pad_touch_input none(void) {
    ps4_pad_touch_input in;
    memset(&in, 0, sizeof(in));
    return in;
}

static ps4_pad_touch_input finger(int slot, float x, float y) {
    ps4_pad_touch_input in = none();
    in.pad_x[slot] = x;
    in.pad_y[slot] = y;
    return in;
}

static void case_layout(void) {
    CHECK(offsetof(OrbisPadTouchData, heldTimeUs) == 4, "heldTimeUs at 4");
    CHECK(offsetof(OrbisPadTouchData, touch) == 8, "touch at 8");
    CHECK(offsetof(OrbisPadTouch, finger) == 4, "id at 4");
}

// GameController -1..1 (y up) -> 0..1919 x 0..942 (y down)
static void case_gc_mapping(void) {
    CHECK(ps4_pad_touch_x(-1.0f) == 0, "x -1 = %u", ps4_pad_touch_x(-1.0f));
    CHECK(ps4_pad_touch_x(1.0f) == 1919, "x 1 = %u", ps4_pad_touch_x(1.0f));
    CHECK(ps4_pad_touch_x(0.0f) == 960, "x 0 = %u", ps4_pad_touch_x(0.0f));
    CHECK(ps4_pad_touch_x(-0.5f) == 480, "x -0.5 = %u", ps4_pad_touch_x(-0.5f));
    CHECK(ps4_pad_touch_x(0.5f) == 1439, "x 0.5 = %u", ps4_pad_touch_x(0.5f));
    CHECK(ps4_pad_touch_y(1.0f) == 0, "y 1 (top) = %u", ps4_pad_touch_y(1.0f));
    CHECK(ps4_pad_touch_y(-1.0f) == 942, "y -1 (bottom) = %u", ps4_pad_touch_y(-1.0f));
    CHECK(ps4_pad_touch_y(0.0f) == 471, "y 0 = %u", ps4_pad_touch_y(0.0f));
    // out of range and NaN clamp
    CHECK(ps4_pad_touch_x(-1.5f) == 0 && ps4_pad_touch_x(3.0f) == 1919, "x clamp");
    CHECK(ps4_pad_touch_y(2.0f) == 0 && ps4_pad_touch_y(-2.0f) == 942, "y clamp");
    CHECK(ps4_pad_touch_x(__builtin_nanf("")) == 0, "x NaN");
    // monotonic: x grows with x, y falls with GameController y
    for (int i = -100; i < 100; i++) {
        const float a = i / 100.0f, b = (i + 1) / 100.0f;
        if (ps4_pad_touch_x(b) < ps4_pad_touch_x(a) || ps4_pad_touch_y(b) > ps4_pad_touch_y(a)) {
            CHECK(false, "not monotonic at %f", a);
            break;
        }
    }
    // a controller finger reaches the touch data
    ps4_pad_touch_tracker t;
    ps4_pad_touch_init(&t);
    OrbisPadTouchData d = read_touch(&t, finger(0, -0.5f, 0.5f), 100);
    CHECK(d.fingers == 1 && d.touch[0].x == 480 && d.touch[0].y == 236, "left upper finger: n=%u %u,%u", d.fingers,
          d.touch[0].x, d.touch[0].y);
    CHECK(d.pad1[0] == 0 && d.touch[0].pad[0] == 0 && d.touch[1].x == 0 && d.touch[1].finger == 0,
          "unused bytes are zero");
    // (0, 0) is no finger (GameController's lifted finger)
    d = read_touch(&t, none(), 200);
    CHECK(d.fingers == 0 && d.touch[0].x == 0 && d.touch[0].finger == 0 && d.heldTimeUs == 0, "lifted finger");
}

static void case_keyboard(void) {
    ps4_pad_touch_tracker t;
    ps4_pad_touch_init(&t);
    // the click key (T): a finger on the left half
    ps4_pad_touch_input in = none();
    in.clicked = true;
    OrbisPadTouchData d = read_touch(&t, in, 10);
    CHECK(d.fingers == 1 && d.touch[0].x == PS4_PAD_TOUCH_LEFT_X && d.touch[0].y == PS4_PAD_TOUCH_HALF_Y &&
              d.touch[0].finger != 0,
          "left click: n=%u %u,%u id %u", d.fingers, d.touch[0].x, d.touch[0].y, d.touch[0].finger);
    d = read_touch(&t, none(), 20);
    CHECK(d.fingers == 0, "left click released");
    // the right-half key (Backspace): a click with a finger on the right half
    in = none();
    in.key_right = true;
    in.clicked = true;
    d = read_touch(&t, in, 30);
    CHECK(d.fingers == 1 && d.touch[0].x == PS4_PAD_TOUCH_RIGHT_X && d.touch[0].y == PS4_PAD_TOUCH_HALF_Y,
          "right click: n=%u %u,%u", d.fingers, d.touch[0].x, d.touch[0].y);
    CHECK(PS4_PAD_TOUCH_LEFT_X < PS4_PAD_TOUCH_RES_X / 2 && PS4_PAD_TOUCH_RIGHT_X >= PS4_PAD_TOUCH_RES_X / 2,
          "the halves");
    // a click while a controller finger is down uses that finger, not a synthetic one
    in = finger(0, 0.6f, -0.2f);
    in.clicked = true;
    d = read_touch(&t, in, 40);
    CHECK(d.fingers == 1 && d.touch[0].x == ps4_pad_touch_x(0.6f) && d.touch[0].y == ps4_pad_touch_y(-0.2f),
          "click on a real finger: n=%u %u,%u", d.fingers, d.touch[0].x, d.touch[0].y);
}

static void case_ids(void) {
    ps4_pad_touch_tracker t;
    ps4_pad_touch_init(&t);
    // first finger: id 1, kept while it moves
    OrbisPadTouchData d = read_touch(&t, finger(0, -0.8f, 0.1f), 1000);
    CHECK(d.fingers == 1 && d.touch[0].finger == 1, "first id %u", d.touch[0].finger);
    d = read_touch(&t, finger(0, -0.2f, 0.3f), 2000);
    CHECK(d.fingers == 1 && d.touch[0].finger == 1, "moved finger keeps its id (%u)", d.touch[0].finger);
    CHECK(d.heldTimeUs == 1000, "held for 1000 us: %u", d.heldTimeUs);
    // a second finger joins: id 2
    ps4_pad_touch_input two = finger(0, -0.2f, 0.3f);
    two.pad_x[1] = 0.7f;
    two.pad_y[1] = -0.4f;
    d = read_touch(&t, two, 3000);
    CHECK(d.fingers == 2 && d.touch[0].finger == 1 && d.touch[1].finger == 2, "two fingers: ids %u %u",
          d.touch[0].finger, d.touch[1].finger);
    CHECK(d.touch[1].x == ps4_pad_touch_x(0.7f) && d.touch[1].y == ps4_pad_touch_y(-0.4f), "second finger position");
    CHECK(d.heldTimeUs == 2000, "held since the first finger: %u", d.heldTimeUs);
    // the first lifts: the second moves to touch[0] with its id
    d = read_touch(&t, finger(1, 0.7f, -0.4f), 4000);
    CHECK(d.fingers == 1 && d.touch[0].finger == 2 && d.touch[0].x == ps4_pad_touch_x(0.7f),
          "second finger in touch[0]: n=%u id %u", d.fingers, d.touch[0].finger);
    CHECK(d.heldTimeUs == 3000, "still held: %u", d.heldTimeUs);
    // a new finger in the first slot: a new id, not the lifted one's
    two.pad_x[0] = 0.1f;
    d = read_touch(&t, two, 5000);
    CHECK(d.fingers == 2 && d.touch[0].finger == 3 && d.touch[1].finger == 2, "new finger: ids %u %u",
          d.touch[0].finger, d.touch[1].finger);
    // all lift: no touch, held time 0; the next touch gets the next id and a new held time
    d = read_touch(&t, none(), 6000);
    CHECK(d.fingers == 0 && d.heldTimeUs == 0, "all lifted");
    d = read_touch(&t, finger(0, 0.3f, 0.3f), 7000);
    CHECK(d.fingers == 1 && d.touch[0].finger == 4 && d.heldTimeUs == 0, "touch after lift: id %u held %u",
          d.touch[0].finger, d.heldTimeUs);
    // keyboard clicks are fingers too: a new id for every press
    ps4_pad_touch_input click = none();
    click.clicked = true;
    read_touch(&t, none(), 8000);
    d = read_touch(&t, click, 9000);
    const uint8_t a = d.touch[0].finger;
    d = read_touch(&t, click, 9500);
    CHECK(d.touch[0].finger == a, "held click keeps its id");
    read_touch(&t, none(), 10000);
    d = read_touch(&t, click, 11000);
    CHECK(a == 5 && d.touch[0].finger == 6, "click ids %u then %u", a, d.touch[0].finger);
    // ids run 1..127 and wrap to 1
    for (int i = 0; i < 300; i++) {
        read_touch(&t, none(), 12000 + 2 * i);
        d = read_touch(&t, click, 12001 + 2 * i);
        const unsigned want = (6u + 1u + (unsigned)i - 1u) % 127u + 1u;
        if (d.touch[0].finger != want || d.touch[0].finger == 0 || d.touch[0].finger > 127) {
            CHECK(false, "touch %d: id %u, want %u", i, d.touch[0].finger, want);
            break;
        }
    }
}

// More than two fingers: the game sees two; a third gets its id when it is on the pad for it.
static void case_third_finger(void) {
    ps4_pad_touch_tracker t;
    ps4_pad_touch_init(&t);
    ps4_pad_touch_input in = finger(0, -0.5f, 0.0f);
    in.pad_x[1] = 0.5f;
    in.key_right = true;
    in.clicked = true;
    OrbisPadTouchData d = read_touch(&t, in, 1);
    CHECK(d.fingers == 2 && d.touch[0].finger == 1 && d.touch[1].finger == 2, "two of three: ids %u %u",
          d.touch[0].finger, d.touch[1].finger);
    in.pad_x[0] = 0;
    in.pad_y[0] = 0;
    d = read_touch(&t, in, 2);
    CHECK(d.fingers == 2 && d.touch[0].finger == 2 && d.touch[1].finger == 3 &&
              d.touch[1].x == PS4_PAD_TOUCH_RIGHT_X,
          "the third shows up: ids %u %u", d.touch[0].finger, d.touch[1].finger);
}

static const struct {
    const char *name;
    void (*fn)(void);
} kCases[] = {
    {"layout", case_layout},       {"gc-mapping", case_gc_mapping},     {"keyboard", case_keyboard},
    {"ids", case_ids},             {"third-finger", case_third_finger},
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
