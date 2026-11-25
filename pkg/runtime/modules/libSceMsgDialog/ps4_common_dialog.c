// SPDX-License-Identifier: GPL-2.0-or-later

// libSceCommonDialog system state, as in shadPS4 (src/core/libraries/system/commondialog.cpp).

#include "ps4_common_dialog.h"
#include <stdatomic.h>

static atomic_bool g_system_initialized;
static atomic_bool g_used;

int32_t sceCommonDialogInitialize(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_system_initialized, &expected, true)) {
        return ORBIS_COMMON_DIALOG_ERROR_ALREADY_SYSTEM_INITIALIZED;
    }
    return 0;
}

bool sceCommonDialogIsUsed(void) {
    return atomic_load(&g_used);
}

bool ps4_common_dialog_system_initialized(void) {
    return atomic_load(&g_system_initialized);
}

bool ps4_common_dialog_acquire(void) {
    bool expected = false;
    return atomic_compare_exchange_strong(&g_used, &expected, true);
}

void ps4_common_dialog_release(void) {
    atomic_store(&g_used, false);
}

void shim_sceCommonDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCommonDialogInitialize();
    SHIM_RETURN();
}

void shim_sceCommonDialogIsUsed(GuestContext *ctx) {
    ctx->rax = sceCommonDialogIsUsed() ? 1 : 0;
    SHIM_RETURN();
}
