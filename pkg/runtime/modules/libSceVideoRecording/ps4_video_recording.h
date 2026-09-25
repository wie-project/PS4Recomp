#ifndef PS4_VIDEO_RECORDING_H
#define PS4_VIDEO_RECORDING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OrbisVideoRecordingStatus {
    ORBIS_VIDEO_RECORDING_STATUS_NONE    = 0,
    ORBIS_VIDEO_RECORDING_STATUS_RUNNING = 1,
    ORBIS_VIDEO_RECORDING_STATUS_PAUSED  = 2
} OrbisVideoRecordingStatus;

typedef enum OrbisVideoRecordingInfo {
    ORBIS_VIDEO_RECORDING_INFO_SUBTITLE         = 0x2,
    ORBIS_VIDEO_RECORDING_INFO_DESCRIPTION      = 0x6,
    ORBIS_VIDEO_RECORDING_INFO_COMMENTS         = 0x7,
    ORBIS_VIDEO_RECORDING_INFO_KEYWORDS         = 0x8,
    ORBIS_VIDEO_RECORDING_INFO_CHAPTER          = 0xd,
    ORBIS_VIDEO_RECORDING_INFO_COPYRIGHT        = 0xa01,
    ORBIS_VIDEO_RECORDING_INFO_PERMISSION_LEVEL = 0xa007,
    ORBIS_VIDEO_RECORDING_INFO_GUARD_AREA       = 0xa008,
    ORBIS_VIDEO_RECORDING_INFO_USER_META        = 0xa009
} OrbisVideoRecordingInfo;

typedef struct OrbisVideoRecordingParam2 {
    uint64_t size;
    uint64_t affinity_mask;
    int32_t thread_priority;
    int32_t ring_sec;
} OrbisVideoRecordingParam2;

typedef struct OrbisVideoRecordingInfoGuardArea {
    float x1, y1;
    float x2, y2;
} OrbisVideoRecordingInfoGuardArea;

typedef struct OrbisVideoRecordingInfoUserMeta {
    uint64_t size;
    int32_t flags;
    char name[32];
    char data[128];
} OrbisVideoRecordingInfoUserMeta;

// C API
int32_t sceVideoRecordingQueryMemSize2(const OrbisVideoRecordingParam2 *param, size_t *out_size);
int32_t sceVideoRecordingOpen2(const OrbisVideoRecordingParam2 *param, void *work_mem);
int32_t sceVideoRecordingClose(void);
int32_t sceVideoRecordingStart(void);
int32_t sceVideoRecordingStop(void);
OrbisVideoRecordingStatus sceVideoRecordingGetStatus(void);
int32_t sceVideoRecordingSetInfo(OrbisVideoRecordingInfo set_info, const void *info, uint64_t info_len);

// Shims
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
