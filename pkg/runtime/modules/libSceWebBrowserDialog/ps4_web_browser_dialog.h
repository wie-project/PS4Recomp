#ifndef PS4_WEB_BROWSER_DIALOG_H
#define PS4_WEB_BROWSER_DIALOG_H

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

typedef struct OrbisWebBrowserDialogParam {
    OrbisCommonDialogBaseParam baseParam;
    const char *url;
    void *userData;
    uint8_t reserved[32];
} OrbisWebBrowserDialogParam;

typedef struct OrbisWebBrowserDialogResult {
    OrbisCommonDialogResult result;
    int32_t pad;
    void *userData;
    uint8_t reserved[32];
} OrbisWebBrowserDialogResult;

int32_t sceWebBrowserDialogInitialize(void);
int32_t sceWebBrowserDialogOpen(const OrbisWebBrowserDialogParam *param);
OrbisCommonDialogStatus sceWebBrowserDialogGetStatus(void);
OrbisCommonDialogStatus sceWebBrowserDialogUpdateStatus(void);
int32_t sceWebBrowserDialogGetResult(OrbisWebBrowserDialogResult *result);
int32_t sceWebBrowserDialogClose(void);
int32_t sceWebBrowserDialogTerminate(void);

void shim_sceWebBrowserDialogInitialize(GuestContext *ctx);
void shim_sceWebBrowserDialogOpen(GuestContext *ctx);
void shim_sceWebBrowserDialogGetStatus(GuestContext *ctx);
void shim_sceWebBrowserDialogUpdateStatus(GuestContext *ctx);
void shim_sceWebBrowserDialogGetResult(GuestContext *ctx);
void shim_sceWebBrowserDialogClose(GuestContext *ctx);
void shim_sceWebBrowserDialogTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_WEB_BROWSER_DIALOG_H
