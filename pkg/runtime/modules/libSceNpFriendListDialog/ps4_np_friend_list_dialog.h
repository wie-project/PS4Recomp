// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_NP_FRIEND_LIST_DIALOG_H
#define PS4_NP_FRIEND_LIST_DIALOG_H

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

typedef struct OrbisNpFriendListDialogResultA {
    OrbisCommonDialogResult result;
    int32_t reserved0;
    void *userData;
    uint8_t reserved[32];
} OrbisNpFriendListDialogResultA;

// C API
int32_t sceNpFriendListDialogInitialize(void);
int32_t sceNpFriendListDialogOpenA(const void *param);
OrbisCommonDialogStatus sceNpFriendListDialogUpdateStatus(void);
OrbisCommonDialogStatus sceNpFriendListDialogGetStatus(void);
int32_t sceNpFriendListDialogGetResultA(OrbisNpFriendListDialogResultA *result);
int32_t sceNpFriendListDialogClose(void);
int32_t sceNpFriendListDialogTerminate(void);

// Shims
void shim_sceNpFriendListDialogInitialize(GuestContext *ctx);
void shim_sceNpFriendListDialogOpenA(GuestContext *ctx);
void shim_sceNpFriendListDialogUpdateStatus(GuestContext *ctx);
void shim_sceNpFriendListDialogGetStatus(GuestContext *ctx);
void shim_sceNpFriendListDialogGetResultA(GuestContext *ctx);
void shim_sceNpFriendListDialogClose(GuestContext *ctx);
void shim_sceNpFriendListDialogTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NP_FRIEND_LIST_DIALOG_H
