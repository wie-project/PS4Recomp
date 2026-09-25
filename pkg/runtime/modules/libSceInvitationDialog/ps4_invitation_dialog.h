#ifndef PS4_INVITATION_DIALOG_H
#define PS4_INVITATION_DIALOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ORBIS_COMMON_DIALOG_TYPES_DEFINED
#define ORBIS_COMMON_DIALOG_TYPES_DEFINED

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

#endif // ORBIS_COMMON_DIALOG_TYPES_DEFINED

#define ORBIS_INVITATION_DIALOG_ADDRESS_USER_LIST_MAX_SIZE 16

typedef struct OrbisNpOnlineId {
    char data[16];
    char term;
    char dummy[3];
} OrbisNpOnlineId;

typedef uint64_t OrbisNpAccountId;

typedef struct OrbisInvitationDialogUserList {
    uint32_t count;
    struct {
        OrbisNpOnlineId onlineId;
        OrbisNpAccountId accountId;
    } users[ORBIS_INVITATION_DIALOG_ADDRESS_USER_LIST_MAX_SIZE];
} OrbisInvitationDialogUserList;

typedef struct OrbisInvitationDialogParamA {
    OrbisCommonDialogBaseParam baseParam;
    uint32_t size;
    int32_t mode;
    int32_t userId;
    int32_t pad;
    void *callbackArg;
    const void *dataParam;
    uint8_t reserved[64];
} OrbisInvitationDialogParamA;

typedef struct OrbisInvitationDialogResultA {
    void *callbackArg;
    int32_t errorCode;
    OrbisCommonDialogResult result;
    OrbisInvitationDialogUserList *sentUsers;
    uint8_t reserved[32];
} OrbisInvitationDialogResultA;

// C API
int32_t sceInvitationDialogInitialize(void);
int32_t sceInvitationDialogOpenA(const OrbisInvitationDialogParamA *param);
OrbisCommonDialogStatus sceInvitationDialogUpdateStatus(void);
OrbisCommonDialogStatus sceInvitationDialogGetStatus(void);
int32_t sceInvitationDialogGetResultA(OrbisInvitationDialogResultA *result);
int32_t sceInvitationDialogTerminate(void);

// Shims
void shim_sceInvitationDialogInitialize(GuestContext *ctx);
void shim_sceInvitationDialogOpenA(GuestContext *ctx);
void shim_sceInvitationDialogUpdateStatus(GuestContext *ctx);
void shim_sceInvitationDialogGetStatus(GuestContext *ctx);
void shim_sceInvitationDialogGetResultA(GuestContext *ctx);
void shim_sceInvitationDialogTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_INVITATION_DIALOG_H
