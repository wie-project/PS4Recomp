// SPDX-License-Identifier: GPL-2.0-or-later

// libSceMsgDialog, ported from shadPS4 (src/core/libraries/system/msgdialog.cpp and
// msgdialog_ui.cpp): the same state machine, error codes, parameter copy and result, the
// progress bar calls and the button-to-result mapping. The window is a non-modal AppKit panel
// (ps4_msg_dialog_ui.m) instead of shadPS4's ImGui layer: the game keeps running and polls
// sceMsgDialogUpdateStatus while it is up.
//
// Without a window (headless run) nobody can press a button, so Open presses the one the
// console focuses first (OK, Yes, No for the *_FOCUS_NO types, ...). A dialog without buttons
// (NONE, progress bars) stays RUNNING until the game closes it, as it would on screen.

#include "ps4_msg_dialog.h"
#include "ps4_common_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_status = MSG_DIALOG_STATUS_NONE;
static OrbisMsgDialogResult g_result;
static uint32_t g_serial;
static MsgDialogView g_view;
static char *g_msg; // owned copies behind g_view
static char *g_button1;
static char *g_button2;
static uint32_t g_progress;

// The buttons of a user message (shadPS4 msgdialog_ui.cpp user_button_texts): how many, and
// whether the first one has the focus when the dialog opens. TWO_BUTTONS uses the game's labels.
static const struct {
    uint8_t count;
    bool focus_first;
} k_user_buttons[] = {
    {1, true},  // OK
    {2, true},  // YESNO
    {0, true},  // NONE
    {2, true},  // OK_CANCEL
    {0, true},  // 4: undefined, no button
    {1, true},  // WAIT
    {2, true},  // WAIT_CANCEL
    {2, false}, // YESNO_FOCUS_NO
    {2, false}, // OK_CANCEL_FOCUS_CANCEL
    {2, false}, // 2BUTTONS
};

static void *gaddr(uint8_t *mem, uint64_t addr) {
    return addr ? (void *)((uintptr_t)mem + addr) : NULL;
}

// shadPS4 asserts (or builds a std::string from NULL) where the console would reject a broken
// parameter; the process stops the same way here instead of guessing a result.
static void fatal(const char *what) {
    fprintf(stderr, "[ps4-msg-dialog] %s\n", what);
    abort();
}

static char *copy_guest_string(uint8_t *mem, uint64_t addr, const char *what) {
    const char *s = gaddr(mem, addr);
    if (!s) fatal(what);
    char *copy = strdup(s);
    if (!copy) fatal("out of memory");
    return copy;
}

static void reset_view_locked(void) {
    free(g_msg);
    free(g_button1);
    free(g_button2);
    g_msg = g_button1 = g_button2 = NULL;
    memset(&g_view, 0, sizeof(g_view));
    g_progress = 0;
}

// Moves a running dialog to FINISHED (shadPS4 MsgDialogUi::Finish); false when none runs.
static bool finish_locked(uint32_t button_id, int32_t result) {
    if (g_status != MSG_DIALOG_STATUS_RUNNING) return false;
    g_result.result = result;
    g_result.buttonId = button_id;
    g_status = MSG_DIALOG_STATUS_FINISHED;
    return true;
}

void ps4_msg_dialog_finish(uint32_t serial, uint32_t button_id, int32_t result) {
    pthread_mutex_lock(&g_mu);
    if (serial == g_view.serial) finish_locked(button_id, result);
    pthread_mutex_unlock(&g_mu);
}

// The result of pressing the second button of a two-button user message (shadPS4 DrawUser).
static int32_t second_button_result(uint32_t type, uint32_t *button_id) {
    switch (type) {
    case MSG_DIALOG_BUTTON_TYPE_OK_CANCEL:
    case MSG_DIALOG_BUTTON_TYPE_WAIT_CANCEL:
    case MSG_DIALOG_BUTTON_TYPE_OK_CANCEL_FOCUS_CANCEL:
        *button_id = MSG_DIALOG_BUTTON_ID_INVALID;
        return MSG_DIALOG_RESULT_USER_CANCELED;
    default:
        *button_id = MSG_DIALOG_BUTTON_ID_BUTTON2;
        return MSG_DIALOG_RESULT_OK;
    }
}

