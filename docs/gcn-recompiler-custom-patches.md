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

### 3.2 64-bit Scalar Shift Instructions (`S_LSHR_B64`, `S_LSHL_B64`, `S_ASHR_I64`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/scalar_alu.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/translate.h`
- **Rationale**: GCN ISA takes shift amounts for 64-bit shifts from a 32-bit scalar source operand (low 6 bits).
- **Implementation**: Added missing opcode `S_LSHR_B64`; corrected `S_LSHL_B64` and `S_ASHR_I64` to read 32-bit `src1` with `ir.BitwiseAnd(src1, ir.Imm32(0x3F))` instead of 64-bit register reads.

### 3.3 Flow Control & Hardware Registration (`S_GETREG_B32`, `S_SLEEP`, `EmitCut`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/scalar_flow.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/translate.h`
- **Rationale**: Titles querying Liverpool hardware register state or geometry shader stream cuts.
- **Implementation**: Added `S_GETREG_B32` (returns virtual shader engine ID 0), ignored `S_SLEEP` with warning, and supported `SendMsgSimm::GsOp::EmitCut`.

### 3.4 Deferred Fragment Discard (`valid_mask_exported`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/export.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/scalar_flow.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/translate.h`
- **Rationale**: When an export specifies `vm` (valid mask), GCN does not terminate fragment execution midway; instructions following the export still execute before inactive lanes are discarded.
- **Implementation**: Set `valid_mask_exported = true` during `EmitExport` and deferred `ir.Discard(ir.LogicalNot(ir.GetExec()))` to `S_ENDPGM`.

### 3.5 Alpha-to-Coverage / Alpha-to-Mask & MSAA Mask Emulation
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/export.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/translate.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/translate.h`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/runtime_info.h`
  - `3rdparty/gcn-recompiler/src/api/gcn_shader_cache.cpp`
- **Rationale**: Accurate multisample alpha-to-mask and pixel AA coverage mask emulation.
- **Implementation**: Synchronized `AlphaToMaskLevel`, `ExportAlphaToMask`, and `ReplicateSampleMask`; handled `aa_mask_per_pixel` in fragment setup; updated `HwFragmentRuntimeInfo` and bumped cache serialization version to 12 (`GCN_SHADER_CACHE_FORMAT "4"`).

### 3.6 Vector Memory Coherence Barriers (`BUFFER_WBINVL1`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/frontend/translate/vector_memory.cpp`
- **Rationale**: Cache writeback and invalidation instructions in compute shaders.
- **Implementation**: Mapped `Opcode::BUFFER_WBINVL1` and `Opcode::BUFFER_WBINVL1_SC` to `ir.DeviceMemoryBarrier()`.

### 3.7 Preserved Custom Features (Do NOT overwrite during upstream sync)
- **Files**:
  - `frontend/translate/vector_alu.cpp`: Neo 16-bit ALU (`V_INT16_ARITH`, `V_CMP_16`), dynamic VGPR indexers `VMovRelSHelper` / `VMovRelDHelper`, and `ClampInf`/`SignBits` bitwise protections against Metal `-ffast-math`.
  - `frontend/translate/data_share.cpp`: `ir.ReadFirstLane(value, ir.GetExec())` for Wave64 on Metal SIMD32, `Optimization::SetLaneSourceVgpr` tracking.
  - `frontend/translate/scalar_alu.cpp`: Metal-safe 64-bit bit scanning in `S_FF1_I32_B64` using `ir.FindILsb(src0)` (host SIMD32 ballots only scan 32 bits).
  - `frontend/translate/vector_memory.cpp`: `BufferAddress` (with `addr64`), `ImageAddressBody` (PS4 Pro `a16` packed coordinates), and packed `d16` texel exports.
  - `frontend/translate/translate.cpp`: SDWA source/destination selects and `Lane64()` wavefront indexing for `ExecLo`/`ExecHi`.

---

## 4. IR Passes & Optimization Sync (Stage 2)

### 4.1 Dominator Calculation Stale State Reset
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/dominance.cpp`
- **Rationale**: When `ComputeDominators` runs multiple times or across mutating passes, stale dominator references can corrupt the dominance tree.
- **Implementation**: Added initialization loop resetting `block->immediate_dominator = nullptr` across all blocks before computing frontiers.

### 4.2 Deterministic Phi Simplification & GetUserData Disallow
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/phi_simplification_pass.cpp`
- **Rationale**: `GetUserData` (pointers to flattened SRT descriptors) must never be folded across basic block phis. Worklist iteration on `std::unordered_map` created non-deterministic shader compilation.
- **Implementation**: Added `IR::Opcode::GetUserData` to `IsDisallowedOp`; switched worklist loop to iterate ordered `sccs` vector.

