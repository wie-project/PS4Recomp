#ifndef PS4_REMOTEPLAY_H
#define PS4_REMOTEPLAY_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_REMOTEPLAY_CONNECTION_STATUS_DISCONNECT 0
#define ORBIS_REMOTEPLAY_CONNECTION_STATUS_CONNECT 1

int32_t sceRemoteplayInitialize(void);
int32_t sceRemoteplayApprove(void);
int32_t sceRemoteplayGetConnectionStatus(int32_t userId, int32_t *status);
int32_t sceRemoteplayProhibit(int32_t mode);
int32_t sceRemoteplayProhibitStreaming(int32_t mode);

void shim_sceRemoteplayInitialize(GuestContext *ctx);
void shim_sceRemoteplayApprove(GuestContext *ctx);
void shim_sceRemoteplayGetConnectionStatus(GuestContext *ctx);
void shim_sceRemoteplayProhibit(GuestContext *ctx);
void shim_sceRemoteplayProhibitStreaming(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_REMOTEPLAY_H
