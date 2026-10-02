#include "ps4_social_screen.h"
#include <string.h>

int32_t sceSocialScreenInitialize(void) {
    return 0;
}

int32_t sceSocialScreenTerminate(void) {
    return 0;
}

int32_t sceSocialScreenSetMode(int32_t mode) {
    (void)mode;
    return 0;
}

int32_t sceSocialScreenInitializeSeparateModeParameter(void *param) {
    if (param) {
        memset(param, 0, 64);
    }
    return 0;
}

int32_t sceSocialScreenOpenSeparateMode(void *param) {
    (void)param;
    return 0;
}

int32_t sceSocialScreenCloseSeparateMode(void) {
    return 0;
}

int32_t sceSocialScreenConfigureSeparateMode(void *param) {
    (void)param;
    return 0;
}

void shim_sceSocialScreenInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSocialScreenInitialize();
    SHIM_RETURN();
}

void shim_sceSocialScreenTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSocialScreenTerminate();
    SHIM_RETURN();
}

void shim_sceSocialScreenSetMode(GuestContext *ctx) {
    int32_t mode = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceSocialScreenSetMode(mode);
    SHIM_RETURN();
}

void shim_sceSocialScreenInitializeSeparateModeParameter(GuestContext *ctx) {
    uint64_t paramGuest = ctx->rdi;
    void *param = paramGuest ? (void *)(ctx->mem_base + paramGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSocialScreenInitializeSeparateModeParameter(param);
    SHIM_RETURN();
}

void shim_sceSocialScreenOpenSeparateMode(GuestContext *ctx) {
    uint64_t paramGuest = ctx->rdi;
    void *param = paramGuest ? (void *)(ctx->mem_base + paramGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSocialScreenOpenSeparateMode(param);
    SHIM_RETURN();
}

void shim_sceSocialScreenCloseSeparateMode(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSocialScreenCloseSeparateMode();
    SHIM_RETURN();
}

void shim_sceSocialScreenConfigureSeparateMode(GuestContext *ctx) {
    uint64_t paramGuest = ctx->rdi;
    void *param = paramGuest ? (void *)(ctx->mem_base + paramGuest) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceSocialScreenConfigureSeparateMode(param);
    SHIM_RETURN();
}
