#ifndef PS4_AUDIO3D_MIX_H
#define PS4_AUDIO3D_MIX_H

// Pure mixing / panning functions of the libSceAudio3d HLE (no state, no
// locking, no guest memory). See ps4_audio3d_mix.c for the panning model.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Output channel order of an 8-channel Audio3d mix: the AudioOut
// FLOAT_8CH_STD layout (FL FR FC LFE BL BR SL SR; Microsoft WAVE order).
enum {
  PS4_A3D_CH_FL = 0,
  PS4_A3D_CH_FR = 1,
  PS4_A3D_CH_FC = 2,
  PS4_A3D_CH_LFE = 3,
  PS4_A3D_CH_BL = 4,
  PS4_A3D_CH_BR = 5,
  PS4_A3D_CH_SL = 6,
  PS4_A3D_CH_SR = 7,
};

// SCE_AUDIO3D_PASSTHROUGH_*
enum {
  PS4_A3D_PASSTHROUGH_NONE = 0,
  PS4_A3D_PASSTHROUGH_LEFT = 1,
  PS4_A3D_PASSTHROUGH_RIGHT = 2,
};

typedef struct {
  float pos[3];         // x right, y up, z forward (listener at the origin)
  float gain;           // linear
  float spread;         // radians, 0 = point source, >= 2*pi = everywhere
  uint32_t passthrough; // PS4_A3D_PASSTHROUGH_*
} Ps4A3dPanParams;

// Per-output-channel gains (out_channels = 2 or 8; unused entries are 0) for
// a mono object with the given attributes, object gain included.
void ps4_a3d_pan_gains(uint32_t out_channels, const Ps4A3dPanParams *p, float g[8]);

// out[f * out_channels + c] += in[f] * g[c]
void ps4_a3d_mix_mono(float *out, uint32_t out_channels, const float *in, uint32_t frames,
                      const float g[8]);

// Adds a bed (in_channels = 2, 6 or 8, interleaved) to out. 6-channel beds are
// FL FR FC LFE SL SR; 8-channel beds use the output order above. Into a
// stereo output the bed is folded like libSceAudioOut folds 8-channel ports
// (centre and surrounds at -3 dB, LFE dropped).
void ps4_a3d_mix_bed(float *out, uint32_t out_channels, const float *in, uint32_t in_channels,
                     uint32_t frames);

// Converts count samples of SCE_AUDIO3D_FORMAT_S16 (0) / FLOAT (1) to float.
// NaN floats become 0.
void ps4_a3d_convert(float *dst, const void *src, uint32_t format, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif // PS4_AUDIO3D_MIX_H
