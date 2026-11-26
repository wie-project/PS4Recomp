// SPDX-License-Identifier: GPL-2.0-or-later

// libSceHmdSetupDialog after shadPS4 (hmd/hmd_setup_dialog.cpp). The console shows a dialog
// asking the player to connect a PlayStation VR headset; the host has none, so the dialog is
// always finished and its result is the player cancelling it (Result::OK would mean a headset
// was connected). Initialize, Open, Close and Terminate have no effect, as in shadPS4.

#include "ps4_hle_stub.h"
#include "recomp_runtime.h"
#include <stdint.h>

#define HMD_SETUP_DIALOG_STATUS_FINISHED 3
#define HMD_SETUP_DIALOG_RESULT_USER_CANCELED 1

// OrbisHmdSetupDialogResult: {CommonDialog::Result result; u8 reserved[32];}
typedef struct HmdSetupDialogResult {
    int32_t result;
    uint8_t reserved[32];
} HmdSetupDialogResult;

PS4_HLE_STUB(sceHmdSetupDialogInitialize)
PS4_HLE_STUB(sceHmdSetupDialogOpen)
PS4_HLE_STUB(sceHmdSetupDialogClose)
PS4_HLE_STUB(sceHmdSetupDialogTerminate)

// s32 sceHmdSetupDialogGetResult(OrbisHmdSetupDialogResult *result): only result is written.
void shim_sceHmdSetupDialogGetResult(GuestContext *ctx) {
    if (ctx->rdi) {
        HmdSetupDialogResult *r = (HmdSetupDialogResult *)(ctx->mem_base + ctx->rdi);
        r->result = HMD_SETUP_DIALOG_RESULT_USER_CANCELED;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceHmdSetupDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = HMD_SETUP_DIALOG_STATUS_FINISHED;
    SHIM_RETURN();
}

void shim_sceHmdSetupDialogGetStatus(GuestContext *ctx) {
    ctx->rax = HMD_SETUP_DIALOG_STATUS_FINISHED;
    SHIM_RETURN();
}
