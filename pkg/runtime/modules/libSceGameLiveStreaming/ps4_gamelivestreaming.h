#ifndef PS4_GAMELIVESTREAMING_H
#define PS4_GAMELIVESTREAMING_H

#include "recomp_runtime.h"

void shim_sceGameLiveStreamingInitialize(GuestContext *ctx);
void shim_sceGameLiveStreamingTerminate(GuestContext *ctx);
void shim_sceGameLiveStreamingEnableLiveStreaming(GuestContext *ctx);
void shim_sceGameLiveStreamingPermitLiveStreaming(GuestContext *ctx);
void shim_sceGameLiveStreamingPermitServerSideRecording(GuestContext *ctx);
void shim_sceGameLiveStreamingGetCurrentStatus(GuestContext *ctx);
void shim_sceGameLiveStreamingGetCurrentStatus2(GuestContext *ctx);
void shim_sceGameLiveStreamingSetServiceProviderPermission(GuestContext *ctx);
void shim_sceGameLiveStreamingSetDefaultServiceProviderPermission(GuestContext *ctx);
void shim_sceGameLiveStreamingSetGuardAreas(GuestContext *ctx);
void shim_sceGameLiveStreamingSetSpoilerTag(GuestContext *ctx);
void shim_sceGameLiveStreamingClearSpoilerTag(GuestContext *ctx);
void shim_sceGameLiveStreamingStartSocialFeedbackMessageFiltering(GuestContext *ctx);
void shim_sceGameLiveStreamingStopSocialFeedbackMessageFiltering(GuestContext *ctx);
void shim_sceGameLiveStreamingCheckCallback(GuestContext *ctx);

#endif // PS4_GAMELIVESTREAMING_H
