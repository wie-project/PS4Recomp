#include "ps4_np_profile_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_npprofile_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisCommonDialogStatus g_npprofile_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static void *g_npprofile_user_data = NULL;
static OrbisNpProfileDialogResult g_npprofile_result = {0};

int32_t sceNpProfileDialogInitialize(void) {
    pthread_mutex_lock(&g_npprofile_mutex);
    if (g_npprofile_status != ORBIS_COMMON_DIALOG_STATUS_NONE) {
        pthread_mutex_unlock(&g_npprofile_mutex);
        return (int32_t)0x80b80004; // ALREADY_INITIALIZED
    }
    g_npprofile_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
    memset(&g_npprofile_result, 0, sizeof(g_npprofile_result));
    g_npprofile_user_data = NULL;
    pthread_mutex_unlock(&g_npprofile_mutex);
    return 0;
}

int32_t sceNpProfileDialogOpenA(const OrbisNpProfileDialogParamA *param) {
    if (!param) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }

    pthread_mutex_lock(&g_npprofile_mutex);
    if (g_npprofile_status != ORBIS_COMMON_DIALOG_STATUS_INITIALIZED &&
        g_npprofile_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_npprofile_mutex);
        return (int32_t)0x80b80006; // INVALID_STATE
    }

    g_npprofile_user_data = param->userData;
    memset(&g_npprofile_result, 0, sizeof(g_npprofile_result));
    g_npprofile_result.userData = param->userData;
    g_npprofile_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
    pthread_mutex_unlock(&g_npprofile_mutex);

    return 0;
}

// int sceNpProfileDialogClose(void)
//
// Closes the running dialog. Behaviour as in shadPS4
// (libraries/np/np_profile_dialog/np_profile_dialog.cpp sceNpProfileDialogClose): before
// Initialize it is NOT_INITIALIZED, outside RUNNING it is NOT_RUNNING, and for a running
// dialog it finishes it with result OK (user action OK, as UpdateStatus does for the
// auto-finishing offline dialog) and returns SCE_OK. The dialog then reads FINISHED.
int32_t sceNpProfileDialogClose(void) {
    pthread_mutex_lock(&g_npprofile_mutex);
    int32_t rc = 0;
    if (g_npprofile_status == ORBIS_COMMON_DIALOG_STATUS_NONE) {
        rc = (int32_t)0x80b80003; // NOT_INITIALIZED
    } else if (g_npprofile_status != ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        rc = (int32_t)0x80b8000b; // NOT_RUNNING
    } else {
        g_npprofile_result.userData = g_npprofile_user_data;
        g_npprofile_result.result = 0;
        g_npprofile_result.userAction = ORBIS_COMMON_DIALOG_RESULT_OK;
        g_npprofile_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    }
    pthread_mutex_unlock(&g_npprofile_mutex);
    return rc;
}

OrbisCommonDialogStatus sceNpProfileDialogUpdateStatus(void) {
    pthread_mutex_lock(&g_npprofile_mutex);
    if (g_npprofile_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        g_npprofile_result.userData = g_npprofile_user_data;
        g_npprofile_result.result = 0;
        g_npprofile_result.userAction = ORBIS_COMMON_DIALOG_RESULT_OK;
        g_npprofile_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    }
    OrbisCommonDialogStatus st = g_npprofile_status;
    pthread_mutex_unlock(&g_npprofile_mutex);
    return st;
}

OrbisCommonDialogStatus sceNpProfileDialogGetStatus(void) {
    pthread_mutex_lock(&g_npprofile_mutex);
    OrbisCommonDialogStatus st = g_npprofile_status;
    pthread_mutex_unlock(&g_npprofile_mutex);
    return st;
}

int32_t sceNpProfileDialogGetResult(OrbisNpProfileDialogResult *result) {
    if (!result) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }

    pthread_mutex_lock(&g_npprofile_mutex);
    if (g_npprofile_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_npprofile_mutex);
        return (int32_t)0x80b80005; // NOT_FINISHED
    }

    result->result = g_npprofile_result.result;
    result->userAction = g_npprofile_result.userAction;
    result->userData = g_npprofile_result.userData;
    pthread_mutex_unlock(&g_npprofile_mutex);
    return 0;
}

int32_t sceNpProfileDialogTerminate(void) {
    pthread_mutex_lock(&g_npprofile_mutex);
    g_npprofile_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
    memset(&g_npprofile_result, 0, sizeof(g_npprofile_result));
    g_npprofile_user_data = NULL;
    pthread_mutex_unlock(&g_npprofile_mutex);
    return 0;
}

// Shims
void shim_sceNpProfileDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpProfileDialogInitialize();
    SHIM_RETURN();
}

void shim_sceNpProfileDialogOpenA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpProfileDialogOpenA(
        ctx->rdi ? (const OrbisNpProfileDialogParamA *)(ctx->mem_base + ctx->rdi) : NULL
    );
    SHIM_RETURN();
}

void shim_sceNpProfileDialogClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceNpProfileDialogClose();
    SHIM_RETURN();
}

void shim_sceNpProfileDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpProfileDialogUpdateStatus();
    SHIM_RETURN();
}

void shim_sceNpProfileDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpProfileDialogGetStatus();
    SHIM_RETURN();
}

void shim_sceNpProfileDialogGetResult(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpProfileDialogGetResult(
        ctx->rdi ? (OrbisNpProfileDialogResult *)(ctx->mem_base + ctx->rdi) : NULL
    );
    SHIM_RETURN();
}

void shim_sceNpProfileDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpProfileDialogTerminate();
    SHIM_RETURN();
}

// Legacy (non-A) sceNpProfileDialogOpen. SceNpProfileDialogParam:
//   baseParam(0x30) size_t size; mode; userId; SceNpOnlineId targetOnlineId(20);
//   void *userData (0x58); reserved[32]
// Only userData matters for the offline (auto-finish) dialog.
void shim_sceNpProfileDialogOpen(GuestContext *ctx) {
    if (!ctx->rdi) {
        ctx->rax = (uint64_t)(int64_t)(int32_t)0x80b8000d; // ARG_NULL
        SHIM_RETURN();
    }
    OrbisNpProfileDialogParamA p;
    memset(&p, 0, sizeof(p));
    p.userData = *(void **)(ctx->mem_base + ctx->rdi + 0x58);
    ctx->rax = (uint64_t)(int64_t)sceNpProfileDialogOpenA(&p);
    SHIM_RETURN();
}
