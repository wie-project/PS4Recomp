#ifndef PS4_SHAREPLAY_H
#define PS4_SHAREPLAY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_SHARE_PLAY_ERROR_INVALID_ARGUMENT ((int32_t)0x810e0001)
#define ORBIS_SHARE_PLAY_CONNECTION_STATUS_DORMANT 0

typedef struct OrbisSharePlayConnectionInfo {
    int32_t status;
    int32_t mode;
    char hostOnlineId[16];
    int32_t hostUserId;
    char visitorOnlineId[16];
    int32_t visitorUserId;
    int32_t reserved[2];
} OrbisSharePlayConnectionInfo;

// C API
int32_t sceSharePlayInitialize(void);
int32_t sceSharePlayTerminate(void);
int32_t sceSharePlaySetProhibition(int32_t prohibited);
int32_t sceSharePlayGetCurrentConnectionInfo(OrbisSharePlayConnectionInfo *pInfo);

// Shims
void shim_sceSharePlayInitialize(GuestContext *ctx);
void shim_sceSharePlayTerminate(GuestContext *ctx);
void shim_sceSharePlaySetProhibition(GuestContext *ctx);
void shim_sceSharePlayGetCurrentConnectionInfo(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SHAREPLAY_H
