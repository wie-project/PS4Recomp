#include "ps4_sysmodule.h"
#include <stdio.h>
#include <pthread.h>
#include <errno.h>

#define MAX_LOADED_MODULES 64

static uint32_t g_loaded_modules[MAX_LOADED_MODULES];
static int g_loaded_modules_count = 0;
static pthread_mutex_t g_sysmodule_mutex = PTHREAD_MUTEX_INITIALIZER;

int32_t sceSysmoduleLoadModule(uint16_t moduleId) {
    pthread_mutex_lock(&g_sysmodule_mutex);
    for (int i = 0; i < g_loaded_modules_count; i++) {
        if (g_loaded_modules[i] == (uint32_t)moduleId) {
            pthread_mutex_unlock(&g_sysmodule_mutex);
            return 0; // already loaded
        }
    }
    if (g_loaded_modules_count < MAX_LOADED_MODULES) {
        g_loaded_modules[g_loaded_modules_count++] = (uint32_t)moduleId;
    }
    pthread_mutex_unlock(&g_sysmodule_mutex);
    return 0;
}

int32_t sceSysmoduleIsLoaded(uint16_t moduleId) {
    pthread_mutex_lock(&g_sysmodule_mutex);
    for (int i = 0; i < g_loaded_modules_count; i++) {
        if (g_loaded_modules[i] == (uint32_t)moduleId) {
            pthread_mutex_unlock(&g_sysmodule_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_sysmodule_mutex);
    return 0;
}

int32_t sceSysmoduleUnloadModule(uint16_t moduleId) {
    pthread_mutex_lock(&g_sysmodule_mutex);
    for (int i = 0; i < g_loaded_modules_count; i++) {
        if (g_loaded_modules[i] == (uint32_t)moduleId) {
            g_loaded_modules[i] = g_loaded_modules[--g_loaded_modules_count];
            break;
        }
    }
    pthread_mutex_unlock(&g_sysmodule_mutex);
    return 0;
}

uint32_t sceSysmoduleLoadModuleInternal(uint32_t moduleId) {
    pthread_mutex_lock(&g_sysmodule_mutex);
    for (int i = 0; i < g_loaded_modules_count; i++) {
        if (g_loaded_modules[i] == moduleId) {
            pthread_mutex_unlock(&g_sysmodule_mutex);
            return 0;
        }
    }
    if (g_loaded_modules_count < MAX_LOADED_MODULES) {
        g_loaded_modules[g_loaded_modules_count++] = moduleId;
    }
    pthread_mutex_unlock(&g_sysmodule_mutex);
    return 0;
}

int32_t sceSysmoduleUnloadModuleInternal(uint32_t moduleId) {
    return sceSysmoduleUnloadModule((uint16_t)moduleId);
}

// Guest ABI shims
void shim_sceSysmoduleLoadModule(GuestContext *ctx) {
    uint16_t moduleId = (uint16_t)ctx->rdi;
    int32_t ret = sceSysmoduleLoadModule(moduleId);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceSysmoduleIsLoaded(GuestContext *ctx) {
    uint16_t moduleId = (uint16_t)ctx->rdi;
    int32_t ret = sceSysmoduleIsLoaded(moduleId);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceSysmoduleUnloadModule(GuestContext *ctx) {
    uint16_t moduleId = (uint16_t)ctx->rdi;
    int32_t ret = sceSysmoduleUnloadModule(moduleId);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceSysmoduleLoadModuleInternal(GuestContext *ctx) {
    uint32_t moduleId = (uint32_t)ctx->rdi;
    uint32_t ret = sceSysmoduleLoadModuleInternal(moduleId);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
}

void shim_sceSysmoduleUnloadModuleInternal(GuestContext *ctx) {
    uint32_t moduleId = (uint32_t)ctx->rdi;
    int32_t ret = sceSysmoduleUnloadModuleInternal(moduleId);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}
