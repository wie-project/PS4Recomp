# Metal 3 shader-read hazard indexing

Date: 2026-11-14. Measured on Apple M4 Max, macOS 27.3, Metal 3. The game retains its
60 FPS cap, 1920x1080 resolution, PS4 Pro setting, and disabled MetalFX setting. The baseline
already includes the linear CPU texture-upload optimization in `render-performance.md`.
It also includes the separate completion-fence changes in PR #35. This PR is independently
based on `main`; the measurements establish the incremental effects on that recorded baseline,
not the end-to-end performance difference against unmodified `main`.

## Change

Every draw/dispatch checks whether GPU writes invalidated shader data copied earlier by the
command processor. The old check scanned all open writes and retained committed write lists
for each individual source range. The new check reuses `GnmPendingWrites`' existing 1 MiB
address buckets, retaining writes until the oldest shader snapshot no longer needs them.

`OverlapsAfter` filters by the snapshot sequence and returns the newest overlapping sequence.
One index per queue preserves an open write even when another queue has committed a newer
recording of the same address. Repeated identical ranges on a single queue retain their newest
write; that queue's commit sequences are monotonic. Partial ranges remain separate entries.

The change does not remove hazard checks, GPU waits, shader-data refreshes, permutation
validation, or shader bounds checks. It does not enable the experimental asynchronous
guest-memory path, whose earlier soak failures remain unresolved. Existing render settings
and backend selection remain unchanged. It adds no dependency and downloads nothing.

## Correctness evidence

- Host regression: snapshot boundaries, completed-since-snapshot writes, reused ranges,
  reverse commit order between queues, adjacent non-overlapping ranges, and pruning.
- A deterministic 30,000-operation test compares indexed answers and newest sequences with
  a full write-event history. It exercises wide ranges, cross-bucket queries, pruning, and
  compaction, and checks that each returned hit belongs to the reference history.
- The same cases pass with AddressSanitizer and UndefinedBehaviorSanitizer.
- A separate diagnostic game binary retains the old full scan beside the index and aborts
  on any overlap/newest-sequence mismatch. At least 16,777,216 actual shader-read queries
  match in the 120-second menu-to-boat run. The diagnostic implementation is retained only
  in the ignored validation artifacts; no diagnostic flag or parallel scan ships in production.
- The native runtime builds; full Go tests, `go vet`, and Go escape analysis pass. Go escape
  analysis does not measure native renderer allocations.

## Isolated query measurements

The retained native microbenchmark uses 512 writes, eight committed batches, two queues,
and 65,536 prebuilt queries repeated eight times per trial. Queries include hits, misses,
and different snapshot ages. Both implementations produce the same checksum in each of
three alternating trials:

| Implementation | ns/query, three trials | Allocations during queries |
| --- | --- | ---: |
| Full scan | 283.19, 282.43, 283.21 | 0 |
| Indexed | 20.81, 17.69, 22.83 | 0 |

This is a synthetic index benchmark, not an FPS prediction. Index construction, updates,
and pruning are excluded from that timing; the game measurements include their costs.
The extra indexes retain entries and bucket storage for reuse, so zero query allocations
does not mean zero renderer allocation or zero additional memory.

## Game comparison protocol

Both executables use the same guest assembly and memory image. Each receives separate,
identical copies of saves, settings, and the warmed shader cache. There are three runs per
variant, ordered baseline/changed, changed/baseline, baseline/changed. Each run quits normally
after 120 seconds of renderer uptime. Scripted Cross presses occur at 20, 40, 60, 80, and 90
seconds for 300 ms each; screenshots confirm the existing boat checkpoint and camera.

Only presents 6001–7200 are compared: four equal 300-present windows. FPS is their harmonic
mean; per-frame GPU busy, encoder thread CPU, and waiting times are their arithmetic means.
These timings overlap and must not be added. The capped FPS is context, not maximum throughput.
The diagnostic run is excluded from performance results.

| Run | Capped FPS | Encoder CPU/frame | GPU busy/frame | Waiting/frame |
| --- | ---: | ---: | ---: | ---: |
| Baseline 1 | 59.444 | 6.100 ms | 9.700 ms | 8.600 ms |
| Baseline 2 | 59.468 | 6.000 ms | 9.625 ms | 8.525 ms |
| Baseline 3 | 59.724 | 5.725 ms | 9.925 ms | 8.575 ms |
| Changed 1 | 60.000 | 4.875 ms | 10.125 ms | 7.850 ms |
| Changed 2 | 60.000 | 5.075 ms | 10.075 ms | 7.825 ms |
| Changed 3 | 59.950 | 5.300 ms | 9.600 ms | 8.325 ms |

Mean encoder CPU cost falls from 5.942 to 5.083 ms/frame (14.4%). The three run means do
not overlap between variants. This is an encoder CPU improvement, not a claim about all host
CPU time, power, or maximum FPS. The mean GPU busy metric is 9.750 vs 9.933 ms/frame (+1.9%);
its per-run ranges overlap. No GPU cost reduction is established. Mean waiting is 8.567 vs
8.000 ms/frame; synchronization is still present.

The two 600-frame p95 intervals are 18.1/19.5, 18.0/19.1, and 18.2/18.0 ms for the
baseline, and 18.0/17.9, 18.0/18.1, and 18.9/18.9 ms for the change. These do not establish
a consistent whole-frame percentile improvement. No interval in these fixed windows exceeds
33 ms. Every run has the same 1,200 empty-scissor skips and zero pending-compilation skips in
the measured windows. All six comparison processes and the oracle run exit normally; their
logs contain no assertion, fatal error, Signal 11, or GPU watchdog failure.

In the first matched post-present-6001 memory report, both variants charge 1,858 MiB of GPU
resources and report 1,884 MiB of device allocation. Process footprint is 3,959 vs 3,958 MiB.
These coarse samples do not establish a RAM saving or isolate the index's own memory usage.

Raw game logs, screenshots, diagnostic source/build, benchmark source, sanitized test binary,
and comparison script are local ignored artifacts under `ps4recomp/metal3-hazards/`.

## Limits

The goal is lower resource consumption without a quality reduction. No weaker Mac was available
for validation, so compatibility, memory fit, and a target frame rate on those machines are
not established. Other games, long sessions, and Metal 4 are not validated by this comparison.
The pre-existing gray character material/pose issue is not fixed. Animated scenes are not
pixel-identical; the exact hazard-oracle comparisons establish matching dependency decisions,
not complete visual equivalence of the renderer.
