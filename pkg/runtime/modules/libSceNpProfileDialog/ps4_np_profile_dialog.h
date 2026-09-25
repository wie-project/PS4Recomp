#ifndef PS4_NP_PROFILE_DIALOG_H
#define PS4_NP_PROFILE_DIALOG_H

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

typedef enum OrbisNpProfileDialogMode {
    ORBIS_NP_PROFILE_DIALOG_MODE_INVALID           = 0,
    ORBIS_NP_PROFILE_DIALOG_MODE_NORMAL            = 1,
    ORBIS_NP_PROFILE_DIALOG_MODE_FRIEND_REQUEST    = 2,
    ORBIS_NP_PROFILE_DIALOG_MODE_ADD_TO_BLOCK_LIST = 3,
    ORBIS_NP_PROFILE_DIALOG_MODE_GRIEF_REPORT      = 4
} OrbisNpProfileDialogMode;

typedef struct OrbisNpProfileDialogParamA {
    OrbisCommonDialogBaseParam baseParam;
    uint64_t size;
    OrbisNpProfileDialogMode mode;
    int32_t userId;
    uint64_t targetAccountId;
    int32_t pad;
    void *userData;
    uint8_t reserved[32];
} OrbisNpProfileDialogParamA;

typedef struct OrbisNpProfileDialogResult {
    int32_t result;
    OrbisCommonDialogResult userAction;
    void *userData;
    uint8_t reserved[32];
} OrbisNpProfileDialogResult;

// C API
int32_t sceNpProfileDialogInitialize(void);
int32_t sceNpProfileDialogOpenA(const OrbisNpProfileDialogParamA *param);
OrbisCommonDialogStatus sceNpProfileDialogUpdateStatus(void);
OrbisCommonDialogStatus sceNpProfileDialogGetStatus(void);
int32_t sceNpProfileDialogGetResult(OrbisNpProfileDialogResult *result);
int32_t sceNpProfileDialogTerminate(void);

// Shims
void shim_sceNpProfileDialogInitialize(GuestContext *ctx);
void shim_sceNpProfileDialogOpenA(GuestContext *ctx);
void shim_sceNpProfileDialogUpdateStatus(GuestContext *ctx);
void shim_sceNpProfileDialogGetStatus(GuestContext *ctx);
void shim_sceNpProfileDialogGetResult(GuestContext *ctx);
void shim_sceNpProfileDialogTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NP_PROFILE_DIALOG_H
