#include "ps4_video_recording.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_vr_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_vr_open = false;
static OrbisVideoRecordingStatus g_vr_status = ORBIS_VIDEO_RECORDING_STATUS_NONE;

int32_t sceVideoRecordingQueryMemSize2(const OrbisVideoRecordingParam2 *param, size_t *out_size) {
    if (!param || !out_size) {
        return (int32_t)0x80a80003; // INVALID_VALUE
    }

    size_t required = 16 * 1024 * 1024; // Standard 16MB ring buffer
    if (param->ring_sec > 0) {
        required = (size_t)param->ring_sec * 32768 + 1048576;
    }
    *out_size = required;
    return 0;
}

int32_t sceVideoRecordingOpen2(const OrbisVideoRecordingParam2 *param, void *work_mem) {
    (void)work_mem;
    if (!param) {
        return (int32_t)0x80a80003; // INVALID_VALUE
    }

    pthread_mutex_lock(&g_vr_mutex);
    g_vr_open = true;
    g_vr_status = ORBIS_VIDEO_RECORDING_STATUS_NONE;
    pthread_mutex_unlock(&g_vr_mutex);
    return 0;
}

int32_t sceVideoRecordingClose(void) {
    pthread_mutex_lock(&g_vr_mutex);
    g_vr_open = false;
    g_vr_status = ORBIS_VIDEO_RECORDING_STATUS_NONE;
    pthread_mutex_unlock(&g_vr_mutex);
    return 0;
}

int32_t sceVideoRecordingStart(void) {
    pthread_mutex_lock(&g_vr_mutex);
    g_vr_status = ORBIS_VIDEO_RECORDING_STATUS_RUNNING;
    pthread_mutex_unlock(&g_vr_mutex);
    return 0;
}

int32_t sceVideoRecordingStop(void) {
    pthread_mutex_lock(&g_vr_mutex);
    g_vr_status = ORBIS_VIDEO_RECORDING_STATUS_NONE;
    pthread_mutex_unlock(&g_vr_mutex);
    return 0;
}

OrbisVideoRecordingStatus sceVideoRecordingGetStatus(void) {
    pthread_mutex_lock(&g_vr_mutex);
    OrbisVideoRecordingStatus st = g_vr_status;
    pthread_mutex_unlock(&g_vr_mutex);
    return st;
}

int32_t sceVideoRecordingSetInfo(OrbisVideoRecordingInfo set_info, const void *info, uint64_t info_len) {
    (void)info_len;
    if (!info) {
        return (int32_t)0x80a80003; // INVALID_VALUE
    }

    switch (set_info) {
    case ORBIS_VIDEO_RECORDING_INFO_SUBTITLE:
    case ORBIS_VIDEO_RECORDING_INFO_DESCRIPTION:
    case ORBIS_VIDEO_RECORDING_INFO_COMMENTS:
    case ORBIS_VIDEO_RECORDING_INFO_KEYWORDS:
    case ORBIS_VIDEO_RECORDING_INFO_CHAPTER:
    case ORBIS_VIDEO_RECORDING_INFO_COPYRIGHT:
    case ORBIS_VIDEO_RECORDING_INFO_PERMISSION_LEVEL:
    case ORBIS_VIDEO_RECORDING_INFO_GUARD_AREA:
    case ORBIS_VIDEO_RECORDING_INFO_USER_META:
        break;
    default:
        return (int32_t)0x80a80003; // INVALID_VALUE
    }

    return 0;
}

// Shims
void shim_sceVideoRecordingQueryMemSize2(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVideoRecordingQueryMemSize2(
        (const OrbisVideoRecordingParam2 *)ctx->rdi,
        (size_t *)ctx->rsi
    );
}

void shim_sceVideoRecordingOpen2(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVideoRecordingOpen2(
        (const OrbisVideoRecordingParam2 *)ctx->rdi,
        (void *)ctx->rsi
    );
}

void shim_sceVideoRecordingClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVideoRecordingClose();
}

void shim_sceVideoRecordingStart(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVideoRecordingStart();
}

void shim_sceVideoRecordingStop(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVideoRecordingStop();
}

void shim_sceVideoRecordingGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVideoRecordingGetStatus();
}

void shim_sceVideoRecordingSetInfo(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVideoRecordingSetInfo(
        (OrbisVideoRecordingInfo)ctx->rdi,
        (const void *)ctx->rsi,
        (uint64_t)ctx->rdx
    );
}
