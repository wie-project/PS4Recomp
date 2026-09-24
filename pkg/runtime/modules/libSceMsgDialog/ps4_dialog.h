#ifndef PS4_DIALOG_H
#define PS4_DIALOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#define ORBIS_COMMON_DIALOG_MAGIC_NUMBER 0xC0D1A109

typedef enum OrbisCommonDialogResult {
    ORBIS_COMMON_DIALOG_RESULT_OK            = 0,
    ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED = 1
} OrbisCommonDialogResult;

typedef struct OrbisCommonDialogBaseParam {
    size_t size;
    uint8_t reserved[36];
    uint32_t magic;
} OrbisCommonDialogBaseParam;

typedef enum OrbisCommonDialogStatus {
    ORBIS_COMMON_DIALOG_STATUS_NONE        = 0,
    ORBIS_COMMON_DIALOG_STATUS_INITIALIZED = 1,
    ORBIS_COMMON_DIALOG_STATUS_RUNNING     = 2,
    ORBIS_COMMON_DIALOG_STATUS_FINISHED    = 3
} OrbisCommonDialogStatus;

typedef int32_t OrbisUserServiceUserId;
typedef int32_t OrbisMsgDialogButtonId;
typedef int32_t OrbisMsgDialogProgressBarTarget;

typedef enum OrbisMsgDialogButton {
    ORBIS_MSG_DIALOG_BUTTON_ID_INVALID = 0,
    ORBIS_MSG_DIALOG_BUTTON_ID_OK      = 1,
    ORBIS_MSG_DIALOG_BUTTON_ID_YES     = 1,
    ORBIS_MSG_DIALOG_BUTTON_ID_NO      = 2,
    ORBIS_MSG_DIALOG_BUTTON_ID_BUTTON1 = 1,
    ORBIS_MSG_DIALOG_BUTTON_ID_BUTTON2 = 2,
} OrbisMsgDialogButton;

typedef enum OrbisMsgDialogMode {
    ORBIS_MSG_DIALOG_MODE_USER_MSG     = 1,
    ORBIS_MSG_DIALOG_MODE_PROGRESS_BAR = 2,
    ORBIS_MSG_DIALOG_MODE_SYSTEM_MSG   = 3,
} OrbisMsgDialogMode;

typedef struct OrbisMsgDialogResult {
    OrbisMsgDialogMode mode;
    int32_t result;
    OrbisMsgDialogButton buttonId;
    char reserved[32];
} OrbisMsgDialogResult;

typedef enum OrbisMsgDialogButtonType {
    ORBIS_MSG_DIALOG_BUTTON_TYPE_OK                         = 0,
    ORBIS_MSG_DIALOG_BUTTON_TYPE_YESNO                      = 1,
    ORBIS_MSG_DIALOG_BUTTON_TYPE_NONE                       = 2,
    ORBIS_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL                  = 3,
    ORBIS_MSG_DIALOG_BUTTON_TYPE_WAIT                       = 5,
    ORBIS_MSG_DIALOG_BUTTON_TYPE_WAIT_CANCEL                = 6,
    ORBIS_MSG_DIALOG_BUTTON_TYPE_YESNO_FOCUS_NO             = 7,
    ORBIS_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL_FOCUS_CANCEL     = 8,
    ORBIS_MSG_DIALOG_BUTTON_TYPE_2BUTTONS                   = 9,
} OrbisMsgDialogButtonType;

typedef struct OrbisMsgDialogButtonsParam {
    const char *msg1;
    const char *msg2;
    char reserved[32];
} OrbisMsgDialogButtonsParam;

typedef struct OrbisMsgDialogUserMessageParam {
    OrbisMsgDialogButtonType buttonType;
    int32_t pad;
    const char *msg;
    OrbisMsgDialogButtonsParam *buttonsParam;
    char reserved[24];
} OrbisMsgDialogUserMessageParam;

typedef struct OrbisMsgDialogParam {
    OrbisCommonDialogBaseParam baseParam;
    size_t size;
    OrbisMsgDialogMode mode;
    int32_t pad;
    OrbisMsgDialogUserMessageParam *userMsgParam;
    void *progBarParam;
    void *sysMsgParam;
    OrbisUserServiceUserId userId;
    char reserved[40];
    int32_t pad2;
} OrbisMsgDialogParam;

// C API
int32_t sceCommonDialogInitialize(void);
bool sceCommonDialogIsUsed(void);

int32_t sceMsgDialogInitialize(void);
int32_t sceMsgDialogOpen(const OrbisMsgDialogParam *param);
int32_t sceMsgDialogGetResult(OrbisMsgDialogResult *result);
OrbisCommonDialogStatus sceMsgDialogGetStatus(void);
OrbisCommonDialogStatus sceMsgDialogUpdateStatus(void);
int32_t sceMsgDialogClose(void);
int32_t sceMsgDialogTerminate(void);

// Shims
void shim_sceCommonDialogInitialize(GuestContext *ctx);
void shim_sceCommonDialogIsUsed(GuestContext *ctx);
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

#endif // PS4_DIALOG_H
