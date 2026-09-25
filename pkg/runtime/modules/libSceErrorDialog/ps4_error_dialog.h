#ifndef PS4_ERROR_DIALOG_H
#define PS4_ERROR_DIALOG_H

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

typedef struct OrbisErrorDialogParam {
    int32_t size;
    int32_t errorCode;
    int32_t userId;
    int32_t reserved;
} OrbisErrorDialogParam;

// C API
int32_t sceErrorDialogInitialize(void);
int32_t sceErrorDialogOpen(const OrbisErrorDialogParam *param);
OrbisCommonDialogStatus sceErrorDialogUpdateStatus(void);
OrbisCommonDialogStatus sceErrorDialogGetStatus(void);
int32_t sceErrorDialogTerminate(void);

// Shims
void shim_sceErrorDialogInitialize(GuestContext *ctx);
void shim_sceErrorDialogOpen(GuestContext *ctx);
void shim_sceErrorDialogUpdateStatus(GuestContext *ctx);
void shim_sceErrorDialogGetStatus(GuestContext *ctx);
void shim_sceErrorDialogTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_ERROR_DIALOG_H
