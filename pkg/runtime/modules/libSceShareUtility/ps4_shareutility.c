#include "ps4_shareutility.h"
#include <stdio.h>
#include <string.h>

static bool g_share_utility_initialized = false;
static bool g_share_utility_advance_initialized = false;

int32_t sceShareUtilityInitialize(const void *param) {
    (void)param;
    printf("[ps4-share] sceShareUtilityInitialize\n");
    g_share_utility_initialized = true;
    return ORBIS_SHARE_UTILITY_OK;
}

int32_t sceShareUtilityInitializeEx2(const void *param) {
    (void)param;
    printf("[ps4-share] sceShareUtilityInitializeEx2\n");
    g_share_utility_initialized = true;
    return ORBIS_SHARE_UTILITY_OK;
}

int32_t sceShareUtilityTerminate(void) {
    printf("[ps4-share] sceShareUtilityTerminate\n");
    g_share_utility_initialized = false;
    return ORBIS_SHARE_UTILITY_OK;
}

int32_t sceShareUtilityOpenShareMenu(const void *param) {
    (void)param;
    printf("[ps4-share] sceShareUtilityOpenShareMenu (desktop stub)\n");
    return ORBIS_SHARE_UTILITY_OK;
}

int32_t sceShareUtilityOpenShareMenuDefault(const void *param) {
    (void)param;
    printf("[ps4-share] sceShareUtilityOpenShareMenuDefault (desktop stub)\n");
    return ORBIS_SHARE_UTILITY_OK;
}

int32_t sceShareUtilityAdvanceInitialize(const void *param) {
    (void)param;
    printf("[ps4-share] sceShareUtilityAdvanceInitialize\n");
    g_share_utility_advance_initialized = true;
    return ORBIS_SHARE_UTILITY_OK;
}

int32_t sceShareUtilityAdvanceTerminate(void) {
    printf("[ps4-share] sceShareUtilityAdvanceTerminate\n");
    g_share_utility_advance_initialized = false;
    return ORBIS_SHARE_UTILITY_OK;
}

int32_t sceShareUtilityAdvanceSetUploadContentData(const void *data) {
    (void)data;
    printf("[ps4-share] sceShareUtilityAdvanceSetUploadContentData\n");
    return ORBIS_SHARE_UTILITY_OK;
}

// Shims
void shim_sceShareUtilityInitialize(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceShareUtilityInitialize(param);
    SHIM_RETURN();
}

void shim_sceShareUtilityInitializeEx2(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceShareUtilityInitializeEx2(param);
    SHIM_RETURN();
}

void shim_sceShareUtilityTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceShareUtilityTerminate();
    SHIM_RETURN();
}

void shim_sceShareUtilityOpenShareMenu(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceShareUtilityOpenShareMenu(param);
    SHIM_RETURN();
}

void shim_sceShareUtilityOpenShareMenuDefault(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceShareUtilityOpenShareMenuDefault(param);
    SHIM_RETURN();
}

void shim_sceShareUtilityAdvanceInitialize(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceShareUtilityAdvanceInitialize(param);
    SHIM_RETURN();
}

void shim_sceShareUtilityAdvanceTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceShareUtilityAdvanceTerminate();
    SHIM_RETURN();
}

void shim_sceShareUtilityAdvanceSetUploadContentData(GuestContext *ctx) {
    const void *data = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceShareUtilityAdvanceSetUploadContentData(data);
    SHIM_RETURN();
}
