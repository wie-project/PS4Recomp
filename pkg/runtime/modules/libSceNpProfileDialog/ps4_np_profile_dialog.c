// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpProfileDialog, ported from shadPS4 (src/core/libraries/np/np_profile_dialog): the same
// state machine, error order and result. The window is native AppKit
// (ps4_np_profile_dialog_ui.m) instead of shadPS4's ImGui layer.
//
// shadPS4's window loads the target's profile card from its NpHandler (the shadNet WebAPI) and,
// once it has loaded, offers the mode's action (send or accept a friend request, block, report).
// This dialog is not connected to the NP handler's shadNet sessions (libSceNp/ps4_np_handler.c) yet: it shows the state in which
// shadPS4's NpHandler fails every profile lookup: the window then shows the target with
// "This player's profile could not be loaded." and only Close, which finishes with
// USER_CANCELED. The operating user's own online ID is empty and account ID 0, as NpHandler
// reports for a user without an NP client, which decides the grief-report self check below.
//
// The library takes the common dialog system between Initialize and Terminate, as in shadPS4.

#include "ps4_np_profile_dialog.h"
#include "ps4_common_dialog.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_status = NP_PROFILE_DIALOG_STATUS_NONE;
static NpProfileDialogResult g_result;
static uint32_t g_serial; // of the dialog opened last

// shadPS4 NpProfileDialogUi::Finish: the result code is always ORBIS_OK.
static bool finish_locked(int32_t user_action) {
    if (g_status != NP_PROFILE_DIALOG_STATUS_RUNNING) return false;
    g_result.result = 0;
    g_result.userAction = user_action;
    g_status = NP_PROFILE_DIALOG_STATUS_FINISHED;
    return true;
}

void ps4_np_profile_dialog_finish(uint32_t serial, int32_t user_action) {
    pthread_mutex_lock(&g_mu);
    if (serial == g_serial) finish_locked(user_action);
    pthread_mutex_unlock(&g_mu);
}

int32_t sceNpProfileDialogInitialize(void) {
    if (!ps4_common_dialog_system_initialized()) return ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED;
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status != NP_PROFILE_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED;
    } else if (!ps4_common_dialog_acquire()) {
        rc = ORBIS_COMMON_DIALOG_ERROR_BUSY;
    } else {
        g_status = NP_PROFILE_DIALOG_STATUS_INITIALIZED;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceNpProfileDialogTerminate(void) {
    pthread_mutex_lock(&g_mu);
    const bool was_running = finish_locked(NP_PROFILE_DIALOG_RESULT_OK); // Close first
    int32_t rc = 0;
    if (g_status == NP_PROFILE_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    } else {
        g_status = NP_PROFILE_DIALOG_STATUS_NONE;
        ps4_common_dialog_release();
    }
    pthread_mutex_unlock(&g_mu);
    if (was_running) ps4_np_profile_dialog_ui_dismiss();
    return rc;
}

int32_t sceNpProfileDialogClose(void) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    switch (g_status) {
    case NP_PROFILE_DIALOG_STATUS_NONE:
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
        break;
    case NP_PROFILE_DIALOG_STATUS_FINISHED:
        rc = ORBIS_COMMON_DIALOG_ERROR_ALREADY_CLOSE;
        break;
    case NP_PROFILE_DIALOG_STATUS_RUNNING:
        finish_locked(NP_PROFILE_DIALOG_RESULT_OK);
        break;
    default:
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING;
        break;
    }
    pthread_mutex_unlock(&g_mu);
    if (rc == 0) ps4_np_profile_dialog_ui_dismiss();
    return rc;
}

int32_t sceNpProfileDialogGetStatus(void) {
    pthread_mutex_lock(&g_mu);
    const int32_t st = g_status;
    pthread_mutex_unlock(&g_mu);
    return st;
}

int32_t sceNpProfileDialogUpdateStatus(void) {
    return sceNpProfileDialogGetStatus();
}

// shadPS4 CheckCanOpen.
static int32_t check_can_open_locked(void) {
    if (g_status == NP_PROFILE_DIALOG_STATUS_NONE) return ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    if (g_status != NP_PROFILE_DIALOG_STATUS_INITIALIZED && g_status != NP_PROFILE_DIALOG_STATUS_FINISHED) {
        return ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE;
    }
    return 0;
}

static bool valid_mode(uint32_t mode) {
    return mode >= NP_PROFILE_DIALOG_MODE_NORMAL && mode <= NP_PROFILE_DIALOG_MODE_GRIEF_REPORT;
}

// Starts the dialog for `view` (target filled in by the caller); called with the lock held.
static void start_locked(NpProfileDialogView *view, uint32_t mode, uint64_t user_data) {
    memset(&g_result, 0, sizeof(g_result));
    g_result.userData = user_data;
    g_status = NP_PROFILE_DIALOG_STATUS_RUNNING;
    view->mode = mode;
    view->serial = ++g_serial;
}

