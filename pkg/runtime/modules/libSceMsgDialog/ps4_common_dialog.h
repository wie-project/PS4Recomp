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

#ifdef __cplusplus
}
#endif

#endif // PS4_COMMON_DIALOG_H
