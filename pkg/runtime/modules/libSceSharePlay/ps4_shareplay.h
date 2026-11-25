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
#define ORBIS_SHARE_PLAY_ERROR_NOT_INITIALIZED ((int32_t)0x810e0004)

#define ORBIS_SHARE_PLAY_CONNECTION_STATUS_DORMANT 0
#define ORBIS_SHARE_PLAY_CONNECTION_STATUS_READY 1
#define ORBIS_SHARE_PLAY_CONNECTION_STATUS_CONNECTED 2

#define ORBIS_SHARE_PLAY_ONLINEID_MAX_LENGTH 16

typedef struct OrbisSharePlayConnectionInfo {
    int32_t status;
    int32_t mode;
    char hostOnlineId[ORBIS_SHARE_PLAY_ONLINEID_MAX_LENGTH];
    int32_t hostUserId;
    char visitorOnlineId[ORBIS_SHARE_PLAY_ONLINEID_MAX_LENGTH];
    int32_t visitorUserId;
    int32_t reserved[2];
} OrbisSharePlayConnectionInfo;
_Static_assert(sizeof(OrbisSharePlayConnectionInfo) == 0x38, "OrbisSharePlayConnectionInfo layout");

// The extended variant returned by sceSharePlayGetCurrentConnectionInfoA.
typedef struct OrbisSharePlayConnectionInfoA {
    int32_t status;
    int32_t mode;
    char hostOnlineId[ORBIS_SHARE_PLAY_ONLINEID_MAX_LENGTH];
    int32_t hostUserId;
    char visitorOnlineId[ORBIS_SHARE_PLAY_ONLINEID_MAX_LENGTH];
    int32_t visitorUserId;
    uint8_t reserved[24];
} OrbisSharePlayConnectionInfoA;
_Static_assert(sizeof(OrbisSharePlayConnectionInfoA) == 0x48, "OrbisSharePlayConnectionInfoA layout");

// C API
int32_t sceSharePlayInitialize(void);
int32_t sceSharePlayTerminate(void);
int32_t sceSharePlaySetProhibition(int32_t prohibited);
int32_t sceSharePlayGetCurrentConnectionInfo(OrbisSharePlayConnectionInfo *pInfo);
int32_t sceSharePlayGetCurrentConnectionInfoA(OrbisSharePlayConnectionInfoA *pInfo);

// Shims
void shim_sceSharePlayInitialize(GuestContext *ctx);
void shim_sceSharePlayTerminate(GuestContext *ctx);
void shim_sceSharePlaySetProhibition(GuestContext *ctx);
void shim_sceSharePlayGetCurrentConnectionInfo(GuestContext *ctx);
void shim_sceSharePlayGetCurrentConnectionInfoA(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SHAREPLAY_H
