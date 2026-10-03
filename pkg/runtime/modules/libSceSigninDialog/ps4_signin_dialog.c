#include "ps4_signin_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_signin_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisCommonDialogStatus g_signin_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static OrbisSigninDialogParam g_signin_param = {0};
static OrbisSigninDialogResult g_signin_result = {0};

static inline void *recomp_guest_to_host(const GuestContext *ctx, uint64_t gaddr) {
    if (!ctx || !ctx->mem_base || gaddr == 0) return NULL;
    if (gaddr >= (uintptr_t)ctx->mem_base && gaddr < (uintptr_t)ctx->mem_base + ctx->mem_size) {
        return (void *)gaddr;
    }
    if (gaddr < ctx->mem_size) {
        return (void *)(ctx->mem_base + gaddr);
    }
    return NULL;
}

int32_t sceSigninDialogInitialize(void) {
    pthread_mutex_lock(&g_signin_mutex);
    g_signin_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
    memset(&g_signin_result, 0, sizeof(g_signin_result));
    memset(&g_signin_param, 0, sizeof(g_signin_param));
    pthread_mutex_unlock(&g_signin_mutex);
    return 0;
}

int32_t sceSigninDialogOpen(const OrbisSigninDialogParam *param) {
    if (!param) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }
    pthread_mutex_lock(&g_signin_mutex);
    if (g_signin_status != ORBIS_COMMON_DIALOG_STATUS_INITIALIZED &&
        g_signin_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_signin_mutex);
        return (int32_t)0x80b80006; // INVALID_STATE
    }
    g_signin_param = *param;
    memset(&g_signin_result, 0, sizeof(g_signin_result));
    g_signin_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
    g_signin_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
    fprintf(stderr, "[ps4-recomp] sceSigninDialogOpen: User sign-in dialog opened\n");
    pthread_mutex_unlock(&g_signin_mutex);
    return 0;
}

OrbisCommonDialogStatus sceSigninDialogGetStatus(void) {
    pthread_mutex_lock(&g_signin_mutex);
    OrbisCommonDialogStatus st = g_signin_status;
    pthread_mutex_unlock(&g_signin_mutex);
    return st;
}

OrbisCommonDialogStatus sceSigninDialogUpdateStatus(void) {
    pthread_mutex_lock(&g_signin_mutex);
    if (g_signin_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        // User is locally authenticated immediately
        g_signin_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
        g_signin_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
        fprintf(stderr, "[ps4-recomp] sceSigninDialogUpdateStatus: Sign-in completed (FINISHED)\n");
    }
    OrbisCommonDialogStatus st = g_signin_status;
    pthread_mutex_unlock(&g_signin_mutex);
    return st;
}

int32_t sceSigninDialogGetResult(OrbisSigninDialogResult *result) {
    if (!result) {
        return (int32_t)0x80b8000d;
    }
    pthread_mutex_lock(&g_signin_mutex);
    if (g_signin_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_signin_mutex);
        return (int32_t)0x80b80006;
    }
    *result = g_signin_result;
    pthread_mutex_unlock(&g_signin_mutex);
    return 0;
}

int32_t sceSigninDialogClose(void) {
    pthread_mutex_lock(&g_signin_mutex);
    if (g_signin_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        g_signin_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    }
    pthread_mutex_unlock(&g_signin_mutex);
    return 0;
}

int32_t sceSigninDialogTerminate(void) {
    pthread_mutex_lock(&g_signin_mutex);
    g_signin_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
    pthread_mutex_unlock(&g_signin_mutex);
    return 0;
}

// Shims
void shim_sceSigninDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSigninDialogInitialize();
    SHIM_RETURN();
}

void shim_sceSigninDialogOpen(GuestContext *ctx) {
    const OrbisSigninDialogParam *param =
        ctx->rdi ? (const OrbisSigninDialogParam *)recomp_guest_to_host(ctx, ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSigninDialogOpen(param);
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
        ctx->rdi ? (OrbisSigninDialogResult *)recomp_guest_to_host(ctx, ctx->rdi) : NULL;
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
