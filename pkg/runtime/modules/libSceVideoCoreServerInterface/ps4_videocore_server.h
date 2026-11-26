// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceVideoCoreServerInterface, after shadPS4 (src/core/libraries/videodec/videocore_server.{h,cpp},
// videodec_error.h): bookkeeping of the hardware decoder resources the video libraries reserve.
#ifndef PS4_VIDEOCORE_SERVER_H
#define PS4_VIDEOCORE_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_VIDEO_CORE_ERROR_INVALID_ARGUMENT              ((int32_t)0x80DC0001)
#define ORBIS_VIDEO_CORE_ERROR_BE_WRAPPER_RESOURCE_NOT_FOUND ((int32_t)0x80DC000E)
#define ORBIS_VIDEO_CORE_ERROR_RESOURCE_NOT_FOUND            ((int32_t)0x80DC0015)

typedef struct OrbisVideoCoreAcquireDecoderResourceParam {
    uint32_t resource_type;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t stream_spec;
} OrbisVideoCoreAcquireDecoderResourceParam;
_Static_assert(sizeof(OrbisVideoCoreAcquireDecoderResourceParam) == 0x10, "OrbisVideoCoreAcquireDecoderResourceParam");

// callback is the guest address of a void (*)(u32 resource_id, u64 user_data); it is recorded
// with the resource, as in shadPS4, which never calls it either. Identifiers start at 1 and are
// not reused. is_be_wrapper selects the ...ForBeWrapper entry points: a resource can only be
// released through the family that acquired it.
int32_t ps4_videocore_acquire(const OrbisVideoCoreAcquireDecoderResourceParam *param, uint64_t callback,
                              uint32_t *out_resource_id, bool is_be_wrapper);
int32_t ps4_videocore_release(uint32_t resource_id, bool is_be_wrapper);

// Number of resources currently held (for tests).
uint32_t ps4_videocore_resource_count(void);

void shim_sceVideoCoreAcquireDecoderResource(GuestContext *ctx);
void shim_sceVideoCoreReleaseDecoderResource(GuestContext *ctx);
void shim_sceVideoCoreAcquireDecoderResourceForBeWrapper(GuestContext *ctx);
void shim_sceVideoCoreReleaseDecoderResourceForBeWrapper(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_VIDEOCORE_SERVER_H
