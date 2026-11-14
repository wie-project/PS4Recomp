// x87 transcendental instructions the lifter runs in C (through the register-preserving helper
// entry points in the emitter's trampoline).
//
// The lifter's x87 registers hold doubles (pkg/lifter/arm64_lifter_fpu.go). Orbis runs x87 code
// with the FreeBSD default control word 0x127f, whose precision control rounds every result to
// double precision, so a double result is what the guest sees on hardware too.

#include "recomp_context.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define X87_SW_C1 0x0200
#define X87_SW_C2 0x0400

// Pi rounded to the 66 significant bits the x87 uses to reduce the argument of FSIN, FCOS,
// FSINCOS and FPTAN (Intel SDM Vol. 1, 8.3.10 "Transcendental Instruction Accuracy"), as the
// integer kX87Pi66 * 2^-64. The reduction is exact with respect to this constant, so the results
// carry its error: FCOS of the double nearest pi/2 is (pi66 - pi_double) / 2 = 6.1230317691e-17,
// not cos(pi_double / 2) = 6.1232339957e-17.
static const unsigned __int128 kX87Pi66 = ((unsigned __int128)0x3 << 64) | 0x243F6A8885A308D3ULL;

// x87_cos returns cos(a) for 0 <= a < 2^63 with the argument reduced modulo pi66 / 2: with
// A = a * 2^65 and P = kX87Pi66 (pi66 / 2 = P * 2^-65), a = k * pi66 / 2 + r, |r| <= pi66 / 4,
// computed exactly in 128-bit integers. r is carried as rh + rl (two doubles) so that cos and
// sin of it are accurate to the last bit of the double result.
static double x87_cos(double a) {
  if (a < 0.5) {
    return cos(a); // below pi66 / 4: k = 0, no reduction
  }
  int e;
  double m = frexp(a, &e);                 // a = m * 2^e, 0.5 <= m < 1, 0 <= e <= 63
  uint64_t mi = (uint64_t)ldexp(m, 53);    // a = mi * 2^(e - 53)
  unsigned __int128 A = (unsigned __int128)mi << (e + 12); // a * 2^65 < 2^128
  unsigned __int128 k = A / kX87Pi66;
  __int128 r = (__int128)(A - k * kX87Pi66); // 0 <= r < P
  if ((unsigned __int128)r > kX87Pi66 / 2) {
    r -= (__int128)kX87Pi66;
    k++;
  }
  double rh = (double)r;
  double rl = (double)(r - (__int128)rh);
  rh = ldexp(rh, -65);
  rl = ldexp(rl, -65);
  double s = sin(rh), c = cos(rh);
  // cos(rh + rl) = c - s * rl, sin(rh + rl) = s + c * rl (rl is below half an ulp of rh).
  switch ((unsigned)(k & 3)) {
  case 0:
    return fma(-s, rl, c);
  case 1:
    return -fma(c, rl, s);
  case 2:
    return -fma(-s, rl, c);
  default:
    return fma(c, rl, s);
  }
}

// FCOS (Intel SDM Vol. 2A): ST(0) = cos(ST(0)) for -2^63 < ST(0) < 2^63, with C2 = 0. Outside
// that range C2 = 1 and ST(0) is unchanged. +-infinity is an invalid operand: with the invalid
// exception masked (as every Orbis thread runs) the result is the real indefinite (negative
// quiet NaN); a NaN operand comes back quiet. C1 is cleared: the lifter does not track whether
// a result was rounded up (C1 = 1 on hardware), as for every other x87 arithmetic instruction,
// and the x87 exception flags are not modelled. C0 and C3 are undefined and kept.
void recomp_x87_fcos_impl(GuestContext *ctx) {
  double *st0 = &ctx->fpu_stack[ctx->fpu_top & 7];
  uint64_t bits;
  memcpy(&bits, st0, sizeof(bits));
  uint64_t mag = bits & 0x7fffffffffffffffULL;
  uint16_t sw = (uint16_t)(ctx->fpu_sw & ~(X87_SW_C1 | X87_SW_C2));
  if (mag == 0x7ff0000000000000ULL) {
    bits = 0xfff8000000000000ULL;
    memcpy(st0, &bits, sizeof(bits));
  } else if (mag > 0x7ff0000000000000ULL) {
    bits |= 0x0008000000000000ULL;
    memcpy(st0, &bits, sizeof(bits));
  } else if (mag >= 0x43e0000000000000ULL) { // |ST(0)| >= 2^63
    sw |= X87_SW_C2;
  } else {
    double a;
    memcpy(&a, &mag, sizeof(a));
    *st0 = x87_cos(a); // cos is even
  }
  ctx->fpu_sw = sw;
}
