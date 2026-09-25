#ifndef PS4_SAVEDATA_DIALOG_H
#define PS4_SAVEDATA_DIALOG_H

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

typedef enum OrbisSaveDataDialogMode {
    ORBIS_SAVE_DATA_DIALOG_MODE_INVALID      = 0,
    ORBIS_SAVE_DATA_DIALOG_MODE_USER_MSG     = 1,
    ORBIS_SAVE_DATA_DIALOG_MODE_PROGRESS_BAR = 2,
    ORBIS_SAVE_DATA_DIALOG_MODE_SYSTEM_MSG   = 3,
    ORBIS_SAVE_DATA_DIALOG_MODE_ERROR_CODE   = 4,
    ORBIS_SAVE_DATA_DIALOG_MODE_LIST         = 5
} OrbisSaveDataDialogMode;

typedef enum OrbisSaveDataDialogType {
    ORBIS_SAVE_DATA_DIALOG_TYPE_SAVE   = 0,
    ORBIS_SAVE_DATA_DIALOG_TYPE_LOAD   = 1,
    ORBIS_SAVE_DATA_DIALOG_TYPE_DELETE = 2
} OrbisSaveDataDialogType;

typedef struct OrbisSaveDataDialogParam {
    OrbisCommonDialogBaseParam baseParam;
    int32_t size;
    OrbisSaveDataDialogMode mode;
    OrbisSaveDataDialogType dispType;
    int32_t pad;
    void *animParam;
    void *items;
    void *userMsgParam;
    void *sysMsgParam;
    void *errorCodeParam;
    void *progressBarParam;
    void *userData;
    void *optionParam;
    uint8_t reserved[24];
} OrbisSaveDataDialogParam;

typedef struct OrbisSaveDataDialogResult {
    OrbisSaveDataDialogMode mode;
    OrbisCommonDialogResult result;
    int32_t buttonId;
    int32_t pad;
    void *dirName;
    void *param;
    void *userData;
    uint8_t reserved[32];
} OrbisSaveDataDialogResult;

// C API
int32_t sceSaveDataDialogInitialize(void);
int32_t sceSaveDataDialogOpen(const OrbisSaveDataDialogParam *param);
OrbisCommonDialogStatus sceSaveDataDialogUpdateStatus(void);
OrbisCommonDialogStatus sceSaveDataDialogGetStatus(void);
int32_t sceSaveDataDialogGetResult(OrbisSaveDataDialogResult *result);
int32_t sceSaveDataDialogTerminate(void);

// Shims
void shim_sceSaveDataDialogInitialize(GuestContext *ctx);
void shim_sceSaveDataDialogOpen(GuestContext *ctx);
void shim_sceSaveDataDialogUpdateStatus(GuestContext *ctx);
void shim_sceSaveDataDialogGetStatus(GuestContext *ctx);
void shim_sceSaveDataDialogGetResult(GuestContext *ctx);
void shim_sceSaveDataDialogTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SAVEDATA_DIALOG_H