### 4.3 Dead Phi Invalidation in LowerPhisToRegs
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/lower_phis_to_regs_pass.cpp`
- **Rationale**: Eliminates dead virtual registers from unreferenced phis before allocation.
- **Implementation**: Added `if (!inst.HasUses()) { inst.Invalidate(); continue; }`, while strictly preserving our `program.lane_mask_phis` scalar mask tracking for Wave64.

### 4.4 Non-Divergent Workgroup Dimension Filtering in LDS Barriers
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/shared_memory_barrier_pass.cpp`
- **Rationale**: Conditionals depending on `LocalInvocationId` along axes where `workgroup_size[axis] == 1` are identical across all invocations in the workgroup and therefore not divergent.
- **Implementation**: Filtered `workgroup_size[component] > 1` in `IsDivergent`, while strictly retaining our `BarrierPointBefore`, `IsPredicatedEnd`, and `lock_step` barrier insertions for Wave64 on Metal.

### 4.5 Robust Masked Bit Count Dynamic Matching in GDS Append/Consume
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/resource_patching_pass.cpp`
- **Rationale**: Compilers may emit `v_mbcnt_lo` and `v_mbcnt_hi` in either order.
- **Implementation**: Updated `IsMbcntWithExec` to return `std::pair<IR::Value, bool>`, dynamically asserting `is_hi_1 != is_hi_2` rather than expecting a hardcoded sequence.

### 4.6 Vector Undefined Opcode (`UndefF32x2`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/opcodes.inc`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/ssa_rewrite_pass.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/backend/spirv/emit_spirv_instructions.h`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/backend/spirv/emit_spirv_undefined.cpp`
- **Rationale**: SSA rewriting for 2-element float registers (`F32x2`).
- **Implementation**: Added `UndefF32x2` opcode and corresponding SPIR-V emitter returning `ctx.OpUndef(ctx.F32[2])`.

---

## 5. SPIR-V Backend Synchronization (Stage 3)

### 5.1 Multi-Write Cumulative SampleMask Combination
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/backend/spirv/emit_spirv_context_get_set.cpp`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/backend/spirv/emit_spirv_special.cpp`
- **Rationale**: Shaders with both Alpha-to-Mask/Coverage and MRTZ depth/stencil sample masks write to `SampleMask` in multiple passes; subsequent writes must be bitwise-ANDed rather than overwriting each other.
- **Implementation**: Initialized `sample_mask` output to `0xFFFFFFFF` in `EmitPrologue` if `stores.Get(SampleMask)`, and in `EmitSetAttribute` combined writes via `ctx.OpBitwiseAnd(current, value)`.

### 5.2 64-bit Floating-Point Bitcast (`BitCastF64U64`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/backend/spirv/emit_spirv_instructions.h`
  - `3rdparty/gcn-recompiler/src/shader_recompiler/backend/spirv/emit_spirv_bitwise_conversion.cpp`
- **Rationale**: Emits `OpBitcast` for 64-bit integer to double float conversions.
- **Implementation**: Added `EmitBitCastF64U64`.

### 5.3 64-bit LSB Search (`EmitFindILsb64`)
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/backend/spirv/emit_spirv_integer.cpp`
- **Rationale**: Normalized selection of the 32-bit offset high-word result.
- **Implementation**: Adjusted `EmitFindILsb64` to cleanly select `high_result` when the low word has no set bits.

### 5.4 Preserved Metal / macOS Safety Features
- **Files**:
  - `spirv_emit_context.h` / `spirv_emit_context.cpp`: Bounds checking (`EmitBoundsCheckedStore`, `EmitBoundsCheckedValue`) protecting against GPU crashes on out-of-range buffer accesses without Vulkan `robustBufferAccess`.
  - `spirv_emit_context.h` / `spirv_emit_context.cpp`: Buffer base misalignment compensation (`misalign`).
  - `emit_spirv_floating_point.cpp`: Integer bitcast IEEE-754 NaN and Inf protections (`EmitFPSaturate32`, `EmitFPIsNan32`, `EmitFPIsInf32`) immune to Metal `-ffast-math` optimization.
  - `spirv_emit_context.h` / `emit_spirv_context_get_set.cpp`: Dynamic guest memory access functions (`EmitLoadGuestU32`, `EmitStoreGuestU32`, `EmitGuestReportStatus`) and probe guarding.

---

## 6. Metal Hardware Constraint Workarounds

### 6.1 Color1DArray with >2048 Layers Lowered to 2D Texture
- **Files**:
  - `3rdparty/gcn-recompiler/src/video_core/amdgpu/resource.h` (`Is1DArrayAs2D`, `GetViewType`)
  - `3rdparty/gcn-recompiler/src/shader_recompiler/resource.h` (`IsCreatableImage`)
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/resource_patching_pass.cpp` (`AddressType`, `get_coord`)
  - `pkg/runtime/modules/libSceGnmDriver/ps4_gnm_resources.mm` (`DecodeImageUncached`, `GetTextureImpl`)
