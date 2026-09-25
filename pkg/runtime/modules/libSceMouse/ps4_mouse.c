#include "ps4_mouse.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static pthread_mutex_t g_mouse_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_mouse_inited = false;
static int32_t g_mouse_handles[2] = {-1, -1};

static uint64_t get_current_tick_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

int32_t sceMouseInit(void) {
    pthread_mutex_lock(&g_mouse_mutex);
    g_mouse_inited = true;
    pthread_mutex_unlock(&g_mouse_mutex);
    return 0;
}

int32_t sceMouseOpen(int32_t userId, int32_t type, int32_t index, const void *pParam) {
    (void)userId;
    (void)pParam;
    if (type != 0 || index < 0 || index > 1) {
        return (int32_t)0x80920002; // ORBIS_MOUSE_ERROR_INVALID_ARG
    }

    pthread_mutex_lock(&g_mouse_mutex);
    if (g_mouse_handles[index] != -1) {
        pthread_mutex_unlock(&g_mouse_mutex);
        return (int32_t)0x80920004; // ORBIS_MOUSE_ERROR_ALREADY_OPENED
    }
    g_mouse_handles[index] = index;
    int32_t handle = index;
    pthread_mutex_unlock(&g_mouse_mutex);

    return handle;
}

int32_t sceMouseClose(int32_t handle) {
    if (handle != 0 && handle != 1) {
        return (int32_t)0x80920003; // ORBIS_MOUSE_ERROR_INVALID_HANDLE
    }

    pthread_mutex_lock(&g_mouse_mutex);
    g_mouse_handles[handle] = -1;
    pthread_mutex_unlock(&g_mouse_mutex);
    return 0;
}

int32_t sceMouseRead(int32_t handle, OrbisMouseData *pData, int32_t num) {
    if (!pData || num < 1 || num > 64) {
        return (int32_t)0x80920002; // ORBIS_MOUSE_ERROR_INVALID_ARG
    }
    if (handle != 0 && handle != 1) {
        return (int32_t)0x80920003; // ORBIS_MOUSE_ERROR_INVALID_HANDLE
    }

    pthread_mutex_lock(&g_mouse_mutex);
    if (g_mouse_handles[handle] == -1) {
        pthread_mutex_unlock(&g_mouse_mutex);
        return (int32_t)0x80920003; // ORBIS_MOUSE_ERROR_INVALID_HANDLE
    }
    pthread_mutex_unlock(&g_mouse_mutex);

    memset(pData, 0, sizeof(OrbisMouseData) * (size_t)num);
    pData[0].timestamp = get_current_tick_usec();
    pData[0].connected = 0; // No physical mouse attached

    return 1;
}

// Shims
void shim_sceMouseInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceMouseInit();
}

void shim_sceMouseOpen(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceMouseOpen(
        (int32_t)ctx->rdi,
        (int32_t)ctx->rsi,
        (int32_t)ctx->rdx,
        (const void *)ctx->rcx
    );
}

void shim_sceMouseClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceMouseClose((int32_t)ctx->rdi);
}

void shim_sceMouseRead(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceMouseRead(
        (int32_t)ctx->rdi,
        (OrbisMouseData *)ctx->rsi,
        (int32_t)ctx->rdx
    );
}
