// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_INVITATION_DIALOG_H
#define PS4_INVITATION_DIALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// CommonDialog::Status and CommonDialog::Result values the dialog reports.
#define INVITATION_DIALOG_STATUS_NONE 0
#define INVITATION_DIALOG_STATUS_INITIALIZED 1
#define INVITATION_DIALOG_STATUS_RUNNING 2
#define INVITATION_DIALOG_STATUS_FINISHED 3
#define INVITATION_DIALOG_RESULT_OK 0
#define INVITATION_DIALOG_RESULT_USER_CANCELED 1

// Constants of shadPS4 invitation_dialog.h.
#define INVITATION_DIALOG_ADDRESS_USER_LIST_MAX_SIZE 16
#define INVITATION_DIALOG_MAX_USER_MSG_SIZE 512
#define INVITATION_DIALOG_MODE_INVALID 0
#define INVITATION_DIALOG_MODE_SEND 1
#define INVITATION_DIALOG_MODE_RECV 2
#define INVITATION_DIALOG_ADDRESS_TYPE_INVALID 0
#define INVITATION_DIALOG_ADDRESS_TYPE_USERDISABLE 1
#define INVITATION_DIALOG_ADDRESS_TYPE_USERENABLE 2
#define INVITATION_DIALOG_ERROR_BLOCKED_ALL_PLAYERS ((int32_t)0x810A0001)
#define INVITATION_DIALOG_NP_SESSION_ID_MAX_SIZE 45

// OrbisInvitationDialogParam and OrbisInvitationDialogParamA share this layout: the A variant
// pads userId to 8 bytes explicitly, the other by the alignment of callbackArg. Guest pointers
// are guest addresses.
typedef struct InvitationDialogParam {
    uint64_t baseSize; // CommonDialog::BaseParam
    uint8_t baseReserved[36];
    uint32_t baseMagic;
    uint32_t size;
    int32_t mode;
    int32_t userId;
    int32_t pad;
    uint64_t callbackArg;
    uint64_t dataParam; // OrbisInvitationDialogDataParam(A) *
    uint8_t reserved[64];
} InvitationDialogParam;
_Static_assert(offsetof(InvitationDialogParam, callbackArg) == 0x40, "InvitationDialogParam layout");
_Static_assert(sizeof(InvitationDialogParam) == 0x90, "InvitationDialogParam layout");

// SendInfo of OrbisInvitationDialogDataParam(A). The address info union holds
// {const OrbisNpOnlineId *onlineIds; u32 onlineIdsCount;} in the non-A variant and
// {const OrbisNpAccountId *accountIds; u32 accountIdsCount;} in the A variant, at the same
// offsets; with USERENABLE the first u32 is the maximum recipient count.
typedef struct InvitationDialogSendInfo {
    uint64_t userMessage; // const char *
    uint64_t sessionId;   // const OrbisNpSessionId *
    int32_t addressType;
    int32_t pad;
    uint64_t ids;      // onlineIds or accountIds
    uint32_t idsCount; // onlineIdsCount or accountIdsCount
    int32_t pad2;
} InvitationDialogSendInfo;
_Static_assert(offsetof(InvitationDialogSendInfo, ids) == 0x18, "InvitationDialogSendInfo layout");

// OrbisInvitationDialogResult and OrbisInvitationDialogResultA: sentUsers points to an
// OrbisInvitationDialogUserList ({u32 count; {OrbisNpOnlineId; OrbisNpAccountId} users[16];}) in
// the A variant and to an SceInvitationDialogOnlineIdList ({u32 count; OrbisNpOnlineId[16];}) in
// the other; both start with the count.
typedef struct InvitationDialogResult {
    uint64_t callbackArg;
    int32_t errorCode;
    int32_t result;
    uint64_t sentUsers;
    uint8_t reserved[32];
} InvitationDialogResult;
_Static_assert(sizeof(InvitationDialogResult) == 0x38, "InvitationDialogResult layout");

// What the window shows for an opened dialog (shadPS4 DialogState).
typedef struct InvitationDialogView {
    uint32_t serial; // identifies the opened dialog, passed back to ps4_invitation_dialog_finish
    int32_t mode;
    bool userEditable; // USERENABLE: the player picks the recipients from the friend list
    char sessionId[INVITATION_DIALOG_NP_SESSION_ID_MAX_SIZE + 1];
    char message[INVITATION_DIALOG_MAX_USER_MSG_SIZE + 1];
} InvitationDialogView;

int32_t sceInvitationDialogInitialize(void);
int32_t sceInvitationDialogTerminate(void);
int32_t sceInvitationDialogClose(void);
int32_t sceInvitationDialogGetStatus(void);
int32_t sceInvitationDialogUpdateStatus(void);
// `mem` is the guest memory base the parameter's pointers are relative to.
int32_t sceInvitationDialogOpen(uint8_t *mem, const InvitationDialogParam *param);
int32_t sceInvitationDialogOpenA(uint8_t *mem, const InvitationDialogParam *param);
int32_t sceInvitationDialogGetResult(uint8_t *mem, InvitationDialogResult *result);
int32_t sceInvitationDialogGetResultA(uint8_t *mem, InvitationDialogResult *result);

// The window (ps4_invitation_dialog_ui.m). show returns false when no window can be shown
// (headless run, no AppKit application); the dialog then finishes at once as if Cancel had been
// pressed.
bool ps4_invitation_dialog_ui_show(const InvitationDialogView *view);
void ps4_invitation_dialog_ui_dismiss(void);
// Called by the window: Send delivers the invitation and finishes with OK, Cancel finishes with
// USER_CANCELED. A window of an earlier dialog (other serial) is ignored.
void ps4_invitation_dialog_send(uint32_t serial);
void ps4_invitation_dialog_finish(uint32_t serial, int32_t result);

void shim_sceInvitationDialogInitialize(GuestContext *ctx);
void shim_sceInvitationDialogTerminate(GuestContext *ctx);
void shim_sceInvitationDialogClose(GuestContext *ctx);
void shim_sceInvitationDialogGetStatus(GuestContext *ctx);
void shim_sceInvitationDialogUpdateStatus(GuestContext *ctx);
void shim_sceInvitationDialogOpen(GuestContext *ctx);
void shim_sceInvitationDialogOpenA(GuestContext *ctx);
void shim_sceInvitationDialogGetResult(GuestContext *ctx);
void shim_sceInvitationDialogGetResultA(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_INVITATION_DIALOG_H
