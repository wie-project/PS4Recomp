// libSceVideoRecording, following shadPS4 (src/core/libraries/video_recording).
//
// shadPS4 registers only sceVideoRecordingSetInfo. Every other function of the library resolves
// there to the generic unresolved-import stub, which returns 0 and touches nothing; the shims
// for those functions below do the same. There is no recorder on the host.
#include "ps4_video_recording.h"
#include <stdatomic.h>
#include <stdio.h>

// One line per function the first time a game calls it, as shadPS4's stub logs every call.
static void video_recording_stub(const char *name, _Atomic bool *warned) {
    if (!atomic_exchange(warned, true)) {
        fprintf(stderr, "[ps4-stub] %s -> 0 (not implemented in shadPS4)\n", name);
    }
}

#define VIDEO_RECORDING_STUB(fn)                                          \
    void shim_##fn(GuestContext *ctx) {                                   \
        static _Atomic bool warned;                                       \
        video_recording_stub(#fn, &warned);                               \
        ctx->rax = 0;                                                     \
        SHIM_RETURN();                                                    \
    }

VIDEO_RECORDING_STUB(sceVideoRecordingQueryMemSize2)
VIDEO_RECORDING_STUB(sceVideoRecordingOpen2)
VIDEO_RECORDING_STUB(sceVideoRecordingClose)
VIDEO_RECORDING_STUB(sceVideoRecordingStart)
VIDEO_RECORDING_STUB(sceVideoRecordingStop)
VIDEO_RECORDING_STUB(sceVideoRecordingGetStatus)

int32_t sceVideoRecordingSetInfo(uint32_t set_info, const void *info, uint64_t info_len) {
    (void)info_len;
    // shadPS4's range checks, unchanged. Its fourth range, (Chapter, PermissionLevel), also
    // covers Copyright (0xA01), so Copyright is rejected as well.
    if (set_info < ORBIS_VIDEO_RECORDING_INFO_SUBTITLE ||
        (set_info > ORBIS_VIDEO_RECORDING_INFO_SUBTITLE && set_info < ORBIS_VIDEO_RECORDING_INFO_DESCRIPTION) ||
        (set_info > ORBIS_VIDEO_RECORDING_INFO_KEYWORDS && set_info < ORBIS_VIDEO_RECORDING_INFO_CHAPTER) ||
        (set_info > ORBIS_VIDEO_RECORDING_INFO_CHAPTER && set_info < ORBIS_VIDEO_RECORDING_INFO_COPYRIGHT) ||
        (set_info > ORBIS_VIDEO_RECORDING_INFO_CHAPTER && set_info < ORBIS_VIDEO_RECORDING_INFO_PERMISSION_LEVEL) ||
        set_info > ORBIS_VIDEO_RECORDING_INFO_USER_META) {
        return ORBIS_VIDEO_RECORDING_ERROR_INVALID_VALUE;
    }
    // The library relies on libSceAvcap for errors in the other parameters.
    if (!info) {
        return ORBIS_VIDEO_RECORDING_ERROR_INVALID_VALUE;
    }
    return 0;
}

// sceVideoRecordingSetInfo(info, data, size)
void shim_sceVideoRecordingSetInfo(GuestContext *ctx) {
    const void *info = ctx->rsi ? (const void *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVideoRecordingSetInfo((uint32_t)ctx->rdi, info, ctx->rdx);
    SHIM_RETURN();
}
