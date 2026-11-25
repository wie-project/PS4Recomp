// SPDX-License-Identifier: GPL-2.0-or-later

// libSceSysmodule (ps4_sysmodule_lib.c) over the real module loader (ps4_sysmodule.c): ID
// validation, dependency loading and load counts, game modules started through
// sceKernelLoadStartModule, the libkernel preload and the hidden module names.

#include "ps4_kernel_sys.h"
#include "ps4_sysmodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#define OK 0
#define INVALID ORBIS_SYSMODULE_INVALID_ID
#define NOT_LOADED ORBIS_SYSMODULE_NOT_LOADED

#define SDK_450 0x4500000u

// Module IDs from the firmware table.
#define ID_VOICE 0x1a       // libSceVoice: libSceAjm, libSceAudioIn, libSceAudioOut
#define ID_AJM 0x80000023u  // internal ID
#define ID_AUDIOIN 0x80000002u
#define ID_ULT 0x7          // libSceUlt: libSceFiber, ..., libScePerf (debug)
#define ID_FIBER 0x6
#define ID_PERF 0x80000019u // debug module
#define ID_DBG 0x80000025u  // debug module
#define ID_JSON 0x80        // not loadable from SDK 3.00
#define ID_FACE 0x38        // game module /app0/sce_module/libSceFace.prx
#define ID_HAND 0x93        // game module the game does not ship
#define ID_NET 0x8000001cu  // preloaded
#define ID_RTC 0x80000020u  // preloaded, kept by unload
#define ID_DISCMAP 0xd7     // preloaded before SDK 2.00, never unloaded

// ---- The kernel side: the dispatch table, and what libkernel provides ----

recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

