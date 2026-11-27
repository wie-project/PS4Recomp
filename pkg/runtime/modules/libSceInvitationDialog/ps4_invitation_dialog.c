// SPDX-License-Identifier: GPL-2.0-or-later

// libSceInvitationDialog, ported from shadPS4 (src/core/libraries/invitation_dialog): the same
// state machine, error order, captured request and result. The window is native AppKit
// (ps4_invitation_dialog_ui.m) instead of shadPS4's ImGui layer.
//
// shadPS4 fills the dialog from its NpHandler: the friend list for the USERENABLE recipient
// picker, the pending invitations for RECV mode, and the shadNet WebAPI for sending. This dialog
// is not connected to the NP handler's shadNet sessions (libSceNp/ps4_np_handler.c) yet: it shows the state in
// which shadPS4's NpHandler has an empty friend list, no pending invitations and fails every
// SendSessionInvitation. So the picker has nobody to choose, RECV mode lists nothing to accept,
// and Send finishes with OK and no recipients reported as sent.
//
// The library takes the common dialog system between Initialize and Terminate, as in shadPS4.

#include "ps4_invitation_dialog.h"
#include "ps4_common_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_status = INVITATION_DIALOG_STATUS_NONE;
static uint32_t g_serial; // of the dialog opened last

// shadPS4 DialogState: what GetResult/GetResultA read back.
static int32_t g_mode;
static int32_t g_result = INVITATION_DIALOG_RESULT_OK;
static int32_t g_user_id;
static uint64_t g_callback_arg;
static uint32_t g_recipients; // USERDISABLE fixed list size (online IDs or account IDs)

// Ends the running dialog with `result` (shadPS4 InvitationDialogUi::Finish); false when none
// runs.
static bool finish_locked(int32_t result) {
    if (g_status != INVITATION_DIALOG_STATUS_RUNNING) return false;
    g_result = result;
    g_status = INVITATION_DIALOG_STATUS_FINISHED;
    return true;
}

void ps4_invitation_dialog_finish(uint32_t serial, int32_t result) {
    pthread_mutex_lock(&g_mu);
    if (serial == g_serial) finish_locked(result);
    pthread_mutex_unlock(&g_mu);
}

void ps4_invitation_dialog_send(uint32_t serial) {
    pthread_mutex_lock(&g_mu);
    if (serial == g_serial && g_status == INVITATION_DIALOG_STATUS_RUNNING) {
        // NpHandler::SendSessionInvitation needs a signed-in NP client; without one it fails, and
        // shadPS4 then finishes the dialog with OK and leaves the sent lists empty.
        fprintf(stderr, "[libSceInvitationDialog] send: user %d is not signed in to an NP server, "
                        "invitation to %u fixed recipient(s) not delivered\n",
                g_user_id, g_recipients);
        finish_locked(INVITATION_DIALOG_RESULT_OK);
    }
    pthread_mutex_unlock(&g_mu);
}

