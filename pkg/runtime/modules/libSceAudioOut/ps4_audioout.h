#ifndef PS4_AUDIOOUT_H
#define PS4_AUDIOOUT_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_sceAudioOutInit(GuestContext *ctx);
void shim_sceAudioOutOpen(GuestContext *ctx);
void shim_sceAudioOutOutput(GuestContext *ctx);
void shim_sceAudioOutClose(GuestContext *ctx);
void shim_sceAudioOutGetPortState(GuestContext *ctx);
void shim_sceAudioOutSetVolume(GuestContext *ctx);
void shim_sceAudioOutSetMixLevelPadSpk(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_AUDIOOUT_H
