# Completion-label validation for PR #35

Revalidated on 2026-11-15 against main `354fdaeeb02c0ad6258aa7f87c677102e2ccbb2d`.
The rebased implementation head before this test/report commit was `8285c7bce05ba21db699ccdb1564c98ef35ec484`.
Host: Apple M4 Max, macOS 27.3. No dependencies or game data were downloaded.

## Native completion ordering

`tests/gnm_metal_test/fence_order.mm` links the actual runtime objects of a generated native
build. It initializes the real runtime and command processor, gates GPU execution with a real
Metal shared event, queues a GPU fill, and submits an actual PM4 completion packet.

The test verifies that both the destination and completion label remain unchanged while the
GPU is gated. After releasing the gate, it waits for completion and checks the exact label
value and every byte of the GPU fill. It uses no replacement runtime functions or success stubs.
Each packet is tested in a fresh process to avoid sticky synchronization state between cases.

EOP (`0x47`), RELEASE_MEM (`0x49`), and EOS (`0x48`) each pass for 32-bit and 64-bit labels,
under both Metal 3 and Metal 4, with Metal API validation enabled: twelve passing cases.
This covers completion ordering independently of the retail game's visual behavior. The
existing host state tests separately check that a completion label does not enable submit-side
encoder-drain elision.

Reproduce with an existing native build from the same checkout:

```sh
bash scripts/test-gnm-fence-order.sh /absolute/path/to/native-build
PS4_GNM_METAL=4 bash scripts/test-gnm-fence-order.sh /absolute/path/to/native-build
```

The runner needs the native build's real compiled runtime objects and existing third-party
libraries. It does not compile those objects from another checkout or download dependencies.
The Metal 4 invocation requires a supported device/build. These native tests were run manually;
they are not part of the default Go-only CI invocation.

## Controlled retail comparison

Three 160-second runs used byte-identical guest assembly and the same guest image, independent
copies of the same save/settings, and identical copies of the warmed shader cache:

1. Current main.
2. Rebased PR #35, normal behavior.
3. Rebased PR #35 with `PS4_GNM_WAIT_ON_PRESENT=1` as an additional control.

All retained Metal 3, PS4 Pro mode, 1920x1080, the existing 60 FPS cap, and disabled MetalFX.
`PS4_PAD_CLOCK=frames` ties menu selection, L2/R2 aiming/firing, movement, and L1/R1 input to
submitted flip counts. Screenshots were recorded every 60 flips. The first aim sequence starts
at flip 6240, firing at 6360, movement at 6600, and the second shoulder sequence at 6840/6960.

The inspected frame-6300 and frame-6420 captures show the same aiming/weapon pose in main and
PR #35; at frame 6420 all three variants show ammunition reduced from 24 to 23. Frame 6660
shows continuing movement in main and PR #35. Darkening when the character is hit and loses
health is expected gameplay behavior, as confirmed by the user. The earlier interpretation as
a rendering defect was incorrect. This controlled comparison supersedes the unmatched-camera captures.

No new pose/geometry regression was observed in the inspected matching action captures.
Water, fire lighting, NPC animation, and simulation timing vary across runs, so this is not a
claim of pixel-identical whole frames. L1/R1 inputs
were exercised; their complete animation sequence is not separately certified.

All three processes exited normally, without logged fatal errors, assertions, Signal 11, GPU
watchdog failures, or shader-permutation hazard skips. Presents 6001–7200 had the same 248
skipped draws in each run. Mean waiting/frame was 11.075 ms on main, 9.300 ms on #35, and
10.725 ms with the forced drain. This is a correctness/control check with dense screenshots,
not a general performance benchmark.

Full Go tests, `go vet`, Go escape analysis, and native runtime/application builds passed after
the rebase. Go escape analysis does not establish zero native allocations.

Raw logs, input script, native test build, and captures remain local and ignored under
`ps4recomp/pr35-check/`. Other retail titles, weaker Macs, and long gameplay sessions were not
validated. The available tests support marking this change ready for maintainer review within
that stated scope; they do not establish universal renderer correctness.
