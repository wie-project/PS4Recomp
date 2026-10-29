#ifndef PS4_AUDIO3D_H
#define PS4_AUDIO3D_H

// libSceAudio3d HLE: guest shims and the host-pointer API they wrap (the host
// API is what tests/host/audio3d_test.c drives). The API contract is
// documented at the top of ps4_audio3d.c.

#include "recomp_runtime.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_AUDIO3D_ERROR_UNKNOWN ((int32_t)0x80EA0001)
#define PS4_AUDIO3D_ERROR_INVALID_PORT ((int32_t)0x80EA0002)
#define PS4_AUDIO3D_ERROR_INVALID_OBJECT ((int32_t)0x80EA0003)
#define PS4_AUDIO3D_ERROR_INVALID_PARAMETER ((int32_t)0x80EA0004)
#define PS4_AUDIO3D_ERROR_OUT_OF_MEMORY ((int32_t)0x80EA0005)
#define PS4_AUDIO3D_ERROR_OUT_OF_RESOURCES ((int32_t)0x80EA0006)
#define PS4_AUDIO3D_ERROR_NOT_READY ((int32_t)0x80EA0007)
#define PS4_AUDIO3D_ERROR_NOT_SUPPORTED ((int32_t)0x80EA0008)

// SceAudio3dAttributeId (object attributes)
#define PS4_AUDIO3D_ATTR_PCM 1u
#define PS4_AUDIO3D_ATTR_PRIORITY 2u
#define PS4_AUDIO3D_ATTR_POSITION 3u
#define PS4_AUDIO3D_ATTR_SPREAD 4u
#define PS4_AUDIO3D_ATTR_GAIN 5u
#define PS4_AUDIO3D_ATTR_PASSTHROUGH 6u
#define PS4_AUDIO3D_ATTR_RESET_STATE 7u
#define PS4_AUDIO3D_ATTR_APPLICATION_SPECIFIC 8u
#define PS4_AUDIO3D_ATTR_AMBISONICS 9u
#define PS4_AUDIO3D_ATTR_RESTRICTED 10u
#define PS4_AUDIO3D_ATTR_OUTPUT_ROUTE 11u
// port attributes
#define PS4_AUDIO3D_PORT_ATTR_LATE_REVERB_LEVEL 0x10001u
#define PS4_AUDIO3D_PORT_ATTR_DOWNMIX_SPREAD_RADIUS 0x10002u
#define PS4_AUDIO3D_PORT_ATTR_DOWNMIX_SPREAD_HEIGHT_AWARE 0x10003u

#define PS4_AUDIO3D_AMBISONICS_NONE 0xFFFFFFFFu

// SceAudio3dBufferMode
#define PS4_AUDIO3D_BUFFER_NO_ADVANCE 0u
#define PS4_AUDIO3D_BUFFER_ADVANCE_NO_PUSH 1u
#define PS4_AUDIO3D_BUFFER_ADVANCE_AND_PUSH 2u
// SceAudio3dBlocking
#define PS4_AUDIO3D_BLOCKING_ASYNC 0u
#define PS4_AUDIO3D_BLOCKING_SYNC 1u
// SceAudio3dFormat
#define PS4_AUDIO3D_FORMAT_S16 0u
#define PS4_AUDIO3D_FORMAT_FLOAT 1u

// Guest structures (offsets in ps4_audio3d.c).
typedef struct {
  uint64_t size_this;
  uint32_t granularity;
  uint32_t rate;
  uint32_t max_objects;
  uint32_t queue_depth;
  uint32_t buffer_mode;
  uint32_t pad;
  uint32_t num_beds;
} Ps4Audio3dOpenParameters;

typedef struct {
  uint32_t attribute_id;
  uint32_t pad;
  uint64_t value; // guest pointer
  uint64_t value_size;
} Ps4Audio3dAttribute;

typedef struct {
  uint32_t format;
  uint32_t pad;
  uint64_t sample_buffer; // guest pointer
  uint32_t num_samples;
  uint32_t pad2;
} Ps4Audio3dPcm;

typedef struct {
  int32_t handle;
  uint32_t pad;
  uint64_t ptr; // guest pointer
} Ps4Audio3dAudioOutParam;

