// SPDX-License-Identifier: GPL-2.0-or-later

// libSceSigninDialog after shadPS4. The local user is always signed in, so an opened dialog
// finishes at once with RESULT_OK; no window is shown. Status is NONE until the library is both
// initialized and open.

#include "ps4_signin_dialog.h"
#include <pthread.h>
#include <stdbool.h>

static pthread_mutex_t g_signin_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_signin_init;
static bool g_signin_open;

int32_t sceSigninDialogInitialize(void) {
    pthread_mutex_lock(&g_signin_mutex);
    g_signin_init = true;
    pthread_mutex_unlock(&g_signin_mutex);
    return 0;
}

int32_t sceSigninDialogOpen(void) {
    pthread_mutex_lock(&g_signin_mutex);
    g_signin_open = true;
    pthread_mutex_unlock(&g_signin_mutex);
    return 0;
}

OrbisSigninDialogStatus sceSigninDialogGetStatus(void) {
    pthread_mutex_lock(&g_signin_mutex);
    const bool finished = g_signin_init && g_signin_open;
    pthread_mutex_unlock(&g_signin_mutex);
    return finished ? ORBIS_SIGNIN_DIALOG_STATUS_FINISHED : ORBIS_SIGNIN_DIALOG_STATUS_NONE;
}

OrbisSigninDialogStatus sceSigninDialogUpdateStatus(void) {
    return sceSigninDialogGetStatus();
}

// Only the result field is written; reserved keeps what the caller had there.
int32_t sceSigninDialogGetResult(OrbisSigninDialogResult *result) {
    pthread_mutex_lock(&g_signin_mutex);
    const bool init = g_signin_init;
    pthread_mutex_unlock(&g_signin_mutex);
    if (!init) {
        return ORBIS_SIGNIN_DIALOG_ERROR_NOT_INITIALIZED;
    }
    if (!result) {
        return ORBIS_SIGNIN_DIALOG_ERROR_PARAM_INVALID;
    }
    result->result = ORBIS_SIGNIN_DIALOG_RESULT_OK;
    return 0;
}

int32_t sceSigninDialogClose(void) {
    pthread_mutex_lock(&g_signin_mutex);
    g_signin_open = false;
    pthread_mutex_unlock(&g_signin_mutex);
    return 0;
}

int32_t sceSigninDialogTerminate(void) {
    pthread_mutex_lock(&g_signin_mutex);
    g_signin_open = false;
    g_signin_init = false;
    pthread_mutex_unlock(&g_signin_mutex);
    return 0;
}

void shim_sceSigninDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSigninDialogInitialize();
    SHIM_RETURN();
}

// sceSigninDialogOpen(const OrbisSigninDialogParam *param): the parameter is not read.
void shim_sceSigninDialogOpen(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSigninDialogOpen();
    SHIM_RETURN();
}

void shim_sceSigninDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSigninDialogGetStatus();
    SHIM_RETURN();
}

void shim_sceSigninDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSigninDialogUpdateStatus();
    SHIM_RETURN();
}

void shim_sceSigninDialogGetResult(GuestContext *ctx) {
    OrbisSigninDialogResult *result =
        ctx->rdi ? (OrbisSigninDialogResult *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSigninDialogGetResult(result);
    SHIM_RETURN();
}

void shim_sceSigninDialogClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSigninDialogClose();
    SHIM_RETURN();
}

void shim_sceSigninDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSigninDialogTerminate();
    SHIM_RETURN();
}
