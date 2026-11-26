// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpCommerce, ported from shadPS4 (src/core/libraries/np/np_commerce/np_commerce.cpp): the
// same state machine, error order, captured request and result. The store dialog and the store
// icon are native AppKit (ps4_np_commerce_ui.m) instead of shadPS4's ImGui layers.
//
// As in shadPS4 the dialog only shows the request: the mode's action button ("Simulate purchase",
// "Redeem code", "Join Plus"; none in DOWNLOADLIST mode) finishes with PURCHASED, Close with
// USER_CANCELED, and sceNpCommerceDialogClose from the game with OK. Nothing is bought or
// downloaded. The library does not take the common dialog system, as in shadPS4.

#include "ps4_np_commerce.h"
#include "ps4_common_dialog.h"
#include "ps4_user_service.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_status = NP_COMMERCE_STATUS_NONE;
static int32_t g_result = NP_COMMERCE_RESULT_USER_CANCELED;
static int32_t g_mode = NP_COMMERCE_MODE_CATEGORY; // of the dialog opened last
static uint64_t g_user_data;                        // echoed back in the result
static uint32_t g_serial;                           // of the dialog opened last

static bool g_icon_shown;
static int32_t g_icon_pos = NP_COMMERCE_ICON_POS_CENTER;
static int32_t g_icon_layout = NP_COMMERCE_ICON_LAYOUT_DEFAULT;

// Ends the running dialog with `result` (shadPS4 CommerceDialogUi::Finish); false when none runs.
static bool finish_locked(int32_t result) {
    if (g_status != NP_COMMERCE_STATUS_RUNNING) return false;
    g_result = result;
    g_status = NP_COMMERCE_STATUS_FINISHED;
    return true;
}

void ps4_np_commerce_finish(uint32_t serial, int32_t result) {
    pthread_mutex_lock(&g_mu);
    if (serial == g_serial) finish_locked(result);
    pthread_mutex_unlock(&g_mu);
}

int32_t sceNpCommerceDialogInitialize(void) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status != NP_COMMERCE_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED;
    } else {
        g_status = NP_COMMERCE_STATUS_INITIALIZED;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceNpCommerceDialogInitializeInternal(void) {
    return sceNpCommerceDialogInitialize();
}

int32_t sceNpCommerceDialogTerminate(void) {
    pthread_mutex_lock(&g_mu);
    if (g_status == NP_COMMERCE_STATUS_NONE) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    }
    const bool was_running = finish_locked(NP_COMMERCE_RESULT_OK);
    g_status = NP_COMMERCE_STATUS_NONE;
    pthread_mutex_unlock(&g_mu);
    if (was_running) ps4_np_commerce_ui_dismiss();
    return 0;
}

int32_t sceNpCommerceDialogClose(void) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    switch (g_status) {
    case NP_COMMERCE_STATUS_NONE:
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
        break;
    case NP_COMMERCE_STATUS_INITIALIZED:
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING;
        break;
    case NP_COMMERCE_STATUS_FINISHED:
        rc = ORBIS_COMMON_DIALOG_ERROR_ALREADY_CLOSE;
        break;
    default:
        finish_locked(NP_COMMERCE_RESULT_OK);
        break;
    }
    pthread_mutex_unlock(&g_mu);
    if (rc == 0) ps4_np_commerce_ui_dismiss();
    return rc;
}

int8_t sceNpCommerceDialogGetStatus(void) {
    pthread_mutex_lock(&g_mu);
    const int8_t st = (int8_t)g_status;
    pthread_mutex_unlock(&g_mu);
    return st;
}

int32_t sceNpCommerceDialogUpdateStatus(void) {
    return sceNpCommerceDialogGetStatus();
}

