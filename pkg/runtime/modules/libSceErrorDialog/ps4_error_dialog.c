// SPDX-License-Identifier: GPL-2.0-or-later

// libSceErrorDialog after shadPS4 (ime/error_dialog.cpp). Open shows "An error has occurred."
// with the error code and an OK button; the game keeps running and polls UpdateStatus until the
// player dismisses the window (RUNNING -> FINISHED). The library does not take the common dialog
// system, as in shadPS4. sceErrorDialogOpenDetail and sceErrorDialogOpenWithReport are stubs in
// shadPS4 as well.

#include "ps4_error_dialog.h"
#include "ps4_common_dialog.h"
#include "ps4_hle_stub.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_status = ERROR_DIALOG_STATUS_NONE;
static uint32_t g_serial; // of the dialog opened last

// Ends the running dialog; false when none runs.
static bool finish_locked(void) {
    if (g_status != ERROR_DIALOG_STATUS_RUNNING) return false;
    g_status = ERROR_DIALOG_STATUS_FINISHED;
    return true;
}

void ps4_error_dialog_finish(uint32_t serial) {
    pthread_mutex_lock(&g_mu);
    if (serial == g_serial) finish_locked();
    pthread_mutex_unlock(&g_mu);
}

int32_t sceErrorDialogInitialize(void) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status != ERROR_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED;
    } else {
        g_status = ERROR_DIALOG_STATUS_INITIALIZED;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceErrorDialogOpen(const OrbisErrorDialogParam *param) {
    pthread_mutex_lock(&g_mu);
    if (g_status != ERROR_DIALOG_STATUS_INITIALIZED && g_status != ERROR_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE;
    }
    if (!param) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    }
    if (param->size != (int32_t)sizeof(OrbisErrorDialogParam)) {
        // shadPS4 asserts on the parameter size.
        fprintf(stderr, "[ps4-error-dialog] sceErrorDialogOpen: param size %d, expected %zu\n", param->size,
                sizeof(OrbisErrorDialogParam));
        abort();
    }
    char message[ERROR_DIALOG_MESSAGE_SIZE];
    snprintf(message, sizeof(message), "An error has occurred. \nCode: 0X%X", (uint32_t)param->errorCode);
    const uint32_t serial = ++g_serial;
    g_status = ERROR_DIALOG_STATUS_RUNNING;
    pthread_mutex_unlock(&g_mu);

    fprintf(stderr, "[ps4-error-dialog] error code 0x%08X\n", (uint32_t)param->errorCode);
    if (!ps4_error_dialog_ui_show(serial, message)) ps4_error_dialog_finish(serial);
    return 0;
}

int32_t sceErrorDialogGetStatus(void) {
    pthread_mutex_lock(&g_mu);
    const int32_t st = g_status;
    pthread_mutex_unlock(&g_mu);
    return st;
}

int32_t sceErrorDialogUpdateStatus(void) {
    return sceErrorDialogGetStatus();
}

int32_t sceErrorDialogClose(void) {
    pthread_mutex_lock(&g_mu);
    const bool closed = finish_locked();
    pthread_mutex_unlock(&g_mu);
    if (!closed) return ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING;
    ps4_error_dialog_ui_dismiss();
    return 0;
}

int32_t sceErrorDialogTerminate(void) {
    pthread_mutex_lock(&g_mu);
    const bool was_running = finish_locked();
    int32_t rc = 0;
    if (g_status == ERROR_DIALOG_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    } else {
        g_status = ERROR_DIALOG_STATUS_NONE;
    }
    pthread_mutex_unlock(&g_mu);
    if (was_running) ps4_error_dialog_ui_dismiss();
    return rc;
}

static void error_dialog_return(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceErrorDialogInitialize(GuestContext *ctx) {
    error_dialog_return(ctx, sceErrorDialogInitialize());
}

void shim_sceErrorDialogOpen(GuestContext *ctx) {
    const OrbisErrorDialogParam *param =
        ctx->rdi ? (const OrbisErrorDialogParam *)(ctx->mem_base + ctx->rdi) : NULL;
    error_dialog_return(ctx, sceErrorDialogOpen(param));
}

void shim_sceErrorDialogUpdateStatus(GuestContext *ctx) {
    error_dialog_return(ctx, sceErrorDialogUpdateStatus());
}

void shim_sceErrorDialogGetStatus(GuestContext *ctx) {
    error_dialog_return(ctx, sceErrorDialogGetStatus());
}

void shim_sceErrorDialogClose(GuestContext *ctx) {
    error_dialog_return(ctx, sceErrorDialogClose());
}

void shim_sceErrorDialogTerminate(GuestContext *ctx) {
    error_dialog_return(ctx, sceErrorDialogTerminate());
}

PS4_HLE_STUB(sceErrorDialogOpenDetail)
PS4_HLE_STUB(sceErrorDialogOpenWithReport)