- **Rationale**: Apple Silicon Metal strictly limits `MTLTextureType2DArray` `arrayLength` to 2048. Titles like God of War (2018) allocate a 4096-layer material LUT texture of type `Color1DArray` (256x1x1, 4096 layers, 1 level). When treated as a 2D array, Metal aborts with `arrayLength (4096) is greater than max allowed size (2048)`, which previously caused PS4Recomp to reject it and bind it as a dummy null texture (causing missing material/lighting parameters).
- **Implementation**:
  - `Is1DArrayAs2D()` detects `Color1DArray` with `depth >= 2048`.
  - In `resource.h`, `IsCreatableImage` accepts `Color1DArray` with up to 16384 layers.
  - `GetViewType` and `AddressType` return `ImageType::Color2D` for such textures, emitting `texture2d<float>` instead of `texture2d_array<float>`.
  - In `resource_patching_pass.cpp`, sampling coordinate generation normalizes the slice index along dimension 1: `(slice + 0.5f) / float(height)`. For `ImageRead`, `(x, slice)` integer texel indexing is used directly.
  - In `ps4_gnm_resources.mm`, `DecodeImageUncached` and `GetTextureImpl` create `MTLTextureType2D` with `width = 256` and `height = 4096`, setting `num_layers = 1` and `layout_layers = 1` so surface layout does not overflow 32-bit total_size, and detiled memory rows are uploaded directly into Metal's 2D texture.

### 6.2 Dynamic Image Table Bitwise Mask Unwrapping
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/resource_patching_pass.cpp` (`MatchTableSite`)
- **Rationale**: Shaders in titles like God of War (2018) load T# texture descriptors dynamically from constant buffer tables (`s_buffer_load_dwordx8`), and apply bitwise masks or shifts to dword 1 (e.g. `BitwiseAnd32`) to adjust descriptor fields at runtime. `MatchTableSite` previously expected every dword of `tsharp` to be directly `ReadConstBuffer`, which caused table recognition to fail. The recompiler consequently dropped the table binding and fell back to patching the image instruction to read the dummy null image (25 shaders reading black textures).
- **Implementation**:
  - In `MatchTableSite`, unwrapped `BitwiseAnd32`, `BitwiseOr32`, `ShiftLeftLogical32`, and `ShiftRightLogical32` instructions recursively down to the underlying `ReadConstBuffer`, allowing dynamic T# descriptor tables with masked dwords to be matched and lowered properly.

### 6.3 Static Sharp Descriptor Bitwise Unwrapping
- **Files**:
  - `3rdparty/gcn-recompiler/src/shader_recompiler/ir/passes/resource_patching_pass.cpp` (`SharpLocationFromSource`, `IsFlattenedSharpDword`)
- **Rationale**: In complex shaders (e.g. God of War materials), static T# image descriptors passed via user data or constant buffers may also have bitwise mask or shift operations applied to individual dwords (such as dword 1 for LOD/format flags). `SharpLocationFromSource` and `IsFlattenedSharpDword` previously only inspected the root instruction opcode (`GetUserData`, `ReadConst`, `ReadConstBuffer`), rejecting masked dwords as `UNKNOWN_LOCATION` / non-flattened and causing `ConstructSharpFetch` to treat the fetch as `Invalid`, falling back to reading the null image.
- **Implementation**:
  - Unwrapped bitwise instructions (`BitwiseAnd32`, `BitwiseOr32`, `ShiftLeftLogical32`, `ShiftRightLogical32`) in both `SharpLocationFromSource` and `IsFlattenedSharpDword` down to the root constant or user-data instruction, preserving the flatbuf location so static descriptor fetches resolve correctly.



