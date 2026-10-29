#ifndef PS4_VIDEOOUT_H
#define PS4_VIDEOOUT_H

#include "recomp_runtime.h"
#include "ps4_equeue.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// sceVideoOut* error codes (SCE_VIDEO_OUT_ERROR_*).
#define ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE 0x80290001u
#define ORBIS_VIDEO_OUT_ERROR_INVALID_ADDRESS 0x80290002u
#define ORBIS_VIDEO_OUT_ERROR_INVALID_PITCH 0x80290004u
#define ORBIS_VIDEO_OUT_ERROR_INVALID_TILING_MODE 0x80290007u
#define ORBIS_VIDEO_OUT_ERROR_INVALID_ASPECT_RATIO 0x80290008u
#define ORBIS_VIDEO_OUT_ERROR_INVALID_INDEX 0x8029000Au
#define ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE 0x8029000Bu
#define ORBIS_VIDEO_OUT_ERROR_NO_EMPTY_SLOT 0x8029000Fu
#define ORBIS_VIDEO_OUT_ERROR_SLOT_OCCUPIED 0x80290010u

// Display buffer indices per handle, and buffer sets (sceVideoOutRegisterBuffers calls).
#define PS4_VIDEO_OUT_MAX_BUFFERS 16
#define PS4_VIDEO_OUT_MAX_BUFFER_SETS 4

typedef struct OrbisVideoOutBufferAttribute {
    int32_t format;
    int32_t tmode;
    int32_t aspect;
    uint32_t width;
    uint32_t height;
    uint32_t pixelPitch;
    uint32_t option;
    uint32_t reserved0;
    uint64_t reserved1;
} OrbisVideoOutBufferAttribute;
_Static_assert(sizeof(OrbisVideoOutBufferAttribute) == 0x28, "SceVideoOutBufferAttribute is 0x28 bytes");

// OrbisVideoOutBufferAttribute.option values. STRICT_COLORIMETRY asks the display path to output
// the buffer's colorimetry without system adjustment; the presenter applies none and ignores it.
#define ORBIS_VIDEO_OUT_BUFFER_ATTRIBUTE_OPTION_NONE 0
#define ORBIS_VIDEO_OUT_BUFFER_ATTRIBUTE_OPTION_VR 7
#define ORBIS_VIDEO_OUT_BUFFER_ATTRIBUTE_OPTION_STRICT_COLORIMETRY 8

typedef struct OrbisVideoOutFlipStatus {
    uint64_t num;
    uint64_t ptime;
    uint64_t stime;
    int64_t flipArg;
    uint64_t reserved[2];
    int32_t numGpuFlipPending;
    int32_t numFlipPending;
    int32_t currentBuffer;
    uint32_t reserved1;
} OrbisVideoOutFlipStatus;

typedef struct OrbisVideoOutResolutionStatus {
    uint32_t width;
    uint32_t height;
    uint32_t paneWidth;
    uint32_t paneHeight;
    uint64_t refreshRate;
    float screenSize;
    uint16_t flags;
    uint16_t reserved0;
    uint32_t reserved1[3];
} OrbisVideoOutResolutionStatus;

typedef struct SceVideoOutVblankStatus {
    uint64_t count;
    uint64_t process_time;
    uint64_t tsc;
    uint64_t reserved[1];
    uint8_t flags;
    uint8_t pad1[7];
} SceVideoOutVblankStatus;

typedef struct SceVideoOutDeviceCapabilityInfo {
    uint64_t capability;
} SceVideoOutDeviceCapabilityInfo;

int32_t sceVideoOutOpen(int32_t userId, int32_t busType, int32_t index, const void *param);
int32_t sceVideoOutClose(int32_t handle);
void sceVideoOutSetBufferAttribute(OrbisVideoOutBufferAttribute *attr, uint32_t pixelFormat, uint32_t tilingMode, uint32_t aspectRatio, uint32_t width, uint32_t height, uint32_t pitch);
// Registers bufferCount buffers at indices startIndex.. as one buffer set with attribute attr.
// Returns the set index (0..3, for sceVideoOutUnregisterBuffers) or an ORBIS_VIDEO_OUT_ERROR_*
// code: INVALID_ADDRESS (no array or attribute), INVALID_HANDLE, NO_EMPTY_SLOT (4 sets
// registered), INVALID_VALUE (indices outside 0..15, a reserved attribute word set; option is stored, not checked),
// SLOT_OCCUPIED (an index already belongs to a set), INVALID_ASPECT_RATIO, INVALID_PITCH
// (width above the pitch), INVALID_TILING_MODE.
int32_t sceVideoOutRegisterBuffers(int32_t handle, int32_t startIndex, void * const *bufferArray, int32_t bufferCount, const OrbisVideoOutBufferAttribute *attr);
int32_t sceVideoOutSetFlipRate(int32_t handle, int32_t fliprate);
int32_t sceVideoOutAddFlipEvent(OrbisKernelEqueue eq, int32_t handle, void *flipArg);
int32_t sceVideoOutSubmitFlip(GuestContext *ctx, int32_t handle, int32_t bufferIndex, uint32_t flipMode, int64_t flipArg);
// sceVideoOutSubmitFlip for the GPU flips of libSceGnmDriver (gpu_flip = 1: SubmitAndFlip,
// RequestFlipAndSubmitDone), which also lock the buffer's flip label when the command processor
// reaches the flip.
int32_t ps4_videoout_submit_flip(GuestContext *ctx, int32_t handle, int32_t bufferIndex, uint32_t flipMode,
                                 int64_t flipArg, int gpu_flip);
