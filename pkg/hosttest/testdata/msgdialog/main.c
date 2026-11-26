// SPDX-License-Identifier: GPL-2.0-or-later

// libSceMsgDialog against shadPS4's msgdialog.cpp / msgdialog_ui.cpp: the state machine and
// error codes, the parameter copy, the button-to-result mapping, the progress bar calls and the
// headless answers. The AppKit window is replaced by recording hooks.

#include "ps4_common_dialog.h"
#include "ps4_msg_dialog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#define A(p) ((uint64_t)(uintptr_t)(p))

// ---------------------------------------------------------------------------------------------
// Window hooks

static bool g_ui_available = true;
static int g_shown, g_dismissed, g_progress_calls;
static MsgDialogView g_view;
static char g_view_msg[256], g_view_b1[64], g_view_b2[64];
static char g_progress_msg[256];
static uint32_t g_progress_value;

bool ps4_msg_dialog_ui_show(const MsgDialogView *view) {
    if (!g_ui_available) return false;
    g_shown++;
    g_view = *view;
    snprintf(g_view_msg, sizeof(g_view_msg), "%s", view->msg ? view->msg : "(null)");
    snprintf(g_view_b1, sizeof(g_view_b1), "%s", view->button1 ? view->button1 : "(null)");
    snprintf(g_view_b2, sizeof(g_view_b2), "%s", view->button2 ? view->button2 : "(null)");
    return true;
}

void ps4_msg_dialog_ui_progress(uint32_t serial, const char *msg, uint32_t value) {
    if (serial != g_view.serial) return;
    g_progress_calls++;
    snprintf(g_progress_msg, sizeof(g_progress_msg), "%s", msg);
    g_progress_value = value;
}

void ps4_msg_dialog_ui_dismiss(void) {
    g_dismissed++;
}

// ---------------------------------------------------------------------------------------------
// Parameter builders (host pointers, guest memory base 0)

static OrbisMsgDialogParam g_param;
static OrbisMsgDialogUserMessageParam g_user;
static OrbisMsgDialogButtonsParam g_buttons;
static OrbisMsgDialogProgressBarParam g_bar;
static OrbisMsgDialogSystemMessageParam g_sys;

static void base_param(uint32_t mode) {
    memset(&g_param, 0, sizeof(g_param));
    g_param.baseParam.size = sizeof(g_param.baseParam);
    g_param.baseParam.magic = 0xC0D1A109;
    g_param.size = sizeof(g_param);
    g_param.mode = mode;
    g_param.userId = 1;
}

static const OrbisMsgDialogParam *user_param(uint32_t type, const char *msg) {
    base_param(MSG_DIALOG_MODE_USER_MSG);
    memset(&g_user, 0, sizeof(g_user));
    g_user.buttonType = type;
    g_user.msg = A(msg);
    g_param.userMsgParam = A(&g_user);
    return &g_param;
}

static const OrbisMsgDialogParam *bar_param(uint32_t type, const char *msg) {
    base_param(MSG_DIALOG_MODE_PROGRESS_BAR);
    memset(&g_bar, 0, sizeof(g_bar));
    g_bar.barType = type;
    g_bar.msg = A(msg);
    g_param.progBarParam = A(&g_bar);
    return &g_param;
}

static const OrbisMsgDialogParam *sys_param(uint32_t type) {
    base_param(MSG_DIALOG_MODE_SYSTEM_MSG);
    memset(&g_sys, 0, sizeof(g_sys));
    g_sys.sysMsgType = type;
    g_param.sysMsgParam = A(&g_sys);
    return &g_param;
}

static int open_user(uint32_t type, const char *msg) {
    return sceMsgDialogOpen(NULL, user_param(type, msg));
}

static bool result_is(uint32_t button, int32_t result) {
    OrbisMsgDialogResult r;
    memset(&r, 0xAA, sizeof(r));
    if (sceMsgDialogGetResult(&r) != 0) return false;
    static const char zero[32];
    return r.mode == 0 && r.buttonId == button && r.result == result && memcmp(r.reserved, zero, 32) == 0;
}

// ---------------------------------------------------------------------------------------------
// Cases

