// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_audioin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                    \
        }                                                                \
    } while (0)

#define USER 0x10000000
#define MONO ORBIS_AUDIO_IN_PARAM_FORMAT_S16_MONO
#define STEREO ORBIS_AUDIO_IN_PARAM_FORMAT_S16_STEREO

static int test_open_validate(void) {
    CHECK(sceAudioInOpen(USER, 0, 0, 0, 16000, MONO) == ORBIS_AUDIO_IN_ERROR_INVALID_SIZE);
    CHECK(sceAudioInOpen(USER, 0, 0, ORBIS_AUDIO_IN_MAX_GRAIN + 1, 16000, MONO) == ORBIS_AUDIO_IN_ERROR_INVALID_SIZE);
    CHECK(sceAudioInOpen(USER, 0, 0, 256, 16000, 1) == ORBIS_AUDIO_IN_ERROR_INVALID_PARAM);
    CHECK(sceAudioInOpen(USER, 0, 0, 256, 44100, MONO) == ORBIS_AUDIO_IN_ERROR_INVALID_FREQ);

    int32_t h[ORBIS_AUDIO_IN_NUM_PORTS];
    for (int i = 0; i < ORBIS_AUDIO_IN_NUM_PORTS; i++) {
        h[i] = sceAudioInHqOpen(USER, ORBIS_AUDIO_IN_TYPE_GENERAL, 0, 256, 48000, STEREO);
        CHECK(h[i] == (ORBIS_AUDIO_IN_HANDLE_TAG | (ORBIS_AUDIO_IN_TYPE_GENERAL << 16) | i));
    }
    CHECK(sceAudioInOpen(USER, 0, 0, 256, 48000, STEREO) == ORBIS_AUDIO_IN_ERROR_PORT_FULL);
    CHECK(sceAudioInClose(h[3]) == 0);
    // The freed port is reused, and the handle carries the new type.
    CHECK(sceAudioInOpen(USER, ORBIS_AUDIO_IN_TYPE_VOICE_CHAT, 0, 256, 16000, MONO) == (ORBIS_AUDIO_IN_HANDLE_TAG | 3));
    for (int i = 0; i < ORBIS_AUDIO_IN_NUM_PORTS; i++) {
        CHECK(sceAudioInClose(ORBIS_AUDIO_IN_HANDLE_TAG | i) == 0);
    }
    printf("PASS open-validate\n");
    return 0;
}

static int test_input_null_device(void) {
    const int32_t h = sceAudioInOpen(USER, ORBIS_AUDIO_IN_TYPE_VOICE_CHAT, 0, 256, 16000, MONO);
    CHECK(h == ORBIS_AUDIO_IN_HANDLE_TAG);
    CHECK(sceAudioInGetSilentState(h) == ORBIS_AUDIO_IN_SILENT_STATE_DEVICE_NONE);

    uint8_t buf[1024];
    memset(buf, 0xAA, sizeof(buf));
    CHECK(sceAudioInInput(h, buf) == 256);
    for (int i = 0; i < 512; i++) CHECK(buf[i] == 0);
    CHECK(buf[512] == 0xAA);

    CHECK(sceAudioInInput(h, NULL) == ORBIS_AUDIO_IN_ERROR_INVALID_POINTER);
    CHECK(sceAudioInInput(0x12345, buf) == ORBIS_AUDIO_IN_ERROR_INVALID_HANDLE);
    CHECK(sceAudioInInput(ORBIS_AUDIO_IN_HANDLE_TAG | 7, buf) == ORBIS_AUDIO_IN_ERROR_INVALID_HANDLE);
    CHECK(sceAudioInInput(ORBIS_AUDIO_IN_HANDLE_TAG | 1, buf) == ORBIS_AUDIO_IN_ERROR_NOT_OPENED);
    CHECK(sceAudioInGetSilentState(0) == ORBIS_AUDIO_IN_ERROR_INVALID_HANDLE);

    CHECK(sceAudioInClose(h) == 0);
    CHECK(sceAudioInClose(h) == ORBIS_AUDIO_IN_ERROR_NOT_OPENED);
    CHECK(sceAudioInInput(h, buf) == ORBIS_AUDIO_IN_ERROR_NOT_OPENED);
    CHECK(sceAudioInGetSilentState(h) == ORBIS_AUDIO_IN_ERROR_NOT_OPENED);
    printf("PASS input-null-device\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    // No capture device: the tests must not depend on a microphone or its permission.
    setenv("PS4_AUDIO_IN", "none", 1);
    if (strcmp(argv[1], "open-validate") == 0) return test_open_validate();
    if (strcmp(argv[1], "input-null-device") == 0) return test_input_null_device();
    return 2;
}
