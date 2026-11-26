// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_AUDIOIN_H
#define PS4_AUDIOIN_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_AUDIO_IN_ERROR_INVALID_HANDLE ((int32_t)0x80260101)
#define ORBIS_AUDIO_IN_ERROR_INVALID_SIZE ((int32_t)0x80260102)
#define ORBIS_AUDIO_IN_ERROR_INVALID_FREQ ((int32_t)0x80260103)
#define ORBIS_AUDIO_IN_ERROR_INVALID_TYPE ((int32_t)0x80260104)
#define ORBIS_AUDIO_IN_ERROR_INVALID_POINTER ((int32_t)0x80260105)
#define ORBIS_AUDIO_IN_ERROR_INVALID_PARAM ((int32_t)0x80260106)
#define ORBIS_AUDIO_IN_ERROR_PORT_FULL ((int32_t)0x80260107)
#define ORBIS_AUDIO_IN_ERROR_OUT_OF_MEMORY ((int32_t)0x80260108)
#define ORBIS_AUDIO_IN_ERROR_NOT_OPENED ((int32_t)0x80260109)
#define ORBIS_AUDIO_IN_ERROR_BUSY ((int32_t)0x8026010A)
#define ORBIS_AUDIO_IN_ERROR_SYSTEM_MEMORY ((int32_t)0x8026010B)
#define ORBIS_AUDIO_IN_ERROR_SYSTEM_IPC ((int32_t)0x8026010C)

#define ORBIS_AUDIO_IN_NUM_PORTS 7
#define ORBIS_AUDIO_IN_MAX_GRAIN 2048

// sceAudioInOpen param: sample format.
#define ORBIS_AUDIO_IN_PARAM_FORMAT_S16_MONO 0
#define ORBIS_AUDIO_IN_PARAM_FORMAT_S16_STEREO 2

// sceAudioInOpen type.
#define ORBIS_AUDIO_IN_TYPE_VOICE_CHAT 0
#define ORBIS_AUDIO_IN_TYPE_GENERAL 1
#define ORBIS_AUDIO_IN_TYPE_VOICE_RECOGNITION 5

// sceAudioInGetSilentState bits.
#define ORBIS_AUDIO_IN_SILENT_STATE_DEVICE_NONE 0x00000001
#define ORBIS_AUDIO_IN_SILENT_STATE_PRIORITY_LOW 0x00000002
#define ORBIS_AUDIO_IN_SILENT_STATE_UNABLE_FORMAT 0x00000008

// Handles are 0x30000000 | type << 16 | port.
#define ORBIS_AUDIO_IN_HANDLE_TAG 0x30000000

// C API. The capture device is the host default input; PS4_AUDIO_IN=none disables capture
// (shadPS4's mic device "None"), and ports then read silence and report DEVICE_NONE.
int32_t sceAudioInOpen(int32_t userId, uint32_t type, uint32_t index, uint32_t len, uint32_t freq,
                       uint32_t param);
int32_t sceAudioInHqOpen(int32_t userId, uint32_t type, uint32_t index, uint32_t len, uint32_t freq,
                         uint32_t param);
int32_t sceAudioInClose(int32_t handle);
int32_t sceAudioInInput(int32_t handle, void *dest);
int32_t sceAudioInGetSilentState(int32_t handle);

void shim_sceAudioInOpen(GuestContext *ctx);
void shim_sceAudioInHqOpen(GuestContext *ctx);
void shim_sceAudioInClose(GuestContext *ctx);
void shim_sceAudioInInput(GuestContext *ctx);
void shim_sceAudioInGetSilentState(GuestContext *ctx);

// Functions that shadPS4 stubs as well.
void shim_sceAudioInChangeAppModuleState(GuestContext *ctx);
void shim_sceAudioInCountPorts(GuestContext *ctx);
void shim_sceAudioInDeviceHqOpen(GuestContext *ctx);
void shim_sceAudioInDeviceIdHqOpen(GuestContext *ctx);
void shim_sceAudioInDeviceIdOpen(GuestContext *ctx);
void shim_sceAudioInDeviceOpen(GuestContext *ctx);
void shim_sceAudioInDeviceOpenEx(GuestContext *ctx);
void shim_sceAudioInExtClose(GuestContext *ctx);
void shim_sceAudioInExtCtrl(GuestContext *ctx);
void shim_sceAudioInExtInput(GuestContext *ctx);
void shim_sceAudioInExtOpen(GuestContext *ctx);
void shim_sceAudioInExtSetAecMode(GuestContext *ctx);
void shim_sceAudioInGetGain(GuestContext *ctx);
void shim_sceAudioInGetHandleStatusInfo(GuestContext *ctx);
void shim_sceAudioInGetRerouteCount(GuestContext *ctx);
void shim_sceAudioInHqOpenEx(GuestContext *ctx);
void shim_sceAudioInInit(GuestContext *ctx);
void shim_sceAudioInInputs(GuestContext *ctx);
void shim_sceAudioInIsSharedDevice(GuestContext *ctx);
void shim_sceAudioInOpenEx(GuestContext *ctx);
void shim_sceAudioInSetAllMute(GuestContext *ctx);
void shim_sceAudioInSetCompressorPreGain(GuestContext *ctx);
void shim_sceAudioInSetConnections(GuestContext *ctx);
void shim_sceAudioInSetConnectionsForUser(GuestContext *ctx);
void shim_sceAudioInSetDevConnection(GuestContext *ctx);
void shim_sceAudioInSetFocusForUser(GuestContext *ctx);
void shim_sceAudioInSetMode(GuestContext *ctx);
void shim_sceAudioInSetMode2(GuestContext *ctx);
void shim_sceAudioInSetPortConnections(GuestContext *ctx);
void shim_sceAudioInSetPortStatuses(GuestContext *ctx);
void shim_sceAudioInSetSparkParam(GuestContext *ctx);
void shim_sceAudioInSetSparkSideTone(GuestContext *ctx);
void shim_sceAudioInSetUsbGain(GuestContext *ctx);
void shim_sceAudioInSetUserMute(GuestContext *ctx);
void shim_sceAudioInVmicCreate(GuestContext *ctx);
void shim_sceAudioInVmicDestroy(GuestContext *ctx);
void shim_sceAudioInVmicWrite(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_AUDIOIN_H
