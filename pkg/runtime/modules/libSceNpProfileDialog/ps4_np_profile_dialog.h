// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_NP_PROFILE_DIALOG_H
#define PS4_NP_PROFILE_DIALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// CommonDialog::Status and CommonDialog::Result values the dialog reports.
#define NP_PROFILE_DIALOG_STATUS_NONE 0
#define NP_PROFILE_DIALOG_STATUS_INITIALIZED 1
#define NP_PROFILE_DIALOG_STATUS_RUNNING 2
#define NP_PROFILE_DIALOG_STATUS_FINISHED 3
#define NP_PROFILE_DIALOG_RESULT_OK 0
#define NP_PROFILE_DIALOG_RESULT_USER_CANCELED 1

// OrbisNpProfileDialogMode.
#define NP_PROFILE_DIALOG_MODE_INVALID 0
#define NP_PROFILE_DIALOG_MODE_NORMAL 1
#define NP_PROFILE_DIALOG_MODE_FRIEND_REQUEST 2
#define NP_PROFILE_DIALOG_MODE_ADD_TO_BLOCK_LIST 3
#define NP_PROFILE_DIALOG_MODE_GRIEF_REPORT 4

#define NP_PROFILE_DIALOG_ONLINE_ID_SIZE 16 // OrbisNpOnlineId::data

// OrbisNpProfileDialogParam (sceNpProfileDialogOpen): the target is an online ID.
typedef struct NpProfileDialogParam {
    uint64_t baseSize; // CommonDialog::BaseParam
    uint8_t baseReserved[36];
    uint32_t baseMagic;
    uint64_t size;
    uint32_t mode;
    int32_t userId;
    char targetOnlineId[NP_PROFILE_DIALOG_ONLINE_ID_SIZE]; // OrbisNpOnlineId: data, term, dummy[3]
    char targetOnlineIdTerm;
    char targetOnlineIdDummy[3];
    uint64_t userData;
    uint8_t reserved[32];
} NpProfileDialogParam;
_Static_assert(offsetof(NpProfileDialogParam, userData) == 0x58, "NpProfileDialogParam layout");

// OrbisNpProfileDialogParamA (sceNpProfileDialogOpenA): the target is an account ID, and the
// reserved area starts with OrbisNpProfileGriefReportParam::reportItem.
typedef struct NpProfileDialogParamA {
    uint64_t baseSize;
    uint8_t baseReserved[36];
    uint32_t baseMagic;
    uint64_t size;
    uint32_t mode;
    int32_t userId;
    uint64_t targetAccountId;
    int32_t pad;
    uint64_t userData;
    int32_t griefReportItem; // ORBIS_NP_PROFILE_DIALOG_MENU_GRIEF_REPORT_ITEM_* bits
    uint8_t reserved[28];
} NpProfileDialogParamA;
_Static_assert(offsetof(NpProfileDialogParamA, userData) == 0x50, "NpProfileDialogParamA layout");
_Static_assert(offsetof(NpProfileDialogParamA, griefReportItem) == 0x58, "NpProfileDialogParamA layout");

typedef struct NpProfileDialogResult {
    int32_t result;
    int32_t userAction; // CommonDialog::Result
    uint64_t userData;
    uint8_t reserved[32];
} NpProfileDialogResult;
_Static_assert(sizeof(NpProfileDialogResult) == 0x30, "NpProfileDialogResult layout");

// What the window shows for an opened dialog (shadPS4 NpProfileDialogState).
typedef struct NpProfileDialogView {
    uint32_t serial; // identifies the opened dialog, passed back to ps4_np_profile_dialog_finish
    uint32_t mode;
    char target[32]; // the target's online ID, or its account ID in decimal
} NpProfileDialogView;

int32_t sceNpProfileDialogInitialize(void);
int32_t sceNpProfileDialogTerminate(void);
int32_t sceNpProfileDialogClose(void);
int32_t sceNpProfileDialogGetStatus(void);
int32_t sceNpProfileDialogUpdateStatus(void);
int32_t sceNpProfileDialogOpen(const NpProfileDialogParam *param);
int32_t sceNpProfileDialogOpenA(const NpProfileDialogParamA *param);
int32_t sceNpProfileDialogGetResult(NpProfileDialogResult *result);

// The window (ps4_np_profile_dialog_ui.m). show returns false when no window can be shown
// (headless run, no AppKit application); the dialog then finishes at once as if Close had been
// pressed.
bool ps4_np_profile_dialog_ui_show(const NpProfileDialogView *view);
void ps4_np_profile_dialog_ui_dismiss(void);
// Called by the window when the player presses Close (USER_CANCELED). A window of an earlier
// dialog (other serial) is ignored.
void ps4_np_profile_dialog_finish(uint32_t serial, int32_t user_action);

void shim_sceNpProfileDialogInitialize(GuestContext *ctx);
void shim_sceNpProfileDialogTerminate(GuestContext *ctx);
void shim_sceNpProfileDialogClose(GuestContext *ctx);
void shim_sceNpProfileDialogGetStatus(GuestContext *ctx);
void shim_sceNpProfileDialogUpdateStatus(GuestContext *ctx);
void shim_sceNpProfileDialogOpen(GuestContext *ctx);
void shim_sceNpProfileDialogOpenA(GuestContext *ctx);
void shim_sceNpProfileDialogGetResult(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NP_PROFILE_DIALOG_H
