// libSceGameLiveStreaming module implementation following shadPS4 reference.
#include "ps4_gamelivestreaming.h"
#include <stdio.h>
#include <string.h>
#ifndef ORBIS_OK
#define ORBIS_OK 0
#endif

typedef struct OrbisGameLiveStreamingStatus {
  uint8_t isOnAir;
  uint8_t align[3];
  uint32_t spectatorCounts;
  int32_t userId;
  uint8_t reserved[60];
} OrbisGameLiveStreamingStatus;

typedef struct OrbisGameLiveStreamingStatus2 {
  int32_t userId;
  uint8_t isOnAir;
  uint8_t align[3];
  uint32_t spectatorCounts;
  uint32_t textMessageCounts;
  uint32_t commandMessageCounts;
  uint32_t broadcastVideoResolution;
  uint8_t reserved[48];
} OrbisGameLiveStreamingStatus2;

void shim_sceGameLiveStreamingInitialize(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingTerminate(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingEnableLiveStreaming(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingPermitLiveStreaming(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingPermitServerSideRecording(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingGetCurrentStatus(GuestContext *ctx) {
  if (ctx->rdi && ctx->mem_base) {
    OrbisGameLiveStreamingStatus *status =
        (OrbisGameLiveStreamingStatus *)(ctx->mem_base + ctx->rdi);
    memset(status, 0, sizeof(*status));
    status->isOnAir = 0;
  }
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingGetCurrentStatus2(GuestContext *ctx) {
  if (ctx->rdi && ctx->mem_base) {
    OrbisGameLiveStreamingStatus2 *status =
        (OrbisGameLiveStreamingStatus2 *)(ctx->mem_base + ctx->rdi);
    memset(status, 0, sizeof(*status));
    status->isOnAir = 0;
  }
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingSetServiceProviderPermission(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingSetDefaultServiceProviderPermission(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingSetGuardAreas(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingSetSpoilerTag(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingClearSpoilerTag(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingStartSocialFeedbackMessageFiltering(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingStopSocialFeedbackMessageFiltering(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}

void shim_sceGameLiveStreamingCheckCallback(GuestContext *ctx) {
  ctx->rax = ORBIS_OK;
  SHIM_RETURN();
}
