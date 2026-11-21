#ifndef PS4_SHAREUTILITY_H
#define PS4_SHAREUTILITY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_SHARE_UTILITY_OK 0
#define ORBIS_SHARE_UTILITY_ERROR_NOT_INITIALIZED ((int32_t)0x80520001)
#define ORBIS_SHARE_UTILITY_ERROR_ALREADY_INITIALIZED ((int32_t)0x80520002)
#define ORBIS_SHARE_UTILITY_ERROR_INVALID_PARAM ((int32_t)0x80520003)

// C API
int32_t sceShareUtilityInitialize(const void *param);
int32_t sceShareUtilityInitializeEx2(const void *param);
int32_t sceShareUtilityTerminate(void);
int32_t sceShareUtilityOpenShareMenu(const void *param);
int32_t sceShareUtilityOpenShareMenuDefault(const void *param);

int32_t sceShareUtilityAdvanceInitialize(const void *param);
int32_t sceShareUtilityAdvanceTerminate(void);
int32_t sceShareUtilityAdvanceSetUploadContentData(const void *data);

// Shims
void shim_sceShareUtilityInitialize(GuestContext *ctx);
void shim_sceShareUtilityInitializeEx2(GuestContext *ctx);
void shim_sceShareUtilityTerminate(GuestContext *ctx);
void shim_sceShareUtilityOpenShareMenu(GuestContext *ctx);
void shim_sceShareUtilityOpenShareMenuDefault(GuestContext *ctx);

void shim_sceShareUtilityAdvanceInitialize(GuestContext *ctx);
void shim_sceShareUtilityAdvanceTerminate(GuestContext *ctx);
void shim_sceShareUtilityAdvanceSetUploadContentData(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SHAREUTILITY_H