void recomp_register_fn(uint64_t guest_addr, recomp_fn_t fn) {
    uint64_t i1 = (guest_addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    uint64_t i2 = (guest_addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[i1]) g_dispatch_l1[i1] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    if (!g_dispatch_l1[i1][i2]) g_dispatch_l1[i1][i2] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    g_dispatch_l1[i1][i2][guest_addr & DISPATCH_LEVEL_MASK] = fn;
}

int ps4_vfs_resolve(const char *guest_path, char *out, size_t out_size) {
    return -1; // no files on disk: modules exist only as registered recompiled PRXs
}

static uint32_t g_sdk;
uint32_t ps4_compiled_sdk_version(const GuestContext *ctx) { return g_sdk; }

int32_t sceKernelGetModuleInfoForUnwind(uint64_t addr, int32_t flags, OrbisModuleInfoForUnwind *info) {
    memset(info, 0, sizeof(*info));
    snprintf(info->name, sizeof(info->name), "%s", addr == 1 ? "libc.prx" : "eboot.bin");
    return 0;
}

// ---- Guest module initializers ----

static int g_init_calls[2];

static void guest_ret(GuestContext *ctx) { ctx->rsp += 8; }
static void face_init_a(GuestContext *ctx) { g_init_calls[0]++; guest_ret(ctx); }
static void face_init_b(GuestContext *ctx) { g_init_calls[1]++; guest_ret(ctx); }

static const RecompModuleExport k_exports[] = {{"faceSym", 0x7000}, {NULL, 0}};
static const uint64_t k_face_inits[] = {0x5000, 0x6000};

static GuestContext *new_ctx(void) {
    GuestContext *ctx = calloc(1, sizeof(*ctx));
    ctx->mem_size = 0x10000;
    ctx->mem_base = calloc(1, ctx->mem_size);
    ctx->rsp = 0x8000;
    return ctx;
}

static int32_t handle_of(uint32_t id) {
    int32_t h = -1;
    return sceSysmoduleGetModuleHandleInternal(id, &h) == OK ? h : -1;
}

static int test_ids(void) {
    CHECK(sceSysmoduleIsLoaded(0) == INVALID);
    CHECK(sceSysmoduleIsLoadedInternal(0x80000000u) == INVALID);
    CHECK(sceSysmoduleIsLoaded(0x7fff) == INVALID); // not in the table
    CHECK(sceSysmoduleIsLoaded(ID_VOICE) == NOT_LOADED);
    CHECK(sceSysmoduleGetModuleHandleInternal(ID_AJM, NULL) == NOT_LOADED);
    CHECK(sceSysmoduleLoadModule(NULL, 0, SDK_450) == INVALID);
    CHECK(sceSysmoduleLoadModule(NULL, 0x7fff, SDK_450) == INVALID);
    CHECK(sceSysmoduleLoadModuleInternal(NULL, ID_DBG, SDK_450) == INVALID); // debug: retail console
    CHECK(sceSysmoduleLoadModule(NULL, ID_JSON, 0x3000000u) == INVALID);
    CHECK(sceSysmoduleLoadModule(NULL, ID_JSON, 0x2000000u) == OK && sceSysmoduleIsLoaded(ID_JSON) == OK);
    int32_t res = -1;
    CHECK(sceSysmoduleLoadModuleInternalWithArg(NULL, ID_AJM, 0, NULL, 1, &res, SDK_450) == INVALID && res == -1);
    CHECK(sceSysmoduleLoadModuleInternalWithArg(NULL, ID_AJM, 0, NULL, 0, &res, SDK_450) == OK && res == 0);
    CHECK(sceSysmoduleUnloadModule(0) == INVALID && sceSysmoduleUnloadModuleInternal(0x80000000u) == INVALID);
    printf("PASS ids\n");
    return 0;
}

static int test_deps(void) {
    CHECK(sceSysmoduleLoadModule(NULL, ID_VOICE, SDK_450) == OK);
    CHECK(sceSysmoduleIsLoaded(ID_VOICE) == OK);
    CHECK(sceSysmoduleIsLoadedInternal(ID_AJM) == OK && sceSysmoduleIsLoadedInternal(ID_AUDIOIN) == OK);
    // System modules are HLE: they get stand-in handles from 100 up, one per module.
    const int32_t h_voice = handle_of(ID_VOICE), h_ajm = handle_of(ID_AJM);
    CHECK(h_voice >= 100 && h_ajm >= 100 && h_voice != h_ajm);

    // A second load only counts; each unload takes one count off the module and its dependencies.
    CHECK(sceSysmoduleLoadModule(NULL, ID_VOICE, SDK_450) == OK && handle_of(ID_VOICE) == h_voice);
    CHECK(sceSysmoduleUnloadModule(ID_VOICE) == OK && sceSysmoduleIsLoaded(ID_VOICE) == OK);
    CHECK(sceSysmoduleUnloadModule(ID_VOICE) == OK);
    CHECK(sceSysmoduleIsLoaded(ID_VOICE) == NOT_LOADED && sceSysmoduleIsLoadedInternal(ID_AJM) == NOT_LOADED);
    CHECK(sceSysmoduleUnloadModule(ID_VOICE) == NOT_LOADED);

    // Debug dependencies are skipped on a retail console.
    CHECK(sceSysmoduleLoadModule(NULL, ID_ULT, SDK_450) == OK);
    CHECK(sceSysmoduleIsLoaded(ID_FIBER) == OK && sceSysmoduleIsLoadedInternal(ID_PERF) == NOT_LOADED);
    CHECK(sceSysmoduleUnloadModule(ID_ULT) == OK && sceSysmoduleIsLoaded(ID_FIBER) == NOT_LOADED);
    printf("PASS deps\n");
    return 0;
}

static int test_game(void) {
    GuestContext *ctx = new_ctx();
    recomp_register_fn(0x5000, face_init_a);
    recomp_register_fn(0x6000, face_init_b);
    recomp_module_register("libSceFace.prx", k_exports);
    recomp_module_register_init("libSceFace.prx", k_face_inits, 2);

    CHECK(sceSysmoduleLoadModule(ctx, ID_FACE, SDK_450) == OK);
    CHECK(g_init_calls[0] == 1 && g_init_calls[1] == 1 && ctx->rsp == 0x8000);
    // The handle is the one the kernel loader gave the PRX.
    const int32_t h = handle_of(ID_FACE);
    CHECK(h > 0 && h < 100);
    void *addr = NULL;
    CHECK(sceKernelDlsym(h, "faceSym", &addr) == 0 && (uintptr_t)addr == 0x7000);
    CHECK(sceSysmoduleLoadModule(ctx, ID_FACE, SDK_450) == OK && g_init_calls[0] == 1);

    // A game module the game does not ship fails with the loader's error and stays unloaded.
    CHECK(sceSysmoduleLoadModule(ctx, ID_HAND, SDK_450) < 0);
    CHECK(sceSysmoduleIsLoaded(ID_HAND) == NOT_LOADED);
    printf("PASS game\n");
    return 0;
}

static int test_preload(void) {
    GuestContext *ctx = new_ctx();
    recomp_module_register("libc.prx", k_exports);
    CHECK(sceSysmodulePreloadModuleForLibkernel(ctx, SDK_450) == OK);
    CHECK(sceSysmoduleIsLoadedInternal(ID_NET) == OK && sceSysmoduleIsLoadedInternal(ID_RTC) == OK);
    CHECK(sceSysmoduleIsLoaded(ID_DISCMAP) == NOT_LOADED);  // skipped from SDK 2.00
    CHECK(sceSysmoduleIsLoadedInternal(ID_DBG) == NOT_LOADED); // devkit only
    // A preloaded module loaded once stays loaded.
    CHECK(sceSysmoduleUnloadModuleInternal(ID_RTC) == OK && sceSysmoduleIsLoadedInternal(ID_RTC) == OK);

    // The SDK 1.x preload has libSceDiscMap, which is never unloaded.
    g_sdk = 0x1500000u;
    ps4_sysmodule_preload(ctx);
    CHECK(sceSysmoduleIsLoaded(ID_DISCMAP) == OK && sceSysmoduleUnloadModule(ID_DISCMAP) == INVALID);
    // Loaded twice now: unloading takes it back to the preload's count.
    CHECK(sceSysmoduleUnloadModuleInternal(ID_RTC) == OK && sceSysmoduleIsLoadedInternal(ID_RTC) == OK);
    printf("PASS preload\n");
    return 0;
}

static int test_hidden_names(void) {
    CHECK(ps4_sysmodule_hides_name("libc.prx") && ps4_sysmodule_hides_name("libc.sprx"));
    CHECK(ps4_sysmodule_hides_name("libSceFios2.prx") && ps4_sysmodule_hides_name("libSceFios2.sprx"));
    CHECK(ps4_sysmodule_hides_name("libSceFace.prx"));
    CHECK(!ps4_sysmodule_hides_name("libSceFace.sprx") && !ps4_sysmodule_hides_name("libSceNgs2.prx"));
    CHECK(!ps4_sysmodule_hides_name("eboot.bin") && !ps4_sysmodule_hides_name(NULL));

    // The guest call clears the name of a game library and keeps the others.
    GuestContext *ctx = new_ctx();
    OrbisModuleInfoForUnwind *info = (OrbisModuleInfoForUnwind *)(ctx->mem_base + 0x1000);
    ctx->rdi = 1;
    ctx->rdx = 0x1000;
    shim_sceSysmoduleGetModuleInfoForUnwind(ctx);
    CHECK(ctx->rax == 0 && info->name[0] == '\0' && ctx->rsp == 0x8008);
    ctx->rdi = 2;
    shim_sceSysmoduleGetModuleInfoForUnwind(ctx);
    CHECK(ctx->rax == 0 && strcmp(info->name, "eboot.bin") == 0);
    printf("PASS hidden-names\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *c = argv[1];
    if (strcmp(c, "ids") == 0) return test_ids();
    if (strcmp(c, "deps") == 0) return test_deps();
    if (strcmp(c, "game") == 0) return test_game();
    if (strcmp(c, "preload") == 0) return test_preload();
    if (strcmp(c, "hidden-names") == 0) return test_hidden_names();
    return 2;
}