// The guest address of the handle's flip labels (one uint64_t per buffer index; 1 while the
// buffer has been flipped by the GPU, 0 once the flip after it has completed), as
// polled by sceGnmInsertWaitFlipDone. Returns the number of labels (16) or an error code.
int32_t sceVideoOutGetBufferLabelAddress(GuestContext *ctx, int32_t handle, uint64_t *label_addr);
// Is guest address `addr` one of the flip labels? (Any thread; takes no lock.)
int ps4_videoout_is_flip_label(uint64_t addr);
// Like ps4_videoout_is_flip_label; also the label's handle and buffer index and the buffer of the
// handle's last completed flip (-1 = none), for logs. Any out pointer may be NULL.
int ps4_videoout_flip_label_info(uint64_t addr, int32_t *handle, int32_t *index, int32_t *prev_index);
int32_t sceVideoOutGetFlipStatus(int32_t handle, OrbisVideoOutFlipStatus *status);
int32_t sceVideoOutGetResolutionStatus(int32_t handle, OrbisVideoOutResolutionStatus *status);
int32_t sceVideoOutIsFlipPending(int32_t handle);
// Unregisters every buffer of the set; flips of its indices fail from then on (INVALID_INDEX).
// INVALID_HANDLE, or INVALID_VALUE for a set that is not registered.
int32_t sceVideoOutUnregisterBuffers(int32_t handle, int32_t setIndex);

int32_t sceVideoOutAddVblankEvent(OrbisKernelEqueue eq, int32_t handle, void *udata);
int32_t sceVideoOutDeleteVblankEvent(OrbisKernelEqueue eq, int32_t handle);
int32_t sceVideoOutWaitVblank(int32_t handle);
int32_t sceVideoOutGetVblankStatus(int32_t handle, SceVideoOutVblankStatus *status);
int32_t sceVideoOutGetDeviceCapabilityInfo(int32_t handle, SceVideoOutDeviceCapabilityInfo *info);
int32_t sceVideoOutGetEventCount(const OrbisKernelEvent *ev);
int32_t sceVideoOutGetEventId(const OrbisKernelEvent *ev);

int32_t sceVideoOutConfigureOutputMode_(int32_t handle, uint32_t reserved, const void *mode, const void *options, uint32_t size_mode, uint32_t size_options);
void sceVideoOutConfigureOptionsInitialize_(void *options, uint32_t size);
int32_t sceVideoOutDeleteFlipEvent(OrbisKernelEqueue eq, int32_t handle);
int32_t sceVideoOutGetEventData(const OrbisKernelEvent *ev, int64_t *data);
void sceVideoOutModeSetAny_(void *mode, uint32_t size);

void shim_sceVideoOutOpen(GuestContext *ctx);
void shim_sceVideoOutClose(GuestContext *ctx);
void shim_sceVideoOutSetBufferAttribute(GuestContext *ctx);
void shim_sceVideoOutRegisterBuffers(GuestContext *ctx);
void shim_sceVideoOutSetFlipRate(GuestContext *ctx);
void shim_sceVideoOutAddFlipEvent(GuestContext *ctx);
void shim_sceVideoOutSubmitFlip(GuestContext *ctx);
void shim_sceVideoOutGetFlipStatus(GuestContext *ctx);
void shim_sceVideoOutGetResolutionStatus(GuestContext *ctx);
void shim_sceVideoOutIsFlipPending(GuestContext *ctx);
void shim_sceVideoOutGetBufferLabelAddress(GuestContext *ctx);
void shim_sceVideoOutUnregisterBuffers(GuestContext *ctx);
void shim_sceVideoOutAddVblankEvent(GuestContext *ctx);
void shim_sceVideoOutDeleteVblankEvent(GuestContext *ctx);
void shim_sceVideoOutWaitVblank(GuestContext *ctx);
void shim_sceVideoOutGetVblankStatus(GuestContext *ctx);
void shim_sceVideoOutGetDeviceCapabilityInfo(GuestContext *ctx);
void shim_sceVideoOutGetDeviceCapabilityInfo_(GuestContext *ctx);
void shim_sceVideoOutGetEventCount(GuestContext *ctx);
void shim_sceVideoOutGetEventId(GuestContext *ctx);
void shim_sceVideoOutConfigureOutputMode_(GuestContext *ctx);
void shim_sceVideoOutConfigureOptionsInitialize_(GuestContext *ctx);
void shim_sceVideoOutDeleteFlipEvent(GuestContext *ctx);
void shim_sceVideoOutGetEventData(GuestContext *ctx);
void shim_sceVideoOutModeSetAny_(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_VIDEOOUT_H
