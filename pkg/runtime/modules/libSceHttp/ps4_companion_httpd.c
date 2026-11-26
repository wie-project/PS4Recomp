// SPDX-License-Identifier: GPL-2.0-or-later

// libSceCompanionHttpd (shadPS4 companion/companion_httpd.cpp).
#include "ps4_companion_httpd.h"
#include "ps4_hle_stub.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static inline void *guest_to_host(const GuestContext *ctx, uint64_t addr) {
    if (!addr) return NULL;
    if (ctx && ctx->mem_base && addr < ctx->mem_size) {
        return (void *)(ctx->mem_base + addr);
    }
    return (void *)addr;
}

static pthread_mutex_t g_companion_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_companion_inited = false;
static bool g_companion_started = false;
static void *g_companion_cb = NULL;
static void *g_companion_userdata = NULL;

int32_t sceCompanionHttpdInitialize2(const void *param) {
    (void)param;
    pthread_mutex_lock(&g_companion_mutex);
    if (g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_ALREADY_INITIALIZED;
    }
    g_companion_inited = true;
    g_companion_started = false;
    g_companion_cb = NULL;
    g_companion_userdata = NULL;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdTerminate(void) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    g_companion_inited = false;
    g_companion_started = false;
    g_companion_cb = NULL;
    g_companion_userdata = NULL;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdStart(void) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    if (g_companion_started) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_ALREADY_STARTED;
    }
    g_companion_started = true;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdStop(void) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    if (!g_companion_started) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_STARTED;
    }
    g_companion_started = false;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdGetEvent(OrbisCompanionHttpdEvent *event) {
    if (!event) {
        return ORBIS_COMPANION_HTTPD_ERROR_INVALID_PARAM;
    }
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    memset(event, 0, sizeof(*event));
    event->event = ORBIS_COMPANION_HTTPD_EVENT_DISCONNECT;
    pthread_mutex_unlock(&g_companion_mutex);
    return ORBIS_COMPANION_HTTPD_ERROR_NO_EVENT;
}

int32_t sceCompanionHttpdRegisterRequestCallback2(void *cb, void *userData) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    g_companion_cb = cb;
    g_companion_userdata = userData;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdUnregisterRequestCallback(void) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    g_companion_cb = NULL;
    g_companion_userdata = NULL;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

// No second-screen client ever connects (there is no companion app server), so the request
// callbacks never run; they are kept as registered.
static uint64_t g_companion_body_cb, g_companion_body_param;

int32_t sceCompanionHttpdInitialize(void) {
    return sceCompanionHttpdInitialize2(NULL);
}

int32_t sceCompanionHttpdRegisterRequestCallback(void *cb, void *userData) {
    return sceCompanionHttpdRegisterRequestCallback2(cb, userData);
}

int32_t sceCompanionHttpdRegisterRequestBodyReceptionCallback(uint64_t cb, uint64_t param) {
    pthread_mutex_lock(&g_companion_mutex);
    int32_t r = 0;
    if (!g_companion_inited) {
        r = ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    } else {
        g_companion_body_cb = cb;
        g_companion_body_param = param;
    }
    pthread_mutex_unlock(&g_companion_mutex);
    return r;
}

int32_t sceCompanionHttpdUnregisterRequestBodyReceptionCallback(void) {
    return sceCompanionHttpdRegisterRequestBodyReceptionCallback(0, 0);
}

void shim_sceCompanionHttpdInitialize2(GuestContext *ctx) {
    const void *param = (const void *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdInitialize2(param);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdTerminate();
    SHIM_RETURN();
}

void shim_sceCompanionHttpdStart(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdStart();
    SHIM_RETURN();
}

void shim_sceCompanionHttpdStop(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdStop();
    SHIM_RETURN();
}

void shim_sceCompanionHttpdGetEvent(GuestContext *ctx) {
    OrbisCompanionHttpdEvent *event = (OrbisCompanionHttpdEvent *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdGetEvent(event);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdRegisterRequestCallback2(GuestContext *ctx) {
    void *cb = NULL;
    void *userData = NULL;
    if (ctx->rdi > 0x10000) {
        cb = (void *)guest_to_host(ctx, ctx->rdi);
        userData = (void *)guest_to_host(ctx, ctx->rsi);
    } else {
        cb = (void *)guest_to_host(ctx, ctx->rsi);
        userData = (void *)guest_to_host(ctx, ctx->rdx);
    }
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdRegisterRequestCallback2(cb, userData);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdUnregisterRequestCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdUnregisterRequestCallback();
    SHIM_RETURN();
}

void shim_sceCompanionHttpdInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdInitialize();
    SHIM_RETURN();
}

void shim_sceCompanionHttpdRegisterRequestCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdRegisterRequestCallback((void *)(uintptr_t)ctx->rdi,
                                                                           (void *)(uintptr_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdRegisterRequestBodyReceptionCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdRegisterRequestBodyReceptionCallback(ctx->rdi, ctx->rsi);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdUnregisterRequestBodyReceptionCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdUnregisterRequestBodyReceptionCallback();
    SHIM_RETURN();
}

// Responses are only built inside request callbacks, and the second-screen status and user
// queries have no client to report: stubs in shadPS4 as well.
PS4_HLE_STUB(sceCompanionHttpdAddHeader)
PS4_HLE_STUB(sceCompanionHttpdSetBody)
PS4_HLE_STUB(sceCompanionHttpdSetStatus)
PS4_HLE_STUB(sceCompanionHttpdGet2ndScreenStatus)
PS4_HLE_STUB(sceCompanionHttpdGetUserId)
PS4_HLE_STUB(sceCompanionHttpdOptParamInitialize)
