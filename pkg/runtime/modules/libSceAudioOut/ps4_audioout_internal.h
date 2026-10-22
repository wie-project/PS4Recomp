#ifndef PS4_AUDIOOUT_INTERNAL_H
#define PS4_AUDIOOUT_INTERNAL_H

// Host-side entry points of the libSceAudioOut HLE for other runtime modules
// (libSceAudio3d). They are the same ports the guest sees through
// sceAudioOutOpen/Output/Close: a handle returned here is a normal AudioOut
// handle (sceAudioOutSetVolume, sceAudioOutGetPortState work on it), and the
// guest-visible shims are thin wrappers over these functions.
//
// Return values follow the guest ABI: >= 0 on success, a negative
// ORBIS_AUDIO_OUT_ERROR_* code (as int32_t) on failure.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Error codes returned by these functions (and by the guest shims).
#define PS4_AUDIO_OUT_ERROR_NOT_OPENED ((int32_t)0x80260001)
#define PS4_AUDIO_OUT_ERROR_INVALID_PORT ((int32_t)0x80260004)
#define PS4_AUDIO_OUT_ERROR_INVALID_POINTER ((int32_t)0x80260005)
#define PS4_AUDIO_OUT_ERROR_PORT_FULL ((int32_t)0x80260006)
#define PS4_AUDIO_OUT_ERROR_INVALID_SIZE ((int32_t)0x80260007)
#define PS4_AUDIO_OUT_ERROR_INVALID_FORMAT ((int32_t)0x80260008)

// Port types used by the runtime itself.
#define PS4_AUDIO_OUT_PORT_MAIN 0
#define PS4_AUDIO_OUT_PORT_AUDIO3D 126

// Data formats (low byte of the sceAudioOutOpen param).
#define PS4_AUDIO_OUT_FMT_S16_STEREO 1
#define PS4_AUDIO_OUT_FMT_FLOAT_STEREO 4
#define PS4_AUDIO_OUT_FMT_FLOAT_8CH_STD 7

// Opens a port exactly like sceAudioOutOpen(user, type, index, len, freq,
// param). Returns the handle (> 0).
int32_t ps4_audioout_port_open(int32_t user, int32_t type, int32_t index, uint32_t len,
                               uint32_t freq, uint32_t param);

// Submits one grain (len frames in the port's format) from host memory, like
// sceAudioOutOutput: blocks until the port has room (paced by the output
// device clock, or by wall time when no device runs). pcm == NULL only waits.
// Returns len (0 for pcm == NULL).
int32_t ps4_audioout_port_output(int32_t handle, const void *pcm);

// Pacing query: the number of grains ps4_audioout_port_output would accept
// right now without blocking (0 when it would block).
int32_t ps4_audioout_port_writable(int32_t handle);

// Closes a port like sceAudioOutClose. Returns 0.
int32_t ps4_audioout_port_close(int32_t handle);

#ifdef __cplusplus
}
#endif

#endif // PS4_AUDIOOUT_INTERNAL_H