// The headless answer: the focused button of a user or system message.
static void press_default_button(const MsgDialogView *v) {
    if (v->mode == MSG_DIALOG_MODE_SYSTEM_MSG) {
        ps4_msg_dialog_finish(v->serial, MSG_DIALOG_BUTTON_ID_OK, MSG_DIALOG_RESULT_OK);
        return;
    }
    if (v->mode != MSG_DIALOG_MODE_USER_MSG || k_user_buttons[v->type].count == 0) return;
    if (k_user_buttons[v->type].count == 1 || k_user_buttons[v->type].focus_first) {
        ps4_msg_dialog_finish(v->serial, MSG_DIALOG_BUTTON_ID_BUTTON1, MSG_DIALOG_RESULT_OK);
        return;
    }
    uint32_t button_id;
    const int32_t result = second_button_result(v->type, &button_id);
    ps4_msg_dialog_finish(v->serial, button_id, result);
}

int32_t sceMsgDialogInitialize(void) {
    if (!ps4_common_dialog_system_initialized()) return ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED;
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status != MSG_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED;
    } else if (!ps4_common_dialog_acquire()) {
        rc = ORBIS_COMMON_DIALOG_ERROR_BUSY;
    } else {
        g_status = MSG_DIALOG_STATUS_INITIALIZED;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// Copies the parameter into the view (shadPS4 MsgDialogState). Called with g_mu held.
static void capture_locked(uint8_t *mem, const OrbisMsgDialogParam *p) {
    g_view.mode = p->mode;
    switch (p->mode) {
    case MSG_DIALOG_MODE_USER_MSG: {
        const OrbisMsgDialogUserMessageParam *u = gaddr(mem, p->userMsgParam);
        if (!u) fatal("sceMsgDialogOpen: user message mode without userMsgParam");
        if (u->buttonType > MSG_DIALOG_BUTTON_TYPE_2BUTTONS) fatal("sceMsgDialogOpen: unknown button type");
        g_view.type = u->buttonType;
        g_msg = copy_guest_string(mem, u->msg, "user message without msg");
        if (u->buttonType == MSG_DIALOG_BUTTON_TYPE_2BUTTONS) {
            const OrbisMsgDialogButtonsParam *b = gaddr(mem, u->buttonsParam);
            if (!b) fatal("sceMsgDialogOpen: 2BUTTONS without buttonsParam");
            g_button1 = copy_guest_string(mem, b->msg1, "2BUTTONS without msg1");
            g_button2 = copy_guest_string(mem, b->msg2, "2BUTTONS without msg2");
        }
        break;
    }
    case MSG_DIALOG_MODE_PROGRESS_BAR: {
        const OrbisMsgDialogProgressBarParam *pb = gaddr(mem, p->progBarParam);
        if (!pb) fatal("sceMsgDialogOpen: progress bar mode without progBarParam");
        g_view.type = pb->barType;
        g_msg = copy_guest_string(mem, pb->msg, "progress bar without msg");
        break;
    }
    case MSG_DIALOG_MODE_SYSTEM_MSG: {
        const OrbisMsgDialogSystemMessageParam *s = gaddr(mem, p->sysMsgParam);
        if (!s) fatal("sceMsgDialogOpen: system message mode without sysMsgParam");
        if (s->sysMsgType == 3 || s->sysMsgType > MSG_DIALOG_SYSMSG_TYPE_WARNING_PROFILE_PICTURE_AND_NAME_NOT_SHARED) {
            fatal("sceMsgDialogOpen: unknown system message type");
        }
        g_view.type = s->sysMsgType;
        break;
    }
    default:
        fatal("sceMsgDialogOpen: unknown dialog mode");
    }
    g_view.msg = g_msg;
    g_view.button1 = g_button1;
    g_view.button2 = g_button2;
}

int32_t sceMsgDialogOpen(uint8_t *mem, const OrbisMsgDialogParam *param) {
    pthread_mutex_lock(&g_mu);
    if (g_status != MSG_DIALOG_STATUS_INITIALIZED && g_status != MSG_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE;
    }
    if (!param) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    }
    if (param->size != sizeof(OrbisMsgDialogParam)) fatal("sceMsgDialogOpen: param->size is not sizeof(OrbisParam)");
    if (param->baseParam.size != sizeof(OrbisMsgDialogBaseParam)) fatal("sceMsgDialogOpen: baseParam.size is not sizeof(BaseParam)");
    reset_view_locked();
    capture_locked(mem, param);
    memset(&g_result, 0, sizeof(g_result));
    g_view.serial = ++g_serial;
    g_status = MSG_DIALOG_STATUS_RUNNING;
    const MsgDialogView view = g_view;
    pthread_mutex_unlock(&g_mu);

    fprintf(stderr, "[ps4-msg-dialog] Open mode=%u type=%u msg='%s'\n", view.mode, view.type,
            view.msg ? view.msg : "");
    // The view's strings stay unchanged while the dialog runs: Open is refused until it finishes.
    if (!ps4_msg_dialog_ui_show(&view)) press_default_button(&view);
    return 0;
}

