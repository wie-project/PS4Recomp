#include "ps4_invitation_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_invitation_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisCommonDialogStatus g_invitation_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static void *g_invitation_callback_arg = NULL;
static OrbisInvitationDialogResultA g_invitation_result = {0};

int32_t sceInvitationDialogInitialize(void) {
    pthread_mutex_lock(&g_invitation_mutex);
    if (g_invitation_status != ORBIS_COMMON_DIALOG_STATUS_NONE) {
        pthread_mutex_unlock(&g_invitation_mutex);
        return (int32_t)0x80b80004; // ALREADY_INITIALIZED
    }
    g_invitation_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
    memset(&g_invitation_result, 0, sizeof(g_invitation_result));
    g_invitation_callback_arg = NULL;
    pthread_mutex_unlock(&g_invitation_mutex);
    return 0;
}

int32_t sceInvitationDialogOpenA(const OrbisInvitationDialogParamA *param) {
    if (!param) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }

    pthread_mutex_lock(&g_invitation_mutex);
    if (g_invitation_status != ORBIS_COMMON_DIALOG_STATUS_INITIALIZED &&
        g_invitation_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_invitation_mutex);
        return (int32_t)0x80b80006; // INVALID_STATE
    }

    g_invitation_callback_arg = param->callbackArg;
    memset(&g_invitation_result, 0, sizeof(g_invitation_result));
    g_invitation_result.callbackArg = param->callbackArg;
    g_invitation_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
    pthread_mutex_unlock(&g_invitation_mutex);

    return 0;
}

OrbisCommonDialogStatus sceInvitationDialogUpdateStatus(void) {
    pthread_mutex_lock(&g_invitation_mutex);
    if (g_invitation_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        g_invitation_result.callbackArg = g_invitation_callback_arg;
        g_invitation_result.errorCode = 0;
        g_invitation_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
        g_invitation_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    }
    OrbisCommonDialogStatus st = g_invitation_status;
    pthread_mutex_unlock(&g_invitation_mutex);
    return st;
}

OrbisCommonDialogStatus sceInvitationDialogGetStatus(void) {
    pthread_mutex_lock(&g_invitation_mutex);
    OrbisCommonDialogStatus st = g_invitation_status;
    pthread_mutex_unlock(&g_invitation_mutex);
    return st;
}

int32_t sceInvitationDialogGetResultA(OrbisInvitationDialogResultA *result) {
    if (!result) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }

    pthread_mutex_lock(&g_invitation_mutex);
    if (g_invitation_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_invitation_mutex);
        return (int32_t)0x80b80005; // NOT_FINISHED
    }

    result->callbackArg = g_invitation_result.callbackArg;
    result->errorCode = g_invitation_result.errorCode;
    result->result = g_invitation_result.result;
    if (result->sentUsers) {
        result->sentUsers->count = 0;
    }
    pthread_mutex_unlock(&g_invitation_mutex);
    return 0;
}

int32_t sceInvitationDialogTerminate(void) {
    pthread_mutex_lock(&g_invitation_mutex);
    g_invitation_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
    memset(&g_invitation_result, 0, sizeof(g_invitation_result));
    g_invitation_callback_arg = NULL;
    pthread_mutex_unlock(&g_invitation_mutex);
    return 0;
}

// Shims
void shim_sceInvitationDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceInvitationDialogInitialize();
}

void shim_sceInvitationDialogOpenA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceInvitationDialogOpenA((const OrbisInvitationDialogParamA *)ctx->rdi);
}

void shim_sceInvitationDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceInvitationDialogUpdateStatus();
}

void shim_sceInvitationDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceInvitationDialogGetStatus();
}

void shim_sceInvitationDialogGetResultA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceInvitationDialogGetResultA((OrbisInvitationDialogResultA *)ctx->rdi);
}

void shim_sceInvitationDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceInvitationDialogTerminate();
}
