// SPDX-License-Identifier: GPL-2.0-or-later

// libSceWebBrowserDialog, after shadPS4 (src/core/libraries/web_browser_dialog). No web view is
// shown: an opened dialog finishes on its next status update with RESULT_OK, as in shadPS4. The
// dialog takes the CommonDialog system between Initialize and Terminate. Navigation, cookies,
// zoom and events have no browser to act on and are accepted (stubs in shadPS4 as well).

#include "ps4_web_browser_dialog.h"
#include "ps4_common_dialog.h"
#include "ps4_hle_stub.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_web_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisCommonDialogStatus g_web_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static OrbisWebBrowserDialogParam g_web_param = {0};
static OrbisWebBrowserDialogResult g_web_result = {0};

int32_t sceWebBrowserDialogInitialize(void) {
    if (!ps4_common_dialog_system_initialized()) {
        return ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED;
    }
    pthread_mutex_lock(&g_web_mutex);
    int32_t r = 0;
    if (g_web_status != ORBIS_COMMON_DIALOG_STATUS_NONE) {
        r = ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED;
    } else if (!ps4_common_dialog_acquire()) {
        r = ORBIS_COMMON_DIALOG_ERROR_BUSY;
    } else {
        g_web_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
        memset(&g_web_result, 0, sizeof(g_web_result));
        memset(&g_web_param, 0, sizeof(g_web_param));
    }
    pthread_mutex_unlock(&g_web_mutex);
    return r;
}

int32_t sceWebBrowserDialogOpen(const OrbisWebBrowserDialogParam *param) {
    if (!param) {
        return ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    }
    pthread_mutex_lock(&g_web_mutex);
    if (g_web_status != ORBIS_COMMON_DIALOG_STATUS_INITIALIZED &&
        g_web_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_web_mutex);
        return ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE;
    }
    g_web_param = *param;
    memset(&g_web_result, 0, sizeof(g_web_result));
    g_web_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
    g_web_result.userData = param->userData;
    g_web_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
    fprintf(stderr, "[ps4-web-browser-dialog] opened (no web view; finishes on the next update)\n");
    pthread_mutex_unlock(&g_web_mutex);
    return 0;
}

OrbisCommonDialogStatus sceWebBrowserDialogGetStatus(void) {
    pthread_mutex_lock(&g_web_mutex);
    OrbisCommonDialogStatus st = g_web_status;
    pthread_mutex_unlock(&g_web_mutex);
    return st;
}

OrbisCommonDialogStatus sceWebBrowserDialogUpdateStatus(void) {
    pthread_mutex_lock(&g_web_mutex);
    if (g_web_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        g_web_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    }
    OrbisCommonDialogStatus st = g_web_status;
    pthread_mutex_unlock(&g_web_mutex);
    return st;
}

int32_t sceWebBrowserDialogGetResult(OrbisWebBrowserDialogResult *result) {
    if (!result) {
        return ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    }
    pthread_mutex_lock(&g_web_mutex);
    if (g_web_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_web_mutex);
        return ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE;
    }
    *result = g_web_result;
    pthread_mutex_unlock(&g_web_mutex);
    return 0;
}

int32_t sceWebBrowserDialogClose(void) {
    pthread_mutex_lock(&g_web_mutex);
    int32_t r = 0;
    if (g_web_status != ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        r = ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING;
    } else {
        g_web_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    }
    pthread_mutex_unlock(&g_web_mutex);
    return r;
}

int32_t sceWebBrowserDialogTerminate(void) {
    pthread_mutex_lock(&g_web_mutex);
    int32_t r = 0;
    if (g_web_status == ORBIS_COMMON_DIALOG_STATUS_NONE) {
        r = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    } else {
        g_web_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
        ps4_common_dialog_release();
    }
    pthread_mutex_unlock(&g_web_mutex);
    return r;
}

#define GPTR(type, addr) ((addr) ? (type)(ctx->mem_base + (addr)) : (type)NULL)

// Shims
void shim_sceWebBrowserDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceWebBrowserDialogInitialize();
    SHIM_RETURN();
}

void shim_sceWebBrowserDialogOpen(GuestContext *ctx) {
    const OrbisWebBrowserDialogParam *param =
        GPTR(const OrbisWebBrowserDialogParam *, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceWebBrowserDialogOpen(param);
    SHIM_RETURN();
}

void shim_sceWebBrowserDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceWebBrowserDialogGetStatus();
    SHIM_RETURN();
}

void shim_sceWebBrowserDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceWebBrowserDialogUpdateStatus();
    SHIM_RETURN();
}

void shim_sceWebBrowserDialogGetResult(GuestContext *ctx) {
    OrbisWebBrowserDialogResult *result =
        GPTR(OrbisWebBrowserDialogResult *, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceWebBrowserDialogGetResult(result);
    SHIM_RETURN();
}

void shim_sceWebBrowserDialogClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceWebBrowserDialogClose();
    SHIM_RETURN();
}

void shim_sceWebBrowserDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceWebBrowserDialogTerminate();
    SHIM_RETURN();
}

PS4_HLE_STUB(sceWebBrowserDialogGetEvent)
PS4_HLE_STUB(sceWebBrowserDialogNavigate)
PS4_HLE_STUB(sceWebBrowserDialogOpenForPredeterminedContent)
PS4_HLE_STUB(sceWebBrowserDialogResetCookie)
PS4_HLE_STUB(sceWebBrowserDialogSetCookie)
PS4_HLE_STUB(sceWebBrowserDialogSetZoom)
PS4_HLE_STUB(Func_F2BE042771625F8C)
