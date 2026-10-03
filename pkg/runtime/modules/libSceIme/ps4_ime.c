#include "ps4_ime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_ime_keyboard_opened = 0;
static OrbisImeKeyboardParam g_ime_keyboard_param = {0};

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

int32_t sceImeKeyboardOpen(int32_t userId, const OrbisImeKeyboardParam *param) {
    (void)userId;
    if (!param) {
        return ORBIS_IME_ERROR_INVALID_ADDRESS;
    }
    g_ime_keyboard_param = *param;
    g_ime_keyboard_opened = 1;
    fprintf(stderr, "[ps4-recomp] sceImeKeyboardOpen: Keyboard handler opened for user 0x%x\n", userId);
    return 0;
}

int32_t sceImeKeyboardClose(int32_t userId) {
    (void)userId;
    g_ime_keyboard_opened = 0;
    memset(&g_ime_keyboard_param, 0, sizeof(g_ime_keyboard_param));
    fprintf(stderr, "[ps4-recomp] sceImeKeyboardClose: Keyboard handler closed for user 0x%x\n", userId);
    return 0;
}

int32_t sceImeKeyboardGetInfo(uint32_t resourceId, OrbisImeKeyboardInfo *info) {
    (void)resourceId;
    if (info) {
        memset(info, 0, sizeof(*info));
    }
    return 0;
}

int32_t sceImeKeyboardGetResourceId(int32_t userId, OrbisImeKeyboardResourceIdArray *resourceIdArray) {
    if (!resourceIdArray) {
        return ORBIS_IME_ERROR_INVALID_ADDRESS;
    }
    resourceIdArray->user_id = userId;
    for (int i = 0; i < 5; i++) {
        resourceIdArray->resource_id[i] = 0;
    }
    if (!g_ime_keyboard_opened) {
        return ORBIS_IME_ERROR_NOT_OPENED;
    }
    // Simulate "no external USB keyboard connected" - standard PS4 / Unity behavior
    return ORBIS_IME_ERROR_CONNECTION_FAILED;
}

int32_t sceImeUpdate(uint64_t handler) {
    (void)handler;
    return 0;
}

// Shims
void shim_sceImeKeyboardOpen(GuestContext *ctx) {
    int32_t userId = (int32_t)ctx->rdi;
    const OrbisImeKeyboardParam *param =
        ctx->rsi ? (const OrbisImeKeyboardParam *)recomp_guest_to_host(ctx, ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceImeKeyboardOpen(userId, param);
    SHIM_RETURN();
}

void shim_sceImeKeyboardClose(GuestContext *ctx) {
    int32_t userId = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceImeKeyboardClose(userId);
    SHIM_RETURN();
}

void shim_sceImeKeyboardGetInfo(GuestContext *ctx) {
    uint32_t resId = (uint32_t)ctx->rdi;
    OrbisImeKeyboardInfo *info =
        ctx->rsi ? (OrbisImeKeyboardInfo *)recomp_guest_to_host(ctx, ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceImeKeyboardGetInfo(resId, info);
    SHIM_RETURN();
}

void shim_sceImeKeyboardGetResourceId(GuestContext *ctx) {
    int32_t userId = (int32_t)ctx->rdi;
    OrbisImeKeyboardResourceIdArray *arr =
        ctx->rsi ? (OrbisImeKeyboardResourceIdArray *)recomp_guest_to_host(ctx, ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceImeKeyboardGetResourceId(userId, arr);
    SHIM_RETURN();
}

void shim_sceImeUpdate(GuestContext *ctx) {
    uint64_t handler = ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceImeUpdate(handler);
    SHIM_RETURN();
}
