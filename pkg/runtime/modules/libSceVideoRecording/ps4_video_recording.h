#ifndef PS4_VIDEO_RECORDING_H
#define PS4_VIDEO_RECORDING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error codes (shadPS4 video_recording_error.h).
#define ORBIS_VIDEO_RECORDING_ERROR_OUT_OF_MEMORY ((int32_t)0x80A80001)
#define ORBIS_VIDEO_RECORDING_ERROR_FATAL ((int32_t)0x80A80002)
#define ORBIS_VIDEO_RECORDING_ERROR_INVALID_VALUE ((int32_t)0x80A80003)
#define ORBIS_VIDEO_RECORDING_ERROR_FILE_OPEN ((int32_t)0x80A80004)
#define ORBIS_VIDEO_RECORDING_ERROR_FILE_WRITE ((int32_t)0x80A80005)
#define ORBIS_VIDEO_RECORDING_ERROR_INVALID_STATE ((int32_t)0x80A80006)
#define ORBIS_VIDEO_RECORDING_ERROR_INTERRUPTED ((int32_t)0x80A80007)
#define ORBIS_VIDEO_RECORDING_ERROR_UNSUPPORTED ((int32_t)0x80A80008)
#define ORBIS_VIDEO_RECORDING_ERROR_NO_SPACE ((int32_t)0x80A80009)
#define ORBIS_VIDEO_RECORDING_ERROR_FILE_NO_DATA ((int32_t)0x80A8000A)

// OrbisVideoRecordingInfo: what sceVideoRecordingSetInfo sets.
#define ORBIS_VIDEO_RECORDING_INFO_SUBTITLE 0x2u
#define ORBIS_VIDEO_RECORDING_INFO_DESCRIPTION 0x6u
#define ORBIS_VIDEO_RECORDING_INFO_COMMENTS 0x7u
#define ORBIS_VIDEO_RECORDING_INFO_KEYWORDS 0x8u
#define ORBIS_VIDEO_RECORDING_INFO_CHAPTER 0xDu
#define ORBIS_VIDEO_RECORDING_INFO_COPYRIGHT 0xA01u
#define ORBIS_VIDEO_RECORDING_INFO_PERMISSION_LEVEL 0xA007u
#define ORBIS_VIDEO_RECORDING_INFO_GUARD_AREA 0xA008u
#define ORBIS_VIDEO_RECORDING_INFO_USER_META 0xA009u

int32_t sceVideoRecordingSetInfo(uint32_t set_info, const void *info, uint64_t info_len);

void shim_sceVideoRecordingQueryMemSize2(GuestContext *ctx);
void shim_sceVideoRecordingOpen2(GuestContext *ctx);
void shim_sceVideoRecordingClose(GuestContext *ctx);
void shim_sceVideoRecordingStart(GuestContext *ctx);
void shim_sceVideoRecordingStop(GuestContext *ctx);
void shim_sceVideoRecordingGetStatus(GuestContext *ctx);
void shim_sceVideoRecordingSetInfo(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_VIDEO_RECORDING_H
