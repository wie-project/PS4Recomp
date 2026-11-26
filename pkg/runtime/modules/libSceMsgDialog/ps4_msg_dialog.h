// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_MSG_DIALOG_H
#define PS4_MSG_DIALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// CommonDialog::Status and CommonDialog::Result (shadPS4 system/commondialog.h).
#define MSG_DIALOG_STATUS_NONE 0
#define MSG_DIALOG_STATUS_INITIALIZED 1
#define MSG_DIALOG_STATUS_RUNNING 2
#define MSG_DIALOG_STATUS_FINISHED 3

#define MSG_DIALOG_RESULT_OK 0
#define MSG_DIALOG_RESULT_USER_CANCELED 1

// MsgDialogMode
#define MSG_DIALOG_MODE_USER_MSG 1
#define MSG_DIALOG_MODE_PROGRESS_BAR 2
#define MSG_DIALOG_MODE_SYSTEM_MSG 3

// ButtonId
#define MSG_DIALOG_BUTTON_ID_INVALID 0
#define MSG_DIALOG_BUTTON_ID_OK 1
#define MSG_DIALOG_BUTTON_ID_YES 1
#define MSG_DIALOG_BUTTON_ID_NO 2
#define MSG_DIALOG_BUTTON_ID_BUTTON1 1
#define MSG_DIALOG_BUTTON_ID_BUTTON2 2

// ButtonType (4 is not defined; shadPS4 draws no button for it)
#define MSG_DIALOG_BUTTON_TYPE_OK 0
#define MSG_DIALOG_BUTTON_TYPE_YESNO 1
#define MSG_DIALOG_BUTTON_TYPE_NONE 2
#define MSG_DIALOG_BUTTON_TYPE_OK_CANCEL 3
#define MSG_DIALOG_BUTTON_TYPE_WAIT 5
#define MSG_DIALOG_BUTTON_TYPE_WAIT_CANCEL 6
#define MSG_DIALOG_BUTTON_TYPE_YESNO_FOCUS_NO 7
#define MSG_DIALOG_BUTTON_TYPE_OK_CANCEL_FOCUS_CANCEL 8
#define MSG_DIALOG_BUTTON_TYPE_2BUTTONS 9

// ProgressBarType
#define MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE 0
#define MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE_CANCEL 1

// SystemMessageType (3 is not defined)
#define MSG_DIALOG_SYSMSG_TYPE_TRC_EMPTY_STORE 0
#define MSG_DIALOG_SYSMSG_TYPE_TRC_PSN_CHAT_RESTRICTION 1
#define MSG_DIALOG_SYSMSG_TYPE_TRC_PSN_UGC_RESTRICTION 2
#define MSG_DIALOG_SYSMSG_TYPE_CAMERA_NOT_CONNECTED 4
#define MSG_DIALOG_SYSMSG_TYPE_WARNING_PROFILE_PICTURE_AND_NAME_NOT_SHARED 5

// OrbisMsgDialogProgressBarTarget
#define MSG_DIALOG_PROGRESSBAR_TARGET_DEFAULT 0

// Guest structures (shadPS4 system/msgdialog_ui.h). Pointer members are guest addresses.
typedef struct OrbisMsgDialogBaseParam {
    uint64_t size;
    uint8_t reserved[36];
    uint32_t magic;
} OrbisMsgDialogBaseParam;
_Static_assert(sizeof(OrbisMsgDialogBaseParam) == 48, "CommonDialog::BaseParam layout");

typedef struct OrbisMsgDialogButtonsParam {
    uint64_t msg1;
    uint64_t msg2;
    char reserved[32];
} OrbisMsgDialogButtonsParam;

typedef struct OrbisMsgDialogUserMessageParam {
    uint32_t buttonType;
    int32_t pad;
    uint64_t msg;
    uint64_t buttonsParam;
    char reserved[24];
} OrbisMsgDialogUserMessageParam;

