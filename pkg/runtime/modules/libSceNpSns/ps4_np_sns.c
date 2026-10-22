// libSceNpSns (Facebook requests) and libSceNpSnsFacebookDialog for an
// offline host: requests can be created/deleted but every token fetch fails
// with SIGNED_OUT; the dialog opens, immediately finishes and reports
// "user cancelled" so callers never block.
#include "recomp_runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define SNS_ERROR_SIGNED_OUT             ((int32_t)0x80550006) // SCE_NP_ERROR_SIGNED_OUT
#define CDLG_ERROR_NOT_INITIALIZED       ((int32_t)0x80b80003)
#define CDLG_ERROR_ALREADY_INITIALIZED   ((int32_t)0x80b80004)
#define CDLG_ERROR_NOT_FINISHED          ((int32_t)0x80b80005)
#define CDLG_ERROR_INVALID_STATE         ((int32_t)0x80b80006)
#define CDLG_ERROR_ARG_NULL              ((int32_t)0x80b8000d)

#define CDLG_STATUS_NONE        0
#define CDLG_STATUS_INITIALIZED 1
#define CDLG_STATUS_RUNNING     2
#define CDLG_STATUS_FINISHED    3
#define CDLG_RESULT_USER_CANCELED 1

static pthread_mutex_t g_sns_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_sns_req_counter = 1;
static int32_t g_fbdlg_status = CDLG_STATUS_NONE;

//=============================================================================
// libSceNpSns
//=============================================================================

// int sceNpSnsFacebookCreateRequest(void) -> reqId
void shim_sceNpSnsFacebookCreateRequest(GuestContext *ctx) {
    pthread_mutex_lock(&g_sns_mutex);
    int32_t id = g_sns_req_counter++;
    if (g_sns_req_counter <= 0) g_sns_req_counter = 1;
    pthread_mutex_unlock(&g_sns_mutex);
    ctx->rax = (uint64_t)(int64_t)id;
    SHIM_RETURN();
}

// int sceNpSnsFacebookDeleteRequest(int reqId)
void shim_sceNpSnsFacebookDeleteRequest(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceNpSnsFacebookGetAccessToken(int reqId, const SceNpSnsFacebookAccessTokenParam *param,
//                                    SceNpSnsFacebookAccessTokenResult *result)
void shim_sceNpSnsFacebookGetAccessToken(GuestContext *ctx) {
    static int warned;
    if (!warned) {
        warned = 1;
        fprintf(stderr, "[libSceNpSns] sceNpSnsFacebookGetAccessToken -> SIGNED_OUT (offline)\n");
    }
    ctx->rax = (uint64_t)(int64_t)SNS_ERROR_SIGNED_OUT;
    SHIM_RETURN();
}

//=============================================================================
// libSceNpSnsFacebookDialog
//=============================================================================

void shim_sceNpSnsFacebookDialogInitialize(GuestContext *ctx) {
    pthread_mutex_lock(&g_sns_mutex);
    int32_t rc = 0;
    if (g_fbdlg_status != CDLG_STATUS_NONE) {
        rc = CDLG_ERROR_ALREADY_INITIALIZED;
    } else {
        g_fbdlg_status = CDLG_STATUS_INITIALIZED;
    }
    pthread_mutex_unlock(&g_sns_mutex);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceNpSnsFacebookDialogTerminate(GuestContext *ctx) {
    pthread_mutex_lock(&g_sns_mutex);
    int32_t rc = (g_fbdlg_status == CDLG_STATUS_NONE) ? CDLG_ERROR_NOT_INITIALIZED : 0;
    g_fbdlg_status = CDLG_STATUS_NONE;
    pthread_mutex_unlock(&g_sns_mutex);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// int sceNpSnsFacebookDialogOpen(const SceNpSnsFacebookDialogParam *param)
void shim_sceNpSnsFacebookDialogOpen(GuestContext *ctx) {
    if (!ctx->rdi) {
        ctx->rax = (uint64_t)(int64_t)CDLG_ERROR_ARG_NULL;
        SHIM_RETURN();
    }
    pthread_mutex_lock(&g_sns_mutex);
    int32_t rc = 0;
    if (g_fbdlg_status == CDLG_STATUS_INITIALIZED || g_fbdlg_status == CDLG_STATUS_FINISHED) {
        g_fbdlg_status = CDLG_STATUS_RUNNING;
    } else {
        rc = CDLG_ERROR_INVALID_STATE;
    }
    pthread_mutex_unlock(&g_sns_mutex);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// SceCommonDialogStatus sceNpSnsFacebookDialogUpdateStatus(void)
void shim_sceNpSnsFacebookDialogUpdateStatus(GuestContext *ctx) {
    pthread_mutex_lock(&g_sns_mutex);
    if (g_fbdlg_status == CDLG_STATUS_RUNNING) g_fbdlg_status = CDLG_STATUS_FINISHED;
    int32_t st = g_fbdlg_status;
    pthread_mutex_unlock(&g_sns_mutex);
    ctx->rax = (uint64_t)(int64_t)st;
    SHIM_RETURN();
}

// int sceNpSnsFacebookDialogGetResult(SceNpSnsFacebookDialogResult *result)
// Result starts with { int32 result; ... }: report USER_CANCELED.
void shim_sceNpSnsFacebookDialogGetResult(GuestContext *ctx) {
    if (!ctx->rdi) {
        ctx->rax = (uint64_t)(int64_t)CDLG_ERROR_ARG_NULL;
        SHIM_RETURN();
    }
    pthread_mutex_lock(&g_sns_mutex);
    int32_t st = g_fbdlg_status;
    pthread_mutex_unlock(&g_sns_mutex);
    if (st != CDLG_STATUS_FINISHED) {
        ctx->rax = (uint64_t)(int64_t)CDLG_ERROR_NOT_FINISHED;
        SHIM_RETURN();
    }
    *(int32_t *)(ctx->mem_base + ctx->rdi) = CDLG_RESULT_USER_CANCELED;
    ctx->rax = 0;
    SHIM_RETURN();
}
