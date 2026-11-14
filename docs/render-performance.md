# Linear texture upload performance

Measured locally on 2026-11-14: Apple M4 Max, macOS 27.3 (26C5035r), Apple clang 21.0.0.
Baseline: `6118287bbc665b556c4faa66900d595ace3cd225`. Metal 3, 1920x1080, PS4 Pro mode,
MetalFX disabled. No dependencies were downloaded.
That measurement baseline includes the separate completion-fence changes in PR #35. The
performance changes are submitted independently against `main`; these numbers are not an
end-to-end performance comparison against unmodified `main`.
The game is capped at 60 FPS. These runs measure frame-time stability and CPU/GPU cost under
that cap, not maximum rendering throughput. An unchanged capped FPS value cannot rule out
additional headroom, and isolated upload savings must not be converted into predicted game FPS.

## Change

`TextureCache::UploadLockedImpl` previously allocated and zero-initialized a surface-sized
vector, then copied already-linear guest rows into it before Metal copied the same pixels.
Fully readable `DisplayLinearAligned` and `DisplayLinearGeneral` surfaces now supply guest
bytes directly to the synchronous Metal upload. Tiled surfaces and incomplete readable
ranges retain the existing detile/zero-fill path. Private textures still copy their input
into an owned Metal staging buffer before asynchronous GPU work.

This removes one temporary allocation and one intermediate copy per eligible upload. It
does not remove GPU/CPU synchronization or change shader bounds checks, presentation mode,
the Metal backend default, or draw selection.

