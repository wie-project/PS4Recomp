// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_SIGNIN_DIALOG_H
#define PS4_SIGNIN_DIALOG_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_SIGNIN_DIALOG_ERROR_NOT_INITIALIZED ((int32_t)0x81350001)
#define ORBIS_SIGNIN_DIALOG_ERROR_ALREADY_INITIALIZED ((int32_t)0x81350002)
#define ORBIS_SIGNIN_DIALOG_ERROR_PARAM_INVALID ((int32_t)0x81350003)
#define ORBIS_SIGNIN_DIALOG_ERROR_UNEXPECTED_FATAL ((int32_t)0x81350004)
#define ORBIS_SIGNIN_DIALOG_ERROR_INVALID_STATE ((int32_t)0x81350005)
#define ORBIS_SIGNIN_DIALOG_ERROR_SERVICE_BUSY ((int32_t)0x81350006)
#define ORBIS_SIGNIN_DIALOG_ERROR_INVALID_USER_ID ((int32_t)0x81350007)

typedef enum OrbisSigninDialogStatus {
    ORBIS_SIGNIN_DIALOG_STATUS_NONE = 0,
    ORBIS_SIGNIN_DIALOG_STATUS_INITIALIZED = 1,
    ORBIS_SIGNIN_DIALOG_STATUS_RUNNING = 2,
    ORBIS_SIGNIN_DIALOG_STATUS_FINISHED = 3,
} OrbisSigninDialogStatus;

typedef enum OrbisSigninDialogResultType {
    ORBIS_SIGNIN_DIALOG_RESULT_OK = 0,
    ORBIS_SIGNIN_DIALOG_RESULT_USER_CANCELED = 1,
} OrbisSigninDialogResultType;

typedef struct OrbisSigninDialogResult {
    int32_t result;
    int32_t reserved[3];
} OrbisSigninDialogResult;

int32_t sceSigninDialogInitialize(void);
int32_t sceSigninDialogOpen(void);
OrbisSigninDialogStatus sceSigninDialogGetStatus(void);
OrbisSigninDialogStatus sceSigninDialogUpdateStatus(void);
int32_t sceSigninDialogGetResult(OrbisSigninDialogResult *result);
int32_t sceSigninDialogClose(void);
int32_t sceSigninDialogTerminate(void);

void shim_sceSigninDialogInitialize(GuestContext *ctx);
void shim_sceSigninDialogOpen(GuestContext *ctx);
void shim_sceSigninDialogGetStatus(GuestContext *ctx);
void shim_sceSigninDialogUpdateStatus(GuestContext *ctx);
void shim_sceSigninDialogGetResult(GuestContext *ctx);
void shim_sceSigninDialogClose(GuestContext *ctx);
void shim_sceSigninDialogTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SIGNIN_DIALOG_H
