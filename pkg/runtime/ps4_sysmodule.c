#include "ps4_sysmodule.h"
#include "ps4_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <CommonCrypto/CommonDigest.h>

#define MAX_LOADED_MODULES 64
#define MAX_PRX_MODULES 64
#define MAX_MODULE_INIT 64

static uint32_t g_loaded_modules[MAX_LOADED_MODULES];
static int g_loaded_modules_count = 0;
static pthread_mutex_t g_sysmodule_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int in_use;
    int started;
    int32_t handle;
    char filename[256];
    const RecompModuleExport *exports;
    const uint64_t *inits;
    size_t init_count;
} RegisteredModule;

static RegisteredModule g_prx[MAX_PRX_MODULES];
static int g_prx_count = 0;
static int32_t g_next_handle = 1;

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) {
        slash = bslash;
    }
    return slash ? slash + 1 : path;
}

static void strip_module_ext(const char *name, char *out, size_t out_sz) {
    if (!name || !out || out_sz == 0) {
        return;
    }
    strncpy(out, name, out_sz - 1);
    out[out_sz - 1] = '\0';
    size_t n = strlen(out);
    const char *exts[] = {".prx", ".sprx", ".elf", ".self"};
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        size_t el = strlen(exts[i]);
        if (n > el && strcasecmp(out + n - el, exts[i]) == 0) {
            out[n - el] = '\0';
            return;
        }
    }
}

static const uint8_t k_nid_suffix[16] = {
    0x51, 0x8D, 0x64, 0xA6, 0x35, 0xDE, 0xD8, 0xC1,
    0xE6, 0xB0, 0x39, 0xB1, 0xC3, 0xE5, 0x52, 0x30};

