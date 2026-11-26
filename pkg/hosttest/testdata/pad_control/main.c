// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_pad.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

volatile uint64_t g_ps4_videoout_flip_count;
volatile int g_ps4_screenshot_request;
volatile int g_ps4_gnm_dump_request;
volatile int g_ps4_gnm_gpu_capture_request;

void ps4_metal_screen_request_close(void) { abort(); }
void ps4_metal_screen_request_terminate(void) { abort(); }

static OrbisPadData ReadAtFlip(int32_t handle, uint64_t flip) {
    __atomic_store_n(&g_ps4_videoout_flip_count, flip, __ATOMIC_RELAXED);
    OrbisPadData data;
    assert(scePadReadState(handle, &data) == 0);
    return data;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    FILE* control = fopen(argv[1], "w");
    assert(control);
    assert(setenv("PS4_PAD_CTL", argv[1], 1) == 0);
    assert(setenv("PS4_BACKGROUND_WINDOW", "1", 1) == 0);
    assert(setenv("PS4_PAD_CLOCK", "frames", 1) == 0);
    int32_t handle = scePadOpen(1, 0, 0, NULL);
    assert(handle > 0);
    assert(ReadAtFlip(handle, 100).buttons == 0);

    assert(fputs("pf 4000 3 16 32 64 96\nwf 2\nshot\np 2000 30000\n", control) >= 0);
    assert(fclose(control) == 0);

    OrbisPadData held = ReadAtFlip(handle, 100);
    assert(held.buttons == ORBIS_PAD_BUTTON_CROSS);
    assert(held.leftStick.x == 16 && held.leftStick.y == 32);
    assert(held.rightStick.x == 64 && held.rightStick.y == 96);
    usleep(20000);
    assert(ReadAtFlip(handle, 100).buttons == ORBIS_PAD_BUTTON_CROSS);
    assert(ReadAtFlip(handle, 102).buttons == ORBIS_PAD_BUTTON_CROSS);

    OrbisPadData neutral = ReadAtFlip(handle, 103);
    assert(neutral.buttons == 0 && neutral.rightStick.x == 128 && neutral.rightStick.y == 128);
    assert(ReadAtFlip(handle, 104).buttons == 0);
    assert(g_ps4_screenshot_request == 0);

    assert(ReadAtFlip(handle, 105).buttons == ORBIS_PAD_BUTTON_CIRCLE);
    assert(g_ps4_screenshot_request == 1);
    assert(ReadAtFlip(handle, 100105).buttons == ORBIS_PAD_BUTTON_CIRCLE);
    return 0;
}
