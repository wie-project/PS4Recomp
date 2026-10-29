// Output gamma of sceVideoOutAdjustColor, shared by the presenter (ps4_metal_screen.m) and its
// host test. The game's brightness option sets it; The Last of Us passes 0.8 at its default.
// gamma 1.0 shows the frame as rendered, gamma < 1 darkens and gamma > 1 brightens. The form was
// chosen by calibration against a PS4 reference video (docs/color-measurement.md): the encoded
// (sRGB) value is raised to 1 / gamma.
#ifndef PS4_OUTPUT_GAMMA_H
#define PS4_OUTPUT_GAMMA_H
#include <math.h>

// What the presenter's gamma shader receives in prm.z: the exponent applied to the encoded value.
static inline float ps4_output_gamma_param(float gamma) { return 1.0f / gamma; }

// CPU form of the shader for one encoded channel value in [0, 1].
static inline float ps4_output_gamma_apply(float e, float gamma) {
    const float o = powf(e, ps4_output_gamma_param(gamma));
    return o < 0.0f ? 0.0f : o > 1.0f ? 1.0f : o;
}
#endif
