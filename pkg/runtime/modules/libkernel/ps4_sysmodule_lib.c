// SPDX-License-Identifier: GPL-2.0-or-later

// libSceSysmodule, ported from shadPS4 (src/core/libraries/sysmodule). Every module ID maps to an
// entry of the firmware's module table; loading one loads its dependencies first and counts the
// loads. A game module (/app0/sce_module) goes through sceKernelLoadStartModule, which finds the
// recompiled PRX and runs its initializers. A system module has no code to load here: all of them
// are HLE (their functions are compiled into the runtime), so a load only marks it loaded under a
// stand-in handle, as shadPS4 does when no LLE copy of the module is present.
//
// Differences from shadPS4:
// - unloadModule decrements the load counts (shadPS4 decrements a copy of the entry, so a module
//   never becomes unloaded);
// - sceSysmoduleUnloadModuleInternal unloads by internal ID (shadPS4 has a stub);
// - the table lock is recursive, as a game module's initializers may load modules themselves.

#include "ps4_sysmodule.h"
#include "ps4_hle_stub.h"
#include "ps4_kernel_sys.h"
#include "ps4_sysmodule_table.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(g_modules_array) / sizeof(g_modules_array[0]) == SYSMODULE_NUM_MODULES, "module table");

#define ORBIS_OK 0
#define ORBIS_KERNEL_ERROR_EINVAL ((int32_t)0x80020016)
#define ORBIS_KERNEL_ERROR_ESTART ((int32_t)0x80020064)

#define FW_200 0x2000000u
#define FW_300 0x3000000u
#define FW_450 0x4500000u
#define FW_700 0x7000000u
#define FW_750 0x7500000u
#define FW_1150 0x11500000u

// Table indices with special handling.
#define IDX_LIBSCEFIOS2 3
#define IDX_LIBC 4
#define IDX_DISCMAP 0x22

// A retail console is emulated: debug modules (SYSMODULE_FLAG_DEBUG) never load.
#define IS_DEVKIT 0

static pthread_mutex_t g_mutex;
static pthread_once_t g_mutex_once = PTHREAD_ONCE_INIT;
static int32_t g_stub_handle = 100;