// Cumulative signal levels of a port (read by tests and by the PS4_AUDIO_LOG statistics). The
// counters (advances, grains, frames) always run; the sums of squares only accumulate while
// levels are in use: from Initialize when PS4_AUDIO_LOG is set, otherwise from the first
// ps4_audio3d_port_get_levels call on (that call returns the sums so far, possibly 0).
// Sums of squares are over every channel sample.
//   objects: the panned object mix alone (PortAdvance), object_frames frames of 8 channels;
//   bed:     the BedWrite beds alone, mixed in the same advances;
//   mix:     the port's own output grain (objects + bed) that PortPush submits;
//   buses:   the grains the game queued on its associated AudioOut ports
//            (sceAudio3dAudioOutOutputs), bus_samples channel samples.
typedef struct {
  uint64_t advances;
  uint64_t object_grains; // object PCM grains consumed by advances
  uint64_t frames;        // frames mixed by advances (granularity each)
  double object_sq, bed_sq, mix_sq;
  double mix_ch_sq[8];    // mix_sq per output channel (FL FR FC LFE BL BR SL SR)
  uint64_t bus_grains, bus_samples;
  double bus_sq;
} Ps4Audio3dLevels;

// Host API. Guest pointers embedded in guest structures (attribute values, PCM
// buffers, AudioOut buffers) are translated as base + pointer; tests pass 0.
int32_t ps4_audio3d_initialize(int64_t reserved);
int32_t ps4_audio3d_terminate(void);
int32_t ps4_audio3d_get_default_open_parameters(void *params);
int32_t ps4_audio3d_port_open(int32_t user_id, const void *params, uint32_t *port_id);
int32_t ps4_audio3d_port_close(uint32_t port_id);
int32_t ps4_audio3d_port_set_attribute(uint32_t port_id, uint32_t attribute_id, const void *value,
                                       uint64_t size);
int32_t ps4_audio3d_port_get_attributes_supported(uint32_t port_id, uint32_t *caps, uint32_t *num);
int32_t ps4_audio3d_port_get_queue_level(uint32_t port_id, uint32_t *level, uint32_t *available);
int32_t ps4_audio3d_port_advance(uint32_t port_id);
int32_t ps4_audio3d_port_push(uint32_t port_id, uint32_t blocking);
int32_t ps4_audio3d_object_reserve(uint32_t port_id, uint32_t *object_id);
int32_t ps4_audio3d_object_unreserve(uint32_t port_id, uint32_t object_id);
int32_t ps4_audio3d_object_set_attributes(uint32_t port_id, uint32_t object_id, uint64_t num,
                                          const Ps4Audio3dAttribute *attrs, uintptr_t base);
int32_t ps4_audio3d_bed_write(uint32_t port_id, uint32_t num_channels, uint32_t format,
                              const void *buffer, uint32_t num_samples);
int32_t ps4_audio3d_audioout_open(uint32_t port_id, int32_t user_id, int32_t type, int32_t index,
                                  uint32_t len, uint32_t freq, uint32_t param);
int32_t ps4_audio3d_audioout_close(int32_t handle);
int32_t ps4_audio3d_audioout_outputs(const Ps4Audio3dAudioOutParam *params, uint32_t num,
                                     uintptr_t base);
int32_t ps4_audio3d_port_get_levels(uint32_t port_id, Ps4Audio3dLevels *out);

// Guest shims
void shim_sceAudio3dInitialize(GuestContext *ctx);
void shim_sceAudio3dTerminate(GuestContext *ctx);
void shim_sceAudio3dGetDefaultOpenParameters(GuestContext *ctx);
void shim_sceAudio3dPortOpen(GuestContext *ctx);
void shim_sceAudio3dPortClose(GuestContext *ctx);
void shim_sceAudio3dPortSetAttribute(GuestContext *ctx);
void shim_sceAudio3dPortGetAttributesSupported(GuestContext *ctx);
void shim_sceAudio3dPortGetQueueLevel(GuestContext *ctx);
void shim_sceAudio3dPortAdvance(GuestContext *ctx);
void shim_sceAudio3dPortPush(GuestContext *ctx);
void shim_sceAudio3dObjectReserve(GuestContext *ctx);
void shim_sceAudio3dObjectUnreserve(GuestContext *ctx);
void shim_sceAudio3dObjectSetAttributes(GuestContext *ctx);
void shim_sceAudio3dBedWrite(GuestContext *ctx);
void shim_sceAudio3dBedWrite2(GuestContext *ctx);
void shim_sceAudio3dAudioOutOpen(GuestContext *ctx);
void shim_sceAudio3dAudioOutClose(GuestContext *ctx);
void shim_sceAudio3dAudioOutOutputs(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_AUDIO3D_H
