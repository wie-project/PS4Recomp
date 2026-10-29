# gcn-recompiler

A standalone static library that turns PS4 (AMD GCN / Liverpool) shaders into SPIR-V, and
optionally into Metal Shading Language through SPIRV-Cross. It exposes a small C API in
`include/gcn_recompiler.h`.

## Origin and license

The shader recompiler comes from **shadPS4** (https://github.com/shadps4-emu/shadPS4),
commit `32021e4` ("kernel/time: Remove log info from sceKernelConvertLocaltimeToUtc (#5408)").
It is licensed **GPL-2.0-or-later**. Every file copied from shadPS4 keeps its original SPDX and
copyright header. New files written for PS4Recomp are GPL-2.0-or-later too. `LICENSE` is
shadPS4's license text.

Files copied from shadPS4:
- `src/shader_recompiler/**`: the whole recompiler (frontend, IR passes, SPIR-V backend)
- `src/common/{types,assert,bit_field,div_ceil,enum,func_traits,hash,object_pool,small_vector,alignment,arch}.h`
  and `src/common/logging/{log,classes}.h`
- `src/video_core/amdgpu/{pixel_format.h,pixel_format.cpp,resource.h,tiling.h,regs*.h}`

Vendored externals, each under its own license:
- `externals/sirit` with SPIRV-Headers (BSD-3 / MIT)
- `externals/fmt` (MIT)
- `externals/magic_enum` (MIT)
- `externals/boost`: only the subset of Boost.Container/Intrusive/Move/Config headers the
  build uses, copied from shadPS4's ext-boost (BSL-1.0)

### Changes from shadPS4

- `ir/passes/flatten_extended_userdata_pass.cpp` and `ir/passes/srt.h`: the x86-64 Xbyak JIT
  "SRT walker" has been removed, along with its SIGSEGV patch handler and its Zydis dump. In
  its place, the pass now emits a portable bytecode (`Shader::SrtOp`), which
  `Shader::RunSrtProgram()` interprets through a guest-memory reader callback. The IR analysis
  is unchanged.
- `ir/passes/flatten_extended_userdata_pass.cpp`, `ir/passes/srt.h`: an `s_load` through an SRT
  pointer at an offset only the shader knows (a loop counter: a Phi or ReadFirstLane in the
  offset) cannot be flattened. shadPS4 reads it through direct memory access, which the Metal
  profile does not have; here the walker stores the pointer (`SrtOp::StorePtr`) and the read
  becomes a `ReadConstBuffer` of a 1 MiB guest buffer based at that pointer.
- `backend/spirv/emit_spirv_context_get_set.cpp` (`EmitReadConstBuffer`): `s_buffer_load` past
  the V#'s `num_records` reads 0, as on GCN (bounds from `push_data.buf_sizes`, like the vector
  buffer loads).
- `compat/host_fp_env.h` (new): `gcn_compile`, `gcn_spirv_to_msl` and `gcn_shader_rectlist_msl`
  run with the default floating-point environment and restore the caller's. The recompiler
  carries integer constants through float IR values and SPIRV-Cross prints float literals through
  a double, so a caller with flush to zero (FPCR.FZ: PS4Recomp's guest threads) turned every
  integer constant below 0x00800000 into 0.
- `api/gcn_rectlist.cpp` (new) and `api/gcn_msl.cpp` (`ConvertCapture`):
  `gcn_shader_rectlist_msl()` for RectList draws on Metal, which has no stage between the vertex
  and fragment shaders for shadPS4's RectList tessellation control shader
  (`backend/spirv/emit_spirv_quad_rect.cpp`). It returns the vertex shader's SPIRV-Cross
  `capture_output_to_buffer` variant and a generated vertex shader that expands the captured
  vertices to two triangles per rectangle, with the 4th vertex as in shadPS4's TCS.
- `info.h`, `frontend/fetch_shader.cpp`: shadPS4 dereferences guest GPU addresses directly. In
  this library those reads go through `compat/guest_memory.h`, using a per-thread reader that
  the C API installs.
- `frontend/decode.cpp`: an unused `core/libraries/kernel/process.h` include was dropped.
- `noexcept` was removed mechanically from the copied recompiler, amdgpu and `common/enum.h`
  headers and sources. Asserts now throw `GcnShim::CompileError`, which the C API catches.
  An exception thrown inside a `noexcept` function would otherwise call `std::terminate`.
- Shims that replace shadPS4 infrastructure:
  - `core/emulator_settings.h`: constants. Inline fetch shader is off, DMA/BDA is off, and
    shader dumping is controlled at runtime.
  - `common/path_util.h` and `common/io_file.h`: minimal dump-file writer.
  - `compat/shim.cpp`: the logging sink (stderr or a callback), asserts that throw, and the
    guest memory reader.

## Build

```sh
mkdir -p ~/ps4recomp-work/gcn-build && cd ~/ps4recomp-work/gcn-build
cmake -G Ninja ~/ps4recomp-work/PS4Recomp/3rdparty/gcn-recompiler   # Release, -O2, arm64, C++23
ninja                    # libgcn_recompiler.a, gcn_test, gcn_c_smoke  (~12 s on 8 cores)
```

Options:
- `-DGCN_BUILD_MSL=OFF`: drop the SPIRV-Cross helper (`gcn_spirv_to_msl` then fails cleanly).
- `-DGCN_BUILD_TOOLS=OFF`: build only the library.
- `-DGCN_SPIRV_CROSS_DIR=...`: location of the SPIRV-Cross headers (default `../spirv-cross`).

Requires Apple clang 17+ with C++23. Tested with Apple clang 21.

## Linking

`libgcn_recompiler.a` is C++, but its API is plain C (`extern "C"`). When linking with the C
driver:

```
clang ... libgcn_recompiler.a ../spirv-cross/libspirv-cross.a -lc++
```

- `-lc++` is required: libc++ plus exceptions/RTTI, which the library uses internally.
- `libspirv-cross.a` is needed only for the MSL entry points (`GCN_COMPILE_EMIT_MSL`,
  `gcn_spirv_to_msl`). The library does not embed SPIRV-Cross, so it does not clash with the
  copy PS4Recomp already links.
- No frameworks are needed. Metal is only used by the `gcn_test` tool.
- The library defines symbols in the namespaces `Shader::`, `Common::`, `AmdGpu::`, `Sirit::`,
  `GcnShim::`, `GcnMsl::` and `fmt::v12`.

## Test tool

```sh
./gcn_test --selftest                                  # hand-written VS/PS/CS GCN programs
./gcn_test --quiet ~/ps4recomp-work/shader_dumps/*.bin # dumped game shaders (+ .txt registers)
./gcn_test --msl-out /tmp/msl vs_xxx.bin               # also write .metal/.spv
./gcn_test --zero-mem ...                              # fake guest memory (test only)
```

Each shader goes through the recompiler and then SPIRV-Cross (MSL 2.3, macOS, no argument
buffers). The tool prints the resource table and validates the MSL with
`MTLDevice newLibraryWithSource`. The offline `xcrun metal` toolchain is not installed on this
machine.

The sidecar `.txt` holds `reg value` hex lines: the Liverpool register file, using the same
indexing as `g_liverpool_regs`. A `mem <hexaddr> <file>` line maps a guest memory blob.
