# Guest spin-wait throttle

Date: 2026-11-15. Apple M4 Max, macOS 27.3, Uncharted: The Nathan Drake Collection (CUSA02343),
Drake's Fortune, checkpoint in the jungle.

## Why

Profiling the jungle scene showed where the CPU goes: the six guest job workers of Drake's Fortune
are **89.6 % of process CPU**, and they spend it in their own spin-wait loops — `PAUSE`, a shared
state check and an `RDTSC` time-budget test, over and over. The collection menu, where there is
almost no scene work at all, costs *more* CPU (614 %) than the jungle (563 %), which shows the load
is waiting, not computing. Instruction-level work on that loop does not help: a change that removed
6 % of its instructions moved process CPU by 0.03 %.

x86 `PAUSE` has no architectural effect. It is the guest telling the CPU "I am spinning"; a host
thread that spins keeps its core busy for nothing. So for every guest `PAUSE` the runtime counts
consecutive pauses per thread and, after a configurable run of them, sleeps instead of spinning.

## What changed

- [arm64_lifter.go](../pkg/lifter/arm64_lifter.go): `PAUSE` lowers to `bl _recomp_spin_pause`
  instead of `yield` (`YIELD` is a no-op on Apple cores).
- [asm_trampoline.go](../pkg/emitter/asm_trampoline.go): the bridge stub saves `x8` (guest `r8`,
  caller-saved in AAPCS64) and `NZCV` (the guest flags), calls the counting helper, and only when
  the throttle fires wraps the sleeping call in `SAVE_Q0_31`/`LOAD_Q0_31`, because a call into the C
  library may clobber the guest's xmm/ymm state (`v0-v31`).
- [recomp_runtime.c](../pkg/runtime/core/recomp_runtime.c): `recomp_spin_pause_impl` is built with
  `target("general-regs-only")` so it cannot touch a vector register, and reads/writes its streak
  counter through the `GuestContext` field `spin_pause_streak` rather than a C thread-local — a
  thread-local accessor is a call, and a call may clobber vectors. The configuration is read once in
  a constructor, so the hot path is ten instructions and a leaf.
- `PS4_SPIN_SLEEP_NS` sets the sleep; **20000 (20 µs) is the default**, and **0 turns the throttle
  off** for a title that minds the latency. `PS4_SPIN_PAUSE_STREAK` sets the pause count before a
  sleep (64 by default). The runtime logs the setting once at startup.

Two failures shaped this design, both caught before measuring: a first version called the C helper
directly and the game aborted with a guest stack-smash (the call had destroyed the upper halves of
the guest's xmm registers), and a second one still reached a thread-local accessor from the hot
path.

## Measured effect

Same executable, same data and cache, identical scripted input timeline (a fixed `PS4_PAD_CTL`
timeline with `PS4_BACKGROUND_WINDOW` so the physical controller cannot interfere), 40 s of `top`
samples 5 s apart during the movement block:

| | throttle off | throttle on (20 µs / 64 pauses) |
|---|---:|---:|
| Process CPU, mean of 8 samples | **563.7 %** (544–592) | **156.3 %** (123–190) |
| Renderer fps, matching present range | 59.28 | 59.02 |
| Frame times | p50 16.6–17.0, p95 17.9–19.7, p99 18.3–21.8 ms | p50 16.7–17.3, p95 17.8–20.2, p99 18.2–21.3 ms |
| Frames over 33 ms | 0 | 0 |
| Display | 0 dropped, 0 superseded | 0 dropped, 0 superseded |
| Audio | 0 underruns, 0 starved | 0 underruns, 0 starved |

That is **−72 % process CPU**, from about 5.6 cores to about 1.6, with the same picture: the
checkpoint screenshots of both runs show the same scene, and both runs exited through the normal
control-file `terminate`.

The default (20 µs, no environment variable set) was then measured again in a third run of the same
timeline: **160.2 %** mean of eight samples (136–203 %), so the default path is the one that was
measured, not an untested setting.

## Limits

- This is a deliberate timing deviation: a worker that has already paused 64 times notices new work
  up to one sleep (20 µs) later than it would by spinning. Nothing in the guest's *results* changes
  — `PAUSE` has no architectural effect and the guest's own RDTSC budget keeps running — but a
  timing-sensitive title could in principle care. Both knobs exist so this can be tuned or turned
  off (`PS4_SPIN_SLEEP_NS=0`).
- Measured on one title and one scene. Other games have not been tested.
- The gain is CPU and energy, not frame rate: the scene was already at the 60 FPS cap.
