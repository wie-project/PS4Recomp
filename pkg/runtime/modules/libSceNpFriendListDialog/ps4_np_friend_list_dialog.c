// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_np_friend_list_dialog.h"
#include <pthread.h>
#include <string.h>

static pthread_mutex_t g_friend_dialog_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisCommonDialogStatus g_friend_dialog_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static void *g_friend_dialog_userdata = NULL;

int32_t sceNpFriendListDialogInitialize(void) {
    pthread_mutex_lock(&g_friend_dialog_mutex);
    if (g_friend_dialog_status != ORBIS_COMMON_DIALOG_STATUS_NONE) {
        pthread_mutex_unlock(&g_friend_dialog_mutex);
        return (int32_t)0x80b80004; // ALREADY_INITIALIZED
    }
    g_friend_dialog_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
    g_friend_dialog_userdata = NULL;
    pthread_mutex_unlock(&g_friend_dialog_mutex);
    return 0;
}

int32_t sceNpFriendListDialogOpenA(const void *param) {
    if (!param) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }
    pthread_mutex_lock(&g_friend_dialog_mutex);
    if (g_friend_dialog_status != ORBIS_COMMON_DIALOG_STATUS_INITIALIZED &&
        g_friend_dialog_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_friend_dialog_mutex);
        return (int32_t)0x80b80006; // INVALID_STATE
    }
    g_friend_dialog_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
    pthread_mutex_unlock(&g_friend_dialog_mutex);
    return 0;
}

OrbisCommonDialogStatus sceNpFriendListDialogUpdateStatus(void) {
    pthread_mutex_lock(&g_friend_dialog_mutex);
    if (g_friend_dialog_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        // Offline host: immediately conclude as user canceled
        g_friend_dialog_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    }
    OrbisCommonDialogStatus st = g_friend_dialog_status;
    pthread_mutex_unlock(&g_friend_dialog_mutex);
    return st;
}

OrbisCommonDialogStatus sceNpFriendListDialogGetStatus(void) {
    pthread_mutex_lock(&g_friend_dialog_mutex);
    OrbisCommonDialogStatus st = g_friend_dialog_status;
    pthread_mutex_unlock(&g_friend_dialog_mutex);
    return st;
}

int32_t sceNpFriendListDialogGetResultA(OrbisNpFriendListDialogResultA *result) {
    if (!result) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }
    pthread_mutex_lock(&g_friend_dialog_mutex);
    if (g_friend_dialog_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_friend_dialog_mutex);
        return (int32_t)0x80b80005; // NOT_FINISHED
    }
    result->result = ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED;
    result->reserved0 = 0;
    result->userData = g_friend_dialog_userdata;
    pthread_mutex_unlock(&g_friend_dialog_mutex);
    return 0;
}

int32_t sceNpFriendListDialogClose(void) {
    pthread_mutex_lock(&g_friend_dialog_mutex);
    int32_t rc = 0;
    if (g_friend_dialog_status == ORBIS_COMMON_DIALOG_STATUS_NONE) {
        rc = (int32_t)0x80b80003; // NOT_INITIALIZED
    } else if (g_friend_dialog_status != ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        rc = (int32_t)0x80b8000b; // NOT_RUNNING
    } else {
        g_friend_dialog_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    }
    pthread_mutex_unlock(&g_friend_dialog_mutex);
    return rc;
}

int32_t sceNpFriendListDialogTerminate(void) {
    pthread_mutex_lock(&g_friend_dialog_mutex);
    g_friend_dialog_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
    g_friend_dialog_userdata = NULL;
    pthread_mutex_unlock(&g_friend_dialog_mutex);
    return 0;
}

void shim_sceNpFriendListDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceNpFriendListDialogInitialize();
    SHIM_RETURN();
}

void shim_sceNpFriendListDialogOpenA(GuestContext *ctx) {
    const void *param = (ctx && ctx->rdi && ctx->mem_base && ctx->rdi < ctx->mem_size)
                            ? (const void *)(ctx->mem_base + ctx->rdi)
                            : NULL;
    ctx->rax = (uint64_t)(int64_t)sceNpFriendListDialogOpenA(param);
    SHIM_RETURN();
}

void shim_sceNpFriendListDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpFriendListDialogUpdateStatus();
    SHIM_RETURN();
}

void shim_sceNpFriendListDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpFriendListDialogGetStatus();
    SHIM_RETURN();
}

void shim_sceNpFriendListDialogGetResultA(GuestContext *ctx) {
    OrbisNpFriendListDialogResultA *result =
        (ctx && ctx->rdi && ctx->mem_base && ctx->rdi + sizeof(OrbisNpFriendListDialogResultA) <= ctx->mem_size)
            ? (OrbisNpFriendListDialogResultA *)(ctx->mem_base + ctx->rdi)
            : NULL;
    ctx->rax = (uint64_t)(int64_t)sceNpFriendListDialogGetResultA(result);
    SHIM_RETURN();
}

void shim_sceNpFriendListDialogClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceNpFriendListDialogClose();
    SHIM_RETURN();
}

void shim_sceNpFriendListDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceNpFriendListDialogTerminate();
    SHIM_RETURN();
}
