// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_COMMON_DIALOG_H
#define PS4_COMMON_DIALOG_H

#include <stdbool.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// libSceCommonDialog errors (shadPS4 system/commondialog.h).
#define ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED ((int32_t)0x80B80001)
#define ORBIS_COMMON_DIALOG_ERROR_ALREADY_SYSTEM_INITIALIZED ((int32_t)0x80B80002)
#define ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED ((int32_t)0x80B80003)
#define ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED ((int32_t)0x80B80004)
#define ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED ((int32_t)0x80B80005)
#define ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE ((int32_t)0x80B80006)
#define ORBIS_COMMON_DIALOG_ERROR_RESULT_NONE ((int32_t)0x80B80007)
#define ORBIS_COMMON_DIALOG_ERROR_BUSY ((int32_t)0x80B80008)
#define ORBIS_COMMON_DIALOG_ERROR_OUT_OF_MEMORY ((int32_t)0x80B80009)
#define ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID ((int32_t)0x80B8000A)
#define ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING ((int32_t)0x80B8000B)
#define ORBIS_COMMON_DIALOG_ERROR_ALREADY_CLOSE ((int32_t)0x80B8000C)
#define ORBIS_COMMON_DIALOG_ERROR_ARG_NULL ((int32_t)0x80B8000D)
#define ORBIS_COMMON_DIALOG_ERROR_UNEXPECTED_FATAL ((int32_t)0x80B8000E)
#define ORBIS_COMMON_DIALOG_ERROR_NOT_SUPPORTED ((int32_t)0x80B8000F)
#define ORBIS_COMMON_DIALOG_ERROR_INHIBIT_SHAREPLAY_CLIENT ((int32_t)0x80B80010)

// The common dialog system: sceCommonDialogInitialize must precede any dialog's Initialize,
// and one dialog library at a time holds the system between its Initialize and Terminate
// (shadPS4 CommonDialog::g_isInitialized / g_isUsed).
int32_t sceCommonDialogInitialize(void);
bool sceCommonDialogIsUsed(void);
bool ps4_common_dialog_system_initialized(void);
// Takes the system for a dialog library; false when another library holds it.
bool ps4_common_dialog_acquire(void);
void ps4_common_dialog_release(void);

void shim_sceCommonDialogInitialize(GuestContext *ctx);
void shim_sceCommonDialogIsUsed(GuestContext *ctx);
// Stubs in shadPS4 as well (ps4_hle_stub.h).
void shim__ZN3sce16CommonDialogUtil12getSelfAppIdEv(GuestContext *ctx);
void shim__ZN3sce16CommonDialogUtil6Client11closeModuleEv(GuestContext *ctx);
void shim__ZN3sce16CommonDialogUtil6Client11updateStateEv(GuestContext *ctx);
void shim__ZN3sce16CommonDialogUtil6Client15launchCmnDialogEv(GuestContext *ctx);
void shim__ZN3sce16CommonDialogUtil6ClientD0Ev(GuestContext *ctx);
void shim__ZN3sce16CommonDialogUtil6ClientD1Ev(GuestContext *ctx);
void shim__ZN3sce16CommonDialogUtil6ClientD2Ev(GuestContext *ctx);
void shim__ZNK3sce16CommonDialogUtil6Client10isCloseReqEv(GuestContext *ctx);
void shim__ZNK3sce16CommonDialogUtil6Client13getFinishDataEPvm(GuestContext *ctx);
void shim__ZNK3sce16CommonDialogUtil6Client14getClientStateEv(GuestContext *ctx);
void shim__ZNK3sce16CommonDialogUtil6Client19isInitializedStatusEv(GuestContext *ctx);
void shim__ZNK3sce16CommonDialogUtil6Client8getAppIdEv(GuestContext *ctx);
void shim__ZNK3sce16CommonDialogUtil6Client8isFinishEv(GuestContext *ctx);
void shim__ZNK3sce16CommonDialogUtil6Client9getResultEv(GuestContext *ctx);
void shim__ZTVN3sce16CommonDialogUtil6ClientE(GuestContext *ctx);
void shim_Func_0FF577E4E8457883(GuestContext *ctx);
void shim_Func_41716C2CE379416C(GuestContext *ctx);
void shim_Func_483A427D8F6E0748(GuestContext *ctx);
void shim_Func_6944B83E02727BDF(GuestContext *ctx);
void shim_Func_69F2DD23A8B4950C(GuestContext *ctx);
void shim_Func_9954673DEAC170AD(GuestContext *ctx);
void shim_Func_A7D4D3AB86CB7455(GuestContext *ctx);
void shim_Func_ADE4C51256B8350C(GuestContext *ctx);
void shim_Func_B71349CF15FACAB0(GuestContext *ctx);
void shim_Func_CB18E00EFA946C64(GuestContext *ctx);
void shim_Func_F2AEE270605622B0(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_COMMON_DIALOG_H
