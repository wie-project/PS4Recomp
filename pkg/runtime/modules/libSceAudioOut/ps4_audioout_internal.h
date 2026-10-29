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

// Error codes returned by these functions (and by the guest shims), numbered as the SDK numbers
// them (the OpenOrbis toolchain's errors.h and shadPS4's audioout_error.h agree).
#define PS4_AUDIO_OUT_ERROR_NOT_OPENED ((int32_t)0x80260001)
#define PS4_AUDIO_OUT_ERROR_INVALID_PORT ((int32_t)0x80260003)
#define PS4_AUDIO_OUT_ERROR_INVALID_POINTER ((int32_t)0x80260004)
#define PS4_AUDIO_OUT_ERROR_PORT_FULL ((int32_t)0x80260005)
#define PS4_AUDIO_OUT_ERROR_INVALID_SIZE ((int32_t)0x80260006)
#define PS4_AUDIO_OUT_ERROR_INVALID_FORMAT ((int32_t)0x80260007)
#define PS4_AUDIO_OUT_ERROR_INVALID_PORT_TYPE ((int32_t)0x8026000A)
#define PS4_AUDIO_OUT_ERROR_INVALID_MIXLEVEL ((int32_t)0x80260014)

// Port types used by the runtime itself.
#define PS4_AUDIO_OUT_PORT_MAIN 0
#define PS4_AUDIO_OUT_PORT_PADSPK 4
#define PS4_AUDIO_OUT_PORT_AUDIO3D 126

// Data formats (low byte of the sceAudioOutOpen param).
#define PS4_AUDIO_OUT_FMT_S16_STEREO 1
#define PS4_AUDIO_OUT_FMT_FLOAT_MONO 3
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

// The pad speaker's mix level, like sceAudioOutSetMixLevelPadSpk(handle, level): 0 .. 32768
// (0 dB), linear; a port opens at PS4_AUDIO_OUT_MIXLEVEL_PADSPK_DEFAULT, 11626 (-9 dB; the value
// and its range follow shadPS4's audioout.h/.cpp). The host has no controller speaker, so it
// mixes PADSPK ports into the main output, at this level unless PS4_AUDIO_PADSPK sets a fixed
// gain. Returns 0, PS4_AUDIO_OUT_ERROR_INVALID_PORT,
// PS4_AUDIO_OUT_ERROR_INVALID_PORT_TYPE (not a PADSPK port) or
// PS4_AUDIO_OUT_ERROR_INVALID_MIXLEVEL (above 0 dB). A negative level is taken as silence: a host
// choice (shadPS4 only rejects a level above 0 dB; what the console does with one is unknown).
#define PS4_AUDIO_OUT_MIXLEVEL_PADSPK_DEFAULT 11626
#define PS4_AUDIO_OUT_MIXLEVEL_PADSPK_0DB 32768
int32_t ps4_audioout_port_set_mix_level_padspk(int32_t handle, int32_t level);

// Closes a port like sceAudioOutClose. Returns 0.
int32_t ps4_audioout_port_close(int32_t handle);

// Timing counters of one port since it was opened. Times are CLOCK_MONOTONIC microseconds, the
// clock of the dump's `frame 0 at mono` line, the screenshot cadence line and the Videodec trace.
typedef struct {
  uint64_t outputs;         // grains submitted
  uint64_t underruns;       // device periods that found the ring short (after the first output)
  uint64_t underrun_frames; // the frames of silence those periods filled in
  uint64_t late_outputs;    // grains that arrived after the ring had run empty (after the first;
                            // never under PS4_AUDIO_MUTE, where no device drains the ring)
  uint64_t max_gap_us;      // the longest time between the arrivals of two consecutive grains
  uint64_t last_output_us;  // arrival of the last grain; 0 = none yet
  // The first grain with a non-zero sample: when it was queued, when its first frame reaches the
  // device (after the device's current buffer and the frames queued ahead of it, at the device
  // rate; on CoreAudio without the device's own latency and safety offset, so not when it is
  // heard), and the grains before it. first_sound_us is 0 while the port has had only silence.
  uint64_t first_sound_us;
  uint64_t first_sound_play_us;
  uint64_t first_sound_outputs;
  uint32_t ring_frames; // ring capacity in frames
} Ps4AudioPortStats;

// Fills *out with the port's counters. Returns 0, PS4_AUDIO_OUT_ERROR_INVALID_PORT or
// PS4_AUDIO_OUT_ERROR_INVALID_POINTER.
int32_t ps4_audioout_port_stats(int32_t handle, Ps4AudioPortStats *out);

#ifdef __cplusplus
}
#endif

#endif // PS4_AUDIOOUT_INTERNAL_H
