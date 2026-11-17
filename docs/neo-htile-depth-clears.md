# Neo HTILE depth fast clears of multi-layer arrays

Date: 2026-11-16. Measured on Apple M4 Max, macOS 27.3, Metal 3, game title CUSA02343
(Uncharted: The Nathan Drake Collection, Drake's Fortune), jungle checkpoint. Settings: PS4 Pro
mode, 1920x1080, 60 FPS cap, MetalFX off. No dependency or game data was downloaded.

## Symptom

With the PS4 Pro setting the ground around and in front of the player is uniformly dark and the
cast shadows lose their foliage structure, while distant surfaces look normally lit. The same
scene in the base PS4 setting renders the expected leaf shadows.

## Root cause

`DepthHtileSliceBytes` returned 0 for Neo mode (its alternate depth tiling was not decoded), so
`HtileSliceBytesOf` was 0 and `TakeMetaCleared`/`IsMetaClearedPending` fell back to "a
metadata clear covers the first slice only". The game's HTILE fast clear of the 2048x2048x4
Depth16Unorm shadow map therefore cleared layer 0 only; layers 1..3 kept undefined (zero) depth.
The shadow samplers compare with `Less` and `DB_DEPTH_CLEAR = 1.0`, so stored 0 fails every
comparison and those cascades are fully shadowed. The near cascades cover the ground around the
player.

A layer dump of the shadow array at present 5300 before the fix: slice 0 30.7 % far / 68.2 %
zero, slice 1 95.5 % zero and 0 % far. After the fix every slice has far 28-38 % and geometry
21-29 %.

## Change

`DepthHtileSliceBytes` decodes the Neo alternate tiling: `GetAltPipeConfig` (the 16-pipe
tiled layout, or the 2-pipe layout for display-linear surfaces) through `HtileSliceBytes`,
which computes the metadata macro-tile padded size (32 bits per 8x8 depth tile). The covered
slice count of a fast clear is again the guest's cleared byte count divided by that slice size,
for both base and Neo layouts.

When the cleared byte count is not a multiple of the decoded slice size, one `meta-clear-size`
warning is logged and the count is a lower bound. An unknown layout keeps the previous
conservative behaviour (first slice only, one `htile-size` warning).

Evidence from the running game (HTILE log of the shadow map's metadata address):

```
[htile] take 0x11372d0000 slice=0 covered=4 cleared_bytes=0x100000 slice_bytes=262144 taken=0x0
```

The guest clears 1 MiB of metadata = four 256 KiB slices, so all four cascades clear. No
`meta-clear-size` or `htile-size` warning appears in a full checkpoint run.

## Tests

`tests/gnm_metal_test/htile_layout.mm` checks the layout arithmetic against the real runtime
objects of an existing native build:

```sh
bash scripts/test-gnm-htile-layout.sh /absolute/path/to/native-build
```

It asserts 2048x2048 -> 262144 bytes per slice (the size the game's 1 MiB clear over four layers
implies) and that a 256x256 surface distinguishes the Neo 16-pipe tiling (32768 bytes) from the
base 8-pipe layout (16384 bytes). Native tests are run manually; they are not part of the
Go-only CI invocation.

`go build ./...`, `go vet ./...` and `go test -count=1 ./...` pass.

## Rendered comparison

Three configurations of the same scripted start (identical guest image, save/settings copy and
warmed shader cache, 150 s each):

- PS4 Pro setting before the fix: dark ground, no structured cast shadows.
- Base PS4 setting: correct leaf and ground shadows (the base layout already decoded).
- PS4 Pro setting with the fix: the same correct shadows, including the player's own cast shadow.

## Performance

Emulating a fast clear still costs real render-pass clears, so the A/B compared the old
single-slice coverage (wrong shadows) with the corrected four-slice coverage on the same binary
and script:

- one slice: 8401 presents in 150 s, frame times p50 16.7-19.3 ms, p95 19.3-20.9 ms;
- all slices: 8101 presents in 150 s, p50 16.6-18.7 ms, p95 17.8-22.4 ms (one 473 ms startup
  stall in the first window), settling at p50 16.6-16.7 ms and p95 17.8-18.7 ms.

Cross-build runs of the full checkpoint before and after the fix both reached 8401 presents with
p50 16.8-18.1 ms and p95 19.8-20.9 ms. Frame times do not measurably move on this scene at the
60 FPS cap.

## Limitations

The decoded slice size is validated on the Neo depth targets the game uses, including the
2048x2048 shadow array. A title whose 16-pipe alternate-tiling metadata does not match this
layout would trigger the `meta-clear-size` warning, and its covered slice count would then be a
lower bound rather than an over-clear.