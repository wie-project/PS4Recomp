// SPDX-License-Identifier: GPL-2.0-or-later

// libSceCommonDialog system state, as in shadPS4 (src/core/libraries/system/commondialog.cpp).
// sceCommonDialogInitialize and sceCommonDialogIsUsed carry the system state; the
// sce::CommonDialogUtil::Client methods and the unnamed Func_ exports are stubs in shadPS4 too.

#include "ps4_common_dialog.h"
#include "ps4_hle_stub.h"
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

// sce::CommonDialogUtil (the in-process client of the system dialog service).
PS4_HLE_STUB(_ZN3sce16CommonDialogUtil12getSelfAppIdEv)
PS4_HLE_STUB(_ZN3sce16CommonDialogUtil6Client11closeModuleEv)
PS4_HLE_STUB(_ZN3sce16CommonDialogUtil6Client11updateStateEv)
PS4_HLE_STUB(_ZN3sce16CommonDialogUtil6Client15launchCmnDialogEv)
PS4_HLE_STUB(_ZN3sce16CommonDialogUtil6ClientD0Ev)
PS4_HLE_STUB(_ZN3sce16CommonDialogUtil6ClientD1Ev)
PS4_HLE_STUB(_ZN3sce16CommonDialogUtil6ClientD2Ev)
PS4_HLE_STUB(_ZNK3sce16CommonDialogUtil6Client10isCloseReqEv)
PS4_HLE_STUB(_ZNK3sce16CommonDialogUtil6Client13getFinishDataEPvm)
PS4_HLE_STUB(_ZNK3sce16CommonDialogUtil6Client14getClientStateEv)
PS4_HLE_STUB(_ZNK3sce16CommonDialogUtil6Client19isInitializedStatusEv)
PS4_HLE_STUB(_ZNK3sce16CommonDialogUtil6Client8getAppIdEv)
PS4_HLE_STUB(_ZNK3sce16CommonDialogUtil6Client8isFinishEv)
PS4_HLE_STUB(_ZNK3sce16CommonDialogUtil6Client9getResultEv)
// The Client vtable is a data symbol on the console; shadPS4 registers it as a stub function and
// so does this runtime, so the import resolves the same way in both.
PS4_HLE_STUB(_ZTVN3sce16CommonDialogUtil6ClientE)

// Exports without a known name (shadPS4 names them by their NID hash).
PS4_HLE_STUB(Func_0FF577E4E8457883)
PS4_HLE_STUB(Func_41716C2CE379416C)
PS4_HLE_STUB(Func_483A427D8F6E0748)
PS4_HLE_STUB(Func_6944B83E02727BDF)
PS4_HLE_STUB(Func_69F2DD23A8B4950C)
PS4_HLE_STUB(Func_9954673DEAC170AD)
PS4_HLE_STUB(Func_A7D4D3AB86CB7455)
PS4_HLE_STUB(Func_ADE4C51256B8350C)
PS4_HLE_STUB(Func_B71349CF15FACAB0)
PS4_HLE_STUB(Func_CB18E00EFA946C64)
PS4_HLE_STUB(Func_F2AEE270605622B0)