static void show(const NpProfileDialogView *view) {
    if (!ps4_np_profile_dialog_ui_show(view)) {
        ps4_np_profile_dialog_finish(view->serial, NP_PROFILE_DIALOG_RESULT_USER_CANCELED);
    }
}

int32_t sceNpProfileDialogOpen(const NpProfileDialogParam *param) {
    NpProfileDialogView view;
    memset(&view, 0, sizeof(view));
    pthread_mutex_lock(&g_mu);
    int32_t rc = check_can_open_locked();
    if (rc == 0 && !param) rc = ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    if (rc == 0 && !valid_mode(param->mode)) rc = ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
    // A grief report may not target the operating user, whose online ID is empty here.
    if (rc == 0 && param->mode == NP_PROFILE_DIALOG_MODE_GRIEF_REPORT && param->targetOnlineId[0] == '\0') {
        rc = ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
    }
    if (rc == 0) {
        // std::string(targetOnlineId.data): up to the NUL, which the term byte provides.
        const size_t n = strnlen(param->targetOnlineId, NP_PROFILE_DIALOG_ONLINE_ID_SIZE + 1);
        memcpy(view.target, param->targetOnlineId, n);
        start_locked(&view, param->mode, param->userData);
    }
    pthread_mutex_unlock(&g_mu);
    if (rc != 0) return rc;
    fprintf(stderr, "[libSceNpProfileDialog] open: mode=%u user=%d target='%s'\n", view.mode, param->userId,
            view.target);
    show(&view);
    return 0;
}

int32_t sceNpProfileDialogOpenA(const NpProfileDialogParamA *param) {
    NpProfileDialogView view;
    memset(&view, 0, sizeof(view));
    pthread_mutex_lock(&g_mu);
    int32_t rc = check_can_open_locked();
    if (rc == 0 && !param) rc = ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    if (rc == 0 && !valid_mode(param->mode)) rc = ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
    // A grief report may not target the operating user, whose account ID is 0 here.
    if (rc == 0 && param->mode == NP_PROFILE_DIALOG_MODE_GRIEF_REPORT && param->targetAccountId == 0) {
        rc = ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
    }
    if (rc == 0) {
        snprintf(view.target, sizeof(view.target), "%" PRIu64, param->targetAccountId);
        start_locked(&view, param->mode, param->userData);
    }
    pthread_mutex_unlock(&g_mu);
    if (rc != 0) return rc;
    fprintf(stderr, "[libSceNpProfileDialog] openA: mode=%u user=%d targetAccountId=%s reportItems=0x%x\n",
            view.mode, param->userId, view.target, (uint32_t)param->griefReportItem);
    show(&view);
    return 0;
}

int32_t sceNpProfileDialogGetResult(NpProfileDialogResult *result) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status == NP_PROFILE_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    } else if (!result) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    } else if (g_status != NP_PROFILE_DIALOG_STATUS_FINISHED) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED;
    } else {
        for (size_t i = 0; i < sizeof(result->reserved); i++) {
            if (result->reserved[i] != 0) {
                rc = ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
                break;
            }
        }
        if (rc == 0) *result = g_result;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

static void profile_return(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceNpProfileDialogInitialize(GuestContext *ctx) {
    profile_return(ctx, sceNpProfileDialogInitialize());
}

void shim_sceNpProfileDialogTerminate(GuestContext *ctx) {
    profile_return(ctx, sceNpProfileDialogTerminate());
}

void shim_sceNpProfileDialogClose(GuestContext *ctx) {
    profile_return(ctx, sceNpProfileDialogClose());
}

void shim_sceNpProfileDialogGetStatus(GuestContext *ctx) {
    profile_return(ctx, sceNpProfileDialogGetStatus());
}

void shim_sceNpProfileDialogUpdateStatus(GuestContext *ctx) {
    profile_return(ctx, sceNpProfileDialogUpdateStatus());
}

void shim_sceNpProfileDialogOpen(GuestContext *ctx) {
    const NpProfileDialogParam *p = ctx->rdi ? (const NpProfileDialogParam *)(ctx->mem_base + ctx->rdi) : NULL;
    profile_return(ctx, sceNpProfileDialogOpen(p));
}

void shim_sceNpProfileDialogOpenA(GuestContext *ctx) {
    const NpProfileDialogParamA *p = ctx->rdi ? (const NpProfileDialogParamA *)(ctx->mem_base + ctx->rdi) : NULL;
    profile_return(ctx, sceNpProfileDialogOpenA(p));
}

void shim_sceNpProfileDialogGetResult(GuestContext *ctx) {
    NpProfileDialogResult *r = ctx->rdi ? (NpProfileDialogResult *)(ctx->mem_base + ctx->rdi) : NULL;
    profile_return(ctx, sceNpProfileDialogGetResult(r));
}