Apple documents that [replaceRegion copies immediately on the CPU](https://developer.apple.com/documentation/metal/mtltexture/replace(region:mipmaplevel:slice:withbytes:bytesperrow:bytesperimage:)).
The source does not need to outlive this call. That API does not synchronize GPU access;
the renderer's existing synchronization remains necessary.

## Measurements

Local evidence is retained under the ignored `ps4recomp/render-performance/` directory,
including native benchmark source, executables, logs, screenshots, CPU samples, Metal traces,
and the game comparison script. These artifacts are not part of the source checkout.

The isolated benchmark calls the real texture cache, changes the source pixels each iteration,
and validates every output pixel through a GPU compute shader. Each trial performs 120 uploads.
Its allocation counter observes surface-sized C++ allocations only during `GetTexture`; it
does not measure all renderer or driver allocations. Timings exclude source-pattern generation
and pixel verification. GPU timings cover the readback compute command buffer.

There are 18 trials per size and variant (2,160 uploads), balanced between the two linear modes:

| RGBA8 size | Baseline CPU upload | Changed CPU upload | Reduction | Surface-sized allocations/upload |
| --- | ---: | ---: | ---: | ---: |
| 128x128 | 0.00558 ms | 0.00384 ms | 31.2% | 1 → 0 |
| 1024x1024 | 0.26989 ms | 0.19813 ms | 26.6% | 1 → 0 |

The trial ranges do not overlap: 0.0050–0.0063 vs 0.0035–0.0044 ms at 128x128,
and 0.2648–0.2798 vs 0.1949–0.2050 ms at 1024x1024. These are isolated CPU upload
results, not whole-frame improvements. Large sampled textures normally use the existing GPU
upload path above its 256 KiB threshold; the large CPU result applies when CPU upload is used.
All benchmark GPU pixel comparisons passed.

Game comparisons use the existing CUSA02343 retail build and its boat checkpoint. Both
executables use the same guest assembly and memory image. Each run receives a fresh copy of
the same saves, settings, and warmed shader cache. The order is baseline/candidate,
candidate/baseline, baseline/candidate. Runs last 120 seconds after renderer initialization.
Cross is held for 300 ms at 20, 40, 60, 80, and 90 seconds on the existing pad-script clock.
Screenshots confirm the boat scene and matching camera. No profiler runs in these comparisons.

Only presents 6001 through 7200 are compared: four consecutive 300-present windows after
loading. FPS is the harmonic mean of those equal-frame windows. GPU busy, encoder thread CPU,
and waiting times are the arithmetic means of the same windows; they overlap and must not
be added. Frame-time percentiles remain separate 600-frame windows, not pooled percentiles.

| Run | Capped FPS | GPU busy/frame | Encoder thread CPU/frame | Waiting/frame |
| --- | ---: | ---: | ---: | ---: |
| Baseline 1 | 59.674 | 9.475 ms | 6.350 ms | 8.850 ms |
| Baseline 2 | 57.709 | 9.850 ms | 6.975 ms | 9.600 ms |
| Baseline 3 | 53.823 | 10.275 ms | 8.425 ms | 9.775 ms |
| Changed 1 | 59.154 | 9.700 ms | 6.300 ms | 9.175 ms |
| Changed 2 | 58.938 | 9.825 ms | 6.575 ms | 9.500 ms |
| Changed 3 | 57.785 | 9.925 ms | 7.050 ms | 9.575 ms |

All six fixed windows contain 1,200 skipped empty-scissor draws and zero pending-compilation
skips. All six processes exit normally; no logged assertion, fatal error, Signal 11, or GPU
watchdog failure was found. The two 600-frame p95 intervals are 19.0/19.0, 19.4/20.1,
19.5/25.6 ms for baseline runs, and 18.6/19.6, 18.8/19.6, 19.3/19.9 ms for changed runs.
No interval in those windows exceeds 33 ms. Baseline 3 includes a CPU/frame-time spike;
it is retained, not discarded. The run-to-run variation and the frame cap do not establish a
whole-scene throughput or frame-time improvement. The supported result is the smaller CPU
upload cost and removed temporary allocation.

The earlier menu-only and profiled diagnostic runs are excluded. In particular, the file named
`baseline-1/gameplay-cpu-sample.txt` was still taken in the menus. A later diagnostic sample
was taken during the transition to the boat cutscene and does not establish the steady-gameplay
CPU bottleneck. The native upload optimization is not presented as a fix for that bottleneck.

A separate symbolized run, `gameplay-profile/`, starts its six-second CPU sample only after
present 6301, then records a six-second Metal System Trace. In that CPU sample the encoder
has 1,607 stack observations: 589 are sleeping in
`GmDispatchSync → GmRunSync → GmSubmitWait → WaitAll`, and another 66 sleep in
`ResolveFlatHazards → WaitAll`. These are sampled stack residence counts, not CPU utilization
percentages. They support investigating synchronous guest-memory dispatches and shader-snapshot
dependencies next; they do not prove that either wait can safely be removed. The corresponding
runtime counters report one synchronous guest-memory dispatch per frame. No synchronization
change is included here.
The Metal trace contains render/compute GPU intervals for the game, and its exported
`metal-command-buffer-error` table is empty. The profiled run exits normally.

An alternative suggested by the CPU compression stacks was rejected: disabling
[`allowGPUOptimizedContents`](https://developer.apple.com/documentation/metal/mtltexturedescriptor/allowgpuoptimizedcontents)
took approximately 0.44 ms rather than 0.18 ms per direct 1024x1024 RGBA8 upload in this test.
Apple describes a CPU/GPU tradeoff; its direction must be measured on the actual workload.

## Correctness and limitations

The native sampled-upload regression validates changing RGBA pixels with padded rows, both
linear modes, truncated input with zero-filled missing rows, three mip levels, array slices
with guest slice padding, 3D depth slices, and cube faces. GPU shader reads and GPU blit
readbacks check actual pixel contents. The same regression also passes against the baseline.
Existing linear/sRGB color-alias and depth-companion regressions pass.

Full Go tests, `go vet`, and Go escape analysis were run. Escape analysis describes Go heap
behavior, not Objective-C++ renderer allocation counts or zero-allocation rendering.

Visual checks cover this checkpoint only. The existing gray character material/pose issue is
visible in baseline and candidate captures; this change does not fix it. Animated water,
fire, NPC motion, and timing differences prevent pixel-identical game screenshots. The native
tests provide exact pixel comparisons. Other games, GPU families, managed-storage devices,
and long gameplay sessions were not validated.
