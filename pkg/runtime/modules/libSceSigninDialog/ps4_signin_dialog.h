#ifndef PS4_SIGNIN_DIALOG_H
#define PS4_SIGNIN_DIALOG_H

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

typedef struct OrbisSigninDialogParam {
    OrbisCommonDialogBaseParam baseParam;
    int32_t mode;
    int32_t pad;
    void *userData;
    uint8_t reserved[32];
} OrbisSigninDialogParam;

typedef struct OrbisSigninDialogResult {
    OrbisCommonDialogResult result;
    int32_t pad;
    void *userData;
    uint8_t reserved[32];
} OrbisSigninDialogResult;

int32_t sceSigninDialogInitialize(void);
int32_t sceSigninDialogOpen(const OrbisSigninDialogParam *param);
OrbisCommonDialogStatus sceSigninDialogGetStatus(void);
OrbisCommonDialogStatus sceSigninDialogUpdateStatus(void);
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