static void mutex_init(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

static void lock(void) {
    pthread_once(&g_mutex_once, mutex_init);
    pthread_mutex_lock(&g_mutex);
}

static void unlock(void) {
    pthread_mutex_unlock(&g_mutex);
}

static int is_debug_flag(const SysmoduleEntry *m) {
    return (m->flags & SYSMODULE_FLAG_DEBUG) != 0;
}

static int32_t get_module_handle(uint32_t id, int32_t *handle) {
    if (id == 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    for (size_t i = 0; i < SYSMODULE_NUM_MODULES; i++) {
        const SysmoduleEntry *m = &g_modules_array[i];
        if (m->id != id) {
            continue;
        }
        if (m->is_loaded < 1) {
            return ORBIS_SYSMODULE_NOT_LOADED;
        }
        if (handle) {
            *handle = m->handle;
        }
        return ORBIS_OK;
    }
    return ORBIS_SYSMODULE_INVALID_ID;
}

bool ps4_sysmodule_hides_name(const char *module_name) {
    if (!module_name) {
        return false;
    }
    for (size_t i = 0; i < SYSMODULE_NUM_MODULES; i++) {
        const SysmoduleEntry *m = &g_modules_array[i];
        if ((m->flags & SYSMODULE_FLAG_GAME) == 0) {
            continue;
        }
        // The table stores names without extension.
        char name[0x100];
        snprintf(name, sizeof(name), "%s.prx", m->name);
        if (strcmp(module_name, name) == 0) {
            return true;
        }
        // libSceFios2 and libc are checked as both prx and sprx.
        if ((i == IDX_LIBSCEFIOS2 && strcmp(module_name, "libSceFios2.sprx") == 0) ||
            (i == IDX_LIBC && strcmp(module_name, "libc.sprx") == 0)) {
            return true;
        }
    }
    return false;
}

static int is_debug_module(uint32_t id) {
    for (size_t i = 0; i < SYSMODULE_NUM_MODULES; i++) {
        if (g_modules_array[i].id == id && is_debug_flag(&g_modules_array[i])) {
            return 1;
        }
    }
    return 0;
}

static int32_t validate_module_id(uint32_t id, uint32_t sdk_ver) {
    if ((id & 0x7fffffff) == 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    if (id == 0xb8 && sdk_ver >= FW_750) { // libSceGameCustomDialog
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    if (id == 0xb0 && sdk_ver >= FW_700) { // libSceNpSnsFacebookDialog
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    if (id == 0x80 && sdk_ver >= FW_300) { // libSceJson
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    if (is_debug_module(id) && !IS_DEVKIT) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    return ORBIS_OK;
}

static int32_t load_module_internal(GuestContext *ctx, size_t index, int32_t argc, const void *argv,
                                    int32_t *res_out) {
    SysmoduleEntry *m = &g_modules_array[index];
    if (m->is_loaded > 0) {
        m->is_loaded++;
        return ORBIS_OK;
    }

    int start_result = 0;
    if (m->flags & SYSMODULE_FLAG_GAME) {
        char path[0x120];
        snprintf(path, sizeof(path), "/app0/sce_module/%s.prx", m->name);
        int32_t handle = sceKernelLoadStartModule(path, (size_t)(argc < 0 ? 0 : argc), argv, 0, NULL, &start_result);
        if (handle < 0) {
            fprintf(stderr, "[ps4-sysmodule] Failed to load game library %s\n", path);
            return handle;
        }
        if (ctx) {
            recomp_module_start(ctx, handle);
        }
        m->handle = handle;
    } else {
        m->handle = g_stub_handle++;
    }
    m->is_loaded++;
    if (res_out) {
        *res_out = start_result;
    }
    return ORBIS_OK;
}

static int32_t load_module(GuestContext *ctx, uint32_t id, int32_t argc, const void *argv, int32_t *res_out) {
    const SysmoduleEntry *req = NULL;
    for (size_t i = 0; i < SYSMODULE_NUM_MODULES; i++) {
        if (g_modules_array[i].id == id) {
            req = &g_modules_array[i];
            break;
        }
    }
    if (!req || id == 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    // Every loadable module lists itself first; ORBIS_SYSMODULE_LOCK_FAILED is the library's
    // generic internal error.
    if (!req->to_load) {
        return ORBIS_SYSMODULE_LOCK_FAILED;
    }
    fprintf(stderr, "[ps4-sysmodule] Loading %s\n", req->name);

    // Dependencies first (the list is walked backwards); only the requested module gets the
    // arguments and reports its start result.
    for (int32_t i = req->num_to_load - 1; i >= 0; i--) {
        const uint16_t index = req->to_load[i];
        if (!IS_DEVKIT && is_debug_flag(&g_modules_array[index])) {
            continue;
        }
        int32_t result = i != 0 ? load_module_internal(ctx, index, 0, NULL, NULL)
                                : load_module_internal(ctx, index, argc, argv, res_out);
        if (result != ORBIS_OK) {
            return result;
        }
    }
    return ORBIS_OK;
}

static int32_t unload_module(uint32_t id, int32_t *res_out) {
    SysmoduleEntry *mod = NULL;
    for (size_t i = 0; i < SYSMODULE_NUM_MODULES; i++) {
        SysmoduleEntry *m = &g_modules_array[i];
        if (m->id != id || i == IDX_DISCMAP) { // libSceDiscMap is never unloaded
            continue;
        }
        // A preloaded module loaded only once stays loaded.
        for (size_t k = 0; k < sizeof(g_preload_list_2) / sizeof(g_preload_list_2[0]); k++) {
            if (g_preload_list_2[k] == i && m->is_loaded == 1) {
                return ORBIS_OK;
            }
        }
        mod = m;
        break;
    }
    if (!mod || id == 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    if (mod->num_to_load == 0 || !mod->to_load) {
        return ORBIS_SYSMODULE_LOCK_FAILED;
    }

    for (int32_t i = 0; i < mod->num_to_load; i++) {
        SysmoduleEntry *dep = &g_modules_array[mod->to_load[i]];
        if (is_debug_flag(dep) && !IS_DEVKIT) {
            continue;
        }
        if (dep->is_loaded == 0) {
            return ORBIS_SYSMODULE_NOT_LOADED;
        }
        // The console stops and unloads the module once the count drops to zero; recompiled
        // code stays mapped, so only the count changes.
        dep->is_loaded--;
        if (i == 0 && res_out) {
            *res_out = ORBIS_OK;
        }
    }
    return ORBIS_OK;
}

int32_t sceSysmoduleGetModuleHandleInternal(uint32_t id, int32_t *handle) {
    if ((id & 0x7fffffff) == 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    lock();
    int32_t r = get_module_handle(id, handle);
    unlock();
    return r;
}

int32_t sceSysmoduleIsLoaded(uint16_t id) {
    if (id == 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    lock();
    int32_t r = get_module_handle(id, NULL);
    unlock();
    return r;
}

int32_t sceSysmoduleIsLoadedInternal(uint32_t id) {
    if ((id & 0x7fffffff) == 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    lock();
    int32_t r = get_module_handle(id, NULL);
    unlock();
    return r;
}

int32_t sceSysmoduleLoadModule(GuestContext *ctx, uint16_t id, uint32_t sdk_ver) {
    fprintf(stderr, "[ps4-sysmodule] sceSysmoduleLoadModule(0x%x)\n", id);
    int32_t result = validate_module_id(id, sdk_ver);
    if (result < ORBIS_OK) {
        return result;
    }
    lock();
    result = load_module(ctx, id, 0, NULL, NULL);
    unlock();
    if (result == ORBIS_KERNEL_ERROR_ESTART) {
        return sdk_ver < FW_1150 ? ORBIS_KERNEL_ERROR_EINVAL : ORBIS_KERNEL_ERROR_ESTART;
    }
    return result;
}

int32_t sceSysmoduleLoadModuleInternal(GuestContext *ctx, uint32_t id, uint32_t sdk_ver) {
    fprintf(stderr, "[ps4-sysmodule] sceSysmoduleLoadModuleInternal(0x%x)\n", id);
    int32_t result = validate_module_id(id, sdk_ver);
    if (result < ORBIS_OK) {
        return result;
    }
    lock();
    result = load_module(ctx, id, 0, NULL, NULL);
    unlock();
    return result;
}

int32_t sceSysmoduleLoadModuleInternalWithArg(GuestContext *ctx, uint32_t id, int32_t argc, const void *argv,
                                              uint64_t unk, int32_t *res_out, uint32_t sdk_ver) {
    fprintf(stderr, "[ps4-sysmodule] sceSysmoduleLoadModuleInternalWithArg(0x%x)\n", id);
    int32_t result = validate_module_id(id, sdk_ver);
    if (result < ORBIS_OK) {
        return result;
    }
    if (unk != 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    lock();
    result = load_module(ctx, id, argc, argv, res_out);
    unlock();
    return result;
}

int32_t sceSysmoduleUnloadModule(uint16_t id) {
    if (id == 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    lock();
    int32_t r = unload_module(id, NULL);
    unlock();
    return r;
}

int32_t sceSysmoduleUnloadModuleInternal(uint32_t id) {
    if ((id & 0x7fffffff) == 0) {
        return ORBIS_SYSMODULE_INVALID_ID;
    }
    lock();
    int32_t r = unload_module(id, NULL);
    unlock();
    return r;
}

int32_t sceSysmodulePreloadModuleForLibkernel(GuestContext *ctx, uint32_t sdk_ver) {
    lock();
    for (size_t k = 0; k < sizeof(g_preload_list_3) / sizeof(g_preload_list_3[0]); k++) {
        const uint32_t index = g_preload_list_3[k];
        // libSceDbg, libScePerf, libSceMat and libSceRazorCpu_debug: devkit only.
        if ((index == 0x12 || index == 0x1e || index == 0x24 || index == 0x26) && !IS_DEVKIT) {
            continue;
        }
        if (index == IDX_DISCMAP && sdk_ver >= FW_200) {
            continue;
        }
        if (index == 0x23 && !IS_DEVKIT) { // libSceDbgAssist: testkit only
            continue;
        }
        if (index == 0x25 && sdk_ver < FW_450 && !IS_DEVKIT) { // libSceRazorCpu
            continue;
        }
        if (index == 0x28 && sdk_ver < FW_700) { // libSceHttp2
            continue;
        }
        if ((index == 0x29 || index == 0x2a) && sdk_ver < FW_750) { // libSceNpWebApi2, libSceNpGameIntent
            continue;
        }
        if (index == 1 || index == 2) {
            // libkernel and libSceLibcInternal are already there; they are only counted.
            g_modules_array[index].is_loaded++;
            continue;
        }
        if (load_module_internal(ctx, index, 0, NULL, NULL) != ORBIS_OK && index != IDX_LIBC &&
            index != IDX_LIBSCEFIOS2) {
            // libc and libSceFios2 are optional game modules; anything else failing is fatal on
            // the console.
            fprintf(stderr, "[ps4-sysmodule] Failed to preload %s, expect crashes\n", g_modules_array[index].name);
        }
    }
    unlock();
    return ORBIS_OK;
}

void ps4_sysmodule_preload(GuestContext *ctx) {
    sceSysmodulePreloadModuleForLibkernel(ctx, ps4_compiled_sdk_version(ctx));
}

// ---- Guest ABI ----

#define GPTR(type, addr) ((addr) && ctx->mem_base ? (type)(ctx->mem_base + (addr)) : (type)NULL)
#define RET(v)                                  \
    do {                                        \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v); \
        SHIM_RETURN();                          \
    } while (0)

void shim_sceSysmoduleGetModuleHandleInternal(GuestContext *ctx) {
    RET(sceSysmoduleGetModuleHandleInternal((uint32_t)ctx->rdi, GPTR(int32_t *, ctx->rsi)));
}

void shim_sceSysmoduleGetModuleInfoForUnwind(GuestContext *ctx) {
    OrbisModuleInfoForUnwind *info = GPTR(OrbisModuleInfoForUnwind *, ctx->rdx);
    int32_t r = sceKernelGetModuleInfoForUnwind(ctx->rdi, (int32_t)ctx->rsi, info);
    if (r == ORBIS_OK) {
        lock();
        if (ps4_sysmodule_hides_name(info->name)) {
            memset(info->name, 0, sizeof(info->name));
        }
        unlock();
    }
    RET(r);
}

void shim_sceSysmoduleIsLoaded(GuestContext *ctx) {
    RET(sceSysmoduleIsLoaded((uint16_t)ctx->rdi));
}

void shim_sceSysmoduleIsLoadedInternal(GuestContext *ctx) {
    RET(sceSysmoduleIsLoadedInternal((uint32_t)ctx->rdi));
}

void shim_sceSysmoduleLoadModule(GuestContext *ctx) {
    RET(sceSysmoduleLoadModule(ctx, (uint16_t)ctx->rdi, ps4_compiled_sdk_version(ctx)));
}

void shim_sceSysmoduleLoadModuleInternal(GuestContext *ctx) {
    RET(sceSysmoduleLoadModuleInternal(ctx, (uint32_t)ctx->rdi, ps4_compiled_sdk_version(ctx)));
}

void shim_sceSysmoduleLoadModuleInternalWithArg(GuestContext *ctx) {
    RET(sceSysmoduleLoadModuleInternalWithArg(ctx, (uint32_t)ctx->rdi, (int32_t)ctx->rsi,
                                              GPTR(const void *, ctx->rdx), ctx->rcx, GPTR(int32_t *, ctx->r8),
                                              ps4_compiled_sdk_version(ctx)));
}

void shim_sceSysmodulePreloadModuleForLibkernel(GuestContext *ctx) {
    RET(sceSysmodulePreloadModuleForLibkernel(ctx, ps4_compiled_sdk_version(ctx)));
}

void shim_sceSysmoduleUnloadModule(GuestContext *ctx) {
    RET(sceSysmoduleUnloadModule((uint16_t)ctx->rdi));
}

void shim_sceSysmoduleUnloadModuleInternal(GuestContext *ctx) {
    RET(sceSysmoduleUnloadModuleInternal((uint32_t)ctx->rdi));
}

PS4_HLE_STUB(sceSysmoduleIsCalledFromSysModule)
PS4_HLE_STUB(sceSysmoduleIsCameraPreloaded)
PS4_HLE_STUB(sceSysmoduleLoadModuleByNameInternal)
PS4_HLE_STUB(sceSysmoduleMapLibcForLibkernel)
PS4_HLE_STUB(sceSysmoduleUnloadModuleByNameInternal)
PS4_HLE_STUB(sceSysmoduleUnloadModuleInternalWithArg)