int32_t sceInvitationDialogInitialize(void) {
    if (!ps4_common_dialog_system_initialized()) return ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED;
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status != INVITATION_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED;
    } else if (!ps4_common_dialog_acquire()) {
        rc = ORBIS_COMMON_DIALOG_ERROR_BUSY;
    } else {
        g_status = INVITATION_DIALOG_STATUS_INITIALIZED;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceInvitationDialogTerminate(void) {
    pthread_mutex_lock(&g_mu);
    const bool was_running = finish_locked(INVITATION_DIALOG_RESULT_OK); // Close first
    int32_t rc = 0;
    if (g_status == INVITATION_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    } else {
        g_status = INVITATION_DIALOG_STATUS_NONE;
        ps4_common_dialog_release();
    }
    pthread_mutex_unlock(&g_mu);
    if (was_running) ps4_invitation_dialog_ui_dismiss();
    return rc;
}

int32_t sceInvitationDialogClose(void) {
    pthread_mutex_lock(&g_mu);
    const bool closed = finish_locked(INVITATION_DIALOG_RESULT_OK);
    pthread_mutex_unlock(&g_mu);
    if (!closed) return ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING;
    ps4_invitation_dialog_ui_dismiss();
    return 0;
}

int32_t sceInvitationDialogGetStatus(void) {
    pthread_mutex_lock(&g_mu);
    const int32_t st = g_status;
    pthread_mutex_unlock(&g_mu);
    return st;
}

int32_t sceInvitationDialogUpdateStatus(void) {
    return sceInvitationDialogGetStatus();
}

// The checks shared by Open and OpenA once the parameter is non-NULL and the state allows an
// open: resets the dialog state, captures the request and starts the dialog. `account_ids`
// selects the A variant's fixed recipient list (account IDs instead of online IDs).
static int32_t open_locked(uint8_t *mem, const InvitationDialogParam *param, bool account_ids,
                           InvitationDialogView *view) {
    g_mode = param->mode;
    g_result = INVITATION_DIALOG_RESULT_OK;
    g_user_id = param->userId;
    g_callback_arg = param->callbackArg;
    g_recipients = 0;

    memset(view, 0, sizeof(*view));
    view->mode = param->mode;
    if (param->mode == INVITATION_DIALOG_MODE_SEND) {
        if (!param->dataParam) return ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
        const InvitationDialogSendInfo *send = (const InvitationDialogSendInfo *)(mem + param->dataParam);
        if (send->sessionId) {
            // OrbisNpSessionId: char data[45]; char term; char padding[2].
            const char *sid = (const char *)(mem + send->sessionId);
            const size_t n = strnlen(sid, INVITATION_DIALOG_NP_SESSION_ID_MAX_SIZE + 1);
            memcpy(view->sessionId, sid, n);
        }
        if (send->userMessage) {
            const char *msg = (const char *)(mem + send->userMessage);
            const size_t n = strnlen(msg, INVITATION_DIALOG_MAX_USER_MSG_SIZE);
            memcpy(view->message, msg, n);
        }
        if (send->addressType == INVITATION_DIALOG_ADDRESS_TYPE_USERENABLE) {
            view->userEditable = true;
        } else if (send->addressType == INVITATION_DIALOG_ADDRESS_TYPE_USERDISABLE && send->ids) {
            uint32_t count = send->idsCount;
            if (count > INVITATION_DIALOG_ADDRESS_USER_LIST_MAX_SIZE) count = INVITATION_DIALOG_ADDRESS_USER_LIST_MAX_SIZE;
            g_recipients = count;
        }
        fprintf(stderr, "[libSceInvitationDialog] open%s(SEND) user=%d sessionId='%s' recipients=%u editable=%d\n",
                account_ids ? "A" : "", param->userId, view->sessionId, g_recipients, view->userEditable);
    } else {
        // No pending invitations: they arrive only through a signed-in NP client.
        fprintf(stderr, "[libSceInvitationDialog] open%s(RECV) user=%d pending=0\n", account_ids ? "A" : "",
                param->userId);
    }
    g_status = INVITATION_DIALOG_STATUS_RUNNING;
    view->serial = ++g_serial;
    return 0;
}

static bool valid_mode(int32_t mode) {
    return mode == INVITATION_DIALOG_MODE_SEND || mode == INVITATION_DIALOG_MODE_RECV;
}

static bool can_open_locked(void) {
    return g_status == INVITATION_DIALOG_STATUS_INITIALIZED || g_status == INVITATION_DIALOG_STATUS_FINISHED;
}

// Starts the window of a dialog open_locked started, outside the lock.
static void show(const InvitationDialogView *view) {
    if (!ps4_invitation_dialog_ui_show(view)) {
        ps4_invitation_dialog_finish(view->serial, INVITATION_DIALOG_RESULT_USER_CANCELED);
    }
}

// sceInvitationDialogOpen checks the state before the parameter.
int32_t sceInvitationDialogOpen(uint8_t *mem, const InvitationDialogParam *param) {
    InvitationDialogView view;
    pthread_mutex_lock(&g_mu);
    int32_t rc;
    if (!can_open_locked()) {
        rc = ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE;
    } else if (!param) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    } else if (!valid_mode(param->mode)) {
        rc = ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
    } else {
        rc = open_locked(mem, param, false, &view);
    }
    pthread_mutex_unlock(&g_mu);
    if (rc == 0) show(&view);
    return rc;
}

