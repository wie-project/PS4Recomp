# RDTSC lowering: measurements and why the two divisions stay

Date: 2026-11-15. Apple M4 Max, 16 logical CPUs, macOS 27.3 (26C5035r), clang from Xcode 27.

## What the lowering does

`RDTSC` is lifted to the host virtual counter scaled into guest TSC units
([liftRdtsc](../pkg/lifter/arm64_lifter_alu.go)). `recomp_rdtsc()` in
[recomp_context.h](../pkg/runtime/core/recomp_context.h) defines those units as
`floor(cntvct * 2130000000 / cntfrq)`; the lifted inline sequence computes the same quotient as
`q*F + (r*F)/cntfrq` with `q, r = cntvct divmod cntfrq`, using two 64-bit `udiv` instructions
instead of a 128-bit division. Keeping guest and runtime counters in the same units is required:
guest code compares `RDTSC` against values derived from `sceKernelReadTsc`.

## Counter frequency is not a constant

`CNTFRQ_EL0` reads **1000000000 Hz** on this system, confirmed independently by sampling
`CNTVCT_EL0` across a 200 ms sleep (205,854,105 ticks in 0.205855 s). The 24 MHz value recorded in
an older source comment does not hold here, so any lowering keyed to 24 MHz is dead code on this
machine.

At 1 GHz the ratio to the guest timebase is 213/100, and 100 is not a power of two: no single
64-bit multiply-and-shift form is exact for every counter value (the product needs a 128-bit
intermediate, whose division by 100 needs a 71-bit magic). At 24 MHz the ratio would be 355/4 and
a `mul`/`umulh`/`extr` sequence would be exact, which is what made this worth measuring at 1 GHz.

## Measured cost

Standalone ARM64 loop, 20,000,000 iterations, three rounds, accumulator dependency kept. Values in
ns per iteration.

| Sequence | With `mrs cntvct_el0` per iteration | Arithmetic only, dependent chain |
|---|---:|---:|
| Two divisions (current lowering) | 2.85–2.86 | 5.96–5.97 |
| One division (frequency-checked 1 GHz path) | 2.86–2.87 | 2.75–2.76 |
| `mul`/`umulh`/`extr` by 355 (not exact at 1 GHz) | 0.29 | not measured |

Two observations:

1. Removing one division halves the dependent-chain cost, but once each iteration reads the
   counter — the shape of the guest polling loop this was meant to speed up — both variants run at
   the same speed. The counter read and the per-iteration overlap dominate; the divisions are
   hidden.
2. The zero-division form is only faster because it also drops the second counter read, and it is
   wrong by a factor of 2.13 at 1 GHz.

So the current lowering is kept unchanged. A shorter sequence would add instructions (frequency
test, branch, extra immediates) for no measurable gain.

## Limits of this measurement

- This measures instruction sequences in a standalone process, not the game. No frame-time, CPU
  utilization or energy effect was measured, and none is claimed.
- The reference lowering stays exact while `q*F` fits in 64 bits, i.e. for uptime up to about 274
  years; below that both forms agree with the 128-bit quotient, which
  [TestRdtscLiftedArithmetic](../pkg/lifter/arm64_rdtsc_test.go) checks on hardware up to that
  boundary for several counter frequencies.