static const char k_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Sony NID: SHA-1(name || suffix), first 8 bytes byte-swapped, base64 without padding, '/' → '-'.
static void sce_nid(const char *name, char out[12]) {
    uint8_t hash[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1_CTX sha;
    CC_SHA1_Init(&sha);
    CC_SHA1_Update(&sha, name, strlen(name));
    CC_SHA1_Update(&sha, k_nid_suffix, sizeof(k_nid_suffix));
    CC_SHA1_Final(hash, &sha);

    uint8_t rev[8];
    for (int i = 0; i < 8; i++) {
        rev[i] = hash[7 - i];
    }

    char tmp[12];
    int o = 0;
    for (int i = 0; i < 8; i += 3) {
        unsigned n = ((unsigned)rev[i]) << 16;
        if (i + 1 < 8) {
            n |= ((unsigned)rev[i + 1]) << 8;
        }
        if (i + 2 < 8) {
            n |= (unsigned)rev[i + 2];
        }
        tmp[o++] = k_b64[(n >> 18) & 63];
        tmp[o++] = k_b64[(n >> 12) & 63];
        tmp[o++] = (i + 1 < 8) ? k_b64[(n >> 6) & 63] : '=';
        tmp[o++] = (i + 2 < 8) ? k_b64[n & 63] : '=';
    }
    for (int i = 0; i < 11; i++) {
        out[i] = (tmp[i] == '/') ? '-' : tmp[i];
    }
    out[11] = '\0';
}

static int export_matches(const char *encoded, const char *symbol) {
    if (!encoded || !symbol) {
        return 0;
    }
    if (strcmp(encoded, symbol) == 0) {
        return 1;
    }
    char nid[12];
    sce_nid(symbol, nid);
    size_t n = 0;
    while (encoded[n] && encoded[n] != '#') {
        n++;
    }
    return n == 11 && strncmp(encoded, nid, 11) == 0;
}

static int module_names_match(const char *path_or_name, const char *registered) {
    char a[256], b[256];
    strip_module_ext(path_basename(path_or_name), a, sizeof(a));
    strip_module_ext(path_basename(registered), b, sizeof(b));
    return strcasecmp(a, b) == 0;
}

void recomp_module_register(const char *filename, const RecompModuleExport *exports) {
    if (!filename || !exports) {
        return;
    }
    pthread_mutex_lock(&g_sysmodule_mutex);
    for (int i = 0; i < g_prx_count; i++) {
        if (g_prx[i].in_use && module_names_match(filename, g_prx[i].filename)) {
            g_prx[i].exports = exports;
            pthread_mutex_unlock(&g_sysmodule_mutex);
            return;
        }
    }
    if (g_prx_count >= MAX_PRX_MODULES) {
        pthread_mutex_unlock(&g_sysmodule_mutex);
        return;
    }
    RegisteredModule *slot = &g_prx[g_prx_count++];
    slot->in_use = 1;
    slot->started = 0;
    slot->handle = 0;
    strncpy(slot->filename, path_basename(filename), sizeof(slot->filename) - 1);
    slot->filename[sizeof(slot->filename) - 1] = '\0';
    slot->exports = exports;
    slot->inits = NULL;
    slot->init_count = 0;
    pthread_mutex_unlock(&g_sysmodule_mutex);
}

void recomp_module_register_init(const char *filename, const uint64_t *inits, size_t count) {
    if (!filename) {
        return;
    }
    pthread_mutex_lock(&g_sysmodule_mutex);
    for (int i = 0; i < g_prx_count; i++) {
        if (g_prx[i].in_use && module_names_match(filename, g_prx[i].filename)) {
            g_prx[i].inits = inits;
            g_prx[i].init_count = count;
            break;
        }
    }
    pthread_mutex_unlock(&g_sysmodule_mutex);
}

void recomp_module_start(GuestContext *ctx, int32_t handle) {
    if (!ctx || handle <= 0) {
        return;
    }
    const uint64_t *inits = NULL;
    size_t count = 0;
    pthread_mutex_lock(&g_sysmodule_mutex);
    for (int i = 0; i < g_prx_count; i++) {
        if (g_prx[i].in_use && g_prx[i].handle == handle) {
            if (!g_prx[i].started) {
                g_prx[i].started = 1;
                inits = g_prx[i].inits;
                count = g_prx[i].init_count;
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_sysmodule_mutex);
    for (size_t i = 0; i < count; i++) {
        if (inits[i] != 0) {
            recomp_call_guest(ctx, inits[i]);
        }
    }
}

int32_t sceSysmoduleLoadModule(uint16_t moduleId) {
    pthread_mutex_lock(&g_sysmodule_mutex);
    for (int i = 0; i < g_loaded_modules_count; i++) {
        if (g_loaded_modules[i] == (uint32_t)moduleId) {
            pthread_mutex_unlock(&g_sysmodule_mutex);
            return 0;
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

int32_t sceKernelLoadStartModule(const char *name, size_t argc, const void *argv, uint32_t flags, void *opt, int *res) {
    (void)argc;
    (void)argv;
    (void)flags;
    (void)opt;
    if (res) {
        *res = 0;
    }
    if (!name || !*name) {
        return -EINVAL;
    }

    printf("[ps4-recomp] sceKernelLoadStartModule: %s\n", name);

    pthread_mutex_lock(&g_sysmodule_mutex);
    for (int i = 0; i < g_prx_count; i++) {
        if (!g_prx[i].in_use) {
            continue;
        }
        if (!module_names_match(name, g_prx[i].filename)) {
            continue;
        }
        if (g_prx[i].handle == 0) {
            g_prx[i].handle = g_next_handle++;
        }
        int32_t handle = g_prx[i].handle;
        pthread_mutex_unlock(&g_sysmodule_mutex);
        return handle;
    }
    pthread_mutex_unlock(&g_sysmodule_mutex);

    char host_path[1024];
    if (ps4_vfs_resolve(name, host_path, sizeof(host_path)) == 0 && access(host_path, F_OK) == 0) {
        fprintf(stderr, "[ps4-recomp] module '%s' exists on disk but was not AOT-recompiled\n", name);
        return -ENOSYS;
    }
    return -ENOENT;
}

int32_t sceKernelDlsym(int32_t handle, const char *symbol, void **addr) {
    if (!symbol || !addr) {
        return -EINVAL;
    }
    *addr = NULL;

    pthread_mutex_lock(&g_sysmodule_mutex);
    RegisteredModule *mod = NULL;
    for (int i = 0; i < g_prx_count; i++) {
        if (g_prx[i].in_use && g_prx[i].handle == handle) {
            mod = &g_prx[i];
            break;
        }
    }
    if (!mod || !mod->exports) {
        pthread_mutex_unlock(&g_sysmodule_mutex);
        return -ESRCH;
    }
    for (const RecompModuleExport *exp = mod->exports; exp->name; exp++) {
        if (export_matches(exp->name, symbol)) {
            *addr = (void *)(uintptr_t)exp->addr;
            pthread_mutex_unlock(&g_sysmodule_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_sysmodule_mutex);
    printf("[ps4-recomp] sceKernelDlsym: handle %d symbol %s not found\n", handle, symbol);
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
    if (ret > 0) {
        recomp_module_start(ctx, ret);
    }
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