// Returns the result code itself (0, 1 or 2, or the PARAM_INVALID of a rejected Open), as
// shadPS4 does, not 0.
int32_t sceNpCommerceDialogGetResult(NpCommerceDialogResult *result) {
    if (!result) return ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    pthread_mutex_lock(&g_mu);
    if (g_status != NP_COMMERCE_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED;
    }
    const int32_t code = g_result;
    result->result = code;
    // authorized is only meaningful in PLUS mode.
    result->authorized = g_mode == NP_COMMERCE_MODE_PLUS && code == NP_COMMERCE_RESULT_PURCHASED;
    result->userData = g_user_data;
    pthread_mutex_unlock(&g_mu);
    fprintf(stderr, "[libSceNpCommerce] result=%d authorized=%d\n", code, result->authorized);
    return code;
}

int32_t sceNpCommerceDialogOpen(uint8_t *mem, const NpCommerceDialogParam *param) {
    pthread_mutex_lock(&g_mu);
    if (g_status != NP_COMMERCE_STATUS_INITIALIZED && g_status != NP_COMMERCE_STATUS_FINISHED) {
        const int32_t st = g_status;
        pthread_mutex_unlock(&g_mu);
        fprintf(stderr, "[libSceNpCommerce] Open in bad state %d\n", st);
        return ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    }
    if (!param) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    }
    if (param->mode < NP_COMMERCE_MODE_CATEGORY || param->mode > NP_COMMERCE_MODE_PLUS) {
        // shadPS4 finishes the dialog with the error as its result.
        g_result = ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
        g_status = NP_COMMERCE_STATUS_FINISHED;
        pthread_mutex_unlock(&g_mu);
        fprintf(stderr, "[libSceNpCommerce] Open: invalid mode %d\n", param->mode);
        return ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
    }

    NpCommerceView view;
    memset(&view, 0, sizeof(view));
    view.mode = param->mode;
    view.userId = param->userId;
    view.serviceLabel = param->serviceLabel;
    view.features = param->mode == NP_COMMERCE_MODE_PLUS ? param->features : 0;
    char name[NP_COMMERCE_USER_NAME_SIZE] = {0};
    if (sceUserServiceGetUserName(param->userId, name, sizeof(name)) >= 0) {
        memcpy(view.userName, name, sizeof(view.userName));
    }
    if (param->numTargets > NP_COMMERCE_NUM_TARGETS_MAX) {
        fprintf(stderr, "[libSceNpCommerce] numTargets %u exceeds max %d, clamping\n", param->numTargets,
                NP_COMMERCE_NUM_TARGETS_MAX);
    }
    const uint32_t count =
        param->numTargets < NP_COMMERCE_NUM_TARGETS_MAX ? param->numTargets : NP_COMMERCE_NUM_TARGETS_MAX;
    if (count > 0 && param->targets) {
        const uint64_t *labels = (const uint64_t *)((uintptr_t)mem + param->targets);
        for (uint32_t i = 0; i < count; i++) {
            if (!labels[i]) continue; // shadPS4 skips NULL labels
            const char *label = (const char *)((uintptr_t)mem + labels[i]);
            const size_t n = strnlen(label, NP_COMMERCE_MAX_LABEL_LEN);
            memcpy(view.targets[view.targetCount], label, n);
            view.targets[view.targetCount][n] = '\0';
            view.targetCount++;
        }
    }

    g_mode = param->mode;
    g_user_data = param->userData;
    g_result = NP_COMMERCE_RESULT_USER_CANCELED;
    g_status = NP_COMMERCE_STATUS_RUNNING;
    view.serial = ++g_serial;
    pthread_mutex_unlock(&g_mu);

    fprintf(stderr, "[libSceNpCommerce] open: mode=%d user_id=%d serviceLabel=0x%x numTargets=%u features=0x%llx first='%s'\n",
            view.mode, view.userId, view.serviceLabel, param->numTargets, (unsigned long long)view.features,
            view.targetCount ? view.targets[0] : "");
    if (!ps4_np_commerce_ui_show(&view)) ps4_np_commerce_finish(view.serial, NP_COMMERCE_RESULT_USER_CANCELED);
    return 0;
}

