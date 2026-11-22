# GCN Recompiler Custom Patches and Upstream Divergence

This document catalogs all custom extensions, architecture adaptations, and bugfixes in `3rdparty/gcn-recompiler` relative to upstream `shadPS4`.

Any future changes, opcode implementations, or pass modifications in `3rdparty/gcn-recompiler` **MUST** be recorded in this document.

---

## 1. Architectural Adaptations for macOS / Apple Silicon (ARM64 & Metal)

### 1.1 Portable Bytecode SRT Walker (`RunSrtProgram`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/srt.h`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/flatten_extended_userdata_pass.cpp`
- **Rationale**: Upstream `shadPS4` implements the Shader Resource Table (SRT) walker using an x86-64 JIT engine (`Xbyak`) and OS signal handling (`SIGSEGV` patching). On ARM64 macOS, JIT pages with signal faulting are both non-portable and incompatible with Apple Silicon W^X / codesigning constraints.
- **Implementation**: Replaced with a lightweight, sandboxed bytecode virtual machine (`Shader::RunSrtProgram`) and instruction set (`SrtOp`) that interprets table walking, bounds checking, and pointer chasing deterministically on the host CPU.

### 1.2 Wave64 Emulation on SIMD32 Hardware
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/lower_wave64_pass.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/predicate_wave64_pass.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/wave_uniformity.h`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/lane_mask_bits.h`
- **Rationale**: AMD GCN natively executes in 64-thread wavefronts (Wave64). Apple Silicon GPUs natively execute SIMD32 threadgroups.
- **Implementation**: `LowerWave64` and `PredicateWave64` split 64-bit execution masks and ballots into low and high 32-bit halves, allowing wave64 shaders to execute correctly across Metal SIMD32 lanes.

### 1.3 Metal 1D Image to 2D Texture Lowering (`Is1DAs2D`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/video_core/amdgpu/resource.h`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/backend/spirv/emit_spirv_image.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/resource_patching_pass.cpp`
- **Rationale**: Metal does not support 1D textures with block-compressed formats or mipmaps.
- **Implementation**: All 1D images are translated into 2D textures with height 1. Coordinates `x` are mapped to `(x, 0)` for loads and `(u, 0.5)` for sampling.

### 1.4 C-ABI Host Interface and MSL Pipeline
- **Files**:
  - `3rdparty/gcn-recompiler/include/gcn_recompiler.h`
  - `3rdparty/gcn-recompiler/src/api/gcn_recompiler.cpp`
  - `3rdparty/gcn-recompiler/src/api/gcn_msl.cpp`
  - `3rdparty/gcn-recompiler/src/api/gcn_shader_cache.cpp`
- **Rationale**: PS4Recomp operates with a clean C interface between the Go runtime / Objective-C GNM Metal driver and the C++ recompiler.
- **Implementation**: Provides shader translation to SPIR-V, conversion to Metal Shading Language (MSL) via `spirv-cross`, reflection of descriptor bindings, and pipeline caching.

---

## 2. Resource Lowering & Dynamic Binding Patches

### 2.1 Dynamic Image Table Lowering (`LowerDynamicImageTable`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/info.h`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/resource_patching_pass.cpp`
- **Rationale**: In PS4 games (e.g. Uncharted 4, God of War), pixel and vertex shaders dynamically index texture descriptor arrays ($T\#$) at runtime using values known only during draw execution.
- **Supported Bounds**:
  1. `ImageTableKind::Records`: Table bounded by buffer size.
  2. `ImageTableKind::Indexed`: Table bounded by distinct indices scanned from index buffers (`SrtOp::CollectIndices`).
  3. `ImageTableKind::Loop`: Table bounded by loop counter iteration limits (`MatchLoopCounter`).
  4. `ImageTableKind::BitScan`: Table bounded by least-significant-bit indices from `FindILsb32` / `v_ffbl_b32` with immediate or runtime bitmasks (`MatchBitScanBound`).

### 2.2 Unflattened Buffer Lowering to Guest Memory (`LowerDynamicBufferAccess`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/resource_patching_pass.cpp`
- **Rationale**: Dynamic buffer reads ($V\#$) that cannot be flattened through the SRT are transformed into direct guest memory accesses (`LoadGuestU32`), avoiding compilation failure.

### 2.3 Bitwise Unwrapping on T# Descriptors
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/resource_discover_pass.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/resource_patching_pass.cpp`
- **Rationale**: Games like God of War mask or shift descriptor dwords in shader arithmetic (e.g. `BitwiseAnd32`, `ShiftRightLogical32`).
- **Implementation**: Unwrapped in `MarkReadConstBufferSharpSources` and `PatchImageSharp` to locate underlying constant buffer sources rather than aborting compilation.

---

## 3. Frontend & Opcode Additions

### 3.1 `DS_ORDERED_COUNT` (Opcode 1681)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/data_share.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/translate.h`
- **Rationale**: Compute shaders in God of War utilize `DS_ORDERED_COUNT` for work-group wave ordering.
- **Implementation**: Translated via `ir.SharedAtomicIAdd` targeting the LDS ordered count counter.
