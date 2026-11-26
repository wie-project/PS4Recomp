// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_GAMELIVESTREAMING_H
#define PS4_GAMELIVESTREAMING_H

#include <stdint.h>
#include "recomp_runtime.h"

typedef struct OrbisGameLiveStreamingStatus {
    uint8_t isOnAir;
    uint8_t align[3];
    uint32_t spectatorCounts;
    int32_t userId;
    uint8_t reserved[60];
} OrbisGameLiveStreamingStatus;

_Static_assert(sizeof(OrbisGameLiveStreamingStatus) == 72, "OrbisGameLiveStreamingStatus layout");

void shim_sceGameLiveStreamingGetCurrentStatus(GuestContext *ctx);
void shim_sceGameLiveStreamingStartDebugBroadcast(GuestContext *ctx);
void shim_sceGameLiveStreamingStopDebugBroadcast(GuestContext *ctx);
void shim_sceGameLiveStreamingApplySocialFeedbackMessageFilter(GuestContext *ctx);
void shim_sceGameLiveStreamingCheckCallback(GuestContext *ctx);
void shim_sceGameLiveStreamingClearPresetSocialFeedbackCommands(GuestContext *ctx);
void shim_sceGameLiveStreamingClearSocialFeedbackMessages(GuestContext *ctx);
void shim_sceGameLiveStreamingClearSpoilerTag(GuestContext *ctx);
void shim_sceGameLiveStreamingEnableLiveStreaming(GuestContext *ctx);
void shim_sceGameLiveStreamingEnableSocialFeedback(GuestContext *ctx);
void shim_sceGameLiveStreamingGetCurrentBroadcastScreenLayout(GuestContext *ctx);
void shim_sceGameLiveStreamingGetCurrentStatus2(GuestContext *ctx);
void shim_sceGameLiveStreamingGetProgramInfo(GuestContext *ctx);
void shim_sceGameLiveStreamingGetSocialFeedbackMessages(GuestContext *ctx);
void shim_sceGameLiveStreamingGetSocialFeedbackMessagesCount(GuestContext *ctx);
void shim_sceGameLiveStreamingInitialize(GuestContext *ctx);
void shim_sceGameLiveStreamingLaunchLiveViewer(GuestContext *ctx);
void shim_sceGameLiveStreamingLaunchLiveViewerA(GuestContext *ctx);
void shim_sceGameLiveStreamingPermitLiveStreaming(GuestContext *ctx);
void shim_sceGameLiveStreamingPermitServerSideRecording(GuestContext *ctx);
void shim_sceGameLiveStreamingPostSocialMessage(GuestContext *ctx);
void shim_sceGameLiveStreamingRegisterCallback(GuestContext *ctx);
void shim_sceGameLiveStreamingScreenCloseSeparateMode(GuestContext *ctx);
void shim_sceGameLiveStreamingScreenConfigureSeparateMode(GuestContext *ctx);
void shim_sceGameLiveStreamingScreenInitialize(GuestContext *ctx);
void shim_sceGameLiveStreamingScreenInitializeSeparateModeParameter(GuestContext *ctx);
void shim_sceGameLiveStreamingScreenOpenSeparateMode(GuestContext *ctx);
void shim_sceGameLiveStreamingScreenSetMode(GuestContext *ctx);
void shim_sceGameLiveStreamingScreenTerminate(GuestContext *ctx);
void shim_sceGameLiveStreamingSetCameraFrameSetting(GuestContext *ctx);
void shim_sceGameLiveStreamingSetDefaultServiceProviderPermission(GuestContext *ctx);
void shim_sceGameLiveStreamingSetGuardAreas(GuestContext *ctx);
void shim_sceGameLiveStreamingSetInvitationSessionId(GuestContext *ctx);
void shim_sceGameLiveStreamingSetLinkCommentPreset(GuestContext *ctx);
void shim_sceGameLiveStreamingSetMaxBitrate(GuestContext *ctx);
void shim_sceGameLiveStreamingSetMetadata(GuestContext *ctx);
void shim_sceGameLiveStreamingSetPresetSocialFeedbackCommands(GuestContext *ctx);
void shim_sceGameLiveStreamingSetPresetSocialFeedbackCommandsDescription(GuestContext *ctx);
void shim_sceGameLiveStreamingSetServiceProviderPermission(GuestContext *ctx);
void shim_sceGameLiveStreamingSetSpoilerTag(GuestContext *ctx);
void shim_sceGameLiveStreamingSetStandbyScreenResource(GuestContext *ctx);
void shim_sceGameLiveStreamingStartGenerateStandbyScreenResource(GuestContext *ctx);
void shim_sceGameLiveStreamingStartSocialFeedbackMessageFiltering(GuestContext *ctx);
void shim_sceGameLiveStreamingStopGenerateStandbyScreenResource(GuestContext *ctx);
void shim_sceGameLiveStreamingStopSocialFeedbackMessageFiltering(GuestContext *ctx);
void shim_sceGameLiveStreamingTerminate(GuestContext *ctx);
void shim_sceGameLiveStreamingUnregisterCallback(GuestContext *ctx);

#endif // PS4_GAMELIVESTREAMING_H