int32_t sceNpCommerceShowPsStoreIcon(int32_t pos) {
    pthread_mutex_lock(&g_mu);
    g_icon_pos = (pos >= NP_COMMERCE_ICON_POS_CENTER && pos <= NP_COMMERCE_ICON_POS_RIGHT) ? pos
                                                                                          : NP_COMMERCE_ICON_POS_CENTER;
    g_icon_shown = true;
    const int32_t p = g_icon_pos, l = g_icon_layout;
    pthread_mutex_unlock(&g_mu);
    ps4_np_commerce_ui_icon(true, p, l);
    return 0;
}

int32_t sceNpCommerceHidePsStoreIcon(void) {
    pthread_mutex_lock(&g_mu);
    const bool was_shown = g_icon_shown;
    g_icon_shown = false;
    const int32_t p = g_icon_pos, l = g_icon_layout;
    pthread_mutex_unlock(&g_mu);
    if (was_shown) ps4_np_commerce_ui_icon(false, p, l);
    return 0;
}

// An unknown layout is ignored and still succeeds, as in shadPS4.
int32_t sceNpCommerceSetPsStoreIconLayout(int32_t layout) {
    if (layout < NP_COMMERCE_ICON_LAYOUT_DEFAULT || layout > NP_COMMERCE_ICON_LAYOUT_FIXED_90) {
        fprintf(stderr, "[libSceNpCommerce] invalid icon layout %d, ignoring\n", layout);
        return 0;
    }
    pthread_mutex_lock(&g_mu);
    g_icon_layout = layout;
    const bool shown = g_icon_shown;
    const int32_t p = g_icon_pos;
    pthread_mutex_unlock(&g_mu);
    if (shown) ps4_np_commerce_ui_icon(true, p, layout);
    return 0;
}

static void commerce_return(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceNpCommerceDialogInitialize(GuestContext *ctx) {
    commerce_return(ctx, sceNpCommerceDialogInitialize());
}

void shim_sceNpCommerceDialogInitializeInternal(GuestContext *ctx) {
    commerce_return(ctx, sceNpCommerceDialogInitializeInternal());
}

void shim_sceNpCommerceDialogTerminate(GuestContext *ctx) {
    commerce_return(ctx, sceNpCommerceDialogTerminate());
}

void shim_sceNpCommerceDialogClose(GuestContext *ctx) {
    commerce_return(ctx, sceNpCommerceDialogClose());
}

void shim_sceNpCommerceDialogGetStatus(GuestContext *ctx) {
    commerce_return(ctx, sceNpCommerceDialogGetStatus());
}

void shim_sceNpCommerceDialogUpdateStatus(GuestContext *ctx) {
    commerce_return(ctx, sceNpCommerceDialogUpdateStatus());
}

void shim_sceNpCommerceDialogGetResult(GuestContext *ctx) {
    NpCommerceDialogResult *r = ctx->rdi ? (NpCommerceDialogResult *)(ctx->mem_base + ctx->rdi) : NULL;
    commerce_return(ctx, sceNpCommerceDialogGetResult(r));
}

void shim_sceNpCommerceDialogOpen(GuestContext *ctx) {
    const NpCommerceDialogParam *p = ctx->rdi ? (const NpCommerceDialogParam *)(ctx->mem_base + ctx->rdi) : NULL;
    commerce_return(ctx, sceNpCommerceDialogOpen(ctx->mem_base, p));
}

void shim_sceNpCommerceShowPsStoreIcon(GuestContext *ctx) {
    commerce_return(ctx, sceNpCommerceShowPsStoreIcon((int32_t)ctx->rdi));
}

void shim_sceNpCommerceHidePsStoreIcon(GuestContext *ctx) {
    commerce_return(ctx, sceNpCommerceHidePsStoreIcon());
}

void shim_sceNpCommerceSetPsStoreIconLayout(GuestContext *ctx) {
    commerce_return(ctx, sceNpCommerceSetPsStoreIconLayout((int32_t)ctx->rdi));
}
