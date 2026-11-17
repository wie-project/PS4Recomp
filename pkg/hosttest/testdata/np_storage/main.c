// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_np.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

__attribute__((weak)) int32_t module_start(size_t args, const void *argp) {
    (void)args; (void)argp;
    return 0;
}

__attribute__((weak)) int32_t module_stop(size_t args, const void *argp) {
    (void)args; (void)argp;
    return 0;
}

static void test_np_storage(void) {
    OrbisNpId npId;
    memset(&npId, 0, sizeof(npId));

    // sceNpTssCreateNpTitleCtx redirects to sceNpTusCreateNpTitleCtx -> ORBIS_NP_ERROR_SIGNED_OUT
    int32_t ctx_res = sceNpTssCreateNpTitleCtx(0, &npId);
    assert(ctx_res == (int32_t)ORBIS_NP_ERROR_SIGNED_OUT);

    // sceNpTssGetSmallStorage argument checks
    uint64_t content_len = 0;
    char buf[256];
    int opt = 1;

    // Bad reqId <= 0
    assert(sceNpTssGetSmallStorage(0, buf, sizeof(buf), &content_len, NULL) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(sceNpTssGetSmallStorage(-1, buf, sizeof(buf), &content_len, NULL) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT);

    // Non-null option
    assert(sceNpTssGetSmallStorage(1, buf, sizeof(buf), &content_len, &opt) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT);

    // Valid reqId and NULL option -> NO_LOGIN
    assert(sceNpTssGetSmallStorage(1, buf, sizeof(buf), &content_len, NULL) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN);

    // sceNpTusAddAndGetVariableVUserAsync argument checks
    assert(sceNpTusAddAndGetVariableVUserAsync(0, 100, 1, 10, NULL, NULL) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(sceNpTusAddAndGetVariableVUserAsync(1, 100, 1, 10, NULL, &opt) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(sceNpTusAddAndGetVariableVUserAsync(1, 100, 1, 10, NULL, NULL) == 0);

    // sceNpTusGetMultiSlotVariableVUserAsync argument checks
    int32_t slots[] = { 1, 2, 3 };
    assert(sceNpTusGetMultiSlotVariableVUserAsync(0, 100, slots, 3, NULL, NULL) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(sceNpTusGetMultiSlotVariableVUserAsync(1, 100, NULL, 3, NULL, NULL) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(sceNpTusGetMultiSlotVariableVUserAsync(1, 100, slots, 0, NULL, NULL) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(sceNpTusGetMultiSlotVariableVUserAsync(1, 100, slots, 3, NULL, &opt) == (int32_t)ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT);
    assert(sceNpTusGetMultiSlotVariableVUserAsync(1, 100, slots, 3, NULL, NULL) == 0);

    printf("PASS np-storage\n");
}

static void test_module_lifecycle(void) {
    assert(module_start(0, NULL) == 0);
    assert(module_stop(0, NULL) == 0);
    printf("PASS module-lifecycle\n");
}

static void test_party_state_shim(void) {
    _Alignas(uint32_t) uint8_t guest_mem[32] = {0};
    GuestContext ctx = {0};
    ctx.mem_base = guest_mem;
    ctx.rdi = 8;
    ctx.rsp = 0;
    *(uint32_t *)(void *)(guest_mem + ctx.rdi) = 0xffffffff;

    shim_sceNpPartyGetState(&ctx);
    assert(ctx.rax == 0);
    assert(*(uint32_t *)(void *)(guest_mem + ctx.rdi) == 2);
    assert(ctx.rsp == 8);
    printf("PASS party-state-shim\n");
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *name = argv[1];
    if (!strcmp(name, "np-storage")) test_np_storage();
    else if (!strcmp(name, "module-lifecycle")) test_module_lifecycle();
    else if (!strcmp(name, "party-state-shim")) test_party_state_shim();
    else {
        fprintf(stderr, "Unknown test: %s\n", name);
        return 2;
    }
    return 0;
}
