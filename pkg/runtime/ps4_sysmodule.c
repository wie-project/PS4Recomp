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

#define DYN_LIB_EXAMPLE_HANDLE 100
#define GUEST_ADDR_LIB_EXAMPLE_FN 0x7fff0000ULL

static void shim_testLibraryFunction(GuestContext *ctx) {
    uint64_t bufGuest = ctx->rdi;
    size_t bufSize = (size_t)ctx->rsi;
    int32_t num = (int32_t)ctx->rdx;

    char *buf = bufGuest ? (char *)(ctx->mem_base + bufGuest) : NULL;
    if (buf && bufSize > 0) {
        snprintf(buf, bufSize, "Hi I'm from the library! You passed: %d", num);
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

int32_t sceKernelLoadStartModule(const char *name, size_t argc, const void *argv, uint32_t flags, void *opt, int *res) {
    (void)argc;
    (void)argv;
    (void)flags;
    (void)opt;
    if (res) *res = 0;

    printf("[ps4-recomp] sceKernelLoadStartModule: %s\n", name ? name : "(null)");
    if (!name) {
        return -EINVAL;
    }

    if (strstr(name, "libExample")) {
        recomp_register_fn(GUEST_ADDR_LIB_EXAMPLE_FN, shim_testLibraryFunction);
        return DYN_LIB_EXAMPLE_HANDLE;
    }

    return 1; // Generic loaded module handle
}

int32_t sceKernelDlsym(int32_t handle, const char *symbol, void **addr) {
    if (!symbol || !addr) {
        return -EINVAL;
    }

    printf("[ps4-recomp] sceKernelDlsym: handle %d, symbol %s\n", handle, symbol);
    if (handle == DYN_LIB_EXAMPLE_HANDLE || handle == 1) {
        if (strcmp(symbol, "_Z19testLibraryFunctionPcmi") == 0) {
            *addr = (void *)(uintptr_t)GUEST_ADDR_LIB_EXAMPLE_FN;
            return 0;
        }
    }

    *addr = NULL;
    return -ESRCH;
}

void shim_sceKernelLoadStartModule(GuestContext *ctx) {
    uint64_t nameGuest = ctx->rdi;
    size_t argc = (size_t)ctx->rsi;
    uint64_t argvGuest = ctx->rdx;
    uint32_t flags = (uint32_t)ctx->rcx;
    uint64_t optGuest = ctx->r8;
    uint64_t resGuest = ctx->r9;

    const char *name = nameGuest ? (const char *)(ctx->mem_base + nameGuest) : NULL;
    const void *argv = argvGuest ? (const void *)(ctx->mem_base + argvGuest) : NULL;
    void *opt = optGuest ? (void *)(ctx->mem_base + optGuest) : NULL;
    int *res = resGuest ? (int *)(ctx->mem_base + resGuest) : NULL;

    int32_t ret = sceKernelLoadStartModule(name, argc, argv, flags, opt, res);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceKernelDlsym(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t symGuest = ctx->rsi;
    uint64_t addrGuest = ctx->rdx;

    const char *symbol = symGuest ? (const char *)(ctx->mem_base + symGuest) : NULL;
    void **addr = addrGuest ? (void **)(ctx->mem_base + addrGuest) : NULL;

    int32_t ret = sceKernelDlsym(handle, symbol, addr);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