int32_t sceMsgDialogGetResult(OrbisMsgDialogResult *result) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status != MSG_DIALOG_STATUS_FINISHED) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED;
    } else if (!result) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    } else {
        *result = g_result;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceMsgDialogGetStatus(void) {
    pthread_mutex_lock(&g_mu);
    const int32_t st = g_status;
    pthread_mutex_unlock(&g_mu);
    return st;
}

int32_t sceMsgDialogUpdateStatus(void) {
    return sceMsgDialogGetStatus();
}

int32_t sceMsgDialogClose(void) {
    pthread_mutex_lock(&g_mu);
    const bool closed = finish_locked(MSG_DIALOG_BUTTON_ID_INVALID, MSG_DIALOG_RESULT_OK);
    pthread_mutex_unlock(&g_mu);
    if (!closed) return ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING;
    ps4_msg_dialog_ui_dismiss();
    return 0;
}

int32_t sceMsgDialogTerminate(void) {
    pthread_mutex_lock(&g_mu);
    const bool was_running = finish_locked(MSG_DIALOG_BUTTON_ID_INVALID, MSG_DIALOG_RESULT_OK);
    int32_t rc = 0;
    if (g_status == MSG_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    } else {
        g_status = MSG_DIALOG_STATUS_NONE;
    }
    pthread_mutex_unlock(&g_mu);
    if (was_running) ps4_msg_dialog_ui_dismiss();
    if (rc == 0) ps4_common_dialog_release();
    return rc;
}

// The checks the three progress bar calls share; on success g_mu stays held.
static int32_t lock_progress_bar(uint32_t target) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status != MSG_DIALOG_STATUS_RUNNING) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING;
    } else if (g_view.mode != MSG_DIALOG_MODE_PROGRESS_BAR) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_SUPPORTED;
    } else if (target != MSG_DIALOG_PROGRESSBAR_TARGET_DEFAULT) {
        rc = ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
    }
    if (rc != 0) pthread_mutex_unlock(&g_mu);
    return rc;
}

// Hands the new message and value to the window and releases g_mu.
static void unlock_progress_bar(void) {
    ps4_msg_dialog_ui_progress(g_view.serial, g_msg, g_progress);
    pthread_mutex_unlock(&g_mu);
}

int32_t sceMsgDialogProgressBarInc(uint32_t target, uint32_t delta) {
    const int32_t rc = lock_progress_bar(target);
    if (rc != 0) return rc;
    g_progress += delta;
    unlock_progress_bar();
    return 0;
}

int32_t sceMsgDialogProgressBarSetMsg(uint32_t target, const char *msg) {
    const int32_t rc = lock_progress_bar(target);
    if (rc != 0) return rc;
    if (!msg) fatal("sceMsgDialogProgressBarSetMsg: msg is NULL");
    char *copy = strdup(msg);
    if (!copy) fatal("out of memory");
    free(g_msg);
    g_msg = copy;
    g_view.msg = g_msg;
    unlock_progress_bar();
    return 0;
}

int32_t sceMsgDialogProgressBarSetValue(uint32_t target, uint32_t value) {
    const int32_t rc = lock_progress_bar(target);
    if (rc != 0) return rc;
    g_progress = value;
    unlock_progress_bar();
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Guest ABI

static void msg_dialog_return(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(uint32_t)rc;
    SHIM_RETURN();
}

void shim_sceMsgDialogInitialize(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogInitialize());
}

void shim_sceMsgDialogOpen(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogOpen(ctx->mem_base, gaddr(ctx->mem_base, ctx->rdi)));
}

void shim_sceMsgDialogGetResult(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogGetResult(gaddr(ctx->mem_base, ctx->rdi)));
}

void shim_sceMsgDialogGetStatus(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogGetStatus());
}

void shim_sceMsgDialogUpdateStatus(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogUpdateStatus());
}

void shim_sceMsgDialogClose(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogClose());
}

void shim_sceMsgDialogTerminate(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogTerminate());
}

void shim_sceMsgDialogProgressBarInc(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogProgressBarInc((uint32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceMsgDialogProgressBarSetMsg(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogProgressBarSetMsg((uint32_t)ctx->rdi, gaddr(ctx->mem_base, ctx->rsi)));
}

void shim_sceMsgDialogProgressBarSetValue(GuestContext *ctx) {
    msg_dialog_return(ctx, sceMsgDialogProgressBarSetValue((uint32_t)ctx->rdi, (uint32_t)ctx->rsi));
}
