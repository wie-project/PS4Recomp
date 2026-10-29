// libSceAudio3d mixing and panning.
//
// Objects are mono sources placed by their POSITION attribute. The PS4 renders
// them with the system's speaker virtualiser; here they are panned onto the
// port's speaker layout:
//
// - 8 channels (7.1, FLOAT_8CH_STD order): pairwise constant-power panning
//   between the two speakers adjacent to the source azimuth, speakers at
//   C 0, FR +30, SR +90, BR +150, BL -150, SL -90, FL -30 degrees
//   (positive = right). LFE never receives objects.
// - 2 channels: sources behind the listener are mirrored to the front, then
//   panned with a constant-power law between L (-30) and R (+30); sources
//   outside that arc are hard left / right.
//
// Azimuth is atan2(x, z): x right, z forward, y (elevation) ignored. A source
// at the listener (x = z = 0) is spread evenly over all speakers. SPREAD widens
// a source by blending (in power) the point gains with the even spread:
// w = spread / (2*pi). There is no distance attenuation: games apply distance
// roll-off in GAIN. PASSTHROUGH LEFT / RIGHT sends the object straight to FL /
// FR without panning.

#include "ps4_audio3d_mix.h"
#include <math.h>
#include <string.h>

#define A3D_PI 3.14159265358979323846

// Speaker ring of the 7.1 layout, ascending azimuth in [0, 360).
static const struct {
  double az;
  int ch;
} k_ring71[7] = {
    {0.0, PS4_A3D_CH_FC},   {30.0, PS4_A3D_CH_FR},  {90.0, PS4_A3D_CH_SR}, {150.0, PS4_A3D_CH_BR},
    {210.0, PS4_A3D_CH_BL}, {270.0, PS4_A3D_CH_SL}, {330.0, PS4_A3D_CH_FL},
};

static void pan_point_71(double az, float g[8]) {
  az = fmod(az, 360.0);
  if (az < 0.0) az += 360.0;
  for (int i = 0; i < 7; i++) {
    double a0 = k_ring71[i].az;
    double a1 = (i == 6) ? 360.0 : k_ring71[i + 1].az;
    if (az >= a0 && az < a1) {
      double t = (az - a0) / (a1 - a0) * (A3D_PI / 2.0);
      g[k_ring71[i].ch] = (float)cos(t);
      g[k_ring71[(i + 1) % 7].ch] = (float)sin(t);
      return;
    }
  }
  g[PS4_A3D_CH_FC] = 1.0f; // az == 360.0 after rounding
}

static void pan_point_stereo(double az, float g[8]) {
  if (az > 90.0) az = 180.0 - az;
  if (az < -90.0) az = -180.0 - az;
  if (az > 30.0) az = 30.0;
  if (az < -30.0) az = -30.0;
  double t = (az + 30.0) / 60.0 * (A3D_PI / 2.0);
  g[0] = (float)cos(t);
  g[1] = (float)sin(t);
}

void ps4_a3d_pan_gains(uint32_t out_channels, const Ps4A3dPanParams *p, float g[8]) {
  memset(g, 0, 8 * sizeof(float));
  const int stereo = out_channels == 2;
  if (p->passthrough == PS4_A3D_PASSTHROUGH_LEFT || p->passthrough == PS4_A3D_PASSTHROUGH_RIGHT) {
    g[p->passthrough == PS4_A3D_PASSTHROUGH_LEFT ? PS4_A3D_CH_FL : PS4_A3D_CH_FR] = p->gain;
    return;
  }
  // Speakers that take part in an even spread (all but LFE).
  const int n = stereo ? 2 : 7;
  const float even = (float)(1.0 / sqrt((double)n));
  double x = p->pos[0], z = p->pos[2];
  double w = p->spread / (2.0 * A3D_PI);
  if (!(w > 0.0)) w = 0.0;
  if (w > 1.0) w = 1.0;
  if (fabs(x) < 1e-6 && fabs(z) < 1e-6) {
    w = 1.0;
  } else {
    double az = atan2(x, z) * (180.0 / A3D_PI);
    if (stereo) {
      pan_point_stereo(az, g);
    } else {
      pan_point_71(az, g);
    }
  }
  for (uint32_t c = 0; c < (stereo ? 2u : 8u); c++) {
    if (!stereo && c == PS4_A3D_CH_LFE) continue;
    if (w > 0.0) {
      double pw = (1.0 - w) * (double)g[c] * g[c] + w * (double)even * even;
      g[c] = (float)sqrt(pw);
    }
    g[c] *= p->gain;
  }
}

void ps4_a3d_mix_mono(float *out, uint32_t out_channels, const float *in, uint32_t frames,
                      const float g[8]) {
  for (uint32_t c = 0; c < out_channels; c++) {
    const float k = g[c];
    if (k == 0.0f) continue;
    for (uint32_t f = 0; f < frames; f++) {
      out[f * out_channels + c] += in[f] * k;
    }
  }
}

void ps4_a3d_mix_bed(float *out, uint32_t out_channels, const float *in, uint32_t in_channels,
                     uint32_t frames) {
  const float k = 0.7071f;
  for (uint32_t f = 0; f < frames; f++) {
    const float *s = in + (size_t)f * in_channels;
    float *d = out + (size_t)f * out_channels;
    if (out_channels == 8) {
      d[PS4_A3D_CH_FL] += s[0];
      d[PS4_A3D_CH_FR] += s[1];
      if (in_channels == 6) {
        d[PS4_A3D_CH_FC] += s[2];
        d[PS4_A3D_CH_LFE] += s[3];
        d[PS4_A3D_CH_SL] += s[4];
        d[PS4_A3D_CH_SR] += s[5];
      } else if (in_channels == 8) {
        for (uint32_t c = 2; c < 8; c++) d[c] += s[c];
      }
    } else {
      float l = s[0], r = s[1];
      if (in_channels >= 6) {
        l += k * s[2] + k * s[4];
        r += k * s[2] + k * s[5];
      }
      if (in_channels == 8) {
        l += k * s[6];
        r += k * s[7];
      }
      d[0] += l;
      d[1] += r;
    }
  }
}

void ps4_a3d_convert(float *dst, const void *src, uint32_t format, uint32_t count) {
  if (format == 0) {
    const uint8_t *b = (const uint8_t *)src;
    for (uint32_t i = 0; i < count; i++) {
      int16_t s;
      memcpy(&s, b + (size_t)i * 2, 2);
      dst[i] = (float)s * (1.0f / 32768.0f);
    }
  } else {
    memcpy(dst, src, (size_t)count * sizeof(float));
    for (uint32_t i = 0; i < count; i++) {
      if (!(dst[i] == dst[i])) dst[i] = 0.0f;
    }
  }
}