// sceInvitationDialogOpenA checks the parameter before the state.
int32_t sceInvitationDialogOpenA(uint8_t *mem, const InvitationDialogParam *param) {
    if (!param) return ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    if (!valid_mode(param->mode)) return ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
    InvitationDialogView view;
    pthread_mutex_lock(&g_mu);
    const int32_t rc = can_open_locked() ? open_locked(mem, param, true, &view) : ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE;
    pthread_mutex_unlock(&g_mu);
    if (rc == 0) show(&view);
    return rc;
}

// Both variants return the dialog result itself (OK or USER_CANCELED) on success, as shadPS4
// does. The sent-recipient list reports the recipients the invitation was delivered to: none,
// since delivery needs an NP server (ps4_invitation_dialog_send), so only its count is written.
static int32_t get_result(uint8_t *mem, InvitationDialogResult *result) {
    pthread_mutex_lock(&g_mu);
    int32_t rc;
    if (g_status == INVITATION_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    } else if (g_status != INVITATION_DIALOG_STATUS_FINISHED) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED;
    } else if (!result) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    } else {
        result->callbackArg = g_callback_arg;
        result->errorCode = 0; // BLOCKED_ALL_PLAYERS is reported only by an NP server
        result->result = g_result;
        if (result->sentUsers) {
            const uint32_t count = 0;
            memcpy(mem + result->sentUsers, &count, sizeof(count));
        }
        rc = g_result;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceInvitationDialogGetResult(uint8_t *mem, InvitationDialogResult *result) {
    return get_result(mem, result);
}

int32_t sceInvitationDialogGetResultA(uint8_t *mem, InvitationDialogResult *result) {
    return get_result(mem, result);
}

static void invitation_return(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceInvitationDialogInitialize(GuestContext *ctx) {
    invitation_return(ctx, sceInvitationDialogInitialize());
}

void shim_sceInvitationDialogTerminate(GuestContext *ctx) {
    invitation_return(ctx, sceInvitationDialogTerminate());
}

void shim_sceInvitationDialogClose(GuestContext *ctx) {
    invitation_return(ctx, sceInvitationDialogClose());
}

void shim_sceInvitationDialogGetStatus(GuestContext *ctx) {
    invitation_return(ctx, sceInvitationDialogGetStatus());
}

void shim_sceInvitationDialogUpdateStatus(GuestContext *ctx) {
    invitation_return(ctx, sceInvitationDialogUpdateStatus());
}

void shim_sceInvitationDialogOpen(GuestContext *ctx) {
    const InvitationDialogParam *p = ctx->rdi ? (const InvitationDialogParam *)(ctx->mem_base + ctx->rdi) : NULL;
    invitation_return(ctx, sceInvitationDialogOpen(ctx->mem_base, p));
}

void shim_sceInvitationDialogOpenA(GuestContext *ctx) {
    const InvitationDialogParam *p = ctx->rdi ? (const InvitationDialogParam *)(ctx->mem_base + ctx->rdi) : NULL;
    invitation_return(ctx, sceInvitationDialogOpenA(ctx->mem_base, p));
}

void shim_sceInvitationDialogGetResult(GuestContext *ctx) {
    InvitationDialogResult *r = ctx->rdi ? (InvitationDialogResult *)(ctx->mem_base + ctx->rdi) : NULL;
    invitation_return(ctx, sceInvitationDialogGetResult(ctx->mem_base, r));
}

void shim_sceInvitationDialogGetResultA(GuestContext *ctx) {
    InvitationDialogResult *r = ctx->rdi ? (InvitationDialogResult *)(ctx->mem_base + ctx->rdi) : NULL;
    invitation_return(ctx, sceInvitationDialogGetResultA(ctx->mem_base, r));
}
