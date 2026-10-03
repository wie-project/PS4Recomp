#include "ps4_web_browser_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_web_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisCommonDialogStatus g_web_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static OrbisWebBrowserDialogParam g_web_param = {0};
static OrbisWebBrowserDialogResult g_web_result = {0};

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

int32_t sceWebBrowserDialogInitialize(void) {
    pthread_mutex_lock(&g_web_mutex);
    g_web_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
    memset(&g_web_result, 0, sizeof(g_web_result));
    memset(&g_web_param, 0, sizeof(g_web_param));
    pthread_mutex_unlock(&g_web_mutex);
    return 0;
}

int32_t sceWebBrowserDialogOpen(const OrbisWebBrowserDialogParam *param) {
    if (!param) {
        return (int32_t)0x80b8000d;
    }
    pthread_mutex_lock(&g_web_mutex);
    if (g_web_status != ORBIS_COMMON_DIALOG_STATUS_INITIALIZED &&
        g_web_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_web_mutex);
        return (int32_t)0x80b80006;
    }
    g_web_param = *param;
    memset(&g_web_result, 0, sizeof(g_web_result));
    g_web_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
    g_web_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
    fprintf(stderr, "[ps4-recomp] sceWebBrowserDialogOpen: Web browser dialog opened\n");
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
        g_web_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
        fprintf(stderr, "[ps4-recomp] sceWebBrowserDialogUpdateStatus: Web browser completed (FINISHED)\n");
    }
    OrbisCommonDialogStatus st = g_web_status;
    pthread_mutex_unlock(&g_web_mutex);
    return st;
}

int32_t sceWebBrowserDialogGetResult(OrbisWebBrowserDialogResult *result) {
    if (!result) {
        return (int32_t)0x80b8000d;
    }
    pthread_mutex_lock(&g_web_mutex);
    if (g_web_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_web_mutex);
        return (int32_t)0x80b80006;
    }
    *result = g_web_result;
    pthread_mutex_unlock(&g_web_mutex);
    return 0;
}

int32_t sceWebBrowserDialogClose(void) {
    pthread_mutex_lock(&g_web_mutex);
    if (g_web_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        g_web_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    }
    pthread_mutex_unlock(&g_web_mutex);
    return 0;
}

int32_t sceWebBrowserDialogTerminate(void) {
    pthread_mutex_lock(&g_web_mutex);
    g_web_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
    pthread_mutex_unlock(&g_web_mutex);
    return 0;
}

// Shims
void shim_sceWebBrowserDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceWebBrowserDialogInitialize();
    SHIM_RETURN();
}

void shim_sceWebBrowserDialogOpen(GuestContext *ctx) {
    const OrbisWebBrowserDialogParam *param =
        ctx->rdi ? (const OrbisWebBrowserDialogParam *)recomp_guest_to_host(ctx, ctx->rdi) : NULL;
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
        ctx->rdi ? (OrbisWebBrowserDialogResult *)recomp_guest_to_host(ctx, ctx->rdi) : NULL;
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
