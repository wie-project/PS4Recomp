#ifndef PS4_VOICE_QOS_H
#define PS4_VOICE_QOS_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceVoiceQoSInit(void);
int32_t sceVoiceQoSInitHQ(void);
int32_t sceVoiceQoSEnd(void);
int32_t sceVoiceQoSConnect(void);
int32_t shim_sceVoiceQoSDisconnect_impl(void);
int32_t sceVoiceQoSGetStatus(void);

void shim_sceVoiceQoSInit(GuestContext *ctx);
void shim_sceVoiceQoSInitHQ(GuestContext *ctx);
void shim_sceVoiceQoSEnd(GuestContext *ctx);
void shim_sceVoiceQoSConnect(GuestContext *ctx);
void shim_sceVoiceQoSDisconnect(GuestContext *ctx);
void shim_sceVoiceQoSGetStatus(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_VOICE_QOS_H
