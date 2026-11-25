// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_mouse.h"
#include "ps4_user_service.h"
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

#define USER ORBIS_USER_SERVICE_INITIAL_USER_ID

static uint64_t g_clock;

// The runtime's process clock; a counter here so report timestamps are checkable.
uint64_t sceKernelGetProcessTime(void) {
    return ++g_clock;
}

static int test_open_close(void) {
    const OrbisMouseOpenParam merged = {.behaviorFlag = ORBIS_MOUSE_OPEN_PARAM_MERGED};
    CHECK(sceMouseInit() == 0);
    CHECK(sceMouseOpen(USER + 1, 0, 0, NULL) == ORBIS_MOUSE_ERROR_INVALID_ARG);
    CHECK(sceMouseOpen(USER, 1, 0, NULL) == ORBIS_MOUSE_ERROR_INVALID_ARG);
    CHECK(sceMouseOpen(USER, 0, -1, NULL) == ORBIS_MOUSE_ERROR_INVALID_ARG);
    CHECK(sceMouseOpen(USER, 0, 2, NULL) == ORBIS_MOUSE_ERROR_INVALID_ARG);
    CHECK(sceMouseOpen(USER, 0, 1, &merged) == ORBIS_MOUSE_ERROR_ALREADY_OPENED);

    CHECK(sceMouseOpen(USER, 0, 0, &merged) == 0);
    CHECK(sceMouseOpen(USER, 0, 0, NULL) == ORBIS_MOUSE_ERROR_ALREADY_OPENED);
    CHECK(sceMouseOpen(USER, 0, 1, NULL) == 1);

    // -1 marks a free port; it is never a valid handle.
    CHECK(sceMouseClose(-1) == ORBIS_MOUSE_ERROR_INVALID_HANDLE);
    CHECK(sceMouseClose(2) == ORBIS_MOUSE_ERROR_INVALID_HANDLE);
    CHECK(sceMouseClose(0) == 0);
    CHECK(sceMouseClose(0) == ORBIS_MOUSE_ERROR_INVALID_HANDLE);
    CHECK(sceMouseClose(1) == 0);
    printf("PASS open-close\n");
    return 0;
}

static int test_read_disabled(void) {
    OrbisMouseData data[ORBIS_MOUSE_MAX_DATA_NUM + 1];
    CHECK(sceMouseInit() == 0);
    CHECK(!ps4_mouse_take_capture_request());
    CHECK(sceMouseOpen(USER, 0, 0, NULL) == 0);
    CHECK(sceMouseRead(0, NULL, 1) == ORBIS_MOUSE_ERROR_INVALID_ARG);
    CHECK(sceMouseRead(0, data, 0) == ORBIS_MOUSE_ERROR_INVALID_ARG);
    CHECK(sceMouseRead(0, data, ORBIS_MOUSE_MAX_DATA_NUM + 1) == ORBIS_MOUSE_ERROR_INVALID_ARG);
    CHECK(sceMouseRead(1, data, 1) == ORBIS_MOUSE_ERROR_INVALID_HANDLE);

    ps4_mouse_host_motion(5, 5);
    memset(data, 0xFF, sizeof(data));
    CHECK(sceMouseRead(0, data, 4) == 1);
    CHECK(!data[0].connected && data[0].buttons == 0 && data[0].x_axis == 0 && data[0].timestamp == 0);
    printf("PASS read-disabled\n");
    return 0;
}

static int test_read_host(void) {
    OrbisMouseData data[ORBIS_MOUSE_MAX_DATA_NUM];
    CHECK(sceMouseInit() == 0);
    CHECK(ps4_mouse_take_capture_request());
    CHECK(!ps4_mouse_take_capture_request());
    CHECK(sceMouseOpen(USER, 0, 0, NULL) == 0);
    CHECK(sceMouseRead(0, data, 8) == 0);

    ps4_mouse_host_motion(3, -2);
    ps4_mouse_host_button(ORBIS_MOUSE_BUTTON_PRIMARY, true);
    ps4_mouse_host_button(ORBIS_MOUSE_BUTTON_SECONDARY, true);
    ps4_mouse_host_wheel(-1, 2);
    ps4_mouse_host_button(ORBIS_MOUSE_BUTTON_PRIMARY, false);
    CHECK(sceMouseRead(0, data, ORBIS_MOUSE_MAX_DATA_NUM) == 5);
    for (int i = 0; i < 5; i++) {
        CHECK(data[i].connected);
        CHECK(i == 0 || data[i].timestamp > data[i - 1].timestamp);
    }
    CHECK(data[0].x_axis == 3 && data[0].y_axis == -2 && data[0].buttons == 0);
    CHECK(data[1].x_axis == 0 && data[1].buttons == ORBIS_MOUSE_BUTTON_PRIMARY);
    CHECK(data[2].buttons == (ORBIS_MOUSE_BUTTON_PRIMARY | ORBIS_MOUSE_BUTTON_SECONDARY));
    CHECK(data[3].wheel == -1 && data[3].tilt == 2 && data[3].buttons == data[2].buttons);
    CHECK(data[4].wheel == 0 && data[4].tilt == 0 && data[4].buttons == ORBIS_MOUSE_BUTTON_SECONDARY);

    // A full queue drops the oldest reports.
    for (int i = 0; i < ORBIS_MOUSE_MAX_DATA_NUM + 6; i++) ps4_mouse_host_motion(i, 0);
    CHECK(sceMouseRead(0, data, 2) == 2);
    CHECK(data[0].x_axis == 6 && data[1].x_axis == 7);
    CHECK(sceMouseRead(0, data, ORBIS_MOUSE_MAX_DATA_NUM) == ORBIS_MOUSE_MAX_DATA_NUM - 2);
    CHECK(data[ORBIS_MOUSE_MAX_DATA_NUM - 3].x_axis == ORBIS_MOUSE_MAX_DATA_NUM + 5);
    CHECK(sceMouseRead(0, data, 1) == 0);
    printf("PASS read-host\n");
    return 0;
}

static int test_report_before_init(void) {
    OrbisMouseData data[4];
    ps4_mouse_host_motion(1, 1);
    CHECK(sceMouseOpen(USER, 0, 0, NULL) == 0);
    CHECK(sceMouseRead(0, data, 4) == 0);
    CHECK(sceMouseInit() == 0);
    ps4_mouse_host_motion(2, 2);
    CHECK(sceMouseRead(0, data, 4) == 1 && data[0].x_axis == 2);
    printf("PASS report-before-init\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *c = argv[1];
    if (strcmp(c, "read-disabled") == 0) {
        unsetenv("PS4_MOUSE");
    } else {
        setenv("PS4_MOUSE", "1", 1);
    }
    if (strcmp(c, "open-close") == 0) return test_open_close();
    if (strcmp(c, "read-disabled") == 0) return test_read_disabled();
    if (strcmp(c, "read-host") == 0) return test_read_host();
    if (strcmp(c, "report-before-init") == 0) return test_report_before_init();
    return 2;
}