static int test_state(void) {
    CHECK(sceMsgDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED);
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceMsgDialogGetStatus() == MSG_DIALOG_STATUS_NONE);
    CHECK(open_user(MSG_DIALOG_BUTTON_TYPE_OK, "x") == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    CHECK(sceMsgDialogOpen(NULL, NULL) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE); // state first
    CHECK(sceMsgDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(sceMsgDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceMsgDialogGetResult(NULL) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED); // state first

    // Another dialog library holds the common dialog system.
    CHECK(ps4_common_dialog_acquire());
    CHECK(sceMsgDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_BUSY);
    ps4_common_dialog_release();

    CHECK(sceMsgDialogInitialize() == 0);
    CHECK(sceCommonDialogIsUsed());
    CHECK(sceMsgDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED);
    CHECK(sceMsgDialogUpdateStatus() == MSG_DIALOG_STATUS_INITIALIZED);
    CHECK(sceMsgDialogOpen(NULL, NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    CHECK(sceMsgDialogGetResult(NULL) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);

    // The game closes a running dialog: OK with no button.
    char msg[] = "Saving...";
    CHECK(open_user(MSG_DIALOG_BUTTON_TYPE_OK, msg) == 0);
    CHECK(g_shown == 1);
    msg[0] = 'X'; // the library keeps its own copy
    CHECK(strcmp(g_view.msg, "Saving...") == 0);
    CHECK(sceMsgDialogUpdateStatus() == MSG_DIALOG_STATUS_RUNNING);
    CHECK(open_user(MSG_DIALOG_BUTTON_TYPE_OK, "again") == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    CHECK(sceMsgDialogGetResult(NULL) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);
    CHECK(sceMsgDialogClose() == 0);
    CHECK(g_dismissed == 1);
    CHECK(sceMsgDialogGetStatus() == MSG_DIALOG_STATUS_FINISHED);
    CHECK(sceMsgDialogGetResult(NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    CHECK(result_is(MSG_DIALOG_BUTTON_ID_INVALID, MSG_DIALOG_RESULT_OK));
    CHECK(sceMsgDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);

    // Reopened from FINISHED; a window of an earlier dialog is ignored.
    CHECK(open_user(MSG_DIALOG_BUTTON_TYPE_YESNO, "Quit?") == 0);
    ps4_msg_dialog_finish(g_view.serial - 1, MSG_DIALOG_BUTTON_ID_NO, MSG_DIALOG_RESULT_OK);
    CHECK(sceMsgDialogGetStatus() == MSG_DIALOG_STATUS_RUNNING);
    ps4_msg_dialog_finish(g_view.serial, MSG_DIALOG_BUTTON_ID_NO, MSG_DIALOG_RESULT_OK);
    CHECK(sceMsgDialogGetStatus() == MSG_DIALOG_STATUS_FINISHED);
    CHECK(result_is(MSG_DIALOG_BUTTON_ID_NO, MSG_DIALOG_RESULT_OK));

    // Terminate closes a running dialog and frees the common dialog system.
    CHECK(open_user(MSG_DIALOG_BUTTON_TYPE_NONE, "Please wait") == 0);
    CHECK(sceMsgDialogTerminate() == 0);
    CHECK(g_dismissed == 2);
    CHECK(sceMsgDialogGetStatus() == MSG_DIALOG_STATUS_NONE);
    CHECK(!sceCommonDialogIsUsed());
    CHECK(sceMsgDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    printf("PASS state\n");
    return 0;
}

static int test_user_view(void) {
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceMsgDialogInitialize() == 0);
    base_param(MSG_DIALOG_MODE_USER_MSG);
    memset(&g_user, 0, sizeof(g_user));
    g_user.buttonType = MSG_DIALOG_BUTTON_TYPE_2BUTTONS;
    g_user.msg = A("Pick one");
    g_buttons.msg1 = A("Left");
    g_buttons.msg2 = A("Right");
    g_user.buttonsParam = A(&g_buttons);
    g_param.userMsgParam = A(&g_user);
    CHECK(sceMsgDialogOpen(NULL, &g_param) == 0);
    CHECK(g_view.mode == MSG_DIALOG_MODE_USER_MSG && g_view.type == MSG_DIALOG_BUTTON_TYPE_2BUTTONS);
    CHECK(strcmp(g_view_msg, "Pick one") == 0);
    CHECK(strcmp(g_view_b1, "Left") == 0 && strcmp(g_view_b2, "Right") == 0);
    CHECK(sceMsgDialogClose() == 0);

    // Other types copy no button labels; progress bar calls are refused outside a progress bar.
    CHECK(open_user(MSG_DIALOG_BUTTON_TYPE_OK_CANCEL, "Continue?") == 0);
    CHECK(strcmp(g_view_b1, "(null)") == 0 && strcmp(g_view_b2, "(null)") == 0);
    CHECK(sceMsgDialogProgressBarSetValue(0, 5) == ORBIS_COMMON_DIALOG_ERROR_NOT_SUPPORTED);
    CHECK(sceMsgDialogProgressBarInc(1, 5) == ORBIS_COMMON_DIALOG_ERROR_NOT_SUPPORTED); // mode first
    CHECK(sceMsgDialogClose() == 0);
    CHECK(sceMsgDialogProgressBarSetMsg(0, "x") == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(sceMsgDialogTerminate() == 0);
    printf("PASS user-view\n");
    return 0;
}

static int test_progress_bar(void) {
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceMsgDialogInitialize() == 0);
    CHECK(sceMsgDialogOpen(NULL, bar_param(MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE_CANCEL, "Copying")) == 0);
    CHECK(g_view.mode == MSG_DIALOG_MODE_PROGRESS_BAR);
    CHECK(g_view.type == MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE_CANCEL);
    CHECK(strcmp(g_view_msg, "Copying") == 0);

    CHECK(sceMsgDialogProgressBarSetValue(1, 10) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    CHECK(g_progress_calls == 0);
    CHECK(sceMsgDialogProgressBarSetValue(0, 10) == 0);
    CHECK(g_progress_value == 10 && strcmp(g_progress_msg, "Copying") == 0);
    CHECK(sceMsgDialogProgressBarInc(0, 25) == 0);
    CHECK(g_progress_value == 35);
    CHECK(sceMsgDialogProgressBarSetMsg(0, "Almost done") == 0);
    CHECK(g_progress_value == 35 && strcmp(g_progress_msg, "Almost done") == 0);
    CHECK(sceMsgDialogProgressBarInc(0, 100) == 0); // not clamped, as in shadPS4
    CHECK(g_progress_value == 135);
    CHECK(g_progress_calls == 4);

    // The player cancels.
    ps4_msg_dialog_finish(g_view.serial, MSG_DIALOG_BUTTON_ID_INVALID, MSG_DIALOG_RESULT_USER_CANCELED);
    CHECK(result_is(MSG_DIALOG_BUTTON_ID_INVALID, MSG_DIALOG_RESULT_USER_CANCELED));
    CHECK(sceMsgDialogProgressBarInc(0, 1) == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);

    // A new dialog starts from 0 with a clean result.
    CHECK(sceMsgDialogOpen(NULL, bar_param(MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE, "Loading")) == 0);
    CHECK(sceMsgDialogProgressBarInc(0, 3) == 0);
    CHECK(g_progress_value == 3);
    CHECK(sceMsgDialogGetResult(NULL) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);
    CHECK(sceMsgDialogTerminate() == 0);
    printf("PASS progress-bar\n");
    return 0;
}

static int test_system_msg(void) {
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceMsgDialogInitialize() == 0);
    CHECK(sceMsgDialogOpen(NULL, sys_param(MSG_DIALOG_SYSMSG_TYPE_CAMERA_NOT_CONNECTED)) == 0);
    CHECK(g_view.mode == MSG_DIALOG_MODE_SYSTEM_MSG);
    CHECK(g_view.type == MSG_DIALOG_SYSMSG_TYPE_CAMERA_NOT_CONNECTED);
    CHECK(g_view.msg == NULL);
    ps4_msg_dialog_finish(g_view.serial, MSG_DIALOG_BUTTON_ID_OK, MSG_DIALOG_RESULT_OK);
    CHECK(result_is(MSG_DIALOG_BUTTON_ID_OK, MSG_DIALOG_RESULT_OK));
    CHECK(sceMsgDialogTerminate() == 0);
    printf("PASS system-msg\n");
    return 0;
}

// Without a window the focused button is pressed; dialogs without a button keep running.
static int test_headless(void) {
    g_ui_available = false;
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceMsgDialogInitialize() == 0);
    static const struct {
        uint32_t type;
        uint32_t button;
        int32_t result;
    } k_cases[] = {
        {MSG_DIALOG_BUTTON_TYPE_OK, MSG_DIALOG_BUTTON_ID_OK, MSG_DIALOG_RESULT_OK},
        {MSG_DIALOG_BUTTON_TYPE_YESNO, MSG_DIALOG_BUTTON_ID_YES, MSG_DIALOG_RESULT_OK},
        {MSG_DIALOG_BUTTON_TYPE_OK_CANCEL, MSG_DIALOG_BUTTON_ID_OK, MSG_DIALOG_RESULT_OK},
        {MSG_DIALOG_BUTTON_TYPE_WAIT, MSG_DIALOG_BUTTON_ID_BUTTON1, MSG_DIALOG_RESULT_OK},
        {MSG_DIALOG_BUTTON_TYPE_WAIT_CANCEL, MSG_DIALOG_BUTTON_ID_BUTTON1, MSG_DIALOG_RESULT_OK},
        {MSG_DIALOG_BUTTON_TYPE_YESNO_FOCUS_NO, MSG_DIALOG_BUTTON_ID_NO, MSG_DIALOG_RESULT_OK},
        {MSG_DIALOG_BUTTON_TYPE_OK_CANCEL_FOCUS_CANCEL, MSG_DIALOG_BUTTON_ID_INVALID,
         MSG_DIALOG_RESULT_USER_CANCELED},
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        CHECK(open_user(k_cases[i].type, "headless") == 0);
        CHECK(sceMsgDialogGetStatus() == MSG_DIALOG_STATUS_FINISHED);
        CHECK(result_is(k_cases[i].button, k_cases[i].result));
    }
    CHECK(sceMsgDialogOpen(NULL, sys_param(MSG_DIALOG_SYSMSG_TYPE_TRC_EMPTY_STORE)) == 0);
    CHECK(result_is(MSG_DIALOG_BUTTON_ID_OK, MSG_DIALOG_RESULT_OK));

    CHECK(open_user(MSG_DIALOG_BUTTON_TYPE_NONE, "busy") == 0);
    CHECK(sceMsgDialogGetStatus() == MSG_DIALOG_STATUS_RUNNING);
    CHECK(sceMsgDialogClose() == 0);
    CHECK(sceMsgDialogOpen(NULL, bar_param(MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE_CANCEL, "bar")) == 0);
    CHECK(sceMsgDialogGetStatus() == MSG_DIALOG_STATUS_RUNNING);
    CHECK(sceMsgDialogClose() == 0);
    CHECK(sceMsgDialogTerminate() == 0);
    printf("PASS headless\n");
    return 0;
}

// Parameters shadPS4 asserts on: the process stops.
static int test_bad_param(const char *which) {
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceMsgDialogInitialize() == 0);
    const OrbisMsgDialogParam *p = user_param(MSG_DIALOG_BUTTON_TYPE_OK, "x");
    if (strcmp(which, "size") == 0) g_param.size = 8;
    if (strcmp(which, "mode") == 0) g_param.mode = 7;
    if (strcmp(which, "sysmsg") == 0) p = sys_param(3);
    sceMsgDialogOpen(NULL, p);
    printf("sceMsgDialogOpen returned\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *c = argv[1];
    if (strcmp(c, "state") == 0) return test_state();
    if (strcmp(c, "user-view") == 0) return test_user_view();
    if (strcmp(c, "progress-bar") == 0) return test_progress_bar();
    if (strcmp(c, "system-msg") == 0) return test_system_msg();
    if (strcmp(c, "headless") == 0) return test_headless();
    if (strncmp(c, "bad-", 4) == 0) return test_bad_param(c + 4);
    return 2;
}
