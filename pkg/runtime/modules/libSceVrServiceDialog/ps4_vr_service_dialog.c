// SPDX-License-Identifier: GPL-2.0-or-later

// libSceVrServiceDialog after shadPS4 (hmd/vrservicedialog.cpp). There is no PlayStation VR
// service on the host: every call is a stub, and UpdateStatus reports the dialog finished
// (CommonDialog::Status::FINISHED) so a game polling it moves on.

#include "ps4_hle_stub.h"
#include "recomp_runtime.h"

#define VR_SERVICE_DIALOG_STATUS_FINISHED 3

PS4_HLE_STUB(sceVrServiceDialogClose)
PS4_HLE_STUB(sceVrServiceDialogGetResult)
PS4_HLE_STUB(sceVrServiceDialogGetStatus)
PS4_HLE_STUB(sceVrServiceDialogInitialize)
PS4_HLE_STUB(sceVrServiceDialogOpen)
PS4_HLE_STUB(sceVrServiceDialogTerminate)

void shim_sceVrServiceDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = VR_SERVICE_DIALOG_STATUS_FINISHED;
    SHIM_RETURN();
}