typedef struct OrbisMsgDialogProgressBarParam {
    uint32_t barType;
    int32_t pad;
    uint64_t msg;
    char reserved[64];
} OrbisMsgDialogProgressBarParam;

typedef struct OrbisMsgDialogSystemMessageParam {
    uint32_t sysMsgType;
    char reserved[32];
} OrbisMsgDialogSystemMessageParam;

typedef struct OrbisMsgDialogParam {
    OrbisMsgDialogBaseParam baseParam;
    uint64_t size;
    uint32_t mode;
    int32_t pad;
    uint64_t userMsgParam;
    uint64_t progBarParam;
    uint64_t sysMsgParam;
    int32_t userId;
    char reserved[40];
    int32_t pad2;
} OrbisMsgDialogParam;
_Static_assert(sizeof(OrbisMsgDialogParam) == 136, "OrbisParam layout");

typedef struct OrbisMsgDialogResult {
    uint32_t mode; // always 0: shadPS4 keeps it a FixedValue<u32, 0>
    int32_t result;
    uint32_t buttonId;
    char reserved[32];
} OrbisMsgDialogResult;
_Static_assert(sizeof(OrbisMsgDialogResult) == 44, "DialogResult layout");

// What the window shows, copied from the guest parameter at Open (shadPS4 MsgDialogState).
// The strings are owned by the library and stay valid until the next Open.
typedef struct MsgDialogView {
    uint32_t serial; // identifies the opened dialog
    uint32_t mode;
    uint32_t type; // ButtonType, ProgressBarType or SystemMessageType, by mode
    const char *msg; // user or progress bar message; NULL for a system message
    const char *button1; // 2BUTTONS labels
    const char *button2;
} MsgDialogView;

// The library over host pointers; mem is the guest memory base the parameter's addresses are
// relative to.
int32_t sceMsgDialogInitialize(void);
int32_t sceMsgDialogOpen(uint8_t *mem, const OrbisMsgDialogParam *param);
int32_t sceMsgDialogGetResult(OrbisMsgDialogResult *result);
int32_t sceMsgDialogGetStatus(void);
int32_t sceMsgDialogUpdateStatus(void);
int32_t sceMsgDialogClose(void);
int32_t sceMsgDialogTerminate(void);
int32_t sceMsgDialogProgressBarInc(uint32_t target, uint32_t delta);
int32_t sceMsgDialogProgressBarSetMsg(uint32_t target, const char *msg);
int32_t sceMsgDialogProgressBarSetValue(uint32_t target, uint32_t value);

// The window (ps4_msg_dialog_ui.m). show returns false when no window can be shown (headless
// run, no AppKit application); the library then presses the button that has the focus on the
// console, or leaves a dialog without one running until the game closes it.
bool ps4_msg_dialog_ui_show(const MsgDialogView *view);
// A progress bar dialog's message or value changed (value in percent, not clamped).
void ps4_msg_dialog_ui_progress(uint32_t serial, const char *msg, uint32_t value);
void ps4_msg_dialog_ui_dismiss(void);
// Called by the window when the player presses a button; ignored for an earlier dialog.
void ps4_msg_dialog_finish(uint32_t serial, uint32_t button_id, int32_t result);

void shim_sceMsgDialogInitialize(GuestContext *ctx);
void shim_sceMsgDialogOpen(GuestContext *ctx);
void shim_sceMsgDialogGetResult(GuestContext *ctx);
void shim_sceMsgDialogGetStatus(GuestContext *ctx);
void shim_sceMsgDialogUpdateStatus(GuestContext *ctx);
void shim_sceMsgDialogClose(GuestContext *ctx);
void shim_sceMsgDialogTerminate(GuestContext *ctx);
void shim_sceMsgDialogProgressBarInc(GuestContext *ctx);
void shim_sceMsgDialogProgressBarSetMsg(GuestContext *ctx);
void shim_sceMsgDialogProgressBarSetValue(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_MSG_DIALOG_H
