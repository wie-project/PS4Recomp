// SPDX-License-Identifier: GPL-2.0-or-later

// libSceGameLiveStreaming after shadPS4: there is no broadcasting service on a desktop host.
// sceGameLiveStreamingGetCurrentStatus reports "not on air"; every other function has no effect
// and returns ORBIS_OK, as in shadPS4.

#include "ps4_gamelivestreaming.h"
#include "ps4_hle_stub.h"
#include <string.h>

// sceGameLiveStreamingGetCurrentStatus(OrbisGameLiveStreamingStatus *status): the console writes
// the status without a null check.
void shim_sceGameLiveStreamingGetCurrentStatus(GuestContext *ctx) {
    if (ctx->rdi) {
        OrbisGameLiveStreamingStatus *status =
            (OrbisGameLiveStreamingStatus *)(ctx->mem_base + ctx->rdi);
        memset(status, 0, sizeof(*status));
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

PS4_HLE_STUB(sceGameLiveStreamingStartDebugBroadcast)
PS4_HLE_STUB(sceGameLiveStreamingStopDebugBroadcast)
PS4_HLE_STUB(sceGameLiveStreamingApplySocialFeedbackMessageFilter)
PS4_HLE_STUB(sceGameLiveStreamingCheckCallback)
PS4_HLE_STUB(sceGameLiveStreamingClearPresetSocialFeedbackCommands)
PS4_HLE_STUB(sceGameLiveStreamingClearSocialFeedbackMessages)
PS4_HLE_STUB(sceGameLiveStreamingClearSpoilerTag)
PS4_HLE_STUB(sceGameLiveStreamingEnableLiveStreaming)
PS4_HLE_STUB(sceGameLiveStreamingEnableSocialFeedback)
PS4_HLE_STUB(sceGameLiveStreamingGetCurrentBroadcastScreenLayout)
PS4_HLE_STUB(sceGameLiveStreamingGetCurrentStatus2)
PS4_HLE_STUB(sceGameLiveStreamingGetProgramInfo)
PS4_HLE_STUB(sceGameLiveStreamingGetSocialFeedbackMessages)
PS4_HLE_STUB(sceGameLiveStreamingGetSocialFeedbackMessagesCount)
PS4_HLE_STUB(sceGameLiveStreamingInitialize)
PS4_HLE_STUB(sceGameLiveStreamingLaunchLiveViewer)
PS4_HLE_STUB(sceGameLiveStreamingLaunchLiveViewerA)
PS4_HLE_STUB(sceGameLiveStreamingPermitLiveStreaming)
PS4_HLE_STUB(sceGameLiveStreamingPermitServerSideRecording)
PS4_HLE_STUB(sceGameLiveStreamingPostSocialMessage)
PS4_HLE_STUB(sceGameLiveStreamingRegisterCallback)
PS4_HLE_STUB(sceGameLiveStreamingScreenCloseSeparateMode)
PS4_HLE_STUB(sceGameLiveStreamingScreenConfigureSeparateMode)
PS4_HLE_STUB(sceGameLiveStreamingScreenInitialize)
PS4_HLE_STUB(sceGameLiveStreamingScreenInitializeSeparateModeParameter)
PS4_HLE_STUB(sceGameLiveStreamingScreenOpenSeparateMode)
PS4_HLE_STUB(sceGameLiveStreamingScreenSetMode)
PS4_HLE_STUB(sceGameLiveStreamingScreenTerminate)
PS4_HLE_STUB(sceGameLiveStreamingSetCameraFrameSetting)
PS4_HLE_STUB(sceGameLiveStreamingSetDefaultServiceProviderPermission)
PS4_HLE_STUB(sceGameLiveStreamingSetGuardAreas)
PS4_HLE_STUB(sceGameLiveStreamingSetInvitationSessionId)
PS4_HLE_STUB(sceGameLiveStreamingSetLinkCommentPreset)
PS4_HLE_STUB(sceGameLiveStreamingSetMaxBitrate)
PS4_HLE_STUB(sceGameLiveStreamingSetMetadata)
PS4_HLE_STUB(sceGameLiveStreamingSetPresetSocialFeedbackCommands)
PS4_HLE_STUB(sceGameLiveStreamingSetPresetSocialFeedbackCommandsDescription)
PS4_HLE_STUB(sceGameLiveStreamingSetServiceProviderPermission)
PS4_HLE_STUB(sceGameLiveStreamingSetSpoilerTag)
PS4_HLE_STUB(sceGameLiveStreamingSetStandbyScreenResource)
PS4_HLE_STUB(sceGameLiveStreamingStartGenerateStandbyScreenResource)
PS4_HLE_STUB(sceGameLiveStreamingStartSocialFeedbackMessageFiltering)
PS4_HLE_STUB(sceGameLiveStreamingStopGenerateStandbyScreenResource)
PS4_HLE_STUB(sceGameLiveStreamingStopSocialFeedbackMessageFiltering)
PS4_HLE_STUB(sceGameLiveStreamingTerminate)
PS4_HLE_STUB(sceGameLiveStreamingUnregisterCallback)
