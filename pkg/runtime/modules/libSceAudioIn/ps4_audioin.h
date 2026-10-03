// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_AUDIOIN_H
#define PS4_AUDIOIN_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_sceAudioInOpen(GuestContext *ctx);
void shim_sceAudioInClose(GuestContext *ctx);
void shim_sceAudioInInput(GuestContext *ctx);
void shim_sceAudioInGetSilentState(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_AUDIOIN_H
