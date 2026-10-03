#ifndef PS4_IME_H
#define PS4_IME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_IME_ERROR_CONNECTION_FAILED ((int32_t)0x80bc0004)
#define ORBIS_IME_ERROR_NOT_OPENED        ((int32_t)0x80bc0002)
#define ORBIS_IME_ERROR_INVALID_ADDRESS   ((int32_t)0x80bc0031)

typedef struct OrbisImeKeyboardResourceIdArray {
    int32_t user_id;
    uint32_t resource_id[5];
} OrbisImeKeyboardResourceIdArray;

typedef struct OrbisImeKeyboardInfo {
    int32_t user_id;
    uint32_t device;
    uint32_t type;
    uint32_t repeat_delay;
    uint32_t repeat_rate;
    uint32_t status;
    int8_t reserved[12];
} OrbisImeKeyboardInfo;

typedef struct OrbisImeKeyboardParam {
    uint32_t option;
    int8_t reserved1[4];
    uint64_t arg;
    uint64_t handler;
    int8_t reserved2[8];
} OrbisImeKeyboardParam;

int32_t sceImeKeyboardOpen(int32_t userId, const OrbisImeKeyboardParam *param);
int32_t sceImeKeyboardClose(int32_t userId);
int32_t sceImeKeyboardGetInfo(uint32_t resourceId, OrbisImeKeyboardInfo *info);
int32_t sceImeKeyboardGetResourceId(int32_t userId, OrbisImeKeyboardResourceIdArray *resourceIdArray);
int32_t sceImeUpdate(uint64_t handler);

void shim_sceImeKeyboardOpen(GuestContext *ctx);
void shim_sceImeKeyboardClose(GuestContext *ctx);
void shim_sceImeKeyboardGetInfo(GuestContext *ctx);
void shim_sceImeKeyboardGetResourceId(GuestContext *ctx);
void shim_sceImeUpdate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_IME_H
